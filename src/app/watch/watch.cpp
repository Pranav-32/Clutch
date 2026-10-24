/* Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause */

/**
 * @file
 * @brief Keep failed edit/build cycles recoverable without overlapping
 * children.
 */
#include "watch.hpp"

#include "core/configuration_store.hpp"
#include "core/options.hpp"
#include "core/project_layout.hpp"
#include "platform/process.hpp"
#include "platform/terminal.hpp"
#include "snapshot.hpp"
#include <algorithm>
#include <chrono>
#include <csignal>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <thread>

namespace clutch {
namespace {
// Signal handlers may only write signal-safe state, never streams/containers.
volatile std::sig_atomic_t interrupted = 0;
constexpr auto signal_status_base = 128;
constexpr auto cancellation_slice = std::chrono::milliseconds(50);

void stop(const int signal) { interrupted = signal; }

/// Restore the caller's handlers on every ordinary return/exception.
class Signals {
  public:
  Signals() {
    interrupted = 0;
    interrupt_ = std::signal(SIGINT, stop);
    terminate_ = std::signal(SIGTERM, stop);
#ifdef SIGBREAK
    break_ = std::signal(SIGBREAK, stop);
#endif
  }

  ~Signals() {
    std::signal(SIGINT, interrupt_);
    std::signal(SIGTERM, terminate_);
#ifdef SIGBREAK
    std::signal(SIGBREAK, break_);
#endif
  }

  Signals(const Signals &) = delete;
  Signals &operator=(const Signals &) = delete;

  private:
  using Handler = void (*)(int);
  Handler interrupt_{}, terminate_{};
#ifdef SIGBREAK
  Handler break_{};
#endif
};

/// Long poll intervals do not force a long wait before an idle Ctrl+C exits.
void pause(const int milliseconds) {
  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(milliseconds);
  while (!interrupted && std::chrono::steady_clock::now() < deadline)
    std::this_thread::sleep_for(
        std::min(cancellation_slice,
                 std::chrono::ceil<std::chrono::milliseconds>(
                     deadline - std::chrono::steady_clock::now())));
}
}  // namespace

int watch_project(const Options &options,
                  const WatchSettings &settings,
                  const fs::path &executable) {
  if (!fs::is_regular_file(options.root / layout::manifest))
    throw std::runtime_error("watch requires a native clutch.toml project");

  const Signals signals;
  std::optional<Graph> graph;
  bool configure = false;
  bool clean = false;
  try {
    graph = load_configuration(options);
  } catch (const std::exception &) {
    configure = true;
  }

  WatchSnapshot previous, current;
  snapshot_watch_inputs(options, settings, graph ? &*graph : nullptr, previous);
  const auto cycle = [&] {
    int code = 0;
    try {
      if (configure) {
        std::vector<std::string> command{executable.u8string(),
                                         options.root.u8string(),
                                         "--provider",
                                         "clutch"};
        if (options.release)
          command.emplace_back("--release");

        code = run(command, options.root);
        if (code == 0) {
          graph = load_configuration(options);
          configure = false;
          // Configuration owns this lock update; it is not another user edit.
          snapshot_watch_inputs(options, settings, &*graph, current);
          // Dropped units/inputs no longer belong to the refreshed watch scope.
          // Forget those sentinels without causing a second configure cycle.
          for (auto it = previous.files.begin(); it != previous.files.end();) {
            if (!current.files.count(it->first)) {
              previous.configuration.erase(it->first);
              previous.timestamps.erase(it->first);
              it = previous.files.erase(it);
            } else
              ++it;
          }

          // Newly discovered template/version inputs were validated by the
          // saved graph. Preserve any later edit by comparing its exact hash.
          for (const auto &input : graph->project.inputs) {
            if (const auto found = current.files.find(input.path);
                found != current.files.end() &&
                found->second == input.signature &&
                !previous.files.count(input.path)) {
              previous.files.insert(*found);
              previous.configuration.insert(input.path);
            }
          }

          const auto lock = fs::canonical(options.root) / layout::lock_file;
          if (const auto found = current.files.find(lock);
              found != current.files.end())
            previous.files[lock] = found->second;
        }
      }

      if (code == 0 && !interrupted) {
        std::vector<std::string> command{executable.u8string(),
                                         settings.test  ? "test"
                                         : settings.run ? "run"
                                                        : "build",
                                         "--project",
                                         options.root.u8string(),
                                         "--provider",
                                         "clutch"};
        if (clean) {
          status("Rebuilding",
                 "content changed without an advancing timestamp");
          command.emplace_back("--clean");
        }
        if (options.release)
          command.emplace_back("--release");

        if (options.jobs > 0) {
          command.emplace_back("--jobs");
          command.push_back(std::to_string(options.jobs));
        }

        for (const auto &target : settings.targets)
          command.insert(command.end(), {"--target", target});

        if (!settings.arguments.empty()) {
          command.emplace_back("--");
          command.insert(command.end(),
                         settings.arguments.begin(),
                         settings.arguments.end());
        }

        code = run(command, options.root);
        clean = false;
      }
    } catch (const std::exception &error) {
      // An editor may replace a configuration input between child completion
      // and graph loading. Retain the last scope and retry on the next edit.
      configure = true;
      code = 1;
      std::cerr << "watch cycle failed: " << error.what() << '\n';
    }

    if (!interrupted)
      status("Watching",
             code == 0 ? "waiting for changes (Ctrl+C to stop)"
                       : "cycle failed; waiting for changes (Ctrl+C to stop)");
  };

  cycle();
  bool pending = false;
  auto last_change = std::chrono::steady_clock::now();
  while (!interrupted) {
    pause(settings.interval_ms);
    if (interrupted)
      break;

    snapshot_watch_inputs(options,
                          settings,
                          graph ? &*graph : nullptr,
                          current);
    if (current.files != previous.files) {
      configure = configure || watch_needs_configuration(previous, current);
      clean = clean || watch_needs_clean(previous, current);
      previous.swap(current);
      last_change = std::chrono::steady_clock::now();
      pending = true;
    }

    if (pending && std::chrono::steady_clock::now() - last_change >=
                       std::chrono::milliseconds(settings.debounce_ms)) {
      pending = false;
      status("Changed",
             configure ? "reconfiguring project" : "rebuilding project");
      // Keep the pre-build snapshot. Edits during a slow child are picked up
      // by the next poll instead of being swallowed into a post-build baseline.
      cycle();
    }
  }

  status("Stopped", "watching");
  return signal_status_base + static_cast<int>(interrupted);
}
}  // namespace clutch

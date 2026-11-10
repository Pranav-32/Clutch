/*
 * Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file
 * @brief Exercise native pipe draining, argv boundaries, output bounds and
 * child cleanup.
 */
#include "platform/capture.hpp"
#include <algorithm>
#include <array>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <thread>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace {
// Check the public diagnostic's four-MiB boundary, independently of pipe chunk
// sizes.
constexpr auto output_limit = size_t{4} * 1024U * 1024U;
constexpr auto child_failure = 37;

/// Keep assertions active in optimized builds.
void require(const bool condition, const char *const message) {
  if (!condition)
    throw std::runtime_error(message);
}

/// A self-contained child avoids shells, external runtimes and platform echo
/// differences.
int child(const int argc, char **const argv) {
  if (std::string(argv[1]) == "--sleep") {
    std::this_thread::sleep_for(std::chrono::seconds{5});
    return 0;
  }

  if (std::string(argv[1]) == "--orphan") {
    // Leave a sleeping grandchild holding the inherited output pipe, then
    // exit immediately: capture must report this child's completion.
#ifdef _WIN32
    auto command = std::wstring(L"\"") +
                   clutch::fs::absolute(argv[0]).wstring() + L"\" --sleep";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    PROCESS_INFORMATION process{};
    require(CreateProcessW(nullptr,
                           command.data(),
                           nullptr,
                           nullptr,
                           TRUE,
                           0,
                           nullptr,
                           nullptr,
                           &startup,
                           &process) != 0,
            "cannot start lingering grandchild");
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
#else
    if (const auto grandchild = fork(); grandchild == 0) {
      std::this_thread::sleep_for(std::chrono::seconds{5});
      _exit(0);
    } else {
      require(grandchild > 0, "cannot start lingering grandchild");
    }
#endif
    std::fputs("parent done", stdout);
    std::fflush(stdout);
    return 0;
  }

  if (std::string(argv[1]) == "--stderr") {
    std::fputs("probe diagnostic", stderr);
    return child_failure;
  }

  if (std::string(argv[1]) == "--echo") {
    for (auto index = 2; index < argc; ++index) {
      const std::string argument = argv[index];
      std::printf("%zu:%s\n", argument.size(), argument.c_str());
    }
    return 0;
  }
  require(argc == 4 && std::string(argv[1]) == "--emit",
          "invalid capture child arguments");
  auto remaining = static_cast<size_t>(std::stoull(argv[2]));
  constexpr auto chunk_bytes = size_t{4096};
  std::array<char, chunk_bytes> chunk{};
  chunk.fill('x');
  while (remaining) {
    const auto count = std::min(remaining, chunk.size());
    require(std::fwrite(chunk.data(), 1, count, stdout) == count,
            "cannot write child output");
    remaining -= count;
  }
  return std::stoi(argv[3]);
}

/// A rejected capture must still drain and reap its child before returning
/// control.
void overflow(const std::string &executable,
              const clutch::fs::path &directory) {
  try {
    (void)clutch::capture(
        {executable, "--emit", std::to_string(output_limit + 1), "0"},
        directory);
  } catch (const std::runtime_error &error) {
    require(std::string(error.what()).find("four MiB") != std::string::npos,
            "oversized output failed for an unrelated reason");
    return;
  }
  throw std::runtime_error("oversized child output was accepted");
}

/// Verify content and exit status, including output larger than an OS pipe
/// buffer.
void unit_cases(const std::string &executable,
                const clutch::fs::path &directory) {
  for (const auto count : {size_t{0}, output_limit}) {
    const auto output = clutch::capture({executable,
                                         "--emit",
                                         std::to_string(count),
                                         std::to_string(child_failure)},
                                        directory);
    require(output.status == child_failure && output.text.size() == count &&
                std::all_of(output.text.begin(),
                            output.text.end(),
                            [](const char value) {
                              return value == 'x';
                            }),
            "captured output was truncated, corrupted or lost its exit status");
  }
  const std::vector<std::string> values{"",
                                        "a b",
                                        "quote\"inside",
                                        "trailing\\",
                                        "two\\\"quotes",
                                        "& | ; $(literal)"};
  std::vector<std::string> command{executable, "--echo"};
  command.insert(command.end(), values.begin(), values.end());
  std::string expected;
  for (const auto &value : values)
    expected += std::to_string(value.size()) + ':' + value + '\n';
  auto output = clutch::capture(command, directory);
#ifdef _WIN32
  // The fixture's C stdout stream emits CRLF, while the protocol bytes remain
  // otherwise exact.
  output.text.erase(std::remove(output.text.begin(), output.text.end(), '\r'),
                    output.text.end());
#endif
  require(output.status == 0 && output.text == expected,
          "child argument boundaries changed");
  overflow(executable, directory);

  const auto diagnostic =
      clutch::capture({executable, "--stderr"}, directory, true);
  require(diagnostic.status == child_failure &&
              diagnostic.text == "probe diagnostic",
          "merged stderr lost its diagnostic or exit status");
  bool timed_out = false;
  const auto started = std::chrono::steady_clock::now();
  try {
    (void)clutch::capture({executable, "--sleep"},
                          directory,
                          true,
                          std::chrono::milliseconds{100});
  } catch (const std::runtime_error &error) {
    timed_out =
        std::string(error.what()).find("timed out") != std::string::npos;
  }
  require(timed_out && std::chrono::steady_clock::now() - started <
                           std::chrono::seconds{4},
          "a stalled child was not terminated within the capture deadline");
#ifndef _WIN32
  int reaped_status = 0;
  require(waitpid(-1, &reaped_status, WNOHANG) == -1 && errno == ECHILD,
          "timed capture left an unreaped child");
#endif

  // A grandchild inheriting the pipe must not delay or fail a bounded capture
  // whose direct child has already exited.
  const auto orphan_started = std::chrono::steady_clock::now();
  const auto orphan = clutch::capture({executable, "--orphan"},
                                      directory,
                                      true,
                                      std::chrono::milliseconds{3000});
  require(orphan.status == 0 && orphan.text == "parent done" &&
              std::chrono::steady_clock::now() - orphan_started <
                  std::chrono::seconds{2},
          "a lingering grandchild caused a delayed or false capture timeout");
#ifndef _WIN32
  require(waitpid(-1, &reaped_status, WNOHANG) == -1 && errno == ECHILD,
          "bounded capture left its exited direct child unreaped");
#endif

  for (const auto &invalid : std::vector<std::vector<std::string>>{
           {},
           {executable, std::string("a\0b", 3)}}) {
    bool rejected = false;
    try {
      (void)clutch::capture(invalid, directory);
    } catch (const std::runtime_error &) {
      rejected = true;
    }
    require(rejected, "invalid capture arguments were accepted");
  }
}

/// Repeat rejection/recovery to expose retained process handles or unreaped
/// children.
void stress_cases(const std::string &executable,
                  const clutch::fs::path &directory) {
#ifdef _WIN32
  DWORD handles_before = 0;
  require(GetProcessHandleCount(GetCurrentProcess(), &handles_before) != 0,
          "cannot count process handles");
#endif
  constexpr auto repetitions = 16;
  for (auto repeat = 0; repeat < repetitions; ++repeat) {
    overflow(executable, directory);
    const auto recovered =
        clutch::capture({executable, "--emit", "0", "0"}, directory);
    require(recovered.status == 0 && recovered.text.empty(),
            "capture did not recover after overflow");
#ifndef _WIN32
    int status = 0;
    require(waitpid(-1, &status, WNOHANG) == -1 && errno == ECHILD,
            "capture left an unreaped child");
#endif
  }
#ifdef _WIN32
  DWORD handles_after = 0;
  require(GetProcessHandleCount(GetCurrentProcess(), &handles_after) != 0,
          "cannot recount process handles");
  require(handles_after == handles_before,
          "capture leaked native process/pipe handles");
#endif
}
}  // namespace

/// Run as either the parent test or its explicitly requested native child
/// fixture.
int main(const int argc, char **const argv) {
  try {
    if (argc > 1 &&
        (std::string(argv[1]) == "--emit" || std::string(argv[1]) == "--echo" ||
         std::string(argv[1]) == "--sleep" ||
         std::string(argv[1]) == "--orphan" ||
         std::string(argv[1]) == "--stderr"))
      return child(argc, argv);

    const auto executable =
        clutch::fs::absolute(clutch::fs::path(argv[0])).u8string();
    const auto directory = clutch::fs::current_path();
    unit_cases(executable, directory);
    if (argc > 1) {
      require(argc == 2 && std::string(argv[1]) == "--stress",
              "unknown capture-test option");
      stress_cases(executable, directory);
    }
    std::fputs("PASS: native output capture\n", stdout);
  } catch (const std::exception &error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
  } catch (...) {
    std::fputs("unexpected capture-test exception\n", stderr);
    return 1;
  }
  return 0;
}

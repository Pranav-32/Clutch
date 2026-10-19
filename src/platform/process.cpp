/*
 * Copyright (c) 2026, Pranav Sanjeevi Kumari
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * See LICENSE in the project root for the full license text.
 */

/**
 * @file
 * @brief Find executable programs, quote arguments and launch native Windows or
 * POSIX child processes.
 */

#include "process.hpp"
#include "platform/arguments.hpp"
#include "platform/environment.hpp"

#include <limits>
#include <stdexcept>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX 1
#endif

#include <windows.h>
#else
#include <cerrno>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace clutch {
/// Locate Clutch itself so generated regeneration commands survive cwd changes.
fs::path current_executable([[maybe_unused]] const std::string &invocation) {
#ifdef _WIN32
  // argv[0] may be a bare name found by cmd.exe in the current directory.
  // Query the loaded module instead of searching PATH for a second executable.
  std::vector<wchar_t> path(MAX_PATH);
  while (true) {
    if (const auto length = GetModuleFileNameW(nullptr,
                                               path.data(),
                                               static_cast<DWORD>(path.size()));
        length == 0)
      throw std::runtime_error("cannot resolve Clutch executable path "
                               "(Windows error " +
                               std::to_string(GetLastError()) + ")");
    else if (length < path.size())
      return fs::path(std::wstring(path.data(), length));

    // Grow after truncation, keeping the capacity representable by the API.
    if (path.size() > std::numeric_limits<DWORD>::max() / 2U)
      throw std::runtime_error("Clutch executable path exceeds Windows limits");

    path.resize(path.size() * 2U);
  }
#else
  return find_program(invocation);
#endif
}

/// Resolve a file or PATH entry to an absolute executable path; empty means
/// absent.
std::string find_program(const std::string &name) {
  if (name.empty())
    return {};

  // POSIX requires execute permission; Windows resolution also tries .exe
  // below.
  const auto valid = [](const fs::path &p) {
#ifdef _WIN32
    return fs::is_regular_file(p);
#else
    return fs::is_regular_file(p) && access(p.c_str(), X_OK) == 0;
#endif
  };

  if (const auto p = fs::u8path(name); p.has_parent_path()) {
    return valid(p) ? fs::absolute(p).u8string() : "";
  } else if (const auto path = environment_value("PATH"); path) {
    const auto &paths = *path;
#ifdef _WIN32
    constexpr char separator = ';';
#else
    constexpr char separator = ':';
#endif
    size_t begin = 0;
    while (begin <= paths.size()) {
      const auto end = paths.find(separator, begin);
      if (const auto dir = paths.substr(begin, end - begin); !dir.empty()) {
        auto candidate = fs::u8path(dir) / p;
        if (valid(candidate))
          return fs::absolute(candidate).u8string();
#ifdef _WIN32
        candidate += ".exe";
        if (valid(candidate))
          return fs::absolute(candidate).u8string();
#endif
      }

      if (end == std::string::npos)
        break;

      begin = end + 1;
    }
  }

  return {};
}

/// Launch with inherited environment/output, wait, and propagate the child
/// status.
int run(const std::vector<std::string> &args, const fs::path &directory) {
  if (args.empty())
    throw std::runtime_error("empty subprocess command");
#ifdef _WIN32
  // Arguments originate from filesystem paths or UTF-8 manifest strings.
  const auto wide = [](const std::string &s) {
    const int n = MultiByteToWideChar(CP_UTF8,
                                      MB_ERR_INVALID_CHARS,
                                      s.data(),
                                      static_cast<int>(s.size()),
                                      nullptr,
                                      0);
    if (!n && !s.empty())
      throw std::runtime_error("invalid UTF-8 process argument");

    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8,
                        0,
                        s.data(),
                        static_cast<int>(s.size()),
                        w.data(),
                        n);
    return w;
  };

  // CreateProcessW needs a mutable command line and an explicit working
  // directory.
  auto cmd = wide(command_line(args));
  const auto cwd = directory.wstring();
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  if (!CreateProcessW(nullptr,
                      cmd.data(),
                      nullptr,
                      nullptr,
                      TRUE,
                      0,
                      nullptr,
                      cwd.c_str(),
                      &si,
                      &pi))
    throw std::runtime_error("cannot launch " + args.front() +
                             " (Windows error " +
                             std::to_string(GetLastError()) + ")");

  // Release both native handles after collecting the process's exit status.
  CloseHandle(pi.hThread);
  WaitForSingleObject(pi.hProcess, INFINITE);
  DWORD code = 1;
  GetExitCodeProcess(pi.hProcess, &code);
  CloseHandle(pi.hProcess);
  return static_cast<int>(code);
#else
  // Preserve conventional shell statuses for child setup/exec failures and
  // signal termination, so callers receive familiar process exit codes.
  constexpr auto child_directory_failure = 126;
  constexpr auto child_execution_failure = 127;
  constexpr auto signal_exit_status_base = 128;

  const auto pid = fork();
  if (pid < 0)
    throw std::runtime_error("fork failed");

  // Only the child changes directory. execvp receives original argument
  // boundaries directly, so no shell interprets manifest text during ordinary
  // execution.
  if (pid == 0) {
    if (chdir(directory.c_str()) != 0)
      _exit(child_directory_failure);

    std::vector<char *> argv;
    argv.reserve(args.size() + 1U);
    for (const auto &arg : args)
      argv.push_back(const_cast<char *>(arg.c_str()));

    argv.push_back(nullptr);
    execvp(argv.front(), argv.data());
    _exit(child_execution_failure);
  }

  // Interrupted waits are retried; signal termination uses the shell
  // convention.
  int code;
  while (waitpid(pid, &code, 0) < 0)
    if (errno != EINTR)
      throw std::runtime_error("waitpid failed");

  return WIFEXITED(code) ? WEXITSTATUS(code)
                         : signal_exit_status_base + WTERMSIG(code);
#endif
}
}  // namespace clutch

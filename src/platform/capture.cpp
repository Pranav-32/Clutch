/* Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause */

/**
 * @file
 * @brief Read child stdout with native pipes while preserving argument
 * boundaries.
 */

#include "capture.hpp"
#include "platform/arguments.hpp"

#include <array>
#include <exception>
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
namespace {
// Bound remote ref listings and diagnostics without retaining unbounded output.
constexpr auto maximum_output_bytes = size_t{4} * 1024U * 1024U;
constexpr auto read_buffer_bytes = size_t{16} * 1024U;
}  // namespace

ProcessOutput capture(const std::vector<std::string> &arguments,
                      const fs::path &directory) {
  if (arguments.empty())
    throw std::runtime_error("empty capture command");

  for (const auto &argument : arguments)
    if (argument.find('\0') != std::string::npos)
      throw std::runtime_error("NUL in process argument");

  ProcessOutput result{1, {}};
  std::array<char, read_buffer_bytes> buffer{};
  bool overflow = false, read_error = false;
  std::exception_ptr allocation_error;
  const auto append = [&](const size_t count) {
    if (count > maximum_output_bytes - result.text.size())
      overflow = true;
    else if (!overflow) {
      try {
        result.text.append(buffer.data(), count);
      } catch (...) {
        // Drain and reap even if retaining output fails.
        allocation_error = std::current_exception();
        overflow = true;
      }
    }
  };
#ifdef _WIN32
  const auto command = command_line(arguments);
  if (command.size() > static_cast<size_t>(std::numeric_limits<int>::max()))
    throw std::runtime_error("capture command is too long");

  const auto length = MultiByteToWideChar(CP_UTF8,
                                          MB_ERR_INVALID_CHARS,
                                          command.data(),
                                          static_cast<int>(command.size()),
                                          nullptr,
                                          0);
  // CreateProcessW includes the terminating NUL in its character limit.
  constexpr auto windows_command_characters = 32767;
  if (!length || length >= windows_command_characters)
    throw std::runtime_error("invalid UTF-8 or oversized capture command");

  std::wstring wide(static_cast<size_t>(length), L'\0');
  MultiByteToWideChar(CP_UTF8,
                      MB_ERR_INVALID_CHARS,
                      command.data(),
                      static_cast<int>(command.size()),
                      wide.data(),
                      length);
  SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
  HANDLE reader = nullptr, writer = nullptr;
  if (!CreatePipe(&reader, &writer, &security, 0))
    throw std::runtime_error("cannot create child output pipe");

  if (!SetHandleInformation(reader, HANDLE_FLAG_INHERIT, 0)) {
    CloseHandle(reader);
    CloseHandle(writer);
    throw std::runtime_error("cannot protect child output reader");
  }

  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  startup.dwFlags = STARTF_USESTDHANDLES;
  startup.hStdOutput = writer;
  startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
  startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  PROCESS_INFORMATION process{};
  const auto started = CreateProcessW(nullptr,
                                      wide.data(),
                                      nullptr,
                                      nullptr,
                                      TRUE,
                                      0,
                                      nullptr,
                                      directory.c_str(),
                                      &startup,
                                      &process);
  CloseHandle(writer);
  if (!started) {
    CloseHandle(reader);
    throw std::runtime_error("cannot launch captured process: " +
                             arguments.front());
  }

  CloseHandle(process.hThread);
  DWORD count = 0;
  while (true) {
    if (!ReadFile(reader,
                  buffer.data(),
                  static_cast<DWORD>(buffer.size()),
                  &count,
                  nullptr)) {
      read_error = GetLastError() != ERROR_BROKEN_PIPE;
      break;
    }

    if (!count)
      break;

    append(count);
  }

  CloseHandle(reader);
  WaitForSingleObject(process.hProcess, INFINITE);
  DWORD status = 1;
  GetExitCodeProcess(process.hProcess, &status);
  CloseHandle(process.hProcess);
  result.status = static_cast<int>(status);
#else
  // Allocate before fork: the child performs only native descriptor/exec work.
  std::vector<char *> argv;
  argv.reserve(arguments.size() + 1);
  for (const auto &argument : arguments)
    argv.push_back(const_cast<char *>(argument.c_str()));
  argv.push_back(nullptr);
  int descriptors[2];
  if (pipe(descriptors) != 0)
    throw std::runtime_error("cannot create child output pipe");

  const auto child = fork();
  if (child < 0) {
    close(descriptors[0]);
    close(descriptors[1]);
    throw std::runtime_error("cannot fork captured process");
  }

  if (child == 0) {
    close(descriptors[0]);
    constexpr auto setup_failure = 126, execution_failure = 127;
    if (dup2(descriptors[1], STDOUT_FILENO) < 0 ||
        chdir(directory.c_str()) != 0)
      _exit(setup_failure);
    close(descriptors[1]);
    execvp(argv.front(), argv.data());
    _exit(execution_failure);
  }

  close(descriptors[1]);
  while (true) {
    const auto count = read(descriptors[0], buffer.data(), buffer.size());
    if (count > 0)
      append(static_cast<size_t>(count));
    else if (!count)
      break;
    else if (errno != EINTR) {
      read_error = true;
      break;
    }
  }

  close(descriptors[0]);
  int status = 0;
  while (waitpid(child, &status, 0) < 0)
    if (errno != EINTR)
      throw std::runtime_error("cannot wait for captured process");

  constexpr auto signal_status_base = 128;
  result.status = WIFEXITED(status) ? WEXITSTATUS(status)
                                    : signal_status_base + WTERMSIG(status);
#endif
  if (allocation_error)
    std::rethrow_exception(allocation_error);

  if (overflow || read_error)
    throw std::runtime_error(overflow ? "native tool output exceeds four MiB"
                                      : "cannot read native tool output");
  return result;
}
}  // namespace clutch

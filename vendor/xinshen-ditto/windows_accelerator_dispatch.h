#pragma once
#ifdef _WIN32
#include "avatar_runtime.h"
#include "cuda_device.h"
#include <optional>
#include <cwchar>
#include <iostream>
#include <vector>

namespace avatar {
inline std::wstring windowsArgument(const std::wstring &value) {
  std::wstring out = L"\"";
  size_t slashes = 0;
  for (wchar_t ch : value) {
    if (ch == L'\\') { ++slashes; continue; }
    out.append(ch == L'"' ? slashes * 2 + 1 : slashes, L'\\');
    slashes = 0;
    out += ch;
  }
  out.append(slashes * 2, L'\\');
  return out + L'"';
}
inline std::wstring utf16Argument(const char *value) {
  int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value, -1, nullptr, 0);
  if (size < 1) throw std::runtime_error("Invalid UTF-8 worker argument");
  std::wstring result(size, L'\0');
  MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value, -1, result.data(), size);
  result.pop_back();
  return result;
}
inline int runCudaChild(const std::filesystem::path &executable,
                        const std::vector<std::wstring> &arguments, ULONGLONG timeout = 0) {
  struct Handle { HANDLE value = nullptr; ~Handle() { if (value) CloseHandle(value); } };
  std::wstring command = windowsArgument(executable.wstring());
  for (const auto &arg : arguments) command += L" " + windowsArgument(arg);
  // The small child executable ships with the app; independently cached CUDA
  // DLLs must precede both System32's ORT and the parent app's DirectML ORT.
  std::wstring environment;
  bool pathFound = false;
  const auto required = GetEnvironmentVariableW(L"XINSHEN_CUDA_RUNTIME_DIR", nullptr, 0);
  std::wstring runtime(required, L'\0');
  if (required) { GetEnvironmentVariableW(L"XINSHEN_CUDA_RUNTIME_DIR", runtime.data(), required); runtime.resize(required - 1); }
  if (runtime.empty() || !std::filesystem::is_regular_file(std::filesystem::path(runtime) / "onnxruntime.dll"))
    throw std::runtime_error("Verified CUDA runtime directory is unavailable");
  // SetDllDirectory is inherited by Win32 children and precedes System32.
  // PATH and the current directory alone are searched after System32, which
  // can load the Windows ORT before main() and crash on an incompatible API.
  // Scope this to the isolated worker, restoring its setting after the child.
  struct DllDirectory {
    std::wstring previous;
    explicit DllDirectory(const std::wstring &directory) {
      const DWORD length = GetDllDirectoryW(0, nullptr);
      if (length) {
        previous.resize(length);
        previous.resize(GetDllDirectoryW(length, previous.data()));
      }
      if (!SetDllDirectoryW(directory.c_str())) throw std::runtime_error("Cannot select CUDA DLL directory");
    }
    ~DllDirectory() { SetDllDirectoryW(previous.empty() ? nullptr : previous.c_str()); }
  } dllDirectory(runtime);
  auto inherited = GetEnvironmentStringsW();
  if (!inherited) throw std::runtime_error("Cannot read worker environment");
  const auto prefix = (runtime.empty() ? L"" : runtime + L";") + executable.parent_path().wstring() + L";" +
                      executable.parent_path().parent_path().wstring() + L";";
  for (const wchar_t *p = inherited; *p; p += std::wcslen(p) + 1) {
    std::wstring item(p);
    if (_wcsnicmp(p, L"PATH=", 5) == 0) { item = item.substr(0, 5) + prefix + item.substr(5); pathFound = true; }
    environment += item; environment += L'\0';
  }
  FreeEnvironmentStringsW(inherited);
  if (!pathFound) environment += L"PATH=" + prefix + L'\0';
  environment += L'\0';
  Handle job{CreateJobObjectW(nullptr, nullptr)};
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
  limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
  if (!job.value || !SetInformationJobObject(job.value, JobObjectExtendedLimitInformation, &limits, sizeof(limits)))
    throw std::runtime_error("Cannot establish CUDA worker cancellation ownership");
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup); startup.dwFlags = STARTF_USESTDHANDLES;
  startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  startup.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
  startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
  PROCESS_INFORMATION child{};
  if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE,
      CREATE_SUSPENDED | CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
      environment.data(), runtime.empty() ? nullptr : runtime.c_str(), &startup, &child))
    throw std::runtime_error("Cannot start CUDA worker (Windows error " + std::to_string(GetLastError()) + ")");
  Handle process{child.hProcess}, thread{child.hThread};
  if (!AssignProcessToJobObject(job.value, process.value)) {
    TerminateProcess(process.value, 1);
    throw std::runtime_error("Cannot attach CUDA worker to cancellation job");
  }
  if (ResumeThread(thread.value) == static_cast<DWORD>(-1)) throw std::runtime_error("Cannot resume CUDA worker");
  const auto started = GetTickCount64();
  for (;;) {
    const auto state = WaitForSingleObject(process.value, 100);
    if (state == WAIT_OBJECT_0) break;
    if (state == WAIT_FAILED) throw std::runtime_error("Cannot wait for CUDA worker");
    checkCancelled();
    if (timeout && GetTickCount64() - started > timeout) throw std::runtime_error("CUDA execution probe timed out");
  }
  DWORD code = 1;
  if (!GetExitCodeProcess(process.value, &code)) throw std::runtime_error("Cannot read CUDA worker exit status");
  return static_cast<int>(code);
}
inline std::optional<int> dispatchCuda(int argc, char **argv) {
  if (argc < 2) return {};
  const std::string command = argv[1];
  if (command != "generate" && command != "generate-page" && command != "cuda-runtime-check") return {};
  if (command == "generate-page" && argc == 3 && !readJson(argv[2]).value("speech", true)) return {};
  wchar_t buffer[32768];
  const DWORD size = GetModuleFileNameW(nullptr, buffer, 32768);
  if (!size || size >= 32768) throw std::runtime_error("Cannot locate installed Ditto worker");
  const std::filesystem::path self(std::wstring(buffer, size));
  if (self.parent_path().filename() == L"cuda") return {};
  const auto cuda = self.parent_path() / "cuda" / "moss-ditto.exe";
  // CI exercises the same loader isolation without requiring an NVIDIA GPU.
  if (command == "cuda-runtime-check") return runCudaChild(cuda, {L"runtime-check"}, 30000);
  const char *configured = std::getenv("XINSHEN_ONNX_DEVICE");
  const auto requested = normalizedDevice(configured ? configured : "");
  if (requested == "cpu" || requested == "directml") return {};
  try {
    if (!std::filesystem::is_regular_file(cuda)) throw std::runtime_error("CUDA worker is missing");
    if (!GetEnvironmentVariableW(L"XINSHEN_CUDA_RUNTIME_DIR", nullptr, 0))
      throw std::runtime_error("CUDA components have not been prepared by the application");
    (void)cudaAdapter();
    const auto code = runCudaChild(cuda, {L"cuda-check"}, 90000);
    if (code != 0) throw std::runtime_error("CUDA Conv/MatMul/5D GridSample execution probe failed (exit " + std::to_string(code) + ")");
  } catch (const std::exception &error) {
    checkCancelled();
    if (requested == "cuda") throw;
    std::cerr << Json{{"event", "backend-unavailable"}, {"backend", "CUDA"},
        {"fallback", "DirectML"}, {"message", error.what()}}.dump() + '\n';
    return {};
  }
  std::cerr << Json{{"event", "runtime-selected"}, {"backend", "CUDA"},
      {"evidence", "cached-runtime; Conv-MatMul-5D-GridSample-probe-passed"}}.dump() + '\n';
  std::vector<std::wstring> arguments;
  for (int i = 1; i < argc; ++i) arguments.push_back(utf16Argument(argv[i]));
  // Inference errors are preserved. Only initialization is eligible for DML fallback.
  return runCudaChild(cuda, arguments);
}
} // namespace avatar
#endif

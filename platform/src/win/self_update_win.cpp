// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <windows.h>

#include "den/base/files.h"
#include "den/platform/pal.h"
#include "den/platform/self_update.h"

namespace den::pal::self_update {

namespace {

std::wstring Wide(const std::string& text) {
    if (text.empty()) {
        return {};
    }
    const int length = static_cast<int>(text.size());
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), length, nullptr, 0);
    std::wstring out(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), length, out.data(), size);
    return out;
}

void AppendArg(std::wstring& line, const std::wstring& arg) {
    if (!line.empty()) {
        line += L' ';
    }
    if (!arg.empty() && arg.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
        line += arg;
        return;
    }
    line += L'"';
    for (std::size_t i = 0; i < arg.size(); ++i) {
        std::size_t slashes = 0;
        while (i < arg.size() && arg[i] == L'\\') {
            ++slashes;
            ++i;
        }
        if (i == arg.size()) {
            line.append(slashes * 2, L'\\');
            break;
        }
        line.append(arg[i] == L'"' ? slashes * 2 + 1 : slashes, L'\\');
        line += arg[i];
    }
    line += L'"';
}

// A file can be made in the folder (a portable copy in Program Files cannot replace itself).
bool Writable(const std::filesystem::path& folder) {
    const std::filesystem::path probe = folder / ".emuden-write-probe";
    const HANDLE file = CreateFileW(probe.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                    FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    CloseHandle(file);
    return true;
}

} // namespace

Target Detect() {
    Target target;
    const std::filesystem::path dir = ExecutableDir();
    // tools/package.ps1 puts the licences beside the program; a build folder has none and sits
    // under a CMake cache.
    if (!IsRegularFile(dir / "emuden.exe") || !IsRegularFile(dir / "licences" / "EmuDEN.txt") ||
        IsRegularFile(dir.parent_path() / "CMakeCache.txt")) {
        target.reason = "This copy runs from a build folder, not the portable zip.";
        return target;
    }
    if (!Writable(dir)) {
        target.reason = "EmuDEN may not write to its own folder.";
        return target;
    }
    target.method = Method::WindowsPortable;
    target.location = dir;
    return target;
}

int ProcessId() {
    return static_cast<int>(GetCurrentProcessId());
}

bool WaitForExit(int pid, int timeout_ms) {
    const HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(pid));
    if (process == nullptr) {
        return true; // gone already (or never there)
    }
    const DWORD result = WaitForSingleObject(process, static_cast<DWORD>(timeout_ms));
    CloseHandle(process);
    return result == WAIT_OBJECT_0;
}

Status StartDetached(const std::filesystem::path& executable, const std::vector<std::string>& args) {
    std::wstring line;
    AppendArg(line, executable.wstring());
    for (const std::string& arg : args) {
        AppendArg(line, Wide(arg));
    }
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION info{};
    const std::filesystem::path cwd = executable.parent_path();
    // Out of any job the app is in (a launcher's), so it is not ended with the app; a job
    // that forbids that still lets it start inside.
    for (const DWORD flags : {DWORD{CREATE_BREAKAWAY_FROM_JOB | DETACHED_PROCESS}, DWORD{DETACHED_PROCESS}}) {
        if (CreateProcessW(executable.c_str(), line.data(), nullptr, nullptr, FALSE, flags, nullptr,
                           cwd.c_str(), &startup, &info)) {
            CloseHandle(info.hThread);
            CloseHandle(info.hProcess);
            return {};
        }
    }
    return Status::Fail("Could not start " + PathToUtf8(executable.filename()) + " (error " +
                        std::to_string(GetLastError()) + ").");
}

Status StartAfterExit(const std::filesystem::path& executable) {
    // The Windows helper restarts the app itself after the swap.
    return StartDetached(executable, {});
}

Status MakeExecutable(const std::filesystem::path&) {
    return {};
}

Status InstallPackage(const std::filesystem::path&) {
    return Status::Fail("Only Android installs packages.");
}

} // namespace den::pal::self_update

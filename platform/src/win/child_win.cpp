// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <windows.h>

#include <algorithm>
#include <map>

#include "den/base/files.h"
#include "den/platform/child.h"

namespace den::pal {

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

std::string Narrow(const std::wstring& text) {
    return PathToUtf8(std::filesystem::path(text));
}

// One argument quoted the way CommandLineToArgvW reads it back.
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

std::wstring Upper(std::wstring text) {
    CharUpperBuffW(text.data(), static_cast<DWORD>(text.size()));
    return text;
}

std::wstring EnvironmentBlock(const std::vector<std::pair<std::string, std::string>>& extra) {
    // Keyed by the upper-case name, which is also the order the block must be sorted in.
    std::map<std::wstring, std::wstring> lines;
    if (wchar_t* block = GetEnvironmentStringsW()) {
        for (const wchar_t* entry = block; *entry; entry += wcslen(entry) + 1) {
            const std::wstring line(entry);
            const std::size_t eq = line.find(L'=', 1);
            if (eq != std::wstring::npos) {
                lines[Upper(line.substr(0, eq))] = line;
            }
        }
        FreeEnvironmentStringsW(block);
    }
    for (const auto& [name, value] : extra) {
        lines[Upper(Wide(name))] = Wide(name) + L'=' + Wide(value);
    }
    std::wstring out;
    for (const auto& [name, line] : lines) {
        out += line;
        out += L'\0';
    }
    out += L'\0';
    return out;
}

struct JobPids {
    JOBOBJECT_BASIC_PROCESS_ID_LIST list;
    ULONG_PTR more[255];
    std::vector<HWND>* asked = nullptr;   // RequestClose: the windows that were asked
    std::vector<HWND>* dialogs = nullptr; // ConfirmClose: the visible dialogs they own
};

bool Has(const std::vector<HWND>& list, HWND window) {
    return std::find(list.begin(), list.end(), window) != list.end();
}

// The main windows only: visible, with no owner. Dialogs and helper windows follow them.
BOOL CALLBACK CloseWindowOfJob(HWND window, LPARAM param) {
    auto* pids = reinterpret_cast<JobPids*>(param);
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    for (DWORD i = 0; i < pids->list.NumberOfProcessIdsInList; ++i) {
        if (pids->list.ProcessIdList[i] == pid && IsWindowVisible(window) &&
            GetWindow(window, GW_OWNER) == nullptr) {
            if (PostMessageW(window, WM_CLOSE, 0, 0)) {
                pids->asked->push_back(window);
            }
            break;
        }
    }
    return TRUE;
}

BOOL CALLBACK FindDialogOfJob(HWND window, LPARAM param) {
    auto* pids = reinterpret_cast<JobPids*>(param);
    if (IsWindowVisible(window) && IsWindowEnabled(window) &&
        Has(*pids->asked, GetWindow(window, GW_OWNER))) {
        pids->dialogs->push_back(window);
    }
    return TRUE;
}

class WinChild final : public Child {
public:
    WinChild(HANDLE job, HANDLE process) : job_(job), process_(process) {}
    ~WinChild() override {
        Kill();
        CloseHandle(process_);
        CloseHandle(job_); // kill-on-close ends whatever is left in the job
    }

    std::optional<int> Poll() override {
        if (!exit_ && WaitForSingleObject(process_, 0) == WAIT_OBJECT_0) {
            DWORD code = 0;
            GetExitCodeProcess(process_, &code);
            exit_ = static_cast<int>(code);
        }
        return exit_;
    }

    bool RequestClose() override {
        if (Poll()) {
            return false;
        }
        JobPids pids{};
        if (!QueryInformationJobObject(job_, JobObjectBasicProcessIdList, &pids, sizeof(pids),
                                       nullptr)) {
            pids.list.NumberOfProcessIdsInList = 1;
            pids.list.ProcessIdList[0] = GetProcessId(process_);
        }
        asked_.clear();
        answered_.clear();
        pids.asked = &asked_;
        EnumWindows(CloseWindowOfJob, reinterpret_cast<LPARAM>(&pids));
        return !asked_.empty();
    }

    bool ConfirmClose() override {
        if (asked_.empty() || Poll()) {
            return false;
        }
        std::vector<HWND> dialogs;
        JobPids pids{};
        pids.asked = &asked_;
        pids.dialogs = &dialogs;
        EnumWindows(FindDialogOfJob, reinterpret_cast<LPARAM>(&pids));
        if (answered_.empty()) {
            answered_ = dialogs;
        }
        bool sent = false;
        for (const HWND dialog : dialogs) {
            // Never a dialog that came up later: that one is not the question.
            if (Has(answered_, dialog)) {
                const LPARAM scan = MapVirtualKeyW(VK_RETURN, MAPVK_VK_TO_VSC) << 16;
                PostMessageW(dialog, WM_KEYDOWN, VK_RETURN, scan | 1);
                PostMessageW(dialog, WM_KEYUP, VK_RETURN, scan | 0xC0000001);
                sent = true;
            }
        }
        return sent;
    }

    void Kill() override {
        if (!Poll()) {
            TerminateJobObject(job_, 1);
            WaitForSingleObject(process_, 2000);
            Poll();
        }
    }

private:
    HANDLE job_;
    HANDLE process_;
    std::optional<int> exit_;
    std::vector<HWND> asked_;
    std::vector<HWND> answered_;
};

std::string LastError() {
    const DWORD code = GetLastError();
    wchar_t* text = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                       FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, code, 0, reinterpret_cast<wchar_t*>(&text), 0, nullptr);
    std::string out = text ? Trim(Narrow(text)) : std::string();
    if (text) {
        LocalFree(text);
    }
    return out.empty() ? "error " + std::to_string(code) : out;
}

} // namespace

Result<std::unique_ptr<Child>> StartChild(const ChildDesc& desc) {
    const std::string name = PathToUtf8(desc.executable);
    const HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (!job) {
        return Status::Fail("could not start " + name + ": " + LastError());
    }
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));

    std::wstring line;
    AppendArg(line, desc.executable.wstring());
    for (const std::string& arg : desc.args) {
        AppendArg(line, Wide(arg));
    }
    std::wstring env = EnvironmentBlock(desc.env);
    const std::filesystem::path cwd =
        desc.working_dir.empty() ? desc.executable.parent_path() : desc.working_dir;

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION info{};
    // Suspended, so the child is in the job before it runs a single instruction.
    if (!CreateProcessW(desc.executable.c_str(), line.data(), nullptr, nullptr, FALSE,
                        CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT, env.data(),
                        cwd.empty() ? nullptr : cwd.c_str(), &startup, &info)) {
        const std::string error = LastError();
        CloseHandle(job);
        return Status::Fail("could not start " + name + ": " + error);
    }
    if (!AssignProcessToJobObject(job, info.hProcess)) {
        const std::string error = LastError();
        TerminateProcess(info.hProcess, 1);
        CloseHandle(info.hThread);
        CloseHandle(info.hProcess);
        CloseHandle(job);
        return Status::Fail("could not start " + name + ": " + error);
    }
    ResumeThread(info.hThread);
    CloseHandle(info.hThread);
    return std::unique_ptr<Child>(new WinChild(job, info.hProcess));
}

} // namespace den::pal

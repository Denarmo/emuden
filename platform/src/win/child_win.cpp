// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <map>
#include <mutex>

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

// The app's ends of a child's standard input and output.
class PipeStream final : public Stream {
public:
    PipeStream(HANDLE read, HANDLE write) : read_(read), write_(write) {}
    ~PipeStream() override {
        CloseHandle(read_);
        if (write_) {
            CloseHandle(write_);
        }
    }

    bool Read(void* data, std::size_t size) override {
        auto* out = static_cast<char*>(data);
        while (size > 0 && !closed_) {
            DWORD got = 0;
            const DWORD chunk = static_cast<DWORD>(size > (1u << 20) ? (1u << 20) : size);
            if (!ReadFile(read_, out, chunk, &got, nullptr) || got == 0) {
                return false;
            }
            out += got;
            size -= got;
        }
        return size == 0;
    }

    bool Write(const void* data, std::size_t size) override {
        std::lock_guard lock(write_lock_);
        const auto* in = static_cast<const char*>(data);
        while (size > 0) {
            DWORD sent = 0;
            const DWORD chunk = static_cast<DWORD>(size > (1u << 20) ? (1u << 20) : size);
            if (!write_ || !WriteFile(write_, in, chunk, &sent, nullptr) || sent == 0) {
                return false;
            }
            in += sent;
            size -= sent;
        }
        return true;
    }

    void Close() override {
        closed_ = true;
        {
            std::lock_guard lock(write_lock_);
            if (write_) {
                CloseHandle(write_); // the child reads the end of its input
                write_ = nullptr;
            }
        }
        CancelIoEx(read_, nullptr);
    }

private:
    HANDLE read_;
    HANDLE write_;
    std::mutex write_lock_;
    std::atomic<bool> closed_{false};
};

// A child's standard input, output and error while it is being started.
struct ChildPipes {
    HANDLE inherited[3] = {nullptr, nullptr, nullptr}; // the child's input, output, error
    HANDLE our_in = nullptr, our_out = nullptr;

    ~ChildPipes() {
        CloseChildEnds();
        for (HANDLE handle : {our_in, our_out}) {
            if (handle) {
                CloseHandle(handle);
            }
        }
    }

    bool Open(const std::filesystem::path& log_file) {
        SECURITY_ATTRIBUTES inherit{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
        if (!CreatePipe(&inherited[0], &our_in, &inherit, 0) || !CreatePipe(&our_out, &inherited[1], &inherit, 0)) {
            return false;
        }
        SetHandleInformation(our_in, HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(our_out, HANDLE_FLAG_INHERIT, 0);
        HANDLE error = log_file.empty() ? CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                                      &inherit, OPEN_EXISTING, 0, nullptr)
                                        : CreateFileW(log_file.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &inherit,
                                                      CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        inherited[2] = error == INVALID_HANDLE_VALUE ? nullptr : error;
        return inherited[2] != nullptr;
    }

    // The child has its own copies once it was created.
    void CloseChildEnds() {
        for (HANDLE& handle : inherited) {
            if (handle) {
                CloseHandle(handle);
                handle = nullptr;
            }
        }
    }

    std::unique_ptr<PipeStream> TakeStream() {
        auto stream = std::make_unique<PipeStream>(our_out, our_in);
        our_in = our_out = nullptr;
        return stream;
    }
};

class WinChild final : public Child {
public:
    WinChild(HANDLE job, HANDLE process, std::unique_ptr<PipeStream> channel)
        : job_(job), process_(process), channel_(std::move(channel)) {}
    ~WinChild() override {
        Kill();
        channel_.reset();
        CloseHandle(process_);
        CloseHandle(job_); // kill-on-close ends whatever is left in the job
    }

    Stream* Channel() override {
        return channel_.get();
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
    std::unique_ptr<PipeStream> channel_;
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

    ChildPipes pipes;
    if (desc.channel && !pipes.Open(desc.log_file)) {
        const std::string error = LastError();
        CloseHandle(job);
        return Status::Fail("could not start " + name + ": " + error);
    }
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    DWORD flags = CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT;
    std::vector<char> attributes;
    if (desc.channel) {
        // The pipes and the log are the only handles the child inherits.
        SIZE_T size = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
        attributes.resize(size);
        startup.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());
        InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0, &size);
        UpdateProcThreadAttribute(startup.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, pipes.inherited,
                                  sizeof(HANDLE) * 3, nullptr, nullptr);
        startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
        startup.StartupInfo.hStdInput = pipes.inherited[0];
        startup.StartupInfo.hStdOutput = pipes.inherited[1];
        startup.StartupInfo.hStdError = pipes.inherited[2];
        flags |= EXTENDED_STARTUPINFO_PRESENT | CREATE_NO_WINDOW;
    }
    PROCESS_INFORMATION info{};
    // Suspended, so the child is in the job before it runs a single instruction.
    const BOOL created = CreateProcessW(desc.executable.c_str(), line.data(), nullptr, nullptr,
                                        desc.channel ? TRUE : FALSE, flags, env.data(),
                                        cwd.empty() ? nullptr : cwd.c_str(), &startup.StartupInfo, &info);
    if (startup.lpAttributeList) {
        DeleteProcThreadAttributeList(startup.lpAttributeList);
    }
    pipes.CloseChildEnds();
    if (!created) {
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
    return std::unique_ptr<Child>(new WinChild(job, info.hProcess, desc.channel ? pipes.TakeStream() : nullptr));
}

} // namespace den::pal

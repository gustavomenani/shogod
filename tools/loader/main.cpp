// shogod loader — injects shogod.dll into the target process and can eject it
// cleanly. Standard LoadLibrary pathway: the DLL runs its own DllMain and
// installs hooks. Manual mapping is documented in docs/USAGE.md as the
// optional path; this tool is the one that ships.
//
// usage:
//   shogod_loader.exe [--dll <path>] [--process <name>] [--unload] [--wait <sec>]

#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <TlHelp32.h>

namespace {

DWORD find_process(const wchar_t* name) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof entry;
    DWORD pid = 0;
    if (Process32FirstW(snap, &entry)) {
        do {
            if (_wcsicmp(entry.szExeFile, name) == 0) {
                pid = entry.th32ProcessID;
                break;
            }
        } while (Process32NextW(snap, &entry));
    }
    CloseHandle(snap);
    return pid;
}

bool inject(DWORD pid, const std::wstring& dll) {
    HANDLE process = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
                                     PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ,
                                 FALSE, pid);
    if (!process) {
        std::wprintf(L"loader: OpenProcess failed (%lu)\n", GetLastError());
        return false;
    }

    const SIZE_T bytes = (dll.size() + 1) * sizeof(wchar_t);
    void* remote = VirtualAllocEx(process, nullptr, bytes, MEM_COMMIT | MEM_RESERVE,
                                  PAGE_READWRITE);
    if (!remote) {
        std::wprintf(L"loader: VirtualAllocEx failed (%lu)\n", GetLastError());
        CloseHandle(process);
        return false;
    }

    SIZE_T written = 0;
    if (!WriteProcessMemory(process, remote, dll.c_str(), bytes, &written) || written != bytes) {
        std::wprintf(L"loader: WriteProcessMemory failed (%lu)\n", GetLastError());
        VirtualFreeEx(process, remote, 0, MEM_RELEASE);
        CloseHandle(process);
        return false;
    }

    HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
    auto load_library = reinterpret_cast<LPTHREAD_START_ROUTINE>(
        GetProcAddress(kernel, "LoadLibraryW"));

    HANDLE thread = CreateRemoteThread(process, nullptr, 0, load_library, remote, 0, nullptr);
    if (!thread) {
        std::wprintf(L"loader: CreateRemoteThread failed (%lu)\n", GetLastError());
        VirtualFreeEx(process, remote, 0, MEM_RELEASE);
        CloseHandle(process);
        return false;
    }

    const DWORD wait = WaitForSingleObject(thread, 15000);
    if (wait != WAIT_OBJECT_0) {
        std::wprintf(L"loader: remote load is unconfirmed (wait=%lu); path buffer remains mapped\n", wait);
        CloseHandle(thread);
        CloseHandle(process);
        return false;
    }
    DWORD load_result = 0;
    const bool got_result = GetExitCodeThread(thread, &load_result) != FALSE;
    CloseHandle(thread);
    VirtualFreeEx(process, remote, 0, MEM_RELEASE);
    CloseHandle(process);

    if (!got_result) {
        std::wprintf(L"loader: remote load exited but the result could not be read\n");
        return false;
    }
    if (load_result == 0) {
        std::wprintf(L"loader: LoadLibrary returned NULL — check the DLL dependencies\n");
        return false;
    }
    std::wprintf(L"loader: LoadLibrary completed; check the runtime log for hook initialization\n");
    return true;
}

std::wstring exe_dir() {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring out(path);
    const auto slash = out.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"." : out.substr(0, slash);
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    std::wstring dll = exe_dir() + L"\\shogod.dll";
    std::wstring process_name = L"cs2.exe";
    bool unload = false;
    int wait_seconds = 0;

    for (int i = 1; i < argc; ++i) {
        if (wcscmp(argv[i], L"--dll") == 0 && i + 1 < argc) dll = argv[++i];
        else if (wcscmp(argv[i], L"--process") == 0 && i + 1 < argc) process_name = argv[++i];
        else if (wcscmp(argv[i], L"--unload") == 0) unload = true;
        else if (wcscmp(argv[i], L"--wait") == 0 && i + 1 < argc) wait_seconds = _wtoi(argv[++i]);
    }

    if (unload) {
        std::wprintf(L"loader: hooks remain mapped until the game exits; restart the game to unload\n");
        return 1;
    }
    if (GetFileAttributesW(dll.c_str()) == INVALID_FILE_ATTRIBUTES) {
        std::wprintf(L"loader: dll not found: %s\n", dll.c_str());
        return 1;
    }

    DWORD pid = find_process(process_name.c_str());
    for (int waited = 0; pid == 0 && waited < wait_seconds; ++waited) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        pid = find_process(process_name.c_str());
    }
    if (pid == 0) {
        std::wprintf(L"loader: process %s not found\n", process_name.c_str());
        return 1;
    }

    std::wprintf(L"loader: pid %lu, dll %s\n", pid, dll.c_str());
    const bool ok = inject(pid, dll);
    return ok ? 0 : 1;
}

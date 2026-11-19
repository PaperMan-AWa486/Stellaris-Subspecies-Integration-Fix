#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <limits>

namespace {

constexpr DWORD kTimestamp = 0x6af23393;
constexpr DWORD kImageSize = 0x037fa000;
constexpr std::size_t kRightsCall = 0x71c069;
constexpr std::size_t kSetDefaultCall = 0x719378;
constexpr std::size_t kRightsEqual = 0x8f21f0;
constexpr std::size_t kSetDefault = 0x6f82b0;
constexpr std::size_t kReturnToMerge = 0x71c38d;
constexpr std::size_t kStubSpacing = 64;

constexpr std::array<BYTE, 8> kRightsWord = {
    0xcf, 0xe8, 0x82, 0x61, 0x1d, 0x00, 0x84, 0xc0,
};
constexpr std::array<BYTE, 8> kSetDefaultWord = {
    0xe8, 0x33, 0xef, 0xfd, 0xff, 0x4c, 0x8b, 0x0d,
};
constexpr std::array<BYTE, 9> kMergeCaller = {
    0xe8, 0xb3, 0xcf, 0xff, 0xff, 0xc7, 0x44, 0x24, 0x20,
};
constexpr std::array<BYTE, 9> kReadIntegrationRight = {
    0x48, 0x8b, 0x48, 0x50, 0x80, 0xb9, 0x10, 0x07, 0x00,
};
constexpr std::array<BYTE, 16> kPopulationConversionFrame = {
    0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55,
    0x41, 0x56, 0x41, 0x57, 0x48, 0x8d, 0x6c, 0x24,
};

// Windows x64 leaf stubs. Absolute tail jumps return through the original call.
void WriteStubs(BYTE* page, const BYTE* base) {
    // Compare active rights only if the source is not already integrating.
    BYTE rights[] = {
        0x48, 0x8b, 0x41, 0x50,                   // mov rax, [rcx+50h]
        0x80, 0xb8, 0x10, 0x07, 0x00, 0x00, 0x00, // cmp byte ptr [rax+710h], 0
        0x75, 0x0e,                               // jne reject
        0xff, 0x25, 0x00, 0x00, 0x00, 0x00,       // jmp [rip]
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x32, 0xc0, 0xc3,                         // reject: xor al, al; ret
    };
    const BYTE* compare = base + kRightsEqual;
    std::memcpy(rights + 19, &compare, sizeof(compare));
    std::memcpy(page, rights, sizeof(rights));

    // Eight register pushes, 88h of stack space and the stub's own return address.
    BYTE setDefault[] = {
        0x48, 0xb8,                               // mov rax, merge return address
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x48, 0x39, 0x84, 0x24, 0xd0, 0x00, 0x00, 0x00, // cmp [rsp+d0h], rax
        0x74, 0x0e,                               // je skip
        0xff, 0x25, 0x00, 0x00, 0x00, 0x00,       // jmp [rip]
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0xc3,                                     // skip: ret
    };
    const BYTE* mergeReturn = base + kReturnToMerge;
    const BYTE* setter = base + kSetDefault;
    std::memcpy(setDefault + 2, &mergeReturn, sizeof(mergeReturn));
    std::memcpy(setDefault + 26, &setter, sizeof(setter));
    std::memcpy(page + kStubSpacing, setDefault, sizeof(setDefault));
}

bool ExpectedImage(const BYTE* base) {
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < sizeof(*dos) ||
        dos->e_lfanew > 0x1000) {
        return false;
    }
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    return nt->Signature == IMAGE_NT_SIGNATURE &&
           nt->FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64 &&
           nt->FileHeader.TimeDateStamp == kTimestamp &&
           nt->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC &&
           nt->OptionalHeader.SizeOfImage == kImageSize &&
           std::memcmp(base + kRightsCall - 1, kRightsWord.data(), kRightsWord.size()) == 0 &&
           std::memcmp(base + kSetDefaultCall, kSetDefaultWord.data(), kSetDefaultWord.size()) == 0 &&
           std::memcmp(base + 0x71c388, kMergeCaller.data(), kMergeCaller.size()) == 0 &&
           std::memcmp(base + 0x71b34f, kReadIntegrationRight.data(),
                       kReadIntegrationRight.size()) == 0 &&
           std::memcmp(base + 0x71934f, kPopulationConversionFrame.data(),
                       kPopulationConversionFrame.size()) == 0 &&
           std::memcmp(base + 0x719360, "\x48\x81\xec\x88\x00\x00\x00", 7) == 0;
}

BYTE* AllocateStubs(const BYTE* base) {
    SYSTEM_INFO info{};
    GetSystemInfo(&info);
    const auto granularity = static_cast<std::uintptr_t>(info.dwAllocationGranularity);
    const auto imageStart = reinterpret_cast<std::uintptr_t>(base);
    auto cursor = imageStart + kImageSize;
    const auto limit = imageStart + std::numeric_limits<std::int32_t>::max();
    while (cursor < limit) {
        MEMORY_BASIC_INFORMATION region{};
        if (!VirtualQuery(reinterpret_cast<void*>(cursor), &region, sizeof(region))) {
            break;
        }
        const auto end = reinterpret_cast<std::uintptr_t>(region.BaseAddress) + region.RegionSize;
        const auto aligned = (cursor + granularity - 1) & ~(granularity - 1);
        if (region.State == MEM_FREE && aligned < limit && aligned < end &&
            end - aligned >= info.dwPageSize) {
            if (auto* page = static_cast<BYTE*>(VirtualAlloc(
                    reinterpret_cast<void*>(aligned), info.dwPageSize,
                    MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE))) {
                return page;
            }
        }
        cursor = end;
    }
    return nullptr;
}

LONG64 WordWithCall(std::array<BYTE, 8> word, std::size_t offset,
                    const BYTE* site, const BYTE* target) {
    const auto delta = reinterpret_cast<std::intptr_t>(target)
                     - reinterpret_cast<std::intptr_t>(site + 5);
    const auto displacement = static_cast<std::int32_t>(delta);
    std::memcpy(word.data() + offset + 1, &displacement, sizeof(displacement));
    LONG64 result;
    std::memcpy(&result, word.data(), sizeof(result));
    return result;
}

LONG64 OriginalWord(const std::array<BYTE, 8>& word) {
    LONG64 result;
    std::memcpy(&result, word.data(), sizeof(result));
    return result;
}

const char* Install(BYTE* base) {
    if (!ExpectedImage(base)) {
        return "SKIPPED: unsupported Stellaris executable or patch sites already modified\r\n";
    }

    BYTE* stubs = AllocateStubs(base);
    if (!stubs) {
        return "FAILED: no space for the two call stubs\r\n";
    }
    WriteStubs(stubs, base);
    DWORD oldStubProtection;
    if (!VirtualProtect(stubs, 2 * kStubSpacing, PAGE_EXECUTE_READ, &oldStubProtection) ||
        !FlushInstructionCache(GetCurrentProcess(), stubs, 2 * kStubSpacing)) {
        VirtualFree(stubs, 0, MEM_RELEASE);
        return "FAILED: could not prepare executable call stubs\r\n";
    }

    // Each call fits within a single aligned 8-byte word. Keep the adjoining instructions.
    auto* rightsWord = reinterpret_cast<volatile LONG64*>(base + kRightsCall - 1);
    auto* defaultWord = reinterpret_cast<volatile LONG64*>(base + kSetDefaultCall);
    DWORD rightsProtection;
    if (!VirtualProtect(base + kRightsCall - 1, 8, PAGE_EXECUTE_READWRITE, &rightsProtection)) {
        VirtualFree(stubs, 0, MEM_RELEASE);
        return "FAILED: could not make the rights call writable\r\n";
    }
    DWORD defaultProtection;
    if (!VirtualProtect(base + kSetDefaultCall, 8, PAGE_EXECUTE_READWRITE, &defaultProtection)) {
        VirtualProtect(base + kRightsCall - 1, 8, rightsProtection, &rightsProtection);
        VirtualFree(stubs, 0, MEM_RELEASE);
        return "FAILED: could not make the default-template call writable\r\n";
    }

    const auto oldRights = OriginalWord(kRightsWord);
    const auto oldDefault = OriginalWord(kSetDefaultWord);
    const auto newRights = WordWithCall(kRightsWord, 1, base + kRightsCall, stubs);
    const auto newDefault = WordWithCall(kSetDefaultWord, 0, base + kSetDefaultCall,
                                         stubs + kStubSpacing);
    const bool installedDefault =
        InterlockedCompareExchange64(defaultWord, newDefault, oldDefault) == oldDefault;
    const bool installedRights = installedDefault &&
        InterlockedCompareExchange64(rightsWord, newRights, oldRights) == oldRights;
    if (installedDefault && !installedRights) {
        InterlockedCompareExchange64(defaultWord, oldDefault, newDefault);
    }
    FlushInstructionCache(GetCurrentProcess(), base + kRightsCall - 1, 8);
    FlushInstructionCache(GetCurrentProcess(), base + kSetDefaultCall, 8);

    DWORD unused;
    const bool restoredRights =
        VirtualProtect(base + kRightsCall - 1, 8, rightsProtection, &unused) != FALSE;
    const bool restoredDefault =
        VirtualProtect(base + kSetDefaultCall, 8, defaultProtection, &unused) != FALSE;
    if (!installedRights) {
        if (!installedDefault) {
            VirtualFree(stubs, 0, MEM_RELEASE);
        }
        return "FAILED: patch site changed during installation\r\n";
    }
    if (!restoredRights || !restoredDefault) {
        return "INSTALLED: both Stellaris 4.5.4 hooks; failed to restore a code page protection\r\n";
    }
    return "INSTALLED: Stellaris 4.5.4 integration and default-template hooks\r\n";
}

void Log(HINSTANCE dll, const char* message) {
    wchar_t path[MAX_PATH];
    const DWORD length = GetModuleFileNameW(dll, path, ARRAYSIZE(path));
    if (!length || length >= ARRAYSIZE(path)) {
        return;
    }
    wchar_t* filename = std::wcsrchr(path, L'\\');
    constexpr wchar_t logName[] = L"subspecies_integration_fix.log";
    if (!filename || filename + ARRAYSIZE(logName) >= path + ARRAYSIZE(path)) {
        return;
    }
    std::memcpy(filename + 1, logName, sizeof(logName));
    HANDLE log = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                             nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (log != INVALID_HANDLE_VALUE) {
        DWORD written;
        WriteFile(log, message, static_cast<DWORD>(std::strlen(message)), &written, nullptr);
        CloseHandle(log);
    }
}

INIT_ONCE versionInit = INIT_ONCE_STATIC_INIT;
HMODULE version;
BOOL CALLBACK LoadSystemVersion(PINIT_ONCE, PVOID, PVOID*) {
    // Resolve the system DLL by its absolute path to avoid finding this proxy again.
    wchar_t path[MAX_PATH];
    constexpr wchar_t suffix[] = L"\\version.dll";
    const UINT length = GetSystemDirectoryW(path, ARRAYSIZE(path));
    if (!length || length >= ARRAYSIZE(path) - ARRAYSIZE(suffix)) {
        return FALSE;
    }
    std::memcpy(path + length, suffix, sizeof(suffix));
    version = LoadLibraryExW(path, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    return version != nullptr;
}

FARPROC SystemExport(const char* name) {
    if (!InitOnceExecuteOnce(&versionInit, LoadSystemVersion, nullptr, nullptr)) {
        SetLastError(ERROR_MOD_NOT_FOUND);
        return nullptr;
    }
    return GetProcAddress(version, name);
}

} // namespace

extern "C" DWORD WINAPI ProxyVerFindFileA(DWORD flags, LPCSTR file, LPCSTR windowsDir,
    LPCSTR appDir, LPSTR currentDir, PUINT currentLength, LPSTR destDir, PUINT destLength) {
    const auto forward = reinterpret_cast<decltype(&VerFindFileA)>(SystemExport("VerFindFileA"));
    return forward ? forward(flags, file, windowsDir, appDir, currentDir, currentLength,
                             destDir, destLength) : VFF_BUFFTOOSMALL;
}

extern "C" DWORD WINAPI ProxyVerFindFileW(DWORD flags, LPCWSTR file, LPCWSTR windowsDir,
    LPCWSTR appDir, LPWSTR currentDir, PUINT currentLength, LPWSTR destDir, PUINT destLength) {
    const auto forward = reinterpret_cast<decltype(&VerFindFileW)>(SystemExport("VerFindFileW"));
    return forward ? forward(flags, file, windowsDir, appDir, currentDir, currentLength,
                             destDir, destLength) : VFF_BUFFTOOSMALL;
}

extern "C" DWORD WINAPI ProxyVerInstallFileA(DWORD flags, LPCSTR src, LPCSTR dest,
    LPCSTR srcDir, LPCSTR destDir, LPCSTR currentDir, LPSTR tempFile, PUINT tempLength) {
    const auto forward = reinterpret_cast<decltype(&VerInstallFileA)>(SystemExport("VerInstallFileA"));
    return forward ? forward(flags, src, dest, srcDir, destDir, currentDir, tempFile,
                             tempLength) : VIF_CANNOTREADSRC;
}

extern "C" DWORD WINAPI ProxyVerInstallFileW(DWORD flags, LPCWSTR src, LPCWSTR dest,
    LPCWSTR srcDir, LPCWSTR destDir, LPCWSTR currentDir, LPWSTR tempFile, PUINT tempLength) {
    const auto forward = reinterpret_cast<decltype(&VerInstallFileW)>(SystemExport("VerInstallFileW"));
    return forward ? forward(flags, src, dest, srcDir, destDir, currentDir, tempFile,
                             tempLength) : VIF_CANNOTREADSRC;
}

BOOL WINAPI DllMain(HINSTANCE dll, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        wchar_t path[MAX_PATH];
        const DWORD length = GetModuleFileNameW(nullptr, path, ARRAYSIZE(path));
        const wchar_t* separator = length && length < ARRAYSIZE(path) ? std::wcsrchr(path, L'\\') : nullptr;
        if (separator && CompareStringOrdinal(separator + 1, -1, L"stellaris.exe", -1, TRUE) == CSTR_EQUAL) {
            Log(dll, Install(reinterpret_cast<BYTE*>(GetModuleHandleW(nullptr))));
        }
    }
    return TRUE;
}

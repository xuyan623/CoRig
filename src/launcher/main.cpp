#include "../core/container.h"

#include <windows.h>
#include <bcrypt.h>
#include <shellapi.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <string>
#include <vector>

#include <string.h>

namespace {

constexpr int kExitOk = 0;
constexpr int kExitCorrupt = 2;
constexpr int kExitUnwritable = 3;
constexpr int kExitLaunch = 4;
constexpr int kExitVersion = 5;

std::wstring exePath()
{
    std::vector<wchar_t> buffer(1024);
    for (;;) {
        const auto length = GetModuleFileNameW(nullptr,buffer.data(),static_cast<DWORD>(buffer.size()));
        if (length == 0) return {};
        if (length < buffer.size() - 1) return std::wstring(buffer.data(),length);
        buffer.resize(buffer.size() * 2);
    }
}

std::wstring parentDirectory(const std::wstring &path)
{
    const auto position = path.find_last_of(L"\\/");
    return position == std::wstring::npos ? std::wstring() : path.substr(0,position);
}

std::wstring fileStem(const std::wstring &path)
{
    const auto position = path.find_last_of(L"\\/");
    auto name = position == std::wstring::npos ? path : path.substr(position + 1);
    const auto dot = name.find_last_of(L'.');
    if (dot != std::wstring::npos) name.resize(dot);
    return name;
}

std::string toUtf8(const std::wstring &text)
{
    if (text.empty()) return {};
    const auto size = WideCharToMultiByte(CP_UTF8,0,text.data(),static_cast<int>(text.size()),
                                          nullptr,0,nullptr,nullptr);
    if (size <= 0) return {};
    std::string result(static_cast<std::size_t>(size),'\0');
    WideCharToMultiByte(CP_UTF8,0,text.data(),static_cast<int>(text.size()),
                        result.data(),size,nullptr,nullptr);
    return result;
}

std::wstring toWide(const std::string &text)
{
    if (text.empty()) return {};
    const auto size = MultiByteToWideChar(CP_UTF8,0,text.data(),static_cast<int>(text.size()),nullptr,0);
    if (size <= 0) return {};
    std::wstring result(static_cast<std::size_t>(size),L'\0');
    MultiByteToWideChar(CP_UTF8,0,text.data(),static_cast<int>(text.size()),result.data(),size);
    return result;
}

std::string jsonEscape(const std::string &text)
{
    std::string out;
    out.reserve(text.size() + 8);
    for (const auto character : text) {
        switch (character) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default: out += character; break;
        }
    }
    return out;
}

HANDLE stdHandle(DWORD kind)
{
    auto handle = GetStdHandle(kind);
    if (handle != nullptr && handle != INVALID_HANDLE_VALUE) return handle;
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        handle = CreateFileW(L"CONOUT$",GENERIC_WRITE,FILE_SHARE_READ | FILE_SHARE_WRITE,
                             nullptr,OPEN_EXISTING,0,nullptr);
        if (handle != INVALID_HANDLE_VALUE) return handle;
    }
    return nullptr;
}

void writeLine(const std::string &line, DWORD kind)
{
    const auto handle = stdHandle(kind);
    if (!handle) return;
    DWORD written = 0;
    WriteFile(handle,line.data(),static_cast<DWORD>(line.size()),&written,nullptr);
    WriteFile(handle,"\n",1,&written,nullptr);
}

std::string diagnostic(const char *code, const char *phase, const std::string &detail)
{
    return std::string("{\"code\":\"") + code + "\",\"phase\":\"" + phase +
           "\",\"detail\":\"" + jsonEscape(detail) + "\"}";
}

int fail(const std::string &code, const std::string &phase, const std::string &detail, int exitCode)
{
    const auto line = std::string("{\"code\":\"") + jsonEscape(code) + "\",\"phase\":\"" +
                      jsonEscape(phase) + "\",\"detail\":\"" + jsonEscape(detail) + "\"}";
    writeLine(line,STD_ERROR_HANDLE);
    return exitCode;
}

class Sha256 {
public:
    Sha256() = default;
    ~Sha256()
    {
        if (hash_) BCryptDestroyHash(hash_);
        if (algorithm_) BCryptCloseAlgorithmProvider(algorithm_,0);
    }
    Sha256(const Sha256 &) = delete;
    Sha256 &operator=(const Sha256 &) = delete;

    bool start()
    {
        if (BCryptOpenAlgorithmProvider(&algorithm_,BCRYPT_SHA256_ALGORITHM,nullptr,0) < 0) return false;
        DWORD length = 0, written = 0;
        if (BCryptGetProperty(algorithm_,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&length),
                              sizeof(length),&written,0) < 0)
            return false;
        object_.resize(length);
        return BCryptCreateHash(algorithm_,&hash_,object_.data(),length,nullptr,0,0) >= 0;
    }
    bool update(const void *data, std::size_t size)
    {
        return BCryptHashData(hash_,static_cast<PUCHAR>(const_cast<void *>(data)),
                              static_cast<ULONG>(size),0) >= 0;
    }
    bool finish(std::array<std::uint8_t,32> &digest)
    {
        return BCryptFinishHash(hash_,digest.data(),static_cast<ULONG>(digest.size()),0) >= 0;
    }
    static bool hashBuffer(const void *data, std::size_t size, std::array<std::uint8_t,32> &digest)
    {
        Sha256 hasher;
        return hasher.start() && hasher.update(data,size) && hasher.finish(digest);
    }
private:
    BCRYPT_ALG_HANDLE algorithm_ = nullptr;
    BCRYPT_HASH_HANDLE hash_ = nullptr;
    std::vector<UCHAR> object_;
};

bool readAt(HANDLE file, std::uint64_t offset, void *buffer, std::size_t size)
{
    LARGE_INTEGER position{};
    position.QuadPart = static_cast<LONGLONG>(offset);
    if (!SetFilePointerEx(file,position,nullptr,FILE_BEGIN)) return false;
    auto *bytes = static_cast<std::uint8_t *>(buffer);
    std::size_t remaining = size;
    while (remaining > 0) {
        const auto chunk = static_cast<DWORD>(std::min<std::size_t>(remaining,1u << 20));
        DWORD read = 0;
        if (!ReadFile(file,bytes,chunk,&read,nullptr) || read != chunk) return false;
        bytes += read;
        remaining -= read;
    }
    return true;
}

bool fileExists(const std::wstring &path)
{
    const auto attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

bool directoryExists(const std::wstring &path)
{
    const auto attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

bool createDirectories(const std::wstring &path)
{
    if (path.empty()) return false;
    std::wstring current;
    for (std::size_t index = 0; index < path.size(); ++index) {
        current.push_back(path[index]);
        if (path[index] != L'\\' && index + 1 != path.size()) continue;
        if (current.size() == 3 && current[1] == L':') continue; // drive root
        if (!directoryExists(current) && !CreateDirectoryW(current.c_str(),nullptr) &&
            GetLastError() != ERROR_ALREADY_EXISTS)
            return false;
    }
    return directoryExists(path);
}

void removeTree(const std::wstring &path)
{
    const auto attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) return;
    if (!(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
        SetFileAttributesW(path.c_str(),FILE_ATTRIBUTE_NORMAL);
        DeleteFileW(path.c_str());
        return;
    }
    WIN32_FIND_DATAW data{};
    const auto pattern = path + L"\\*";
    const auto find = FindFirstFileW(pattern.c_str(),&data);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            if (std::wcscmp(data.cFileName,L".") == 0 || std::wcscmp(data.cFileName,L"..") == 0) continue;
            removeTree(path + L"\\" + data.cFileName);
        } while (FindNextFileW(find,&data));
        FindClose(find);
    }
    RemoveDirectoryW(path.c_str());
}

bool writeAll(HANDLE file, const void *data, std::size_t size)
{
    const auto *bytes = static_cast<const std::uint8_t *>(data);
    std::size_t remaining = size;
    while (remaining > 0) {
        const auto chunk = static_cast<DWORD>(std::min<std::size_t>(remaining,1u << 20));
        DWORD written = 0;
        if (!WriteFile(file,bytes,chunk,&written,nullptr) || written != chunk) return false;
        bytes += written;
        remaining -= written;
    }
    return true;
}

bool writeMarker(const std::wstring &path, const std::string &text)
{
    const auto file = CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,
                                  FILE_ATTRIBUTE_NORMAL,nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    const auto ok = writeAll(file,text.data(),text.size());
    CloseHandle(file);
    return ok;
}

std::string hexDigest(const std::array<std::uint8_t,32> &digest, std::size_t characters)
{
    static const char digits[] = "0123456789abcdef";
    std::string out;
    out.reserve(characters);
    for (const auto byte : digest) {
        if (out.size() >= characters) break;
        out.push_back(digits[byte >> 4]);
        if (out.size() >= characters) break;
        out.push_back(digits[byte & 0x0F]);
    }
    return out;
}

struct DirectoryEntry {
    std::wstring name;
    FILETIME modified{};
};

std::vector<DirectoryEntry> listDirectories(const std::wstring &path)
{
    std::vector<DirectoryEntry> entries;
    WIN32_FIND_DATAW data{};
    const auto find = FindFirstFileW((path + L"\\*").c_str(),&data);
    if (find == INVALID_HANDLE_VALUE) return entries;
    do {
        if (std::wcscmp(data.cFileName,L".") == 0 || std::wcscmp(data.cFileName,L"..") == 0) continue;
        if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        entries.push_back({data.cFileName,data.ftLastWriteTime});
    } while (FindNextFileW(find,&data));
    FindClose(find);
    return entries;
}

bool olderThan(const FILETIME &time, ULONGLONG seconds)
{
    ULARGE_INTEGER value{};
    value.LowPart = time.dwLowDateTime;
    value.HighPart = time.dwHighDateTime;
    FILETIME now{};
    GetSystemTimeAsFileTime(&now);
    ULARGE_INTEGER current{};
    current.LowPart = now.dwLowDateTime;
    current.HighPart = now.dwHighDateTime;
    constexpr ULONGLONG kSecond = 10000000ull;
    return current.QuadPart > value.QuadPart + seconds * kSecond;
}

// A payload directory is in use while another launcher holds its .lock file
// without sharing. Opening it exclusively is the test, and the probe handle is
// closed immediately.
bool lockedBySomeone(const std::wstring &directory)
{
    const auto path = directory + L"\\.lock";
    const auto file = CreateFileW(path.c_str(),GENERIC_READ | GENERIC_WRITE,0,nullptr,
                                  OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return GetLastError() == ERROR_SHARING_VIOLATION;
    }
    CloseHandle(file);
    return false;
}

void cleanupPayloads(const std::wstring &payloads, const std::wstring &current)
{
    auto entries = listDirectories(payloads);
    // Temporary or broken trees are only kept for diagnostics, for one day.
    for (const auto &entry : entries) {
        if (entry.name == L"." || entry.name == L"..") continue;
        const auto lower = entry.name;
        const auto temporary = lower.find(L".tmp-") != std::wstring::npos ||
                               lower.find(L".broken-") != std::wstring::npos;
        if (temporary && olderThan(entry.modified,24 * 60 * 60))
            removeTree(payloads + L"\\" + entry.name);
    }
    // Keep the three newest payloads; never touch the one this process uses.
    std::sort(entries.begin(),entries.end(),[](const DirectoryEntry &left, const DirectoryEntry &right) {
        ULARGE_INTEGER a{}, b{};
        a.LowPart = left.modified.dwLowDateTime; a.HighPart = left.modified.dwHighDateTime;
        b.LowPart = right.modified.dwLowDateTime; b.HighPart = right.modified.dwHighDateTime;
        return a.QuadPart > b.QuadPart;
    });
    int kept = 0;
    for (const auto &entry : entries) {
        const auto path = payloads + L"\\" + entry.name;
        if (path == current) { ++kept; continue; }
        if (entry.name.find(L".tmp-") != std::wstring::npos ||
            entry.name.find(L".broken-") != std::wstring::npos)
            continue;
        if (kept < 3) { ++kept; continue; }
        if (lockedBySomeone(path)) continue;
        removeTree(path);
    }
}

// A cached tree is reused only when its manifest still matches the container's
// index. A missing or truncated manifest would otherwise start the runtime in
// editor mode, so the launcher rebuilds the tree instead of trusting the marker.
bool cachedManifestIntact(const std::wstring &target,
                          const std::vector<corig::container::Entry> &entries)
{
    const corig::container::Entry *manifest = nullptr;
    for (const auto &entry : entries)
        if (entry.path == "manifest.json") { manifest = &entry; break; }
    if (!manifest) return false;
    const auto file = CreateFileW((target + L"\\manifest.json").c_str(),GENERIC_READ,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                  nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    bool ok = GetFileSizeEx(file,&size) &&
              static_cast<std::uint64_t>(size.QuadPart) == manifest->size;
    Sha256 hasher;
    std::array<std::uint8_t,32> digest{};
    ok = ok && hasher.start();
    std::vector<std::uint8_t> buffer(1u << 16);
    std::uint64_t offset = 0;
    while (ok && offset < manifest->size) {
        const auto chunk = static_cast<std::size_t>(
            std::min<std::uint64_t>(manifest->size - offset,buffer.size()));
        ok = readAt(file,offset,buffer.data(),chunk) && hasher.update(buffer.data(),chunk);
        offset += chunk;
    }
    ok = ok && hasher.finish(digest) &&
         std::memcmp(digest.data(),manifest->sha256.data(),digest.size()) == 0;
    CloseHandle(file);
    return ok;
}

bool writeFileFromContainer(HANDLE container, const corig::container::Entry &entry,
                            const std::wstring &path, std::string &detail)
{
    const auto file = CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,
                                  FILE_ATTRIBUTE_NORMAL,nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        detail = "cannot create " + toUtf8(path);
        return false;
    }
    Sha256 hasher;
    if (!hasher.start()) { CloseHandle(file); detail = "cannot start SHA-256"; return false; }
    std::vector<std::uint8_t> buffer(1u << 20);
    std::uint64_t remaining = entry.size;
    std::uint64_t offset = entry.offset;
    bool ok = true;
    while (remaining > 0) {
        const auto chunk = static_cast<std::size_t>(std::min<std::uint64_t>(remaining,buffer.size()));
        if (!readAt(container,offset,buffer.data(),chunk) ||
            !hasher.update(buffer.data(),chunk) ||
            !writeAll(file,buffer.data(),chunk)) {
            ok = false;
            detail = "cannot extract " + entry.path;
            break;
        }
        offset += chunk;
        remaining -= chunk;
    }
    std::array<std::uint8_t,32> digest{};
    if (ok && !hasher.finish(digest)) { ok = false; detail = "cannot finish SHA-256"; }
    if (ok && std::memcmp(digest.data(),entry.sha256.data(),digest.size()) != 0) {
        ok = false;
        detail = "checksum mismatch for " + entry.path;
    }
    CloseHandle(file);
    if (!ok) DeleteFileW(path.c_str());
    return ok;
}

int diagnoseContainer(const std::vector<corig::container::Entry> &entries,
                      const std::string &indexHex)
{
    const auto line = std::string("{\"code\":\"OK\",\"phase\":\"container\",\"detail\":\"entries=") +
                      std::to_string(entries.size()) + " index=" + indexHex +
                      " payload=" + std::to_string(corig::container::totalSize(entries)) + "\"}";
    writeLine(line,STD_OUTPUT_HANDLE);
    return kExitOk;
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    int argc = 0;
    auto argv = CommandLineToArgvW(GetCommandLineW(),&argc);
    bool diagnose = false;
    for (int index = 1; index < argc; ++index)
        if (_wcsicmp(argv[index],L"--diagnose") == 0) diagnose = true;
    if (argv) LocalFree(argv);

    const auto self = exePath();
    if (self.empty())
        return fail("PAYLOAD_CORRUPT","container","cannot resolve the launcher path",kExitCorrupt);
    const auto file = CreateFileW(self.c_str(),GENERIC_READ,FILE_SHARE_READ | FILE_SHARE_DELETE,
                                  nullptr,OPEN_EXISTING,0,nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return fail("PAYLOAD_CORRUPT","container","cannot open the executable",kExitCorrupt);
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file,&size) || size.QuadPart < static_cast<LONGLONG>(corig::container::kTrailerSize)) {
        CloseHandle(file);
        return fail("PAYLOAD_CORRUPT","container","file is smaller than the container trailer",kExitCorrupt);
    }
    std::array<std::uint8_t,corig::container::kTrailerSize> trailerBytes{};
    if (!readAt(file,static_cast<std::uint64_t>(size.QuadPart) - corig::container::kTrailerSize,
                trailerBytes.data(),trailerBytes.size())) {
        CloseHandle(file);
        return fail("PAYLOAD_CORRUPT","container","cannot read the container trailer",kExitCorrupt);
    }
    corig::container::Trailer trailer;
    corig::container::Error error = corig::container::Error::None;
    if (!corig::container::decodeTrailer(trailerBytes.data(),trailerBytes.size(),
                                         static_cast<std::uint64_t>(size.QuadPart),trailer,&error)) {
        CloseHandle(file);
        const auto code = std::string(corig::container::code(error));
        const auto detail = std::string(corig::container::detail(error));
        return fail(code,corig::container::phase(error),detail,
                    error == corig::container::Error::UnsupportedVersion ? kExitVersion : kExitCorrupt);
    }
    std::vector<std::uint8_t> index(static_cast<std::size_t>(trailer.indexSize));
    if (!index.empty() && !readAt(file,trailer.indexOffset,index.data(),index.size())) {
        CloseHandle(file);
        return fail("PAYLOAD_CORRUPT","index","cannot read the container index",kExitCorrupt);
    }
    std::array<std::uint8_t,32> indexHash{};
    if (!Sha256::hashBuffer(index.data(),index.size(),indexHash)) {
        CloseHandle(file);
        return fail("PAYLOAD_CORRUPT","index","cannot hash the container index",kExitCorrupt);
    }
    if (std::memcmp(indexHash.data(),trailer.indexSha256.data(),indexHash.size()) != 0) {
        CloseHandle(file);
        return fail("PAYLOAD_CORRUPT","index","index SHA-256 does not match the trailer",kExitCorrupt);
    }
    std::vector<corig::container::Entry> entries;
    if (!corig::container::decodeIndex(index.data(),index.size(),trailer.launcherSize,
                                       trailer.indexOffset,entries,&error)) {
        CloseHandle(file);
        const auto code = std::string(corig::container::code(error));
        return fail(code,corig::container::phase(error),corig::container::detail(error),kExitCorrupt);
    }
    const auto indexHex = hexDigest(indexHash,16);
    if (diagnose) {
        CloseHandle(file);
        return diagnoseContainer(entries,indexHex);
    }

    const auto directory = parentDirectory(self);
    const auto workRoot = directory + L"\\" + fileStem(self) + L".data";
    const auto payloads = workRoot + L"\\cache\\payloads";
    if (!createDirectories(payloads)) {
        CloseHandle(file);
        return fail("WORK_ROOT_UNWRITABLE","work-root",
                    "cannot create the work directory beside the executable (" + toUtf8(workRoot) + ")",
                    kExitUnwritable);
    }
    const auto probe = payloads + L"\\.write-test-" + std::to_wstring(GetCurrentProcessId());
    const auto probeFile = CreateFileW(probe.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,
                                       FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE,nullptr);
    if (probeFile == INVALID_HANDLE_VALUE) {
        CloseHandle(file);
        return fail("WORK_ROOT_UNWRITABLE","work-root",
                    "the work directory is not writable (" + toUtf8(workRoot) + ")",
                    kExitUnwritable);
    }
    CloseHandle(probeFile);

    const auto target = payloads + L"\\" + toWide(indexHex);
    const auto marker = target + L"\\.ok";
    if (directoryExists(target) && (!fileExists(marker) || !cachedManifestIntact(target,entries))) {
        // Move the incomplete tree aside first: renaming a directory is atomic and
        // does not race with scanners walking the thousands of extracted files.
        // Deletion stays as the fallback when another process still holds a name.
        const auto broken = target + L".broken-" + std::to_wstring(GetCurrentProcessId());
        removeTree(broken);
        if (!MoveFileExW(target.c_str(),broken.c_str(),0)) removeTree(target);
    }
    if (!fileExists(marker)) {
        const auto staging = target + L".tmp-" + std::to_wstring(GetCurrentProcessId());
        removeTree(staging);
        if (!createDirectories(staging)) {
            CloseHandle(file);
            return fail("WORK_ROOT_UNWRITABLE","extract","cannot create " + toUtf8(staging),
                        kExitUnwritable);
        }
        std::string detail;
        bool extracted = true;
        for (const auto &entry : entries) {
            const auto relative = toWide(entry.path);
            std::wstring native;
            for (const auto character : relative) native.push_back(character == L'/' ? L'\\' : character);
            const auto full = staging + L"\\" + native;
            // decodeIndex rejected traversal; this is a defense-in-depth check.
            if (native.empty() || native.find(L"..") != std::wstring::npos) {
                extracted = false; detail = "unsafe path " + entry.path; break;
            }
            if (!createDirectories(parentDirectory(full))) {
                extracted = false; detail = "cannot create " + toUtf8(parentDirectory(full)); break;
            }
            if (!writeFileFromContainer(file,entry,full,detail)) { extracted = false; break; }
        }
        // The marker must live inside the staging tree: target only exists after
        // the atomic rename, so writing it there fails with ERROR_PATH_NOT_FOUND.
        if (extracted && !writeMarker(staging + L"\\.ok",indexHex))
            { extracted = false; detail = "cannot write the .ok marker"; }
        if (!extracted) {
            CloseHandle(file);
            const auto broken = target + L".broken-" + std::to_wstring(GetCurrentProcessId());
            removeTree(broken);
            MoveFileExW(staging.c_str(),broken.c_str(),MOVEFILE_REPLACE_EXISTING);
            return fail("PAYLOAD_CORRUPT","extract",detail,kExitCorrupt);
        }
        bool published = false;
        DWORD publishError = 0;
        for (int attempt = 0; attempt < 8 && !published; ++attempt) {
            if (MoveFileExW(staging.c_str(),target.c_str(),MOVEFILE_WRITE_THROUGH)) { published = true; break; }
            publishError = GetLastError();
            if (publishError != ERROR_ACCESS_DENIED && publishError != ERROR_SHARING_VIOLATION
                && publishError != ERROR_DIR_NOT_EMPTY) break;
            Sleep(150);
        }
        if (!published) {
            if (!fileExists(marker)) {
                CloseHandle(file);
                removeTree(staging);
                return fail("PAYLOAD_CORRUPT","extract",
                            "cannot publish the extracted payload (" + std::to_string(publishError) + ")",
                            kExitCorrupt);
            }
            removeTree(staging);
        }
    }
    CloseHandle(file);

    const auto lockPath = target + L"\\.lock";
    const auto lock = CreateFileW(lockPath.c_str(),GENERIC_READ | GENERIC_WRITE,0,nullptr,
                                  OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    cleanupPayloads(payloads,target);

    SetEnvironmentVariableW(L"CORIG_APP_ROOT",workRoot.c_str());

    const auto runtime = target + L"\\runtime\\CoRig.exe";
    if (!fileExists(runtime)) {
        if (lock != INVALID_HANDLE_VALUE) CloseHandle(lock);
        return fail("PAYLOAD_CORRUPT","launch","runtime/CoRig.exe is missing",kExitCorrupt);
    }
    std::wstring command = L"\"" + runtime + L"\"";
    std::wstring working = target + L"\\runtime";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(runtime.c_str(),command.data(),nullptr,nullptr,TRUE,0,nullptr,
                        working.c_str(),&startup,&process)) {
        if (lock != INVALID_HANDLE_VALUE) CloseHandle(lock);
        return fail("LAUNCH_FAILED","launch","cannot start runtime/CoRig.exe (" +
                    std::to_string(GetLastError()) + ")",kExitLaunch);
    }
    WaitForSingleObject(process.hProcess,INFINITE);
    DWORD exitCode = 0;
    GetExitCodeProcess(process.hProcess,&exitCode);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    if (lock != INVALID_HANDLE_VALUE) CloseHandle(lock);
    return static_cast<int>(exitCode);
}

#pragma once

// Single-executable container format shared by the publisher (Qt) and the
// launcher (no Qt). The file layout is:
//   [launcher PE][payload data][binary index][64-byte trailer]
// All integers are little-endian. This header only parses and validates the
// structure; hashing stays in the caller so the launcher can use BCrypt while
// the publisher uses Qt's cryptographic hash.
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace corig::container {

inline constexpr std::size_t kTrailerSize = 64;
inline constexpr std::uint16_t kVersion = 1;
inline constexpr std::uint32_t kMaxEntries = 200000;
inline constexpr std::uint64_t kMaxPayloadBytes = 8ull * 1024 * 1024 * 1024;
inline constexpr std::size_t kMaxPathLength = 1024;
inline constexpr std::size_t kMaxIndexBytes = 64u * 1024 * 1024;
inline constexpr char kTrailerMagic[8] = {'C','O','R','I','G','P','K','1'};
inline constexpr char kIndexMagic[8] = {'C','O','R','I','G','I','X','1'};

enum class Error {
    None,
    TrailerTooSmall,
    BadTrailerMagic,
    UnsupportedVersion,
    BadTrailerBounds,
    IndexTooLarge,
    IndexHashMismatch,
    BadIndexMagic,
    BadIndexVersion,
    EntryCountOutOfRange,
    BadPath,
    EntryOutOfBounds,
    PayloadTooLarge
};

const char *code(Error error);    // symbolic uppercase code, e.g. PAYLOAD_CORRUPT
const char *phase(Error error);   // "container" or "index"
std::string detail(Error error);

struct Entry {
    std::string path;       // relative, '/' separated, no "..", no ADS
    std::uint32_t mode = 0; // reserved; 0 for regular files
    std::uint64_t size = 0;
    std::uint64_t offset = 0; // absolute offset inside the container file
    std::array<std::uint8_t,32> sha256{};
};

struct Trailer {
    std::uint16_t version = kVersion;
    std::uint16_t flags = 0; // bit 0: payload compression (0 = store-only)
    std::uint32_t launcherSize = 0;
    std::uint64_t indexOffset = 0;
    std::uint64_t indexSize = 0;
    std::array<std::uint8_t,32> indexSha256{};
};

// True when path is a safe, portable relative path. Rejects absolute paths,
// parent traversal, drive letters, alternate data streams, backslashes and
// names that Windows would normalize into a different file.
bool validPath(const std::string &path, Error *error = nullptr);

// Serializes entries into the binary index. Returns an empty vector on error.
std::vector<std::uint8_t> encodeIndex(const std::vector<Entry> &entries, Error *error = nullptr);

// Parses and validates the index. Data bytes live in [dataStart, dataEnd) of
// the container file; every entry must fall inside that range.
bool decodeIndex(const std::uint8_t *data, std::size_t size,
                 std::uint64_t dataStart, std::uint64_t dataEnd,
                 std::vector<Entry> &entries, Error *error = nullptr);

std::array<std::uint8_t,kTrailerSize> encodeTrailer(const Trailer &trailer);

// Parses and validates the 64-byte trailer against the actual file size.
bool decodeTrailer(const std::uint8_t *data, std::size_t size, std::uint64_t fileSize,
                   Trailer &trailer, Error *error = nullptr);

std::uint64_t totalSize(const std::vector<Entry> &entries);

} // namespace corig::container

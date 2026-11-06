#include "container.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <iterator>

namespace corig::container {
namespace {

void put16(std::vector<std::uint8_t> &out, std::uint16_t value)
{
    out.push_back(static_cast<std::uint8_t>(value & 0xFF));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
}

void put32(std::vector<std::uint8_t> &out, std::uint32_t value)
{
    for (int shift = 0; shift < 32; shift += 8)
        out.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFF));
}

void put64(std::vector<std::uint8_t> &out, std::uint64_t value)
{
    for (int shift = 0; shift < 64; shift += 8)
        out.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFF));
}

std::uint16_t read16(const std::uint8_t *data, std::size_t offset)
{
    return static_cast<std::uint16_t>(data[offset]) |
           (static_cast<std::uint16_t>(data[offset + 1]) << 8);
}

std::uint32_t read32(const std::uint8_t *data, std::size_t offset)
{
    return static_cast<std::uint32_t>(data[offset]) |
           (static_cast<std::uint32_t>(data[offset + 1]) << 8) |
           (static_cast<std::uint32_t>(data[offset + 2]) << 16) |
           (static_cast<std::uint32_t>(data[offset + 3]) << 24);
}

std::uint64_t read64(const std::uint8_t *data, std::size_t offset)
{
    return static_cast<std::uint64_t>(read32(data,offset)) |
           (static_cast<std::uint64_t>(read32(data,offset + 4)) << 32);
}

bool fail(Error *error, Error value)
{
    if (error) *error = value;
    return false;
}

bool blockedName(const std::string &name)
{
    std::string stem;
    for (const auto character : name) {
        if (character == '.') break;
        stem.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
    }
    if (stem == "con" || stem == "prn" || stem == "aux" || stem == "nul") return true;
    if (stem.size() == 4 && (stem.rfind("com",0) == 0 || stem.rfind("lpt",0) == 0) &&
        stem[3] >= '1' && stem[3] <= '9')
        return true;
    return false;
}

} // namespace

const char *code(Error error)
{
    switch (error) {
    case Error::None: return "OK";
    case Error::UnsupportedVersion: return "UNSUPPORTED_VERSION";
    default: return "PAYLOAD_CORRUPT";
    }
}

const char *phase(Error error)
{
    switch (error) {
    case Error::BadIndexMagic:
    case Error::BadIndexVersion:
    case Error::EntryCountOutOfRange:
    case Error::BadPath:
    case Error::EntryOutOfBounds:
    case Error::PayloadTooLarge:
        return "index";
    case Error::None:
        return "container";
    default:
        return "container";
    }
}

std::string detail(Error error)
{
    switch (error) {
    case Error::None: return "container is valid";
    case Error::TrailerTooSmall: return "file is smaller than the container trailer";
    case Error::BadTrailerMagic: return "trailer magic is missing";
    case Error::UnsupportedVersion: return "container version is not supported";
    case Error::BadTrailerBounds: return "trailer points outside the file";
    case Error::IndexTooLarge: return "index is too large";
    case Error::IndexHashMismatch: return "index SHA-256 does not match the trailer";
    case Error::BadIndexMagic: return "index magic is missing";
    case Error::BadIndexVersion: return "index version is not supported";
    case Error::EntryCountOutOfRange: return "index entry count is out of range";
    case Error::BadPath: return "index contains an unsafe path";
    case Error::EntryOutOfBounds: return "index entry points outside the payload";
    case Error::PayloadTooLarge: return "payload exceeds the supported size";
    }
    return "unknown container error";
}

bool validPath(const std::string &path, Error *error)
{
    if (path.empty() || path.size() > kMaxPathLength) return fail(error,Error::BadPath);
    if (path.front() == '/' || path.back() == '/') return fail(error,Error::BadPath);
    for (const auto character : path) {
        const auto byte = static_cast<unsigned char>(character);
        if (byte < 0x20 || byte == 0x7F) return fail(error,Error::BadPath);
        switch (character) {
        case '\\': case ':': case '*': case '?': case '"': case '<': case '>': case '|':
            return fail(error,Error::BadPath);
        default: break;
        }
    }
    std::size_t start = 0;
    while (start <= path.size()) {
        const auto end = path.find('/',start);
        const auto segment = path.substr(start,end == std::string::npos ? std::string::npos : end - start);
        if (segment.empty() || segment == "." || segment == "..") return fail(error,Error::BadPath);
        if (segment.back() == ' ' || segment.back() == '.') return fail(error,Error::BadPath);
        if (segment.size() > 255 || blockedName(segment)) return fail(error,Error::BadPath);
        if (end == std::string::npos) break;
        start = end + 1;
    }
    if (error) *error = Error::None;
    return true;
}

std::vector<std::uint8_t> encodeIndex(const std::vector<Entry> &entries, Error *error)
{
    std::vector<std::uint8_t> out;
    out.reserve(20 + entries.size() * 64);
    if (entries.size() > kMaxEntries) { fail(error,Error::EntryCountOutOfRange); return {}; }
    for (const auto &entry : entries)
        if (!validPath(entry.path,error)) return {};
    out.insert(out.end(),kIndexMagic,kIndexMagic + 8);
    put16(out,kVersion);
    put16(out,0);
    put32(out,static_cast<std::uint32_t>(entries.size()));
    put32(out,0);
    for (const auto &entry : entries) {
        put16(out,static_cast<std::uint16_t>(entry.path.size()));
        out.insert(out.end(),entry.path.begin(),entry.path.end());
        put32(out,entry.mode);
        put64(out,entry.size);
        put64(out,entry.offset);
        out.insert(out.end(),entry.sha256.begin(),entry.sha256.end());
    }
    if (error) *error = Error::None;
    return out;
}

bool decodeIndex(const std::uint8_t *data, std::size_t size,
                 std::uint64_t dataStart, std::uint64_t dataEnd,
                 std::vector<Entry> &entries, Error *error)
{
    entries.clear();
    if (size > kMaxIndexBytes) return fail(error,Error::IndexTooLarge);
    if (size < 20) return fail(error,Error::BadIndexMagic);
    if (std::memcmp(data,kIndexMagic,8) != 0) return fail(error,Error::BadIndexMagic);
    if (read16(data,8) != kVersion) return fail(error,Error::BadIndexVersion);
    const auto count = read32(data,12);
    if (count > kMaxEntries) return fail(error,Error::EntryCountOutOfRange);
    if (dataEnd < dataStart) return fail(error,Error::BadTrailerBounds);
    std::size_t offset = 20;
    std::uint64_t total = 0;
    entries.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index) {
        if (offset + 2 > size) return fail(error,Error::BadIndexVersion);
        const auto pathLength = read16(data,offset);
        offset += 2;
        if (pathLength == 0 || pathLength > kMaxPathLength || offset + pathLength + 52 > size)
            return fail(error,Error::BadPath);
        Entry entry;
        entry.path.assign(reinterpret_cast<const char *>(data + offset),pathLength);
        offset += pathLength;
        entry.mode = read32(data,offset);
        entry.size = read64(data,offset + 4);
        entry.offset = read64(data,offset + 12);
        std::memcpy(entry.sha256.data(),data + offset + 20,32);
        offset += 52;
        if (!validPath(entry.path,error)) return false;
        if (entry.offset < dataStart) return fail(error,Error::EntryOutOfBounds);
        if (entry.size > kMaxPayloadBytes || entry.offset > dataEnd ||
            entry.size > dataEnd - entry.offset)
            return fail(error,Error::EntryOutOfBounds);
        if (entry.size > kMaxPayloadBytes - total) return fail(error,Error::PayloadTooLarge);
        total += entry.size;
        entries.push_back(std::move(entry));
    }
    if (offset != size) return fail(error,Error::BadIndexVersion);
    if (error) *error = Error::None;
    return true;
}

std::array<std::uint8_t,kTrailerSize> encodeTrailer(const Trailer &trailer)
{
    std::array<std::uint8_t,kTrailerSize> out{};
    std::copy(std::begin(kTrailerMagic),std::end(kTrailerMagic),out.begin());
    out[8] = static_cast<std::uint8_t>(trailer.version & 0xFF);
    out[9] = static_cast<std::uint8_t>((trailer.version >> 8) & 0xFF);
    out[10] = static_cast<std::uint8_t>(trailer.flags & 0xFF);
    out[11] = static_cast<std::uint8_t>((trailer.flags >> 8) & 0xFF);
    for (int shift = 0; shift < 32; shift += 8)
        out[12 + shift / 8] = static_cast<std::uint8_t>((trailer.launcherSize >> shift) & 0xFF);
    for (int shift = 0; shift < 64; shift += 8) {
        out[16 + shift / 8] = static_cast<std::uint8_t>((trailer.indexOffset >> shift) & 0xFF);
        out[24 + shift / 8] = static_cast<std::uint8_t>((trailer.indexSize >> shift) & 0xFF);
    }
    std::copy(trailer.indexSha256.begin(),trailer.indexSha256.end(),out.begin() + 32);
    return out;
}

bool decodeTrailer(const std::uint8_t *data, std::size_t size, std::uint64_t fileSize,
                   Trailer &trailer, Error *error)
{
    if (size < kTrailerSize) return fail(error,Error::TrailerTooSmall);
    if (std::memcmp(data,kTrailerMagic,8) != 0) return fail(error,Error::BadTrailerMagic);
    Trailer result;
    result.version = read16(data,8);
    if (result.version != kVersion) return fail(error,Error::UnsupportedVersion);
    result.flags = read16(data,10);
    if ((result.flags & 0x1u) != 0) return fail(error,Error::UnsupportedVersion);
    result.launcherSize = read32(data,12);
    result.indexOffset = read64(data,16);
    result.indexSize = read64(data,24);
    std::memcpy(result.indexSha256.data(),data + 32,32);
    if (fileSize < kTrailerSize) return fail(error,Error::BadTrailerBounds);
    const auto body = fileSize - kTrailerSize;
    if (result.launcherSize == 0 || result.launcherSize > body) return fail(error,Error::BadTrailerBounds);
    if (result.indexOffset < result.launcherSize || result.indexOffset > body)
        return fail(error,Error::BadTrailerBounds);
    if (result.indexSize > kMaxIndexBytes || result.indexSize > body - result.indexOffset)
        return fail(error,Error::BadTrailerBounds);
    trailer = result;
    if (error) *error = Error::None;
    return true;
}

std::uint64_t totalSize(const std::vector<Entry> &entries)
{
    std::uint64_t total = 0;
    for (const auto &entry : entries) total += entry.size;
    return total;
}

} // namespace corig::container

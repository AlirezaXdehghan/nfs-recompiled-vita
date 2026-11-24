#include <winapi/version.h>
#include <lib/file.h>
#include <x86.h>
#include <algorithm>
#include <cstring>
#include <strings.h>
#include <string>
#include <vector>

namespace win32 { namespace version
{
namespace
{
uint16_t word(const uint8_t* bytes) { return bytes[0] | (uint16_t(bytes[1]) << 8); }
uint32_t dword(const uint8_t* bytes) { return word(bytes) | (uint32_t(word(bytes + 2)) << 16); }
size_t aligned(size_t offset) { return (offset + 3) & ~size_t(3); }
bool range(size_t offset, size_t length, size_t size) { return offset <= size && length <= size - offset; }

// Read RT_VERSION from the actual bundled executable, including its FileVersion string.
std::vector<uint8_t> versionResource(const char* filename)
{
    if (!filename) return {};
    File file(filename, GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING);
    if (!file) return {};
    size_t size = file.size();
    if (size < 64 || size > 64 * 1024 * 1024) return {};
    std::vector<uint8_t> bytes(size);
    x86::reg32 read = 0;
    if (!file.read(bytes.data(), bytes.size(), &read) || read != size || word(bytes.data()) != 0x5a4d) return {};
    size_t pe = dword(bytes.data() + 60);
    if (!range(pe, 24, size) || dword(bytes.data() + pe) != 0x4550) return {};
    size_t optional = pe + 24, optionalSize = word(bytes.data() + pe + 20);
    if (!range(optional, optionalSize, size) || optionalSize < 2) return {};
    unsigned magic = word(bytes.data() + optional);
    size_t directories = magic == 0x10b ? 96 : magic == 0x20b ? 112 : 0;
    if (!directories || directories + 24 > optionalSize) return {};
    uint32_t resourceRva = dword(bytes.data() + optional + directories + 16);
    uint32_t resourceSize = dword(bytes.data() + optional + directories + 20);
    size_t sections = optional + optionalSize;
    unsigned sectionCount = word(bytes.data() + pe + 6);
    if (!range(sections, sectionCount * 40, size)) return {};
    auto fileOffset = [&](uint32_t rva, size_t length) -> size_t {
        for (unsigned index = 0; index < sectionCount; ++index)
        {
            const uint8_t* section = bytes.data() + sections + index * 40;
            uint32_t start = dword(section + 12), rawSize = dword(section + 16);
            if (rva < start || !range(rva - start, length, rawSize)) continue;
            size_t offset = size_t(dword(section + 20)) + rva - start;
            if (range(offset, length, size)) return offset;
        }
        return size;
    };
    size_t root = fileOffset(resourceRva, resourceSize);
    if (root == size || !resourceSize) return {};
    size_t directory = 0;
    for (unsigned level = 0; level < 3; ++level)
    {
        if (!range(directory, 16, resourceSize)) return {};
        const uint8_t* header = bytes.data() + root + directory;
        unsigned count = unsigned(word(header + 12)) + word(header + 14);
        if (!range(directory + 16, count * 8, resourceSize)) return {};
        bool found = false;
        for (unsigned index = 0; index < count; ++index)
        {
            const uint8_t* entry = header + 16 + index * 8;
            if (!level && dword(entry) != 16) continue;
            uint32_t child = dword(entry + 4);
            if (level < 2)
            {
                if (!(child & 0x80000000)) return {};
                directory = child & 0x7fffffff;
                found = true;
                break;
            }
            if (child & 0x80000000 || !range(child, 16, resourceSize)) return {};
            const uint8_t* data = bytes.data() + root + child;
            size_t length = dword(data + 4), offset = fileOffset(dword(data), length);
            if (offset == size || length < 6 || word(bytes.data() + offset) > length) return {};
            return {bytes.begin() + offset, bytes.begin() + offset + word(bytes.data() + offset)};
        }
        if (!found) return {};
    }
    return {};
}
struct VersionNode
{
    size_t offset = 0, end = 0, value = 0, children = 0;
    unsigned length = 0, type = 0;
    std::string key;
};
bool nodeAt(const uint8_t* bytes, size_t size, size_t offset, VersionNode& node)
{
    if (!range(offset, 6, size)) return false;
    unsigned length = word(bytes + offset);
    if (length < 6 || !range(offset, length, size)) return false;
    node = {};
    node.offset = offset; node.end = offset + length;
    node.length = word(bytes + offset + 2); node.type = word(bytes + offset + 4);
    size_t position = offset + 6;
    for (;; position += 2)
    {
        if (!range(position, 2, node.end)) return false;
        unsigned character = word(bytes + position);
        if (!character) break;
        node.key.push_back(character <= 255 ? char(character) : '?');
    }
    node.value = aligned(position + 2);
    size_t valueBytes = node.length * (node.type ? 2 : 1);
    if (!range(node.value, valueBytes, node.end)) return false;
    node.children = aligned(node.value + valueBytes);
    return true;
}
}

BOOL GetFileVersionInfoA(WinApplication* app, x86::CPU& cpu, LPCSTR lptstrFilename,
                         DWORD dwHandle, DWORD dwLen, LPVOID lpData)
{
    NFS2_USE(app);
    NFS2_USE(cpu);
    NFS2_USE(dwHandle);
    auto resource = versionResource(lptstrFilename);
    if (resource.empty())
    {
        // No readable executable: keep the old stub answer.
        if (lpData && dwLen >= 4) *reinterpret_cast<x86::reg32*>(lpData) = 0x02000000;
        return 1;
    }
    if (!lpData || dwLen < resource.size() * 2) return 0;
    // The second half stores ANSI query results without changing the Unicode resource.
    memset(lpData, 0, resource.size() * 2);
    memcpy(lpData, resource.data(), resource.size());
    return 1;
}

DWORD GetFileVersionInfoSizeA(WinApplication* app, x86::CPU& cpu,
                              LPCSTR lptstrFilename, LPDWORD lpdwHandle)
{
    NFS2_USE(app);
    NFS2_USE(cpu);
    if (lpdwHandle) *lpdwHandle = 0;
    const size_t size = versionResource(lptstrFilename).size();
    return size ? DWORD(size * 2) : 4; // 4: legacy stub block when the exe is unreadable
}

BOOL VerQueryValueA(WinApplication* app, x86::CPU& cpu,
                    Packed<void> pBlock, LPCSTR lpSubBlock, Packed<void>* lplpBuffer, UINT* puLen)
{
    NFS2_USE(cpu);
    if (!pBlock || !lpSubBlock || !lplpBuffer || !puLen) return 0;
    {
        // Legacy stub block (4 bytes, no VS_VERSION_INFO): answer as before.
        uint8_t* head = &app->getMemory<uint8_t>(x86::reg32(pBlock));
        if (word(head) == 0)
        {
            *lplpBuffer = pBlock;
            return 1;
        }
    }
    *lplpBuffer = Packed<void>();
    *puLen = 0;
    uint8_t* bytes = &app->getMemory<uint8_t>(x86::reg32(pBlock));
    size_t size = word(bytes);
    VersionNode node;
    if (!nodeAt(bytes, size, 0, node) || node.key != "VS_VERSION_INFO") return 0;
    std::string path(lpSubBlock);
    size_t position = 0;
    while (position < path.size())
    {
        if (path[position] == '\\') { ++position; continue; }
        size_t end = path.find('\\', position);
        std::string key = path.substr(position, end - position);
        bool found = false;
        for (size_t offset = node.children; offset < node.end;)
        {
            VersionNode child;
            if (!nodeAt(bytes, node.end, offset, child)) return 0;
            if (!strcasecmp(child.key.c_str(), key.c_str())) { node = child; found = true; break; }
            offset = aligned(child.end);
        }
        if (!found) return 0;
        if (end == std::string::npos) break;
        position = end;
    }
    if (!node.length) return 0;
    size_t value = node.value;
    if (node.type)
    {
        value = size + node.value;
        for (unsigned index = 0; index < node.length; ++index)
        {
            unsigned character = word(bytes + node.value + index * 2);
            bytes[value + index] = character <= 255 ? uint8_t(character) : '?';
        }
    }
    *lplpBuffer = Packed<void>(x86::reg32(pBlock) + value);
    *puLen = node.length;
    return 1;
}
}}

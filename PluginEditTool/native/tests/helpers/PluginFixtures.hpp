#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace plugin_fixtures_test {

inline void appendLittleEndian(std::string& out, std::uint32_t value, int bytes) {
    for (int i = 0; i < bytes; ++i) out.push_back(static_cast<char>((value >> (8 * i)) & 0xFFu));
}

inline std::string minimalPlugin(std::uint32_t flags, const std::string& trailer, const std::vector<std::string>& masters = {}) {
    std::string masterRecords;
    for (const auto& master : masters) {
        masterRecords += "MAST";
        appendLittleEndian(masterRecords, static_cast<std::uint32_t>(master.size() + 1), 2);
        masterRecords += master;
        masterRecords.push_back('\0');
        masterRecords += "DATA";
        appendLittleEndian(masterRecords, 8, 2);
        appendLittleEndian(masterRecords, 0, 4);
        appendLittleEndian(masterRecords, 0, 4);
    }
    std::string bytes = "TES4";
    appendLittleEndian(bytes, static_cast<std::uint32_t>(18 + masterRecords.size()), 4);
    appendLittleEndian(bytes, flags, 4);
    appendLittleEndian(bytes, 0, 4);
    appendLittleEndian(bytes, 0, 4);
    appendLittleEndian(bytes, 131, 2);
    appendLittleEndian(bytes, 0, 2);
    bytes += "HEDR";
    appendLittleEndian(bytes, 12, 2);
    appendLittleEndian(bytes, 0x3F800000u, 4);
    appendLittleEndian(bytes, 0, 4);
    appendLittleEndian(bytes, 0x800, 4);
    return bytes + masterRecords + trailer;
}

}

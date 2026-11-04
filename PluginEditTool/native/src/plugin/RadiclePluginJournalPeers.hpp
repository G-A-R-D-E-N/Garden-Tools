#pragma once

#include <fo4/archive/ba2_reader.hpp>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace fo4ide::plugin::detail {
class Ba2ReadRoot;
struct JournalPeerSnapshot {
    std::shared_ptr<const fo4::archive::Ba2ByteSource> source{};
    std::uint64_t size{0};
    std::string digest{};
};
struct JournalPeerInput {
    std::shared_ptr<const Ba2ReadRoot> root{};
    std::filesystem::path filename{};
};
JournalPeerSnapshot snapshotJournalPeer(const std::shared_ptr<const Ba2ReadRoot>& root,
                                        const std::filesystem::path& filename,
                                        std::uint64_t maxBytes = 512ull * 1024 * 1024);
std::vector<JournalPeerSnapshot> snapshotJournalPeers(const std::vector<JournalPeerInput>& inputs,
                                                       std::uint64_t maxBytes, std::size_t maxCount);
void verifyJournalPeer(const JournalPeerSnapshot& peer);
}

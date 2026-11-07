#include "RadiclePluginJournalPeers.hpp"
#include "RadiclePluginBa2Source.hpp"
#include "RadiclePluginJournalSession.hpp"

#include <algorithm>
#include <cstddef>
#include <span>
#include <stdexcept>
#include <utility>
#include <fo4/io/path_text.hpp>
#include <fo4/io/sha256.hpp>
#include <fo4/plugin/reader.hpp>

namespace fo4ide::plugin::detail {
JournalPeerSnapshot snapshotJournalPeer(const std::shared_ptr<const Ba2ReadRoot>& root,
                                        const std::filesystem::path& filename, std::uint64_t maxBytes) {
    if (!root || filename.empty() || filename != filename.filename()) throw std::runtime_error("invalid peer read authority");
    auto source = root->open(filename, std::min(maxBytes, kJournalMaxFileBytes));
    std::vector<std::byte> bytes(static_cast<std::size_t>(source->size()));
    source->read(0, bytes);
    try {
        static_cast<void>(fo4::read_plugin(bytes, filename));
    } catch (const std::exception&) {
        throw JournalRecoveryProfileInvalid("invalid profile plugin: " + fo4::io::utf8(filename));
    }
    JournalPeerSnapshot result{std::move(source), bytes.size(), fo4::io::sha256_hex(bytes)};
    verifyJournalPeer(result);
    return result;
}

std::vector<JournalPeerSnapshot> snapshotJournalPeers(const std::vector<JournalPeerInput>& inputs,
                                                       std::uint64_t maxBytes, std::size_t maxCount) {
    if (inputs.size() > maxCount) throw std::runtime_error("too many profile plugins");
    std::vector<JournalPeerSnapshot> result;
    result.reserve(inputs.size());
    for (const auto& input : inputs) {
        try { result.push_back(snapshotJournalPeer(input.root, input.filename, maxBytes)); }
        catch (const JournalRecoveryProfileInvalid&) { throw; }
        catch (const std::exception&) { throw JournalRecoveryProfileInvalid("unavailable profile plugin: " + fo4::io::utf8(input.filename)); }
    }
    return result;
}

void verifyJournalPeer(const JournalPeerSnapshot& peer) {
    if (!peer.source || peer.source->size() != peer.size || fo4::archive::hash_byte_source(peer.source) != peer.digest)
        throw std::runtime_error("profile plugin changed");
}
}

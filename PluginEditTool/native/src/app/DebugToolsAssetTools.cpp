#include <algorithm>
#include <bit>
#include <charconv>
#include <cstdint>
#include <filesystem>
#include <iterator>
#include <limits>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <system_error>
#include <vector>

#include <fo4/archive/assets.hpp>
#include <fo4/audio/sound_pcm.hpp>
#include <fo4/localization/strings.hpp>
#include <fo4/nif/nif.hpp>
#include <fo4/nif/nif_write.hpp>
#include <fo4/pex/pex.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4/plugin/text_export.hpp>
#include <fo4/plugin/visit.hpp>
#include <fo4/plugin/writer.hpp>
#include <fo4/texture/dds.hpp>

#include "DebugToolsInternal.hpp"

namespace fo4ide::app::debugtools {
namespace {

namespace fs = std::filesystem;

constexpr std::size_t kDefaultCorpusLimit = 200;

const std::vector<std::string>& assetEvidence() {
    static const std::vector<std::string> evidence{"structural", "real-game-data"};
    return evidence;
}

std::span<const std::uint8_t> asU8(const std::vector<std::byte>& bytes) {
    return {reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size()};
}

template <class Bytes>
std::size_t firstDifference(const Bytes& a, const Bytes& b) {
    return static_cast<std::size_t>(std::distance(a.begin(), std::mismatch(a.begin(), a.end(), b.begin(), b.end()).first));
}

bool readAsset(Ctx& c, Diag& d, std::string& key, std::vector<std::byte>& bytes) {
    const auto resolver = c.env.assets ? c.env.assets() : nullptr;
    if (!resolver) {
        setStatus(d, "skip", "No asset resolver is available.");
        return false;
    }
    try {
        key = fo4::archive::normalize_asset_path(c.param("asset"));
        const auto location = resolver->locate(key);
        if (!location) {
            setStatus(d, "skip", "No asset root or archive provides " + key + ".");
            return false;
        }
        bytes = resolver->read(*location);
    } catch (const fo4::archive::AssetPathError& error) {
        setStatus(d, "error", std::string("The path is not usable: ") + error.what());
        return false;
    }
    return true;
}

bool parseLimit(const std::string& text, std::size_t& limit) {
    limit = kDefaultCorpusLimit;
    const auto word = lowered(text);
    if (word.empty()) return true;
    if (word == "all") {
        limit = std::numeric_limits<std::size_t>::max();
        return true;
    }
    std::size_t value = 0;
    const auto* end = word.data() + word.size();
    const auto parsed = std::from_chars(word.data(), end, value);
    if (parsed.ec != std::errc{} || parsed.ptr != end || value == 0) return false;
    limit = value;
    return true;
}

void nifCorpus(Ctx& c, std::vector<Diag>& out) {
    Diag d = begin("nif.corpusRoundtrip", "NIF round trip over a folder", assetEvidence());
    const auto resolver = c.env.assets ? c.env.assets() : nullptr;
    if (!resolver) {
        setStatus(d, "skip", "No asset resolver is available.");
        out.push_back(std::move(d));
        return;
    }
    std::size_t limit = 0;
    if (!parseLimit(c.param("limit"), limit)) {
        setStatus(d, "error", "The limit must be a whole number above zero, or the word all.");
        out.push_back(std::move(d));
        return;
    }
    auto prefix = c.param("prefix");
    if (prefix.empty()) prefix = "meshes/";
    const auto listing = resolver->list(prefix, ".nif", false);
    if (listing.empty()) {
        setStatus(d, "skip", "No asset root or archive provides a NIF under " + prefix + ".");
        out.push_back(std::move(d));
        return;
    }
    const std::size_t total = listing.size();
    const std::size_t wanted = std::min(limit, total);
    std::size_t checked = 0;
    std::size_t identical = 0;
    std::size_t failed = 0;
    for (std::size_t i = 0; i < wanted; ++i) {
        if (c.cancelled()) {
            c.cancelledRun = true;
            break;
        }
        const auto& entry = listing[static_cast<std::size_t>(static_cast<unsigned long long>(i) * total / wanted)];
        c.progress(i, wanted, entry.path);
        ++checked;
        try {
            const auto shipped = resolver->read(entry.location);
            const auto rebuilt = fo4::nif::write_nif(fo4::nif::read_nif(shipped));
            if (rebuilt == shipped) {
                ++identical;
            } else {
                ++failed;
                addDiff(d, entry.path + ": first differing byte at position " + std::to_string(firstDifference(rebuilt, shipped)) +
                               " (" + std::to_string(shipped.size()) + " shipped, " + std::to_string(rebuilt.size()) +
                               " rewritten)");
            }
        } catch (const std::exception& error) {
            ++failed;
            addDiff(d, entry.path + ": " + error.what());
        }
    }
    d.left = Side{"checked", prefix, std::to_string(checked) + " of " + std::to_string(total)};
    d.right = Side{"identical", "read_nif then write_nif", std::to_string(identical)};
    if (failed > 0) {
        setStatus(d, "fail",
                  std::to_string(failed) + " of " + std::to_string(checked) + " NIF(s) did not survive the round trip." +
                      (c.cancelledRun ? " The run was cancelled, so the rest was not checked." : ""));
    } else if (c.cancelledRun) {
        setStatus(d, "skip", "Cancelled after " + std::to_string(checked) + " of " + std::to_string(wanted) +
                                 " NIF(s) with no mismatch; the report is partial.");
    } else if (checked < total) {
        setStatus(d, "unverified",
                  std::to_string(checked) + " of " + std::to_string(total) +
                      " NIF(s), spread evenly over the listing, are byte-identical; the other " +
                      std::to_string(total - checked) + " were not read. Run again with limit all to check every one.");
    } else {
        setStatus(d, "pass",
                  "All " + std::to_string(total) + " winning NIF(s) under " + prefix +
                      " are byte-identical after read_nif and write_nif. Nothing was written, and this is not a game load.");
    }
    out.push_back(std::move(d));
}

void ddsInspect(Ctx& c, std::vector<Diag>& out) {
    Diag d = begin("dds.inspect", "DDS header and format check", assetEvidence());
    std::string key;
    std::vector<std::byte> raw;
    if (!readAsset(c, d, key, raw)) {
        out.push_back(std::move(d));
        return;
    }
    const auto bytes = asU8(raw);
    std::optional<fo4::texture::DdsInfo> parsed;
    std::int64_t payload = 0;
    try {
        parsed = fo4::texture::read_dds(bytes);
        payload = parsed->payload_size();
    } catch (const fo4::texture::dds_error& error) {
        setStatus(d, error.type() == "UnsupportedFormat" ? "unsupported" : "fail", error.type() + ": " + error.what());
        out.push_back(std::move(d));
        return;
    }
    const auto& info = *parsed;
    const auto available = static_cast<std::int64_t>(raw.size()) - info.data_offset;
    d.left = Side{"header says", key,
                  std::string(info.format.name) + " " + std::to_string(info.width) + "x" + std::to_string(info.height) +
                      ", " + std::to_string(info.mip_count) + " mip(s), " + std::to_string(payload) + " payload bytes"};
    d.right = Side{"file holds", key, std::to_string(available) + " bytes after the header"};
    addDiff(d, std::string("layout: ") + (info.is_cube_map ? "cube map" : "2D") + ", array size " +
                   std::to_string(info.array_size) + ", depth " + std::to_string(info.depth));
    bool failed = false;
    if (available < payload) {
        failed = true;
        addDiff(d, "truncated: " + std::to_string(payload - available) + " byte(s) missing from the last mip");
    } else if (available > payload) {
        addDiff(d, "trailing: " + std::to_string(available - payload) + " byte(s) follow the last mip");
    }
    const auto chain = static_cast<int>(std::bit_width(static_cast<unsigned>(std::max({info.width, info.height, info.depth, 1}))));
    if (info.mip_count > chain) {
        failed = true;
        addDiff(d, "mip chain: " + std::to_string(info.mip_count) + " mips declared, a " + std::to_string(info.width) + "x" +
                       std::to_string(info.height) + "x" + std::to_string(info.depth) + " texture has at most " +
                       std::to_string(chain));
    }
    if (!failed && info.format.is_block_compressed()) {
        if (fo4::texture::can_decode_block_format(info.dxgi_format())) {
            try {
                std::uint32_t width = 0;
                std::uint32_t height = 0;
                const auto rgba = fo4::texture::decode_dds_rgba(bytes, 0U, false, width, height);
                addDiff(d, "decoded mip 0 to " + std::to_string(width) + "x" + std::to_string(height) + " RGBA (" +
                               std::to_string(rgba.size()) + " bytes)");
            } catch (const std::exception& error) {
                failed = true;
                addDiff(d, std::string("decoding mip 0 failed: ") + error.what());
            }
        } else {
            addDiff(d, std::string("this reader cannot decode ") + std::string(info.format.name) +
                           "; only the header and the sizes were checked");
        }
    }
    if (failed) {
        setStatus(d, "fail", "The header, the byte size or the first mip is inconsistent; see the rows.");
    } else {
        setStatus(d, "pass",
                  "The header, the byte size and the mip count agree. This does not prove the game loads the texture.");
    }
    out.push_back(std::move(d));
}

void pexInspect(Ctx& c, std::vector<Diag>& out) {
    Diag d = begin("pex.inspect", "Compiled script (PEX) check", assetEvidence());
    std::string key;
    std::vector<std::byte> raw;
    if (!readAsset(c, d, key, raw)) {
        out.push_back(std::move(d));
        return;
    }
    const auto view = asU8(raw);
    const std::vector<std::uint8_t> shipped(view.begin(), view.end());
    fo4::pex::PexFile file;
    try {
        file = fo4::pex::PexFile::fromBytes(shipped);
    } catch (const std::exception& error) {
        setStatus(d, "fail", std::string("The file does not parse as a PEX: ") + error.what());
        out.push_back(std::move(d));
        return;
    }
    std::vector<std::uint8_t> rebuilt;
    try {
        rebuilt = file.toBytes();
    } catch (const std::exception& error) {
        setStatus(d, "fail", std::string("The parsed script could not be written back: ") + error.what());
        out.push_back(std::move(d));
        return;
    }
    if (rebuilt != shipped) addDiff(d, "first differing byte at position " + std::to_string(firstDifference(rebuilt, shipped)));
    addDiff(d, "version " + std::to_string(file.majorVersion) + "." + std::to_string(file.minorVersion) + ", game id " +
                   std::to_string(file.gameId) + ", source " + file.sourceFileName + ", " +
                   std::to_string(file.stringTable.size()) + " string(s), debug info " + (file.hasDebugInfo ? "present" : "absent"));
    std::size_t variables = 0;
    std::size_t properties = 0;
    std::size_t functions = 0;
    std::size_t instructions = 0;
    for (const auto& object : file.objects) {
        std::size_t objectFunctions = 0;
        for (const auto& state : object.states) {
            objectFunctions += state.functions.size();
            for (const auto& function : state.functions) instructions += function.instructions.size();
        }
        variables += object.variables.size();
        properties += object.properties.size();
        functions += objectFunctions;
        addDiff(d, "object " + object.name + (object.parentClassName.empty() ? "" : " extends " + object.parentClassName) + ": " +
                       std::to_string(object.variables.size()) + " variable(s), " + std::to_string(object.properties.size()) +
                       " property(ies), " + std::to_string(objectFunctions) + " state function(s)");
    }
    d.left = Side{"shipped", key, std::to_string(shipped.size()) + " bytes"};
    d.right = Side{"rewritten in memory", "PexFile::fromBytes then toBytes", std::to_string(rebuilt.size()) + " bytes"};
    const auto counts = std::to_string(file.objects.size()) + " object(s), " + std::to_string(variables) + " variable(s), " +
                        std::to_string(properties) + " property(ies), " + std::to_string(functions) + " state function(s), " +
                        std::to_string(instructions) + " instruction(s)";
    if (rebuilt == shipped) {
        setStatus(d, "pass",
                  "Parses and rewrites to the identical bytes: " + counts +
                      ". The code is not disassembled or checked, and this is not a game load.");
    } else {
        setStatus(d, "fail", "The rewritten script differs from the shipped bytes: " + counts + ".");
    }
    out.push_back(std::move(d));
}

std::string textOf(const fo4::localization::LocalizedStringEntry& entry) {
    return entry.text.empty() ? std::string{}
                              : std::string(reinterpret_cast<const char*>(entry.text.data()), entry.text.size());
}

void stringsInspect(Ctx& c, std::vector<Diag>& out) {
    Diag d = begin("strings.inspect", "String table file check", assetEvidence());
    std::string key;
    std::vector<std::byte> raw;
    if (!readAsset(c, d, key, raw)) {
        out.push_back(std::move(d));
        return;
    }
    const auto dot = key.find_last_of('.');
    const auto extension = dot == std::string::npos ? std::string{} : lowered(key.substr(dot));
    fo4::localization::StringTableKind kind{};
    if (extension == ".strings") {
        kind = fo4::localization::StringTableKind::strings;
    } else if (extension == ".dlstrings") {
        kind = fo4::localization::StringTableKind::dlstrings;
    } else if (extension == ".ilstrings") {
        kind = fo4::localization::StringTableKind::ilstrings;
    } else {
        setStatus(d, "unsupported", "Only .strings, .dlstrings and .ilstrings files are string tables; " + key + " is not one.");
        out.push_back(std::move(d));
        return;
    }
    fo4::localization::LocalizedStringTable table;
    try {
        table = fo4::localization::read_string_table(raw, kind);
    } catch (const fo4::localization::StringTableError& error) {
        setStatus(d, "fail", std::string("The table is malformed: ") + error.what());
        out.push_back(std::move(d));
        return;
    }
    std::set<std::string> distinct;
    std::size_t empty = 0;
    for (const auto& entry : table.entries) {
        auto text = textOf(entry);
        if (text.empty()) ++empty;
        distinct.insert(std::move(text));
    }
    const auto rewritten = fo4::localization::write_string_table(table);
    const auto again = fo4::localization::read_string_table(rewritten, kind);
    bool same = again.entries.size() == table.entries.size();
    for (std::size_t i = 0; same && i < table.entries.size(); ++i) {
        same = again.entries[i].id == table.entries[i].id && again.entries[i].text == table.entries[i].text;
    }
    d.left = Side{"shipped", key, std::to_string(raw.size()) + " bytes"};
    d.right = Side{"rewritten in memory", "write_string_table", std::to_string(rewritten.size()) + " bytes"};
    addDiff(d, std::to_string(table.entries.size()) + " entrie(s), " + std::to_string(distinct.size()) + " distinct text(s), " +
                   std::to_string(empty) + " empty");
    if (!table.entries.empty()) {
        addDiff(d, "ids " + std::to_string(table.entries.front().id) + " to " + std::to_string(table.entries.back().id));
    }
    addDiff(d, std::string("rewritten bytes are ") + (rewritten == raw ? "identical to" : "different from") + " the shipped bytes");
    if (same) {
        setStatus(d, "pass",
                  "The table parses strictly and a rewrite reads back with the same ids and texts. Byte identity is only shown as a row, because the writer keeps each distinct text once.");
    } else {
        setStatus(d, "fail", "A rewritten copy of the table reads back with different ids or texts.");
    }
    out.push_back(std::move(d));
}

void stringsPluginTables(Ctx& c, std::vector<Diag>& out) {
    Diag d = begin("strings.pluginTables", "Plugin string tables", assetEvidence());
    const auto plugin = c.param("plugin");
    auto reply = c.call("strings", "ListStringTables", json::array({plugin, "en"}));
    if (!reply) {
        fromError(d, reply.error());
        out.push_back(std::move(d));
        return;
    }
    if (!hasField(*reply, "tables") || !(*reply)["tables"].is_array()) {
        setStatus(d, "error", "The reply had no list of tables.");
        out.push_back(std::move(d));
        return;
    }
    std::set<std::string> kinds;
    long long pending = 0;
    for (const auto& row : (*reply)["tables"]) {
        kinds.insert(lowered(textField(row, "kind")));
        if (fieldOr(row, "pending", json(false)) == true) ++pending;
        addDiff(d, textField(row, "kind") + " " + std::to_string(countField(row, "count")) + " string(s) from " +
                       textField(row, "source") + ": " + textField(row, "path"));
    }
    const auto skipped = countField(*reply, "skippedArchives");
    const auto found = static_cast<long long>((*reply)["tables"].size());
    d.left = Side{"tables found", plugin, std::to_string(found)};
    d.right = Side{"pending staged edits", plugin, std::to_string(pending)};
    if (found == 0) {
        setStatus(d, "unverified",
                  "No string table was found for the language en. The reply does not say whether the plugin is localized, so a plain plugin and a localized plugin with missing tables look the same.");
    } else if (found != 3 || kinds.size() != 3) {
        setStatus(d, "unverified", std::to_string(found) + " table(s) were found; a localized plugin is expected to have three kinds.");
    } else if (skipped > 0) {
        setStatus(d, "unverified", std::to_string(skipped) + " archive(s) could not be read, so the list may be incomplete.");
    } else if (pending > 0) {
        setStatus(d, "unverified", std::to_string(pending) + " table(s) show staged edits that are not saved to disk.");
    } else {
        setStatus(d, "pass", "All three string tables were found for the language en.");
    }
    out.push_back(std::move(d));
}

std::string containerName(fo4::audio::SoundContainerKind kind) {
    switch (kind) {
    case fo4::audio::SoundContainerKind::wav:
        return "wav";
    case fo4::audio::SoundContainerKind::xwma:
        return "xwma";
    case fo4::audio::SoundContainerKind::fuz:
        return "fuz";
    }
    return "unknown";
}

void audioInspect(Ctx& c, std::vector<Diag>& out) {
    Diag d = begin("audio.inspect", "Sound file decode check", assetEvidence());
    const auto resolver = c.env.assets ? c.env.assets() : nullptr;
    if (!resolver) {
        setStatus(d, "skip", "No asset resolver is available.");
        out.push_back(std::move(d));
        return;
    }
    const auto name = c.param("asset");
    std::optional<fo4::audio::SoundAsset> sound;
    try {
        sound = fo4::audio::read_sound(*resolver, name);
    } catch (const fo4::archive::AssetPathError& error) {
        setStatus(d, "error", std::string("The path is not usable: ") + error.what());
        out.push_back(std::move(d));
        return;
    } catch (const std::exception& error) {
        setStatus(d, "fail", std::string("The decoder rejected the file (damage, or a format it does not support): ") + error.what());
        out.push_back(std::move(d));
        return;
    }
    if (!sound) {
        setStatus(d, "skip", "No asset root or archive provides a sound for " + name + ".");
        out.push_back(std::move(d));
        return;
    }
    const auto& pcm = sound->pcm;
    const std::size_t frameBytes = static_cast<std::size_t>(pcm.channels) * (pcm.bits_per_sample / 8U);
    const std::size_t frames = frameBytes == 0 ? 0 : pcm.pcm.size() / frameBytes;
    const double seconds = pcm.sample_rate == 0 ? 0.0 : static_cast<double>(frames) / pcm.sample_rate;
    d.left = Side{"decoded", sound->path,
                  containerName(pcm.container) + ", " + std::to_string(pcm.sample_rate) + " Hz, " +
                      std::to_string(pcm.channels) + " channel(s), " + std::to_string(pcm.bits_per_sample) + " bit"};
    d.right = Side{"length", sound->path, std::to_string(frames) + " frame(s), " + std::to_string(seconds) + " s"};
    bool failed = false;
    if (pcm.bad_packets > 0) {
        failed = true;
        addDiff(d, std::to_string(pcm.bad_packets) + " packet(s) failed to decode");
    }
    if (pcm.pcm.empty()) {
        failed = true;
        addDiff(d, "the decoder produced no samples");
    } else if (frameBytes == 0 || pcm.pcm.size() % frameBytes != 0) {
        failed = true;
        addDiff(d, "the sample block is not a whole number of frames");
    }
    if (failed) {
        setStatus(d, "fail", "The sound decoded with problems; see the rows.");
    } else {
        setStatus(d, "pass",
                  "The file decodes to whole PCM frames with no bad packets. The lip block of a FUZ is not checked, and this is not a game load.");
    }
    out.push_back(std::move(d));
}

void stripByteRanges(fo4::Plugin& plugin) {
    plugin.original_range.clear();
    plugin.original_data.reset();
    plugin.original_structure_fingerprint = 0;
    fo4::visit_records(plugin, [](fo4::Record& record) { record.original_range.clear(); });
}

void pluginTextRoundtrip(Ctx& c, std::vector<Diag>& out) {
    Diag d = begin("plugin.textRoundtrip", "Plugin text export and re-import", {"structural"});
    const auto source = resolvePluginPath(c.env, c.param("plugin"));
    if (source.empty()) {
        setStatus(d, "skip", "The plugin file was not found in the load order or the Data folder.");
        out.push_back(std::move(d));
        return;
    }
    const auto dataDir = dataDirOf(c.env);
    if (!dataDir.empty() && isInside(source, dataDir)) d.evidence.push_back("real-game-data");
    std::error_code ec;
    if (c.scratch.empty() || !fs::create_directory(c.scratch, ec) || ec) {
        setStatus(d, "error", "The fresh scratch folder could not be created (it may already exist), so nothing was written.");
        out.push_back(std::move(d));
        return;
    }
    [[maybe_unused]] const EmptyScratchCleanup cleanup{c.scratch};
    c.progress(0, 4, "reading " + source.filename().string());
    const auto plugin = fo4::read_plugin_file(source);
    auto canonical = plugin;
    stripByteRanges(canonical);
    if (stoppedForShutdown(c, d)) {
        out.push_back(std::move(d));
        return;
    }
    c.progress(1, 4, "writing the expected bytes");
    const auto expected = fo4::write_plugin(canonical);
    d.left = Side{"expected", source.string(), std::to_string(expected.size()) + " bytes"};
    bool identical = true;
    for (const bool labels : {false, true}) {
        const std::string name = labels ? "with schema labels" : "plain";
        if (stoppedForShutdown(c, d)) {
            out.push_back(std::move(d));
            return;
        }
        c.progress(labels ? 3 : 2, 4, "exporting and re-importing (" + name + ")");
        const auto folder = c.scratch / (labels ? "labels" : "plain");
        if (!fs::create_directory(folder, ec) || ec) {
            setStatus(d, "error", "An export folder could not be created inside the scratch folder.");
            out.push_back(std::move(d));
            return;
        }
        fs::path exported;
        std::vector<std::byte> rebuilt;
        try {
            exported = fo4::export_plugin_text(plugin, folder, fo4::TextExportOptions{labels});
            rebuilt = fo4::write_plugin(fo4::import_plugin_text(exported));
        } catch (const std::exception& error) {
            d.outputs.push_back(folder.string());
            setStatus(d, "error", "The text export or import threw during the " + name + " pass (a very large plugin may exceed a Radicle limit); the folder is kept in the scratch folder: " + error.what());
            out.push_back(std::move(d));
            return;
        }
        if (!labels) d.right = Side{"rebuilt from text", exported.string(), std::to_string(rebuilt.size()) + " bytes"};
        if (rebuilt == expected) {
            addDiff(d, name + ": identical");
            fs::remove_all(folder, ec);
        } else {
            identical = false;
            addDiff(d, name + ": differs, first differing byte at position " + std::to_string(firstDifference(rebuilt, expected)) +
                           " (" + std::to_string(expected.size()) + " expected, " + std::to_string(rebuilt.size()) + " rebuilt)");
            d.outputs.push_back(folder.string());
        }
    }
    if (identical) {
        setStatus(d, "pass",
                  "Export to text and import back rebuilds the expected bytes, with and without schema labels. The exported text folders were removed from the scratch folder. This does not prove the game accepts the plugin.");
    } else {
        setStatus(d, "fail", "A text round trip differs from the expected bytes; the failing export folders are kept in the scratch folder.");
    }
    out.push_back(std::move(d));
}

}  // namespace

void appendAssetTools(std::vector<ToolSpec>& table) {
    const ParamSpec plugin{"plugin", "Plugin file name", "plugin", true};
    const auto& evidence = assetEvidence();

    auto corpus = makeTool("nif.corpusRoundtrip", "assets", "NIF round trip over a folder",
                           "Reads the winning NIFs under a folder, rewrites each in memory and compares the bytes. Checks an even sample of 200 unless limit is a number or all. Writes nothing. Can be cancelled.",
                           evidence,
                           {{"prefix", "Folder prefix (default meshes/)", "text", false},
                            {"limit", "How many NIFs (default 200, or all)", "text", false}},
                           needAssets, nifCorpus);
    corpus.expensive = true;
    corpus.cancellable = true;
    table.push_back(std::move(corpus));

    table.push_back(makeTool("dds.inspect", "assets", "DDS header and format check",
                             "Reads a texture, checks the header, the byte size and the mip count and decodes the first mip when the format is supported. Writes nothing.",
                             evidence, {{"asset", "DDS path (Data relative)", "asset", true}}, needAssets, ddsInspect));
    table.push_back(makeTool("pex.inspect", "assets", "Compiled script (PEX) check",
                             "Parses a compiled script, lists its objects and rewrites it in memory to compare the bytes. Writes nothing.",
                             evidence, {{"asset", "PEX path (Data relative)", "asset", true}}, needAssets, pexInspect));
    table.push_back(makeTool("strings.inspect", "assets", "String table file check",
                             "Parses a .strings, .dlstrings or .ilstrings file strictly and checks that a rewrite reads back the same. Writes nothing.",
                             evidence, {{"asset", "String table path (Data relative)", "asset", true}}, needAssets,
                             stringsInspect));
    table.push_back(makeTool("audio.inspect", "assets", "Sound file decode check",
                             "Decodes a WAV, XWM or FUZ sound in memory and checks the PCM it yields. Writes nothing.",
                             evidence, {{"asset", "Sound path (Data relative)", "asset", true}}, needAssets, audioInspect));
    table.push_back(makeTool("strings.pluginTables", "plugins", "Plugin string tables",
                             "Lists the string tables the IDE finds for a loaded plugin (language en) and flags missing, pending or unreadable ones.",
                             evidence, {plugin}, needCall, stringsPluginTables));

    auto text = makeTool("plugin.textRoundtrip", "plugins", "Plugin text export and re-import",
                         "Exports a plugin to text, imports it back and compares the rebuilt bytes. Writes only folders inside a fresh folder in the output folder you choose. Very large plugins may exceed Radicle limits (reported as an error), and it cannot be cancelled.",
                         {"structural", "real-game-data"}, {plugin}, noRequirement, pluginTextRoundtrip);
    text.readOnly = false;
    text.writes = true;
    text.needsOutputDir = true;
    text.expensive = true;
    table.push_back(std::move(text));
}

}  // namespace fo4ide::app::debugtools

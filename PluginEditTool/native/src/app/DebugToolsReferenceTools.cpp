#include <algorithm>
#include <cctype>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <regex>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <fo4/environment/environment.hpp>
#include <fo4/io/atomic_write.hpp>
#include <fo4/plugin/mutation.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4/plugin/visit.hpp>
#include <fo4/plugin/writer.hpp>
#include <fo4/previs/detect.hpp>
#include <fo4/previs/previs_data.hpp>
#include <fo4/schema/field_address.hpp>
#include <fo4/schema/scalar.hpp>

#include "DebugToolsInternal.hpp"

namespace fo4ide::app::debugtools {
namespace {

namespace fs = std::filesystem;

constexpr std::size_t kMaxTableBytes = 4u << 20;
constexpr std::size_t kRowsPerClass = 15;
constexpr std::size_t kMaxCarriers = 8;
constexpr const char* kPinnedName = "vanilla_1_11_240_broken.tsv";

constexpr const char* kMasters[] = {"Fallout4.esm",    "DLCRobot.esm",      "DLCworkshop01.esm",
                                    "DLCCoast.esm",    "DLCworkshop03.esm", "DLCNukaWorld.esm"};

std::string buildIn(const std::string& line) {
    static const std::regex quad(R"((\d+\.\d+\.\d+\.\d+))");
    std::smatch match;
    return std::regex_search(line, match, quad) ? match[1].str() : std::string{};
}

// Constraint: the joined code string is compared verbatim with the pinned column, so the sort and the comma are the gate's.
std::string issueCodes(const fo4::previs::CellPrevisStatus& status) {
    std::vector<std::string> codes;
    codes.reserve(status.issues.size());
    for (const auto& issue : status.issues) codes.emplace_back(fo4::previs::to_string(issue.code));
    std::sort(codes.begin(), codes.end());
    std::string joined;
    for (std::size_t i = 0; i < codes.size(); ++i) {
        if (i != 0) joined.push_back(',');
        joined += codes[i];
    }
    return joined;
}

void addClassRows(Diag& d, const char* label, const std::vector<std::string>& rows) {
    for (std::size_t i = 0; i < rows.size() && i < kRowsPerClass; ++i) addDiff(d, std::string(label) + ": " + rows[i]);
    if (rows.size() > kRowsPerClass) {
        addDiff(d, std::string(label) + ": " + std::to_string(rows.size() - kRowsPerClass) + " more not listed");
    }
}

std::string orUnknown(const std::string& build) { return build.empty() ? std::string("unknown") : build; }

void previsVanillaBaseline(Ctx& c, std::vector<Diag>& out) {
    Diag d = begin("previs.vanillaBaseline", "Comparison with the pinned vanilla broken-cell list", {"real-game-data"});
    const auto custom = c.param("baseline");
    const auto finish = [&](const char* status, std::string detail) {
        setStatus(d, status, std::move(detail));
        out.push_back(std::move(d));
    };
    std::error_code ec;
    const auto dataDir = dataDirOf(c.env);
    if (dataDir.empty() || !fs::is_directory(dataDir, ec)) {
        return finish("skip", "No Fallout 4 Data folder is configured or found, so there is no game data to check.");
    }
    fs::path pinnedPath;
    if (!custom.empty()) {
        pinnedPath = fs::path(std::u8string(custom.begin(), custom.end()));
    } else if (!c.env.exeDir.empty()) {
        pinnedPath = c.env.exeDir / "data" / "previs" / kPinnedName;
    }
    if (pinnedPath.empty() || !fs::is_regular_file(pinnedPath, ec)) {
        return finish("skip", "The pinned broken-cell list was not found" +
                                  (pinnedPath.empty() ? std::string{} : " at " + pinnedPath.string()) +
                                  ". The packaged editor ships it in data/previs beside the exe; otherwise pass a baseline file.");
    }
    const auto size = fs::file_size(pinnedPath, ec);
    if (ec || size > kMaxTableBytes) {
        return finish("error", "The pinned list " + pinnedPath.string() + " could not be sized or is larger than 4 MiB.");
    }
    std::ifstream file(pinnedPath, std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    auto table = parseBrokenCellTable(text);
    if (!table) return finish("error", "The pinned list " + pinnedPath.string() + " is unusable: " + table.error().message);

    std::vector<fs::path> paths;
    std::string missing;
    for (const char* name : kMasters) {
        const auto path = dataDir / name;
        if (fs::is_regular_file(path, ec)) {
            paths.push_back(path);
        } else {
            missing += (missing.empty() ? "" : ", ") + std::string(name);
        }
    }
    if (!missing.empty()) {
        return finish("skip", "The comparison needs the six official masters in the Data folder " + dataDir.string() +
                                  "; missing: " + missing + ".");
    }

    const auto build = gameBuildBesideData(c.env);
    const bool buildMatches = !build.empty() && !table->build.empty() && build == table->build;

    BrokenCells actual;
    std::size_t cells = 0;
    std::size_t skipped = 0;
    std::size_t notRun = 0;
    std::vector<std::string> problems;
    try {
        c.progress(0, 3, "loading the six official masters");
        const auto environment = fo4::env::Environment::load(paths);
        c.progress(1, 3, "indexing the previs files in the Data folder");
        const fs::path roots[]{dataDir};
        const auto data = fo4::previs::PrevisData::load(roots, environment);
        c.progress(2, 3, "diagnosing every winning cell");
        const auto report = fo4::previs::diagnose_load_order(environment, data.indexes());
        cells = report.size();
        for (const auto& diagnosis : report) {
            if (diagnosis.status.skip_unsupported) ++skipped;
            for (const auto& skip : diagnosis.status.skips) {
                if (skip.rfind("geometry check did not run", 0) == 0) ++notRun;
            }
            if (diagnosis.status.broken) actual.emplace(diagnosis.cell.to_string(), issueCodes(diagnosis.status));
        }
        problems = data.summary().problems;
    } catch (const std::exception& error) {
        return finish("error", std::string("The official masters could not be diagnosed: ") + error.what());
    }

    const auto diff = compareBrokenCells(table->cells, actual);
    d.left = Side{"pinned list", pinnedPath.string(),
                  std::to_string(table->cells.size()) + " broken cell(s), build " + orUnknown(table->build)};
    d.right = Side{"this install", dataDir.string(),
                   std::to_string(actual.size()) + " broken cell(s) of " + std::to_string(cells) + ", build " +
                       orUnknown(build)};
    addClassRows(d, "pinned, not reported", diff.missing);
    addClassRows(d, "reported, not pinned", diff.extra);
    addClassRows(d, "different codes", diff.different);

    std::string caveats;
    if (!buildMatches) {
        caveats += "The pinned list is for build " + orUnknown(table->build) + " and this install is build " +
                   orUnknown(build) + ", so the two lists are not comparable. ";
    }
    if (!problems.empty()) {
        caveats += std::to_string(problems.size()) + " previs data problem(s) were reported while indexing (first: " +
                   problems.front() + "), so the index is partial. ";
    }
    if (notRun > 0) caveats += std::to_string(notRun) + " cell(s) had a geometry check that did not run. ";
    const auto counts = std::to_string(diff.missing.size()) + " pinned cell(s) not reported, " +
                        std::to_string(diff.extra.size()) + " reported cell(s) not pinned, " +
                        std::to_string(diff.different.size()) + " with different codes. " + std::to_string(skipped) +
                        " cell(s) were skipped as unsupported and are not compared. ";
    const bool complete = problems.empty() && notRun == 0;
    if (!diff.equal() && buildMatches && complete) {
        finish("fail", counts +
                           "Either the Data folder is not stock (the scan indexes every general archive and loose previs file in Data, so a mod installed straight into Data counts) or the diagnosis changed; this tool cannot tell which.");
    } else if (!diff.equal() || !buildMatches || !complete) {
        finish("unverified", counts + caveats);
    } else {
        finish("pass", "The broken-cell list of the six official masters equals the pinned list. " + counts +
                           "This is the PrevisReal.VanillaLoadOrderGate diagnosis, not a check that the game accepts previs.");
    }
}

using fo4::schema::ScalarFieldDescriptor;
using fo4::schema::ScalarStorage;

struct SubBytes {
    fo4::Signature signature;
    std::vector<std::byte> data;
};
using Snapshot = std::vector<SubBytes>;

Snapshot snapshotOf(const fo4::Record& record) {
    Snapshot out;
    out.reserve(record.subrecords.size());
    for (const auto& subrecord : record.subrecords) out.push_back({subrecord.signature, subrecord.data});
    return out;
}

bool sameRecord(const Snapshot& before, const fo4::Record& after) {
    if (before.size() != after.subrecords.size()) return false;
    for (std::size_t i = 0; i < before.size(); ++i) {
        if (before[i].signature != after.subrecords[i].signature || before[i].data != after.subrecords[i].data) return false;
    }
    return true;
}

const fo4::Record& mustFind(const fo4::Plugin& plugin, const fo4::FormKey& key) {
    const auto* record = fo4::find_record(plugin, key);
    if (record == nullptr) throw std::runtime_error("the record " + key.to_string() + " is no longer in the plugin");
    return *record;
}

std::optional<fo4::schema::FieldSlot> slotOf(const fo4::Record& record, const ScalarFieldDescriptor& field) {
    const auto resolved =
        fo4::schema::resolve_field_slots(record, fo4::schema::scalar_field_shape(field.scalar), field.scalar.address);
    if (resolved.duplicate || resolved.malformed || resolved.slots.empty()) return std::nullopt;
    const auto slot = resolved.slots[0];
    if (!slot.valid) return std::nullopt;
    return slot;
}

// Constraint: a row decodes the slot it resolves to, so a repeated or strided row reads the element its index names.
std::string decodeRow(const fo4::Record& record, const ScalarFieldDescriptor& field) {
    const auto slot = slotOf(record, field);
    if (!slot || slot->subrecord >= record.subrecords.size()) return {};
    const auto bytes = fo4::schema::scalar_slot_bytes(record.subrecords[slot->subrecord].data, *slot, field.scalar);
    if (bytes.empty()) return {};
    return fo4::schema::decode_scalar(bytes, field.scalar);
}

struct Span {
    std::size_t subrecord{0};
    std::size_t offset{0};
    std::size_t width{0};
};

std::size_t rowWidth(const fo4::Record& record, const ScalarFieldDescriptor& field, const fo4::schema::FieldSlot& slot) {
    if (slot.subrecord >= record.subrecords.size()) return 0;
    const auto width = fo4::schema::scalar_storage_size(field.scalar.storage);
    return width == 0 ? record.subrecords[slot.subrecord].data.size() : width;
}

bool overlaps(const Span& left, const Span& right) {
    if (left.subrecord != right.subrecord) return false;
    if (left.width == 0 || right.width == 0) return left.offset == right.offset;
    return left.offset < right.offset + right.width && right.offset < left.offset + left.width;
}

// Constraint: only the four WRLD bounds rows on NAM0 and NAM9 resize a grid, and only OFST and CLSZ (and XXXX) follow it.
bool resizesWorldGrid(const ScalarFieldDescriptor& field, fo4::Signature changed) {
    if (field.record != fo4::Signature::from_string("WRLD")) return false;
    if (field.scalar.subrecord != fo4::Signature::from_string("NAM0") &&
        field.scalar.subrecord != fo4::Signature::from_string("NAM9")) {
        return false;
    }
    return changed == fo4::Signature::from_string("OFST") || changed == fo4::Signature::from_string("CLSZ") ||
           changed == fo4::Signature::from_string("XXXX");
}

// Constraint: Radicle refuses a WRLD bounds value that leaves no grid or too large a grid, and only that refusal skips the candidate; any other setter error on a bounds row, a malformed OFST or CLSZ included, fails the run. The texts are field_writer.cpp's, so a reworded refusal fails loudly rather than passing.
bool refusesWorldGridShape(const ScalarFieldDescriptor& field, const fo4::schema::ScalarError& error) {
    if (!resizesWorldGrid(field, fo4::Signature::from_string("OFST"))) return false;
    const std::string_view text = error.what();
    return text == "WRLD bounds do not describe a grid" || text == "WRLD bounds describe a grid that is too large";
}

std::vector<std::string> candidates(const ScalarFieldDescriptor& field) {
    std::vector<std::string> out;
    if (!field.scalar.names.empty()) {
        for (const auto& name : field.scalar.names) out.push_back(std::to_string(name.value));
        return out;
    }
    if (field.scalar.storage == ScalarStorage::f32) return {"0.5", "-0.5", "2.5", "1.5"};
    return {"1", "2", "3", "0", "7", "42"};
}

bool insideBounds(const ScalarFieldDescriptor& field, const std::string& value) {
    const auto number = std::stod(value);
    return number >= field.scalar.minimum && number <= field.scalar.maximum;
}

struct Tally {
    std::size_t rows{0};
    std::size_t exercised{0};
    std::size_t wholePayload{0};
    std::size_t derived{0};
    std::size_t absent{0};
    std::size_t unresolved{0};
    std::size_t noCandidate{0};
    std::size_t structural{0};
};

struct EditedRow {
    ScalarFieldDescriptor field;
    fo4::FormKey key;
    std::string expected;
};

struct EditedRecord {
    fo4::FormKey key;
    Snapshot before;
    std::vector<Span> spans;
};

struct RowEdits {
    Tally tally;
    std::vector<EditedRow> rows;
    std::vector<EditedRecord> records;
    std::vector<std::string> failures;
    bool aborted{false};
};

void editRows(fo4::Plugin& edited, const std::vector<fo4::FormKey>& keys, std::span<const ScalarFieldDescriptor> rows,
              RowEdits& e) {
    auto& tally = e.tally;
    for (const auto& field : rows) {
        ++tally.rows;
        if (field.record == fo4::Signature::from_string("ARMA") &&
            field.scalar.subrecord == fo4::Signature::from_string("BSMP")) {
            ++tally.structural;
            continue;
        }
        if (field.scalar.storage == ScalarStorage::zstring || field.scalar.storage == ScalarStorage::fixed_string ||
            field.scalar.storage == ScalarStorage::bytes) {
            ++tally.wholePayload;
            continue;
        }
        if (fo4::schema::is_derived_scalar_row(field.record, field.scalar.subrecord) ||
            fo4::schema::is_object_template_count_row(field)) {
            ++tally.derived;
            continue;
        }

        std::optional<fo4::FormKey> chosen;
        std::optional<Span> chosenSpan;
        bool carried = false;
        bool capped = false;
        std::size_t carriers = 0;
        for (const auto& key : keys) {
            if (capped) break;
            const auto count = fo4::record_field_count(edited, key, field);
            if (!count || *count == 0) continue;
            carried = true;
            capped = ++carriers > kMaxCarriers;
            if (capped) break;
            const auto& current = mustFind(edited, key);
            const auto slot = slotOf(current, field);
            if (!slot) continue;
            const Span span{slot->subrecord, slot->offset, rowWidth(current, field, *slot)};
            const bool taken = std::any_of(e.records.begin(), e.records.end(), [&](const EditedRecord& record) {
                return record.key == key && std::any_of(record.spans.begin(), record.spans.end(),
                                                        [&](const Span& earlier) { return overlaps(earlier, span); });
            });
            if (taken) continue;
            chosen = key;
            chosenSpan = span;
            break;
        }
        if (!carried) {
            ++tally.absent;
            continue;
        }
        if (!chosen) {
            ++tally.unresolved;
            continue;
        }
        const auto key = *chosen;
        const auto span = *chosenSpan;
        auto record = std::find_if(e.records.begin(), e.records.end(),
                                   [&](const EditedRecord& candidate) { return candidate.key == key; });
        std::optional<Snapshot> fresh;
        if (record == e.records.end()) fresh = snapshotOf(mustFind(edited, key));
        const auto original = decodeRow(mustFind(edited, key), field);
        std::string applied;
        std::string decoded;
        for (const auto& candidate : candidates(field)) {
            if (!insideBounds(field, candidate) || candidate == original) continue;
            auto status = fo4::ScalarMutationStatus::unchanged;
            try {
                status = fo4::set_record_scalar(edited, key, field, 0, candidate);
            } catch (const fo4::schema::ScalarError& error) {
                if (refusesWorldGridShape(field, error)) continue;
                e.failures.push_back("the setter threw for " + std::string(field.path) + " on " + key.to_string() +
                                     ": " + error.what());
                e.aborted = true;
                return;
            }
            if (status != fo4::ScalarMutationStatus::updated) continue;
            decoded = decodeRow(mustFind(edited, key), field);
            if (!decoded.empty() && decoded != original) {
                applied = candidate;
                break;
            }
            auto reverted = fo4::ScalarMutationStatus::unchanged;
            std::string why;
            try {
                reverted = fo4::set_record_scalar(edited, key, field, 0, original);
            } catch (const fo4::schema::ScalarError& error) {
                why = std::string(": ") + error.what();
            }
            if (reverted != fo4::ScalarMutationStatus::updated) {
                e.failures.push_back("could not revert a rejected candidate for " + std::string(field.path) + " on " +
                                     key.to_string() + why);
                e.aborted = true;
                return;
            }
        }
        if (applied.empty()) {
            ++tally.noCandidate;
            continue;
        }
        if (fresh) {
            e.records.push_back({key, std::move(*fresh), {}});
            record = e.records.end() - 1;
        }
        const auto& probe = mustFind(edited, key);
        if (span.subrecord >= probe.subrecords.size()) {
            e.failures.push_back(std::string(field.path) + " resolved a subrecord the record no longer has");
            continue;
        }
        const auto& payload = probe.subrecords[span.subrecord].data;
        if (probe.subrecords[span.subrecord].signature != field.scalar.subrecord) {
            e.failures.push_back(std::string(field.path) + " resolved a subrecord other than " +
                                 field.scalar.subrecord.to_string());
        }
        const auto encoded = fo4::schema::encode_scalar(applied, field.scalar);
        if (span.offset + encoded.size() > payload.size() ||
            !std::equal(encoded.begin(), encoded.end(), payload.begin() + static_cast<std::ptrdiff_t>(span.offset))) {
            e.failures.push_back("wrote " + std::string(field.path) + " off its declared slot on " + key.to_string());
        }
        if (field.scalar.address.stride == 0 && field.scalar.address.array_offset == 0 &&
            field.scalar.address.segment.count == 0 && span.offset != field.scalar.offset) {
            e.failures.push_back(std::string(field.path) + " resolved a slot other than its declared position");
        }
        record->spans.push_back(span);
        e.rows.push_back({field, key, decoded});
        ++tally.exercised;
    }
}

void verifyRows(const fo4::Plugin& edited, const fo4::Plugin& reread, RowEdits& e) {
    for (const auto& record : e.records) {
        if (record.spans.empty()) continue;
        const auto name = record.key.to_string();
        const auto* written = fo4::find_record(edited, record.key);
        if (written == nullptr) {
            e.failures.push_back(name + " is missing from the edited plugin");
            continue;
        }
        const auto afterEdit = snapshotOf(*written);
        if (record.before.size() != afterEdit.size()) {
            e.failures.push_back("changed the subrecord count of " + name);
            continue;
        }
        if (sameRecord(record.before, *written)) e.failures.push_back("wrote nothing to " + name);
        const auto* restored = fo4::find_record(reread, record.key);
        if (restored == nullptr) {
            e.failures.push_back(name + " is missing after the write");
            continue;
        }
        if (!sameRecord(afterEdit, *restored)) e.failures.push_back(name + " did not re-read byte for byte");
        for (std::size_t i = 0; i < record.before.size(); ++i) {
            if (record.before[i].data == afterEdit[i].data) continue;
            bool named = false;
            for (const auto& row : e.rows) {
                if (row.key != record.key) continue;
                if (row.field.scalar.subrecord == record.before[i].signature) named = true;
                if (resizesWorldGrid(row.field, record.before[i].signature)) named = true;
            }
            if (!named) {
                e.failures.push_back("changed " + record.before[i].signature.to_string() + " on " + name +
                                     " which no edited row names");
            }
        }
        for (const auto& row : e.rows) {
            if (row.key != record.key) continue;
            if (decodeRow(*restored, row.field) != row.expected) {
                e.failures.push_back("lost " + std::string(row.field.path) + " across the write on " + name);
            }
        }
    }
}

void rowRoundtripBody(Ctx& c, Diag& d, const std::function<void(const char*, std::string)>& finish) {
    std::string family = c.param("family");
    const bool shape = family.size() == 4 && std::all_of(family.begin(), family.end(), [](unsigned char ch) {
                           return std::isalnum(ch) != 0 || ch == '_';
                       });
    if (!shape) return finish("error", "The family must be a four-character record signature such as KEYM.");
    for (auto& ch : family) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    const auto signature = fo4::Signature::from_string(family);

    const auto source = resolvePluginPath(c.env, c.param("plugin"));
    if (source.empty()) return finish("skip", "The plugin file was not found in the load order or the Data folder.");
    const auto dataDir = dataDirOf(c.env);
    if (!dataDir.empty() && isInside(source, dataDir)) d.evidence.push_back("real-game-data");
    const std::string scope =
        " Scalar rows of one record signature only: link rows, link lists and editor ID length changes are not run.";

    c.progress(0, 4, "reading " + source.filename().string());
    auto edited = fo4::read_plugin_file(source);
    const auto rows = fo4::schema::scalar_fields(signature);
    if (rows.empty()) {
        return finish("unsupported", "The schema has no scalar rows for " + family + ", so there is nothing to edit.");
    }
    std::vector<fo4::FormKey> keys;
    fo4::visit_records(std::as_const(edited), [&](const fo4::Record& record) {
        if (record.header.signature == signature) keys.push_back(record.form_key);
    });
    if (keys.empty()) return finish("unsupported", "The plugin has no " + family + " record, so no row can be edited.");

    if (stoppedForShutdown(c, d)) return;
    c.progress(1, 4, "editing one record per " + family + " row");
    RowEdits edits;
    editRows(edited, keys, rows, edits);
    const auto& t = edits.tally;
    const std::string tallyText = std::to_string(t.exercised) + " of " + std::to_string(t.rows) + " row(s) edited (" +
                                  std::to_string(t.absent) + " carried by no record, " + std::to_string(t.derived) +
                                  " derived count, " + std::to_string(t.wholePayload) + " whole-payload, " +
                                  std::to_string(t.structural) + " structural, " + std::to_string(t.noCandidate) +
                                  " with no usable candidate, " + std::to_string(t.unresolved) + " unresolved).";
    d.left = Side{"edited rows", source.string(),
                  std::to_string(t.exercised) + " row(s) on " + std::to_string(edits.records.size()) + " record(s)"};
    if (edits.aborted) {
        for (const auto& failure : edits.failures) addDiff(d, failure);
        return finish("fail", "Editing stopped at the first setter failure, so nothing was written. " + tallyText + scope);
    }
    if (t.exercised == 0 && !edits.failures.empty()) {
        for (const auto& failure : edits.failures) addDiff(d, failure);
        return finish("fail", std::to_string(edits.failures.size()) + " check(s) failed while editing, so nothing was written. " + tallyText + scope);
    }
    if (t.exercised == 0) {
        return finish("unsupported",
                      "No " + family + " row could be exercised on this plugin: the harness needs a record that carries a row, resolves its slot and takes a candidate value. " +
                          tallyText);
    }

    std::error_code ec;
    if (c.scratch.empty() || !fs::create_directory(c.scratch, ec) || ec) {
        return finish("error", ec ? "The fresh scratch folder could not be created: " + ec.message()
                                  : "The fresh scratch folder could not be created (it may already exist), so nothing was written.");
    }
    [[maybe_unused]] const EmptyScratchCleanup cleanup{c.scratch};
    if (stoppedForShutdown(c, d)) return;
    c.progress(2, 4, "writing the edited plugin");
    const auto bytes = fo4::write_plugin(edited, source);
    const auto target = c.scratch / source.filename();
    auto staged = fo4::io::StagedFile::create(target);
    staged.write(bytes);
    staged.finish();
    if (!std::move(staged).commit_if_absent()) {
        return finish("error", "The scratch file already existed and was not replaced.");
    }
    d.outputs.push_back(target.string());
    d.right = Side{"written and re-read", target.string(), std::to_string(bytes.size()) + " bytes"};

    if (stoppedForShutdown(c, d)) return;
    c.progress(3, 4, "re-reading the written file and comparing every edited record");
    const auto reread = fo4::read_plugin_file(target);
    verifyRows(edited, reread, edits);
    for (const auto& failure : edits.failures) addDiff(d, failure);
    const std::string kept =
        " The edited copy stays in the scratch folder under the plugin's own file name; it holds changed values and must not be put in a Data folder.";
    if (!edits.failures.empty()) {
        return finish("fail", std::to_string(edits.failures.size()) + " round trip check(s) failed. " + tallyText + scope + kept);
    }
    if (t.unresolved > 0) {
        return finish("unverified",
                      "Every edited row survived the write, but " + std::to_string(t.unresolved) +
                          " carried row(s) resolved no free slot on the records that carry them; the harness requires none on Fallout4.esm, and on another plugin this may be a modded layout. " +
                          tallyText + scope + kept);
    }
    finish("pass", "Every edited row was written, re-read from disk and compared: same subrecord count, only the named subrecords changed, each value decoded back. " +
                       tallyText + scope + kept);
}

void rowRoundtrip(Ctx& c, std::vector<Diag>& out) {
    Diag d = begin("plugin.rowRoundtrip", "Row edit and write round trip", {"structural"});
    const std::function<void(const char*, std::string)> finish = [&](const char* status, std::string detail) {
        setStatus(d, status, std::move(detail));
    };
    try {
        rowRoundtripBody(c, d, finish);
    } catch (const std::exception& error) {
        d.diffs.clear();
        finish("error", std::string("The round trip stopped: ") + error.what());
    }
    out.push_back(std::move(d));
}

}  // namespace

Result<BrokenCellTable> parseBrokenCellTable(std::string_view text) {
    BrokenCellTable table;
    std::size_t at = 0;
    std::size_t lineNumber = 0;
    while (at < text.size()) {
        ++lineNumber;
        auto end = text.find('\n', at);
        if (end == std::string_view::npos) end = text.size();
        auto line = text.substr(at, end - at);
        at = end + 1;
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (line.empty()) continue;
        if (line.front() == '#') {
            if (table.build.empty()) table.build = buildIn(std::string(line));
            continue;
        }
        const auto tab = line.find('\t');
        if (tab == std::string_view::npos) {
            return std::unexpected(Error{"BadTable", "line " + std::to_string(lineNumber) + " has no tab"});
        }
        if (!table.cells.emplace(std::string(line.substr(0, tab)), std::string(line.substr(tab + 1))).second) {
            return std::unexpected(Error{"BadTable", "line " + std::to_string(lineNumber) + " repeats a cell"});
        }
    }
    return table;
}

BrokenCellDiff compareBrokenCells(const BrokenCells& pinned, const BrokenCells& actual) {
    BrokenCellDiff diff;
    for (const auto& [cell, codes] : pinned) {
        const auto found = actual.find(cell);
        if (found == actual.end()) {
            diff.missing.push_back(cell + " " + codes);
        } else if (found->second != codes) {
            diff.different.push_back(cell + " reported " + found->second + ", pinned " + codes);
        }
    }
    for (const auto& [cell, codes] : actual) {
        if (!pinned.contains(cell)) diff.extra.push_back(cell + " " + codes);
    }
    return diff;
}

void appendReferenceTools(std::vector<ToolSpec>& table) {
    auto baseline = makeTool("previs.vanillaBaseline", "previs", "Comparison with the pinned vanilla broken-cell list",
                             "Diagnoses the six official masters in the Data folder the way the Radicle vanilla gate does and compares the broken cells with the pinned list. Never above unverified unless the game build matches the build the list was measured on. Writes nothing.",
                             {"real-game-data"},
                             {{"baseline", "Pinned list file (optional, default the one shipped with the editor)", "text", false}},
                             noRequirement, previsVanillaBaseline);
    baseline.expensive = true;
    baseline.inGame = true;
    table.push_back(std::move(baseline));

    auto rows = makeTool("plugin.rowRoundtrip", "plugins", "Row edit and write round trip",
                         "Edits one record per scalar row of one record type in memory, writes the plugin to a fresh folder in the output folder you choose, re-reads that file and compares every edited record. Reports unsupported with the reason where the Radicle harness's preconditions do not hold. Scalar rows only. Cannot be cancelled.",
                         {"structural", "real-game-data"},
                         {{"plugin", "Plugin file name", "plugin", true},
                          {"family", "Record type (four letters, for example KEYM)", "text", true}},
                         noRequirement, rowRoundtrip);
    rows.readOnly = false;
    rows.writes = true;
    rows.needsOutputDir = true;
    rows.expensive = true;
    table.push_back(std::move(rows));
}

}  // namespace fo4ide::app::debugtools

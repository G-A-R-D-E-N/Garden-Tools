#include "RadiclePluginSupport.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <fo4/forms/faction.hpp>
#include <fo4/forms/npc.hpp>
#include <fo4/forms/weapon.hpp>
#include <fo4/schema/localized_fields.hpp>

namespace fo4ide::plugin {

namespace {

constexpr const char* k_invalid_npc_entry = "NPC entry values are missing or out of range";
constexpr const char* k_invalid_faction_entry =
    "FACT entry values are missing, out of range or use an unknown reaction";
constexpr const char* k_invalid_operation = "Operation must be add, set or remove";
constexpr const char* k_invalid_index = "Entry index must be a non-negative integer";
constexpr std::array<std::string_view, 4> k_reaction_names{"neutral", "enemy", "ally", "friendly"};

nlohmann::json npcInventoryJson(const fo4::forms::NpcInventoryItem& item) {
    nlohmann::json result{
        {"item", support::formKeyJson(item.item)},
        {"count", item.count},
        {"extra", nullptr}
    };
    if (item.extra_data.has_value()) {
        const auto& extra = *item.extra_data;
        result["extra"] = {
            {"owner", extra.owner.has_value() ? support::formKeyJson(*extra.owner) : nlohmann::json(nullptr)},
            {"globalOrRank", extra.global_or_rank},
            {"condition", extra.condition}
        };
    }
    return result;
}

Result<fo4::forms::NpcFaction> npcFactionFromJson(const nlohmann::json& value) {
    const auto faction = support::formKeyArgument(support::stringField(value, "faction"));
    const auto rank = support::integerField<std::int8_t>(value, "rank");
    if (!faction || !rank) return std::unexpected(Error{"InvalidNpcEntry", k_invalid_npc_entry});
    return fo4::forms::NpcFaction{*faction, *rank};
}

Result<fo4::forms::NpcPerk> npcPerkFromJson(const nlohmann::json& value) {
    const auto perk = support::formKeyArgument(support::stringField(value, "perk"));
    const auto rank = support::integerField<std::uint8_t>(value, "rank");
    if (!perk || !rank) return std::unexpected(Error{"InvalidNpcEntry", k_invalid_npc_entry});
    return fo4::forms::NpcPerk{*perk, *rank};
}

Result<fo4::forms::NpcInventoryItem> npcInventoryFromJson(const nlohmann::json& value) {
    const auto item = support::formKeyArgument(support::stringField(value, "item"));
    const auto count = support::integerField<std::int32_t>(value, "count");
    if (!item || !count) return std::unexpected(Error{"InvalidNpcEntry", k_invalid_npc_entry});

    fo4::forms::NpcInventoryItem entry;
    entry.item = *item;
    entry.count = *count;
    if (value.contains("extra") && !value.at("extra").is_null()) {
        const auto& extra = value.at("extra");
        const auto globalOrRank = support::integerField<std::uint32_t>(extra, "globalOrRank");
        if (!globalOrRank || !extra.contains("condition") || !extra.at("condition").is_number()) {
            return std::unexpected(Error{"InvalidNpcEntry", k_invalid_npc_entry});
        }
        fo4::forms::NpcItemExtraData data;
        data.global_or_rank = *globalOrRank;
        data.condition = extra.at("condition").get<float>();
        if (extra.contains("owner") && !extra.at("owner").is_null()) {
            const auto owner = support::formKeyArgument(support::stringField(extra, "owner"));
            if (!owner) return std::unexpected(owner.error());
            data.owner = *owner;
        }
        entry.extra_data = data;
    }
    return entry;
}

std::string hexText(const std::vector<std::byte>& bytes) {
    static constexpr char digits[] = "0123456789ABCDEF";
    std::string text;
    for (const auto byte : bytes) {
        const auto value = std::to_integer<unsigned>(byte);
        text += digits[value >> 4];
        text += digits[value & 0xF];
    }
    return text;
}

nlohmann::json titleJson(const std::optional<fo4::schema::LocalizedString>& title) {
    if (!title.has_value()) return nlohmann::json(nullptr);
    if (title->text.has_value()) return nlohmann::json(*title->text);
    if (title->id.has_value()) return nlohmann::json{{"stringId", *title->id}};
    if (title->raw_id.has_value()) return nlohmann::json{{"rawId", hexText(*title->raw_id)}};
    return nlohmann::json(nullptr);
}

std::optional<fo4::schema::LocalizedString> titleOf(const nlohmann::json& value) {
    fo4::schema::LocalizedString title;
    if (value.is_string()) {
        title.text = value.get<std::string>();
        return title;
    }
    const auto id = support::integerField<std::uint32_t>(value, "stringId");
    if (!id) return std::nullopt;
    title.id = *id;
    return title;
}

bool rawIdTitle(const nlohmann::json& value) {
    return value.is_object() && value.size() == 1 && value.contains("rawId") && value.at("rawId").is_string();
}

bool validTitle(const nlohmann::json& value, const char* name) {
    if (!value.contains(name)) return true;
    const auto& title = value.at(name);
    return title.is_string() || title.is_null() || rawIdTitle(title) ||
           support::integerField<std::uint32_t>(title, "stringId").has_value();
}

std::string reactionName(fo4::forms::FactionCombatReaction reaction) {
    return std::string(k_reaction_names[static_cast<std::size_t>(reaction)]);
}

std::optional<fo4::forms::FactionCombatReaction> reactionFromName(std::string_view name) {
    for (std::size_t index = 0; index < k_reaction_names.size(); ++index) {
        if (k_reaction_names[index] == name) {
            return static_cast<fo4::forms::FactionCombatReaction>(index);
        }
    }
    return std::nullopt;
}

Result<fo4::forms::FactionRelation> factionRelationFromJson(const nlohmann::json& value) {
    const auto faction = support::formKeyArgument(support::stringField(value, "faction"));
    const auto modifier = support::integerField<std::int32_t>(value, "modifier");
    const auto reaction = reactionFromName(support::stringField(value, "reaction"));
    if (!faction || !modifier || !reaction) {
        return std::unexpected(Error{"InvalidFactionEntry", k_invalid_faction_entry});
    }
    return fo4::forms::FactionRelation{*faction, *modifier, *reaction};
}

bool sameSubrecords(const std::vector<fo4::Subrecord>& before, const std::vector<fo4::Subrecord>& after) {
    return std::equal(before.begin(), before.end(), after.begin(), after.end(),
                      [](const fo4::Subrecord& a, const fo4::Subrecord& b) {
                          return a.signature == b.signature && a.extended_size == b.extended_size &&
                                 a.data == b.data;
                      });
}

}  // namespace

Result<bool> RadiclePluginBackend::stageTypedRecordEdit(
    std::string_view pluginName,
    const fo4::FormKey& key,
    std::string_view failureCode,
    const std::function<void(fo4::Plugin&)>& edit) {
    if (pluginName.empty()) {
        return std::unexpected(Error{"PluginMissing", "Plugin name is empty"});
    }

    OpLock lock(*this, OpLock::Mode::Write, {pluginName});
    auto* loaded = findPlugin(pluginName);
    if (loaded == nullptr) {
        return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + std::string(pluginName)});
    }
    if (const auto error = writeGuard(*loaded); error.has_value()) return std::unexpected(*error);

    auto mutated = loaded->plugin;
    const auto* record = fo4::find_record(mutated, key);
    if (record == nullptr) {
        return std::unexpected(Error{
            "RecordNotFound",
            "Record is not present in " + std::string(pluginName) + ": " + key.to_string()
        });
    }
    if (record->header.flags.is_deleted()) {
        return std::unexpected(Error{
            "RecordDeleted",
            "The record is deleted in " + std::string(pluginName) + ": " + key.to_string()
        });
    }
    const auto before = record->subrecords;
    try {
        edit(mutated);
    } catch (const std::exception& error) {
        return std::unexpected(Error{std::string(failureCode), error.what()});
    }
    const auto* after = fo4::find_record(mutated, key);
    if (after != nullptr && sameSubrecords(before, after->subrecords)) return false;
    stagePluginMutationLocked(*loaded, std::move(mutated));
    return true;
}

Result<const fo4::Plugin*> RadiclePluginBackend::versionPlugin(std::string_view pluginName,
                                                               const fo4::FormKey& key) const {
    if (pluginName.empty()) return support::winningPlugin(plugins_, key);
    const auto* loaded = findPlugin(pluginName);
    if (loaded == nullptr) {
        return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + std::string(pluginName)});
    }
    const auto* record = fo4::find_record(loaded->plugin, key);
    if (record == nullptr) {
        return std::unexpected(Error{
            "RecordNotFound",
            "Record is not present in " + std::string(pluginName) + ": " + key.to_string()
        });
    }
    if (record->header.flags.is_deleted()) {
        return std::unexpected(Error{
            "RecordDeleted",
            "The record is deleted in " + std::string(pluginName) + ": " + key.to_string()
        });
    }
    return &loaded->plugin;
}

Result<nlohmann::json> RadiclePluginBackend::getNpc(const nlohmann::json& arguments) const {
    if (const auto error = testRecoveryGate("GetNpc", arguments)) return std::unexpected(*error);
    const auto key = support::formKeyArgument(support::stringArgument(arguments, 0));
    if (!key) return std::unexpected(key.error());

    OpLock lock(*this, OpLock::Mode::Read);
    const auto version = versionPlugin(support::stringArgument(arguments, 1), *key);
    if (!version) return std::unexpected(version.error());
    const auto* source = *version;
    try {
        nlohmann::json factions = nlohmann::json::array();
        for (const auto& entry : fo4::forms::decode_npc_factions(*source, *key)) {
            factions.push_back(nlohmann::json{{"faction", support::formKeyJson(entry.faction)}, {"rank", entry.rank}});
        }
        nlohmann::json perks = nlohmann::json::array();
        for (const auto& entry : fo4::forms::decode_npc_perks(*source, *key)) {
            perks.push_back(nlohmann::json{{"perk", support::formKeyJson(entry.perk)}, {"rank", entry.rank}});
        }
        nlohmann::json inventory = nlohmann::json::array();
        for (const auto& entry : fo4::forms::decode_npc_inventory(*source, *key)) {
            inventory.push_back(npcInventoryJson(entry));
        }
        return nlohmann::json{
            {"formKey", key->to_string()},
            {"factions", std::move(factions)},
            {"perks", std::move(perks)},
            {"inventory", std::move(inventory)}
        };
    } catch (const std::exception& error) {
        return std::unexpected(Error{"NpcReadFailed", error.what()});
    }
}

Result<nlohmann::json> RadiclePluginBackend::editNpcEntries(const nlohmann::json& arguments) {
    const auto pluginName = support::stringArgument(arguments, 0);
    const auto key = support::formKeyArgument(support::stringArgument(arguments, 1));
    const auto list = support::stringArgument(arguments, 2);
    const auto op = support::stringArgument(arguments, 3);
    const auto index = support::integerArgument<std::uint32_t>(arguments, 4);
    const auto value = support::argumentAt(arguments, 5);
    if (!key) return std::unexpected(key.error());
    if (op != "add" && op != "set" && op != "remove") {
        return std::unexpected(Error{"InvalidNpcOperation", k_invalid_operation});
    }
    if (op != "add" && !index) return std::unexpected(Error{"InvalidNpcEntry", k_invalid_index});
    if (list != "factions" && list != "perks" && list != "inventory") {
        return std::unexpected(Error{"InvalidNpcList", "NPC list must be factions, perks or inventory"});
    }

    const auto position = static_cast<std::size_t>(index.value_or(0u));
    const auto commit = [&](const std::function<void(fo4::Plugin&)>& edit) -> Result<nlohmann::json> {
        const auto staged = stageTypedRecordEdit(pluginName, *key, "NpcWriteFailed", edit);
        if (!staged) return std::unexpected(staged.error());
        if (!*staged) return nlohmann::json("No change for " + list + " entry in " + key->to_string() + " in " + pluginName + ".");
        return nlohmann::json("Updated " + list + " entry in " + key->to_string() + " in " + pluginName + ".");
    };

    if (list == "factions") {
        std::optional<fo4::forms::NpcFaction> entry;
        if (op != "remove") {
            auto parsed = npcFactionFromJson(value);
            if (!parsed) return std::unexpected(parsed.error());
            entry = *parsed;
        }
        return commit([&](fo4::Plugin& mutated) {
            if (op == "add") {
                fo4::forms::add_npc_faction(mutated, *key, *entry);
            } else if (op == "set") {
                fo4::forms::set_npc_faction(mutated, *key, position, *entry);
            } else {
                fo4::forms::remove_npc_faction(mutated, *key, position);
            }
        });
    }
    if (list == "perks") {
        std::optional<fo4::forms::NpcPerk> entry;
        if (op != "remove") {
            auto parsed = npcPerkFromJson(value);
            if (!parsed) return std::unexpected(parsed.error());
            entry = *parsed;
        }
        return commit([&](fo4::Plugin& mutated) {
            if (op == "add") {
                fo4::forms::add_npc_perk(mutated, *key, *entry);
            } else if (op == "set") {
                fo4::forms::set_npc_perk(mutated, *key, position, *entry);
            } else {
                fo4::forms::remove_npc_perk(mutated, *key, position);
            }
        });
    }
    std::optional<fo4::forms::NpcInventoryItem> entry;
    if (op != "remove") {
        auto parsed = npcInventoryFromJson(value);
        if (!parsed) return std::unexpected(parsed.error());
        entry = std::move(*parsed);
    }
    return commit([&](fo4::Plugin& mutated) {
        if (op == "add") {
            fo4::forms::add_npc_inventory_item(mutated, *key, *entry);
        } else if (op == "set") {
            fo4::forms::set_npc_inventory_item(mutated, *key, position, *entry);
        } else {
            fo4::forms::remove_npc_inventory_item(mutated, *key, position);
        }
    });
}

Result<nlohmann::json> RadiclePluginBackend::getFaction(const nlohmann::json& arguments) const {
    const auto key = support::formKeyArgument(support::stringArgument(arguments, 0));
    if (!key) return std::unexpected(key.error());

    OpLock lock(*this, OpLock::Mode::Read);
    const auto version = versionPlugin(support::stringArgument(arguments, 1), *key);
    if (!version) return std::unexpected(version.error());
    const auto* source = *version;
    try {
        nlohmann::json relations = nlohmann::json::array();
        for (const auto& relation : fo4::forms::decode_faction_relations(*source, *key)) {
            relations.push_back(nlohmann::json{
                {"faction", support::formKeyJson(relation.faction)},
                {"modifier", relation.modifier},
                {"reaction", reactionName(relation.reaction)}
            });
        }
        nlohmann::json ranks = nlohmann::json::array();
        for (const auto& rank : fo4::forms::decode_faction_ranks(*source, *key)) {
            ranks.push_back(nlohmann::json{
                {"number", rank.number},
                {"maleTitle", titleJson(rank.male_title)},
                {"femaleTitle", titleJson(rank.female_title)},
                {"hasInsignia", rank.insignia.has_value()}
            });
        }
        return nlohmann::json{
            {"formKey", key->to_string()},
            {"relations", std::move(relations)},
            {"ranks", std::move(ranks)}
        };
    } catch (const std::exception& error) {
        return std::unexpected(Error{"FactionReadFailed", error.what()});
    }
}

Result<nlohmann::json> RadiclePluginBackend::editFactionEntries(const nlohmann::json& arguments) {
    const auto pluginName = support::stringArgument(arguments, 0);
    const auto key = support::formKeyArgument(support::stringArgument(arguments, 1));
    const auto list = support::stringArgument(arguments, 2);
    const auto op = support::stringArgument(arguments, 3);
    const auto index = support::integerArgument<std::uint32_t>(arguments, 4);
    const auto value = support::argumentAt(arguments, 5);
    if (!key) return std::unexpected(key.error());
    if (op != "add" && op != "set" && op != "remove") {
        return std::unexpected(Error{"InvalidFactionOperation", k_invalid_operation});
    }
    if (op != "add" && !index) return std::unexpected(Error{"InvalidFactionEntry", k_invalid_index});
    if (list != "relations" && list != "ranks") {
        return std::unexpected(Error{"InvalidFactionList", "FACT list must be relations or ranks"});
    }

    const auto position = static_cast<std::size_t>(index.value_or(0u));
    const auto commit = [&](const std::function<void(fo4::Plugin&)>& edit) -> Result<nlohmann::json> {
        const auto staged = stageTypedRecordEdit(pluginName, *key, "FactionWriteFailed", edit);
        if (!staged) return std::unexpected(staged.error());
        if (!*staged) return nlohmann::json("No change for " + list + " entry in " + key->to_string() + " in " + pluginName + ".");
        return nlohmann::json("Updated " + list + " entry in " + key->to_string() + " in " + pluginName + ".");
    };

    if (list == "relations") {
        std::optional<fo4::forms::FactionRelation> relation;
        if (op != "remove") {
            auto parsed = factionRelationFromJson(value);
            if (!parsed) return std::unexpected(parsed.error());
            relation = *parsed;
        }
        return commit([&](fo4::Plugin& mutated) {
            if (op == "add") {
                fo4::forms::add_faction_relation(mutated, *key, *relation);
            } else if (op == "set") {
                fo4::forms::set_faction_relation(mutated, *key, position, *relation);
            } else {
                fo4::forms::remove_faction_relation(mutated, *key, position);
            }
        });
    }

    const auto number = support::integerField<std::uint32_t>(value, "number");
    if (op != "remove" && (!number || !validTitle(value, "maleTitle") || !validTitle(value, "femaleTitle"))) {
        return std::unexpected(Error{"InvalidFactionEntry", k_invalid_faction_entry});
    }
    const bool rawIdAdd = op == "add" && ((value.contains("maleTitle") && rawIdTitle(value.at("maleTitle"))) ||
                                          (value.contains("femaleTitle") && rawIdTitle(value.at("femaleTitle"))));
    if (rawIdAdd) {
        return std::unexpected(Error{"InvalidFactionEntry", "A rawId title can only be kept on set, not added"});
    }
    return commit([&](fo4::Plugin& mutated) {
        if (op == "remove") {
            fo4::forms::remove_faction_rank(mutated, *key, position);
            return;
        }
        const auto ranks = fo4::forms::decode_faction_ranks(mutated, *key);
        if (op == "set" && position >= ranks.size()) {
            throw fo4::forms::FactionError("FACT rank index is out of range");
        }
        fo4::forms::FactionRank rank = op == "set" ? ranks[position] : fo4::forms::FactionRank{};
        rank.number = *number;
        if (value.contains("maleTitle") && !rawIdTitle(value.at("maleTitle"))) {
            rank.male_title = titleOf(value.at("maleTitle"));
        }
        if (value.contains("femaleTitle") && !rawIdTitle(value.at("femaleTitle"))) {
            rank.female_title = titleOf(value.at("femaleTitle"));
        }
        if (op == "add") {
            fo4::forms::add_faction_rank(mutated, *key, rank);
        } else {
            fo4::forms::set_faction_rank(mutated, *key, position, rank);
        }
    });
}

Result<nlohmann::json> RadiclePluginBackend::getWeaponDamageTypes(const nlohmann::json& arguments) const {
    const auto key = support::formKeyArgument(support::stringArgument(arguments, 0));
    if (!key) return std::unexpected(key.error());

    OpLock lock(*this, OpLock::Mode::Read);
    const auto winner = support::winningPlugin(plugins_, *key);
    if (!winner) return std::unexpected(winner.error());
    const auto* source = *winner;
    try {
        nlohmann::json damageTypes = nlohmann::json::array();
        for (const auto& damage : fo4::forms::decode_weapon_damage_types(*source, *key)) {
            damageTypes.push_back(nlohmann::json{
                {"type", support::formKeyJson(damage.type)},
                {"amount", damage.amount},
                {"curveTable", support::formKeyJson(damage.curve_table)}
            });
        }
        return nlohmann::json{{"formKey", key->to_string()}, {"damageTypes", std::move(damageTypes)}};
    } catch (const std::exception& error) {
        return std::unexpected(Error{"WeaponReadFailed", error.what()});
    }
}

}  // namespace fo4ide::plugin

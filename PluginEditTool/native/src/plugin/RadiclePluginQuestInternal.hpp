#pragma once

#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <fo4/forms/quest.hpp>
#include <fo4/graph/conflict_graph.hpp>
#include <fo4/localization/strings.hpp>
#include <fo4/plugin/container.hpp>
#include <fo4/schema/ctda.hpp>
#include <fo4/signature.hpp>

namespace fo4ide::plugin::questinternal {

std::optional<std::filesystem::path> allowedVanillaFragmentImports(
    const std::filesystem::path& candidate, const std::filesystem::path& dataDirectory);

#ifdef FO4IDE_TEST_HOOKS
inline LocalizedQuestBeforeCommitHook localizedQuestBeforeCommitHook = nullptr;
inline LocalizedQuestAfterTableParseHook localizedQuestAfterTableParseHook = nullptr;
#endif

struct PendingQuestCondition {
    fo4::CtdaCondition condition;
    std::optional<fo4::FormKey> param1;
    std::optional<fo4::FormKey> param2;
    std::optional<fo4::FormKey> reference;
    std::optional<fo4::FormKey> compareGlobal;
};

struct LocalizedQuestTables {
    std::string language;
    std::filesystem::path stringsPath;
    std::filesystem::path dlstringsPath;
    std::filesystem::path ilstringsPath;
    std::vector<std::byte> originalStringsBytes;
    std::vector<std::byte> originalDlstringsBytes;
    std::vector<std::byte> originalIlstringsBytes;
    bool stringsExist{false};
    bool dlstringsExist{false};
    bool ilstringsExist{false};
    fo4::localization::LocalizedStringTable strings;
    fo4::localization::LocalizedStringTable dlstrings;
    fo4::localization::LocalizedStringTable ilstrings;
    std::uint32_t nextId{1};
};

std::string questStringArg(const nlohmann::json& args, std::size_t index);
const nlohmann::json* questArrayArg(const nlohmann::json& args, std::size_t index);
bool questUnsigned(const nlohmann::json& object,
                   const char* key,
                   std::uint32_t defaultValue,
                   std::uint32_t& value);
bool questInt32(const nlohmann::json& object, const char* key, std::optional<std::int32_t>& value);
bool questFloat(const nlohmann::json& object,
                const char* key,
                float defaultValue,
                float& value);
bool questU8(const nlohmann::json& object, const char* key, std::optional<std::uint8_t>& value);
bool questOptionalString(const nlohmann::json& object,
                         const char* key,
                         std::optional<std::string>& value);
std::optional<Error> rejectUnknownKeys(const nlohmann::json& object,
                                       bool (*known)(std::string_view),
                                       std::string_view label,
                                       std::string_view errorType = "InvalidQuestStagePayload");
bool jsonObjectPresent(const nlohmann::json& object, const char* key);
bool isPlacedSignature(fo4::Signature signature);
std::optional<Error> requireLoadedFill(
    const fo4::ConflictGraph& graph,
    const fo4::FormKey& key,
    std::initializer_list<std::string_view> accepted,
    bool placedTargets,
    std::string_view slotName);
std::optional<Error> parseFillFormKey(const nlohmann::json& object,
                                      const char* key,
                                      std::optional<fo4::FormKey>& out);
std::optional<Error> parseOptionalTypedFormKey(
    const nlohmann::json& object,
    const char* key,
    const fo4::ConflictGraph& graph,
    std::initializer_list<std::string_view> accepted,
    std::optional<fo4::FormKey>& out,
    std::vector<fo4::FormKey>& fillKeys);
std::optional<Error> parseAliasFormKeyList(
    const nlohmann::json& object,
    const char* key,
    const fo4::ConflictGraph& graph,
    std::initializer_list<std::string_view> accepted,
    bool placedTargets,
    std::vector<fo4::FormKey>& out,
    std::vector<fo4::FormKey>& allKeys);
std::optional<Error> parseAliasFill(
    const nlohmann::json& element,
    fo4::forms::QuestAliasKind kind,
    const fo4::ConflictGraph& graph,
    fo4::forms::QuestAliasFill& fill,
    std::vector<fo4::FormKey>& fillKeys);
std::optional<Error> parseQuestCondition(
    const nlohmann::json& element,
    const fo4::ConflictGraph& graph,
    PendingQuestCondition& out,
    std::vector<fo4::FormKey>& conditionKeys,
    std::string_view errorType = "InvalidQuestStagePayload",
    std::string_view surface = "quest log entries");
void carryConditionPadding(const std::vector<fo4::CtdaCondition>& previous,
                           std::vector<fo4::CtdaCondition>& conditions);
bool aliasConditionRun(const fo4::forms::QuestAlias& alias,
                       std::vector<fo4::CtdaCondition>& conditions);
nlohmann::json questAliasJson(const fo4::Plugin& plugin, const fo4::forms::QuestAlias& alias);
std::optional<Error> adapterConditionUnrepresentable(
    const fo4::Plugin& plugin,
    const fo4::ConflictGraph& graph,
    const fo4::CtdaCondition& condition,
    std::string_view surface = "log entry");
std::optional<Error> existingQuestVmadUnrepresentable(const fo4::Record& record,
                                                      bool aliases,
                                                      bool stages);
fo4::FormLinkTypeLookup questOwnerTypes(const fo4::Plugin& plugin, const fo4::ConflictGraph& graph);
std::optional<Error> existingAliasesUnrepresentable(
    const fo4::Plugin& plugin,
    const fo4::FormKey& quest,
    const fo4::ConflictGraph& graph);
std::optional<Error> extraAliasMembersUnrepresentable(
    const std::vector<fo4::Subrecord>& members,
    const fo4::Plugin& plugin,
    const fo4::ConflictGraph& graph);
std::vector<std::byte> readLocalizedFileBytes(const std::filesystem::path& path);
std::optional<Error> encodeLocalizedText(std::string_view language,
                                          std::string_view text,
                                          std::vector<std::byte>& bytes);
std::optional<Error> loadLocalizedQuestTables(const nlohmann::json& arguments,
                                              const std::filesystem::path& pluginPath,
                                              const std::filesystem::path& directory,
                                              LocalizedQuestTables& result,
                                              bool allowAbsent = false);

}

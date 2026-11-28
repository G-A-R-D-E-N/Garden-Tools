#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <fo4ide/core/ProtectedWriteName.hpp>
#include <fo4ide/mcp/McpServer.hpp>
#include <fo4ide/rpc/RpcDispatcher.hpp>
#include <fo4ide/settings/Settings.hpp>

#include "../../apps/fo4ide/McpCatalog.hpp"
#include "../../apps/fo4ide/McpPathPolicy.hpp"
#include "../../apps/fo4ide/McpTools.hpp"

namespace {

namespace fs = std::filesystem;

using fo4ide::app::AutoRootResolver;
using fo4ide::app::PathArgument;
using fo4ide::app::PathForm;
using fo4ide::app::PathUse;
using fo4ide::app::withPathPolicy;

fs::path testRoot() {
    return fs::temp_directory_path() / "fo4ide-path-policy";
}

fo4ide::settings::Settings policySettings() {
    fo4ide::settings::Settings settings;
    settings.dataFolder = testRoot() / "Fallout 4" / "Data";
    settings.mo2InstancePath = testRoot() / "MO2";
    return settings;
}

std::shared_ptr<fo4ide::mcp::Tool> probe() {
    return std::make_shared<fo4ide::mcp::FunctionTool>(
        fo4ide::mcp::ToolDefinition{.name = "probe", .description = "probe"},
        [](const nlohmann::json&) { return fo4ide::mcp::ToolResult{"ok", false}; });
}

std::vector<fs::path> noAutoRoots() {
    return {};
}

bool isPluginNameArgument(const std::string& name, const nlohmann::json& schema) {
    return (name == "plugin" || name.ends_with("_plugin")) && schema.value("type", std::string{}) == "string";
}

void checkRefused(const fo4ide::mcp::Registry& registry, const std::string& tool, const std::string& argument, const std::string& value) {
    nlohmann::json arguments = nlohmann::json::object();
    arguments[argument] = value;
    const auto result = registry.call(tool, arguments);
    INFO(tool << "." << argument << " = " << value);
    CHECK(result.isError);
    CHECK(result.text.starts_with("Path refused for '" + argument + "'"));
}

fo4ide::mcp::ToolResult guardedCall(const std::vector<PathArgument>& paths,
                                    const fo4ide::settings::SharedSettings& settings,
                                    const nlohmann::json& arguments,
                                    const AutoRootResolver& autoRoots = noAutoRoots) {
    return withPathPolicy(probe(), paths, settings, autoRoots)->call(arguments);
}

std::string joined(const std::vector<std::string>& items) {
    std::string text;
    for (const auto& item : items) text += item + " ";
    return text;
}

enum class Shape { Text, List, JsonText };

struct NativePathArgument {
    const char* tool;
    const char* arg;
    Shape shape;
};

const std::vector<NativePathArgument> nativePathArguments{
    {"open_plugin", "path", Shape::Text},
    {"nif_inspect", "path", Shape::Text},
    {"nif_verify", "path", Shape::Text},
    {"nif_tree", "path", Shape::Text},
    {"nif_geometry", "path", Shape::Text},
    {"nif_segments", "path", Shape::Text},
    {"nif_import", "obj_path", Shape::Text},
    {"nif_import", "out_nif", Shape::Text},
    {"nif_create_texture_set", "path", Shape::Text},
    {"nif_create_texture_set", "out_nif", Shape::Text},
    {"nif_attach_alpha", "path", Shape::Text},
    {"nif_attach_alpha", "out_nif", Shape::Text},
    {"nif_remove_block", "path", Shape::Text},
    {"nif_remove_block", "out_nif", Shape::Text},
    {"nif_block_op", "path", Shape::Text},
    {"nif_block_op", "donor_path", Shape::Text},
    {"nif_block_op", "pose_path", Shape::Text},
    {"nif_block_op", "out_nif", Shape::Text},
    {"nif_pose_list", "pose_path", Shape::Text},
    {"nif_pose_list", "path", Shape::Text},
    {"nif_export_gltf", "path", Shape::Text},
    {"nif_export_gltf", "out", Shape::Text},
    {"nif_import_gltf", "gltf_path", Shape::Text},
    {"nif_import_gltf", "out_nif", Shape::Text},
    {"nif_import_gltf", "skeleton", Shape::Text},
    {"nif_collision_info", "path", Shape::Text},
    {"nif_collision_set", "path", Shape::Text},
    {"nif_collision_set", "out_nif", Shape::Text},
    {"nif_collision_set", "donor_path", Shape::Text},
    {"nif_collision_remove", "path", Shape::Text},
    {"nif_collision_remove", "out_nif", Shape::Text},
    {"nif_connect_points_get", "path", Shape::Text},
    {"nif_connect_points_set", "path", Shape::Text},
    {"nif_connect_points_set", "out_nif", Shape::Text},
    {"nif_uv_get", "path", Shape::Text},
    {"nif_uv_transform", "path", Shape::Text},
    {"nif_uv_transform", "out_nif", Shape::Text},
    {"nif_vertex_color_get", "path", Shape::Text},
    {"nif_vertex_color_set", "path", Shape::Text},
    {"nif_vertex_color_set", "out_nif", Shape::Text},
    {"nif_shape_op", "nif_path", Shape::Text},
    {"nif_shape_op", "out_nif", Shape::Text},
    {"nif_skin_influences", "nif_path", Shape::Text},
    {"nif_skin_influenced_by", "nif_path", Shape::Text},
    {"nif_skin_validate", "nif_path", Shape::Text},
    {"nif_skin_binding", "nif_path", Shape::Text},
    {"nif_skin_binding", "skeleton_path", Shape::Text},
    {"nif_skin_binding", "skeleton_nif_path", Shape::Text},
    {"nif_skin_edit", "nif_path", Shape::Text},
    {"nif_skin_edit", "out_nif", Shape::Text},
    {"hkx_skeleton", "skeleton_path", Shape::Text},
    {"hkx_pose", "skeleton_path", Shape::Text},
    {"hkx_pose", "animation_path", Shape::Text},
    {"hkx_summary", "path", Shape::Text},
    {"hkx_animation_inspect", "animation_path", Shape::Text},
    {"hkx_gltf_save", "template_path", Shape::Text},
    {"hkx_gltf_save", "skeleton_path", Shape::Text},
    {"hkx_gltf_save", "gltf_path", Shape::Text},
    {"hkx_gltf_save", "out_path", Shape::Text},
    {"hkx_subgraph", "plugin_path", Shape::Text},
    {"hkx_subgraph", "data_folder", Shape::Text},
    {"hkx_subgraph_id", "data_folder", Shape::Text},
    {"hkx_subgraph_id", "mo2_instance_dir", Shape::Text},
    {"hkx_project_chain", "data_folder", Shape::Text},
    {"hkx_project_chain", "mo2_instance_dir", Shape::Text},
    {"hkx_project_check", "data_folder", Shape::Text},
    {"hkx_project_check", "mo2_instance_dir", Shape::Text},
    {"hkx_objects", "path", Shape::Text},
    {"hkx_scene", "path", Shape::Text},
    {"hkx_root_motion", "skeleton_path", Shape::Text},
    {"hkx_root_motion", "animation_path", Shape::Text},
    {"hkx_skeleton_document", "skeleton_path", Shape::Text},
    {"hkx_skeleton_edit", "skeleton_path", Shape::Text},
    {"hkx_skeleton_save", "skeleton_path", Shape::Text},
    {"hkx_skeleton_save", "out_path", Shape::Text},
    {"hkx_clip_edit", "path", Shape::Text},
    {"hkx_clip_edit", "out_hkx", Shape::Text},
    {"hkx_convert", "path", Shape::Text},
    {"hkx_convert", "out_hkx", Shape::Text},
    {"hkx_behavior_graph", "path", Shape::Text},
    {"hkx_behavior_model", "path", Shape::Text},
    {"hkx_behavior_authoring", "path", Shape::Text},
    {"hkx_behavior_validate", "path", Shape::Text},
    {"hkx_behavior_edit", "path", Shape::Text},
    {"hkx_behavior_edit", "out_path", Shape::Text},
    {"hkx_ragdoll", "path", Shape::Text},
    {"hkx_ragdoll_save", "path", Shape::Text},
    {"hkx_ragdoll_save", "out_path", Shape::Text},
    {"hkx_ragdoll_preview", "path", Shape::Text},
    {"hkx_mapper", "path", Shape::Text},
    {"hkx_mapper_edit", "path", Shape::Text},
    {"hkx_mapper_edit", "out_path", Shape::Text},
    {"hkx_project_index", "root", Shape::Text},
    {"hkx_animation_index", "data_folder", Shape::Text},
    {"hkx_animation_index", "mo2_folder", Shape::Text},
    {"hkx_object", "path", Shape::Text},
    {"hkx_object_tree", "path", Shape::Text},
    {"hkx_packfile", "path", Shape::Text},
    {"hkx_repack_check", "path", Shape::Text},
    {"hkx_search", "path", Shape::Text},
    {"hkx_channels", "skeleton_path", Shape::Text},
    {"hkx_channels", "animations_path", Shape::Text},
    {"hkx_symbol_usage", "path", Shape::Text},
    {"compile_papyrus", "source", Shape::Text},
    {"compile_papyrus", "output", Shape::Text},
    {"compile_papyrus", "imports", Shape::Text},
    {"compile_papyrus", "flags", Shape::Text},
    {"compile_papyrus", "compiler_path", Shape::Text},
    {"decompile_papyrus", "source", Shape::Text},
    {"decompile_papyrus", "output", Shape::Text},
    {"papyrus_check", "source", Shape::Text},
    {"papyrus_check", "imports", Shape::Text},
    {"audio_convert_to_xwm", "source", Shape::Text},
    {"audio_convert_to_xwm", "output", Shape::Text},
    {"audio_convert_from_xwm", "source", Shape::Text},
    {"audio_convert_from_xwm", "output", Shape::Text},
    {"audio_make_fuz", "audioSource", Shape::Text},
    {"audio_make_fuz", "lipPath", Shape::Text},
    {"audio_make_fuz", "fuzOutput", Shape::Text},
    {"audio_extract_fuz", "fuzPath", Shape::Text},
    {"audio_extract_fuz", "xwmOutput", Shape::Text},
    {"audio_extract_fuz", "lipOutput", Shape::Text},
    {"material_inspect", "path", Shape::Text},
    {"material_set_fields", "path", Shape::Text},
    {"material_set_fields", "output", Shape::Text},
    {"archive_pack", "sources", Shape::List},
    {"archive_pack", "output", Shape::Text},
    {"archive_pack", "rootDir", Shape::Text},
    {"compare_plugin_to", "other_path", Shape::Text},
    {"validate_plugin", "contract", Shape::Text},
    {"strip_masters_clean", "source_path", Shape::Text},
    {"strip_masters_clean", "output_path", Shape::Text},
    {"get_texture", "rel_model_path", Shape::Text},
    {"get_texture", "rel_tex_path", Shape::Text},
    {"get_geometry_batch", "rel_model_paths_json", Shape::JsonText},
    {"create_seq_file", "output_dir", Shape::Text},
    {"fix", "nif_path", Shape::Text},
    {"fix", "out_nif", Shape::Text},
    {"shape_op", "nif_path", Shape::Text},
    {"shape_op", "out_nif", Shape::Text},
    {"script_analyze", "path", Shape::Text},
    {"script_symbol_at", "path", Shape::Text},
    {"script_symbol_at", "imports", Shape::Text},
    {"script_read_script", "path", Shape::Text},
    {"script_write_script", "path", Shape::Text},
    {"archive_extract_file", "archive_path", Shape::Text},
    {"archive_extract_file", "out_path", Shape::Text},
    {"archive_extract_selected", "archive_path", Shape::Text},
    {"archive_extract_selected", "out_dir", Shape::Text},
    {"archive_extract_all", "archive_path", Shape::Text},
    {"archive_extract_all", "out_dir", Shape::Text},
    {"archive_compare", "archive_path_a", Shape::Text},
    {"archive_compare", "archive_path_b", Shape::Text},
    {"archive_list", "archive_path", Shape::Text},
    {"resolve_asset", "path", Shape::Text},
    {"graph_compile_to_pex", "output_directory", Shape::Text},
    {"graph_load", "path", Shape::Text},
    {"load_script", "path", Shape::Text},
    {"graph_save", "path", Shape::Text},
    {"get_quest_script", "imports_directory", Shape::Text},
    {"script_complete", "path", Shape::Text},
    {"script_complete", "imports", Shape::Text},
    {"script_signature_help", "path", Shape::Text},
    {"script_signature_help", "imports", Shape::Text},
    {"script_check_text", "path", Shape::Text},
    {"script_check_text", "imports", Shape::Text},
    {"script_references", "path", Shape::Text},
    {"script_references", "imports", Shape::Text},
    {"script_rename", "path", Shape::Text},
    {"script_rename", "imports", Shape::Text},
    {"script_workspace_symbols", "imports", Shape::Text},
    {"set_quest_fragment", "output_directory", Shape::Text},
    {"set_quest_fragment", "imports_directory", Shape::Text},
    {"remove_quest_fragment", "output_directory", Shape::Text},
    {"remove_quest_fragment", "imports_directory", Shape::Text},
    {"edit_quest_script", "output_directory", Shape::Text},
    {"edit_quest_script", "imports_directory", Shape::Text},
    {"resolve_conflict", "patch", Shape::Text},
    {"lod_worlds", "plugin_paths", Shape::Text},
    {"lod_worlds", "data_dir", Shape::Text},
    {"lod_worlds", "archive_paths", Shape::Text},
    {"lod_plan", "plugin_paths", Shape::Text},
    {"lod_plan", "data_dir", Shape::Text},
    {"lod_plan", "archive_paths", Shape::Text},
    {"lod_preview", "plugin_paths", Shape::Text},
    {"lod_preview", "data_dir", Shape::Text},
    {"lod_preview", "archive_paths", Shape::Text},
    {"lod_candidates", "plugin_paths", Shape::Text},
    {"lod_candidates", "data_dir", Shape::Text},
    {"lod_candidates", "archive_paths", Shape::Text},
    {"lod_generate", "plugin_paths", Shape::Text},
    {"lod_generate", "data_dir", Shape::Text},
    {"lod_generate", "archive_paths", Shape::Text},
    {"lod_verify", "plugin_paths", Shape::Text},
    {"lod_verify", "data_dir", Shape::Text},
    {"lod_verify", "archive_paths", Shape::Text},
    {"render_model_preview", "model_path", Shape::Text},
    {"get_model_bounds", "model_paths", Shape::List},
    {"previs_scan", "data_dir", Shape::Text},
    {"bake_oc_nif", "source_nif", Shape::Text},
    {"bake_oc_nif", "output_oc_nif", Shape::Text},
    {"bake_oc_nif", "oc_shell", Shape::Text},
    {"generate_cell_previs", "oc_shell", Shape::Text},
    {"write_previs_container", "source_path", Shape::Text},
    {"write_previs_container", "output_path", Shape::Text},
    {"lod_generate_world", "plugin_paths", Shape::Text},
    {"lod_generate_world", "data_dir", Shape::Text},
    {"lod_generate_world", "archive_paths", Shape::Text},
    {"lod_verify_world", "plugin_paths", Shape::Text},
    {"lod_verify_world", "data_dir", Shape::Text},
    {"lod_verify_world", "archive_paths", Shape::Text},
};

bool looksLikePath(std::string name) {
    std::ranges::transform(name, name.begin(), [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
    for (const auto* part : {"path", "dir", "folder", "root", "imports", "file", "output"}) {
        if (name.find(part) != std::string::npos) return true;
    }
    return false;
}

// Invariant: these arguments name record fields, archive entries, loaded plugins or record text, never files on disk.
const std::set<std::string>& notFilesystemPaths() {
    static const std::set<std::string> names{
        "describe_element.path", "get_conditions_at.path", "add_element.path", "remove_element.path",
        "move_element.path", "clear_element.path", "set_conditions_at.path",
        "copy_element_to_selected.element_path", "remove_element_from_selected.element_path",
        "archive_extract_file.inner_path", "archive_extract_selected.inner_paths_json",
        "set_field.path", "add_master.master_file", "add_masters.master_file", "add_sound_file.file",
        "browse_assets_folder.path", "search_assets.folder", "describe_kit.model_dir", "hkx_subgraph_id.behavior_path",
        "hkx_animation_index.profile", "hkx_animation_index.resolve_path",
        "hkx_project_chain.hkx_path", "hkx_project_check.hkx_path",
        "hkx_project_chain.mo2_profile", "hkx_project_check.mo2_profile", "hkx_subgraph_id.mo2_profile"};
    return names;
}

}

// Detects: every registered path argument must reject an outside absolute path.
TEST_CASE("Every path argument of every native tool refuses an outside absolute path") {
    const fo4ide::settings::SharedSettings settings{policySettings()};
    fo4ide::mcp::Registry registry;
    fo4ide::rpc::Dispatcher rpc;
    const std::shared_ptr<spdlog::logger> noLog;
    CHECK(registerNativeMcpTools(registry, rpc, noLog, settings, noAutoRoots));

    const auto outside = (testRoot() / "Elsewhere" / "x.esp").string();
    for (const auto& item : nativePathArguments) {
        nlohmann::json value = outside;
        if (item.shape == Shape::List) value = nlohmann::json::array({outside});
        if (item.shape == Shape::JsonText) value = nlohmann::json::array({outside}).dump();
        nlohmann::json arguments = nlohmann::json::object();
        arguments[item.arg] = value;
        const auto result = registry.call(item.tool, arguments);
        INFO(item.tool << "." << item.arg);
        CHECK(result.isError);
        CHECK(result.text.starts_with(std::string("Path refused for '") + item.arg + "'"));
    }

    const auto inside = (testRoot() / "Fallout 4" / "Data" / "Mod.esp").string();
    const auto accepted = registry.call("open_plugin", {{"path", inside}});
    CHECK_FALSE(accepted.text.starts_with("Path refused"));
}

// Detects: a merge or edit must not leave a path-looking catalog argument as a plain string, since the path policy never sees it.
TEST_CASE("Every path-looking catalog argument carries a path tag or is a known non-path string") {
    std::vector<std::string> untagged;
    for (const auto tools : {fo4ide::app::readCatalogTools(), fo4ide::app::editCatalogTools(),
                             fo4ide::app::serviceCatalogTools(), fo4ide::app::assetCatalogTools(),
                             fo4ide::app::layoutCatalogTools()}) {
        for (const auto& tool : tools) {
            std::string_view rest = tool.fields;
            while (!rest.empty()) {
                const auto comma = rest.find(',');
                const auto field = rest.substr(0, comma);
                rest = comma == std::string_view::npos ? std::string_view{} : rest.substr(comma + 1);
                const auto colon = field.find(':');
                if (colon == std::string_view::npos) continue;
                const std::string name(field.substr(0, colon));
                auto type = field.substr(colon + 1);
                if (type.ends_with('?')) type.remove_suffix(1);
                if (name.find('?') != std::string::npos) untagged.push_back(std::string(tool.name) + "." + name + " has the optional marker in its name");
                const auto key = std::string(tool.name) + "." + name;
                if (type == "string" && looksLikePath(name) && !notFilesystemPaths().contains(key)) untagged.push_back(key);
            }
        }
    }
    INFO("plain string arguments that look like paths: " << joined(untagged));
    CHECK(untagged.empty());
}

// Detects: the generate_cell_previs catalog fields must keep the positional order generateCellPrevis reads (patch, cell, source, shell, min_instances, apply, copy_vanilla_tome, lod_targets, patch_into_data).
TEST_CASE("generate_cell_previs catalog argument order matches the backend positions") {
    for (const auto& tool : fo4ide::app::editCatalogTools()) {
        if (tool.name != "generate_cell_previs") continue;
        CHECK(tool.fields == "patch_plugin:string,cell:string,source_plugin:string?,oc_shell:rpath?,min_instances:integer?,apply:boolean?,copy_vanilla_tome:boolean?,lod_targets:json?,patch_into_data:boolean?");
        return;
    }
    FAIL("generate_cell_previs is not in the edit catalog");
}

// Detects: the precombine_plan catalog fields must keep the positional order precombinePlan reads (cell, plugin, min_instances, lod_targets, group_limit, include_instances).
TEST_CASE("precombine_plan catalog argument order matches the backend positions") {
    for (const auto& tool : fo4ide::app::editCatalogTools()) {
        if (tool.name != "precombine_plan") continue;
        CHECK(tool.fields == "cell:string,plugin:string,min_instances:integer?,lod_targets:json?,group_limit:integer?,include_instances:boolean?");
        return;
    }
    FAIL("precombine_plan is not in the edit catalog");
}

// Detects: a tool registered by hand in McpTools.cpp that gains a path argument without a path policy entry must fail here, since the catalog scan never sees it.
TEST_CASE("Every path-looking argument of every registered tool is covered by the path policy or is a known non-path string") {
    const fo4ide::settings::SharedSettings settings{policySettings()};
    fo4ide::mcp::Registry registry;
    fo4ide::rpc::Dispatcher rpc;
    const std::shared_ptr<spdlog::logger> noLog;
    REQUIRE(registerNativeMcpTools(registry, rpc, noLog, settings, noAutoRoots));

    std::set<std::string> covered;
    for (const auto& item : nativePathArguments) covered.insert(std::string(item.tool) + "." + item.arg);

    std::vector<std::string> uncovered;
    for (const auto& definition : registry.definitions()) {
        const auto& name = definition.at("name").get_ref<const std::string&>();
        const auto properties = definition.at("inputSchema").value("properties", nlohmann::json::object());
        for (const auto& [argument, schema] : properties.items()) {
            const auto key = name + "." + argument;
            if (schema.value("type", nlohmann::json{}) != "string" || !looksLikePath(argument)) continue;
            if (!covered.contains(key) && !notFilesystemPaths().contains(key)) uncovered.push_back(key);
        }
    }
    INFO("path-looking arguments with no path policy entry: " << joined(uncovered));
    CHECK(uncovered.empty());
}

// Detects: a newline separated lod path list whose first line is allowed must not carry a second line outside the roots, and lod_generate must apply the write rules to data_dir.
TEST_CASE("lod path lists are checked line by line and lod_generate treats data_dir as a write target") {
    const fo4ide::settings::SharedSettings settings{policySettings()};
    fo4ide::mcp::Registry registry;
    fo4ide::rpc::Dispatcher rpc;
    const std::shared_ptr<spdlog::logger> noLog;
    REQUIRE(registerNativeMcpTools(registry, rpc, noLog, settings, noAutoRoots));

    const auto inside = (testRoot() / "Fallout 4" / "Data" / "A.esp").string();
    const auto outside = (testRoot() / "Elsewhere" / "B.esp").string();
    checkRefused(registry, "lod_worlds", "plugin_paths", inside + "\n" + outside);
    checkRefused(registry, "lod_plan", "archive_paths", inside + "\r\n" + outside);
    CHECK_FALSE(registry.call("lod_worlds", {{"plugin_paths", inside + "\n" + inside}}).text.starts_with("Path refused"));

    const auto protectedName = (testRoot() / "Fallout 4" / "Data" / "Tiles.exe").string();
    checkRefused(registry, "lod_generate", "data_dir", protectedName);
    CHECK_FALSE(registry.call("lod_verify", {{"data_dir", protectedName}}).text.starts_with("Path refused"));
    checkRefused(registry, "lod_generate_world", "plugin_paths", inside + "\n" + outside);
    checkRefused(registry, "lod_verify_world", "archive_paths", inside + "\r\n" + outside);
    checkRefused(registry, "lod_generate_world", "data_dir", protectedName);
    CHECK_FALSE(registry.call("lod_verify_world", {{"data_dir", protectedName}}).text.starts_with("Path refused"));
}

// Detects: a json catalog argument must declare a schema type that argument validation accepts, so bake_oc_nif placements_or_plan and generate_cell_previs lod_targets can be supplied.
TEST_CASE("json catalog arguments are not refused by argument validation") {
    const fo4ide::settings::SharedSettings settings{policySettings()};
    fo4ide::mcp::Registry registry;
    fo4ide::rpc::Dispatcher rpc;
    const std::shared_ptr<spdlog::logger> noLog;
    REQUIRE(fo4ide::app::registerCatalogTools(registry, rpc, noLog, settings, noAutoRoots));

    const auto data = testRoot() / "Fallout 4" / "Data";
    const nlohmann::json plan = {{"plan_groups", nlohmann::json::array()}};
    for (const auto& value : {plan, nlohmann::json::array({plan}), nlohmann::json("plan.json")}) {
        const auto result = registry.call("bake_oc_nif", {{"source_nif", (data / "in.nif").string()},
                                                          {"output_oc_nif", (data / "out.nif").string()},
                                                          {"placements_or_plan", value}});
        INFO(value.dump() << " -> " << result.text);
        CHECK(result.text.find("invalid schema type") == std::string::npos);
        CHECK(result.text.find("invalid type") == std::string::npos);
        CHECK_FALSE(result.text.starts_with("placements_or_plan"));
        CHECK_FALSE(result.text.starts_with("Path refused"));
    }
    const auto lod = registry.call("generate_cell_previs", {{"patch_plugin", "Patch.esp"}, {"cell", "Cell"},
                                                            {"lod_targets", nlohmann::json::array()}});
    INFO(lod.text);
    CHECK(lod.text.find("invalid schema type") == std::string::npos);
    CHECK_FALSE(lod.text.starts_with("lod_targets"));
}

// Detects: a NUL or other control character in a write path must not hide a protected file type from the denylist.
TEST_CASE("Path policy refuses control characters in a path") {
    const fo4ide::settings::SharedSettings settings{policySettings()};
    const std::vector<PathArgument> paths{{"path", PathUse::Write, PathForm::Single}};
    const auto root = (testRoot() / "Fallout 4" / "Data").string();

    // Each refusal must come from the control-character check, since an outside-root refusal would hide a missing fix.
    for (const auto& bad : {root + "/evil.exe" + std::string(1, '\0') + ".txt", "\v" + root + "/x.json", root + "/a\fb.json"}) {
        const auto result = guardedCall(paths, settings, {{"path", bad}});
        CHECK(result.isError);
        CHECK(result.text.find("control characters") != std::string::npos);
    }
    CHECK_FALSE(guardedCall(paths, settings, {{"path", root + "/x.json"}}).isError);
}

// Detects: dot-only path segments must not escape an allowed root.
TEST_CASE("Path policy refuses '..' and dot-only path segments") {
    const fo4ide::settings::SharedSettings settings{policySettings()};
    const std::vector<PathArgument> paths{{"path", PathUse::Read, PathForm::Single}};
    const auto root = testRoot() / "Fallout 4";

    CHECK(guardedCall(paths, settings, {{"path", (root / ".." / "x.json").string()}}).isError);
    CHECK(guardedCall(paths, settings, {{"path", (root / "..." / "x.json").string()}}).isError);
    CHECK_FALSE(guardedCall(paths, settings, {{"path", (root / "." / "x.json").string()}}).isError);
}

// Detects: configured install and MO2 paths must remain usable.
TEST_CASE("Path policy accepts paths inside the install folder and the MO2 instance folder") {
    const fo4ide::settings::SharedSettings settings{policySettings()};
    const std::vector<PathArgument> paths{{"path", PathUse::Read, PathForm::Single}};

    CHECK_FALSE(guardedCall(paths, settings, {{"path", (testRoot() / "Fallout 4" / "Data" / "x.json").string()}}).isError);
    CHECK_FALSE(guardedCall(paths, settings, {{"path", (testRoot() / "MO2").string()}}).isError);
    CHECK(guardedCall(paths, settings, {{"path", (testRoot() / "Elsewhere" / "x.json").string()}}).isError);
}

// Detects: the configured game install must be an allowed root.
TEST_CASE("Path policy accepts paths inside the configured Fallout 4 install folder") {
    fo4ide::settings::Settings configured = policySettings();
    configured.fallout4Path = testRoot() / "Install";
    const fo4ide::settings::SharedSettings settings{configured};
    const std::vector<PathArgument> paths{{"path", PathUse::Read, PathForm::Single}};

    CHECK_FALSE(guardedCall(paths, settings, {{"path", (testRoot() / "Install" / "Data" / "x.json").string()}}).isError);
    CHECK(guardedCall(paths, settings, {{"path", (testRoot() / "Elsewhere" / "x.json").string()}}).isError);
}

// Detects: an auto-detected game install must be an allowed root.
TEST_CASE("Path policy accepts paths inside an auto-detected Fallout 4 install folder") {
    const fo4ide::settings::SharedSettings settings{fo4ide::settings::Settings{}};
    const std::vector<PathArgument> paths{{"path", PathUse::Read, PathForm::Single}};
    const auto install = testRoot() / "Steam" / "Fallout 4";
    std::vector<fs::path> detected;
    const AutoRootResolver resolver = [&detected] { return detected; };

    CHECK(guardedCall(paths, settings, {{"path", (install / "Data" / "x.esp").string()}}, resolver).isError);
    detected = {install};
    CHECK_FALSE(guardedCall(paths, settings, {{"path", (install / "Data" / "x.esp").string()}}, resolver).isError);
    CHECK(guardedCall(paths, settings, {{"path", (testRoot() / "Elsewhere" / "x.json").string()}}, resolver).isError);
}

// Detects: MO2 mod and overwrite folders must remain usable while empty roots are ignored.
TEST_CASE("Path policy accepts paths inside the MO2 mods and overwrite folders and ignores empty roots") {
    const fo4ide::settings::SharedSettings settings{fo4ide::settings::Settings{}};
    const std::vector<PathArgument> paths{{"path", PathUse::Read, PathForm::Single}};
    const auto mods = testRoot() / "Mods";
    const auto overwrite = testRoot() / "Overwrite";
    const AutoRootResolver profile = [&mods, &overwrite] { return std::vector<fs::path>{mods, overwrite, fs::path{}}; };

    CHECK_FALSE(guardedCall(paths, settings, {{"path", (mods / "Mod" / "x.esp").string()}}, profile).isError);
    CHECK_FALSE(guardedCall(paths, settings, {{"path", (overwrite / "x.esp").string()}}, profile).isError);
    CHECK(guardedCall(paths, settings, {{"path", (testRoot() / "Elsewhere" / "x.json").string()}}, profile).isError);
}

// Detects: a drive root must not grant access to the whole drive.
TEST_CASE("Path policy never treats a filesystem or drive root as an allowed root") {
    const auto drive = fs::current_path().root_path();
    fo4ide::settings::Settings configured;
    configured.dataFolder = drive / "Data";
    const fo4ide::settings::SharedSettings settings{configured};
    const std::vector<PathArgument> paths{{"path", PathUse::Read, PathForm::Single}};

    CHECK_FALSE(guardedCall(paths, settings, {{"path", (drive / "Data" / "x.json").string()}}).isError);
    CHECK(guardedCall(paths, settings, {{"path", (drive / "Other" / "x.json").string()}}).isError);
}

// Detects: a configured root resolving to a drive root must be rejected.
TEST_CASE("Path policy canonicalises a root before the drive-root test") {
    const auto drive = fs::current_path().root_path();
    fo4ide::settings::Settings configured;
    configured.dataFolder = drive / "Data" / "..";
    const fo4ide::settings::SharedSettings settings{configured};
    const std::vector<PathArgument> paths{{"path", PathUse::Read, PathForm::Single}};

    CHECK(guardedCall(paths, settings, {{"path", (drive / "Other" / "x.json").string()}}).isError);
}

// Detects: relative filesystem paths must resolve inside an allowed root.
TEST_CASE("Path policy root-checks relative paths against the working directory") {
    const fo4ide::settings::SharedSettings settings{policySettings()};
    const std::vector<PathArgument> paths{{"path", PathUse::Read, PathForm::Single}};
    const std::vector<fs::path> workingDirectory{fs::current_path()};

    CHECK(guardedCall(paths, settings, {{"path", ".env"}}).isError);
    CHECK(guardedCall(paths, settings, {{"path", "x.json"}}).isError);
    CHECK_FALSE(guardedCall(paths, settings, {{"path", "x.json"}}, [&workingDirectory] { return workingDirectory; }).isError);
}

// Detects: data-relative paths must reject traversal and absolute paths outside allowed roots.
TEST_CASE("Path policy exempts data-relative arguments from the working-directory check only") {
    const fo4ide::settings::SharedSettings settings{policySettings()};
    const std::vector<PathArgument> paths{{"rel", PathUse::DataRelative, PathForm::Single}};

    CHECK_FALSE(guardedCall(paths, settings, {{"rel", "textures/x.dds"}}).isError);
    CHECK(guardedCall(paths, settings, {{"rel", "../x.dds"}}).isError);
    CHECK(guardedCall(paths, settings, {{"rel", "/outside/x.dds"}}).isError);
    CHECK(guardedCall(paths, settings, {{"rel", (testRoot() / "Elsewhere" / "x.dds").string()}}).isError);
}

// Detects: every path in an input list must satisfy the root policy.
TEST_CASE("Path policy checks every entry of a JSON path list") {
    const fo4ide::settings::SharedSettings settings{policySettings()};
    const std::vector<PathArgument> paths{{"sources", PathUse::Read, PathForm::JsonList}};
    const auto inside = (testRoot() / "Fallout 4" / "Data" / "Folder").string();
    const auto outside = (testRoot() / "Elsewhere" / "Folder").string();

    CHECK_FALSE(guardedCall(paths, settings, {{"sources", nlohmann::json::array({inside})}}).isError);
    CHECK(guardedCall(paths, settings, {{"sources", nlohmann::json::array({inside, outside})}}).isError);
    CHECK(guardedCall(paths, settings, {{"sources", nlohmann::json::array({"..\\x"})}}).isError);
    CHECK(guardedCall(paths, settings, {{"sources", nlohmann::json::array({42})}}).isError);
    CHECK(guardedCall(paths, settings, {{"sources", nlohmann::json::array({outside}).dump()}}).isError);
}

// Detects: a path with ';' in it must be judged as the one path the tool opens, not as two paths that each pass, while a declared list is still checked entry by entry.
TEST_CASE("Path policy checks a single path whole and splits only declared lists") {
    const fo4ide::settings::SharedSettings settings{policySettings()};
    const std::vector<fs::path> workingDirectory{fs::current_path()};
    const auto cwdRoot = [&workingDirectory] { return workingDirectory; };
    const auto inside = (testRoot() / "Fallout 4").string();
    const auto outside = (testRoot() / "Elsewhere").string();
    const auto sibling = inside + ";x/secret.txt";
    const auto call = [&](PathForm form, const nlohmann::json& value) {
        return guardedCall({{"path", PathUse::Read, form}}, settings, {{"path", value}}, cwdRoot);
    };

    CHECK(call(PathForm::Single, sibling).isError);
    CHECK(call(PathForm::JsonList, nlohmann::json::array({sibling})).isError);
    CHECK(call(PathForm::LineList, sibling).isError);
    CHECK(call(PathForm::SemicolonList, inside + "\n" + outside).isError);
    CHECK_FALSE(call(PathForm::SemicolonList, inside + ";x").isError);
    CHECK(call(PathForm::SemicolonList, inside + ";" + outside).isError);
    CHECK_FALSE(call(PathForm::LineList, inside + "\r\nx").isError);

    fo4ide::mcp::Registry registry;
    fo4ide::rpc::Dispatcher rpc;
    const std::shared_ptr<spdlog::logger> noLog;
    REQUIRE(registerNativeMcpTools(registry, rpc, noLog, settings, cwdRoot));
    for (const auto* tool : {"compile_papyrus", "papyrus_check", "script_check_text"}) {
        checkRefused(registry, tool, "imports", inside + ";" + outside);
        CHECK_FALSE(registry.call(tool, {{"imports", inside + ";x"}}).text.starts_with("Path refused"));
    }
    checkRefused(registry, "compile_papyrus", "source", sibling);
    checkRefused(registry, "lod_worlds", "data_dir", sibling);
}

// Detects: protected executable and configuration filenames must not be written.
TEST_CASE("Path policy refuses writes to executables and ModOrganizer.ini") {
    const fo4ide::settings::SharedSettings settings{policySettings()};
    const std::vector<PathArgument> paths{{"path", PathUse::Write, PathForm::Single}};

    CHECK(guardedCall(paths, settings, {{"path", (testRoot() / "Fallout 4" / "x.exe").string()}}).isError);
    CHECK(guardedCall(paths, settings, {{"path", (testRoot() / "MO2" / "ModOrganizer.ini").string()}}).isError);
    CHECK_FALSE(guardedCall(paths, settings, {{"path", (testRoot() / "MO2" / "overwrite" / "Plugin.esp").string()}}).isError);
}

// Detects: protected script and installer extensions must not be written.
TEST_CASE("Path policy refuses writes to script, installer and shortcut types") {
    const fo4ide::settings::SharedSettings settings{policySettings()};
    const std::vector<PathArgument> paths{{"path", PathUse::Write, PathForm::Single}};
    const auto overwrite = testRoot() / "MO2" / "overwrite";

    for (const auto* extension : {".py", ".pyw", ".hta", ".vbe", ".jse", ".wsf", ".wsh", ".reg", ".jar", ".psm1", ".cpl", ".url", ".pif", ".inf"}) {
        INFO(extension);
        CHECK(guardedCall(paths, settings, {{"path", (overwrite / (std::string("x") + extension)).string()}}).isError);
    }
    CHECK_FALSE(guardedCall(paths, settings, {{"path", (overwrite / "x.esp").string()}}).isError);
}

// Detects: Windows alternate streams and disguised executable names must be refused.
TEST_CASE("Path policy refuses alternate data streams and trailing-dot executables") {
    const fo4ide::settings::SharedSettings settings{policySettings()};
    const std::vector<PathArgument> paths{{"path", PathUse::Write, PathForm::Single}};
    const auto overwrite = testRoot() / "MO2" / "overwrite";

    CHECK(guardedCall(paths, settings, {{"path", (overwrite / "Plugin.esp:stream").string()}}).isError);
    CHECK(guardedCall(paths, settings, {{"path", (overwrite / "evil.exe.").string()}}).isError);
}

#if !defined(_WIN32)
// Detects: a link to a protected filename must not bypass the write policy.
TEST_CASE("Path policy checks the resolved file name, so a link to ModOrganizer.ini is refused") {
    const auto mo2 = testRoot() / "MO2";
    fs::create_directories(mo2);
    {
        std::ofstream ini(mo2 / "ModOrganizer.ini");
        ini << "selected_profile=Default\n";
    }
    fs::remove(mo2 / "alias.txt");
    fs::create_symlink(mo2 / "ModOrganizer.ini", mo2 / "alias.txt");

    const fo4ide::settings::SharedSettings settings{policySettings()};
    const std::vector<PathArgument> paths{{"path", PathUse::Write, PathForm::Single}};
    CHECK(guardedCall(paths, settings, {{"path", (mo2 / "alias.txt").string()}}).isError);
    CHECK_FALSE(guardedCall(paths, settings, {{"path", (mo2 / "plain.txt").string()}}).isError);

    fs::remove(mo2 / "alias.txt");
    fs::remove(mo2 / "ModOrganizer.ini");
}
#endif

// Detects: unapproved network and device paths must be rejected.
TEST_CASE("Path policy refuses network and device paths before resolving them") {
    const fo4ide::settings::SharedSettings settings{policySettings()};
    const std::vector<PathArgument> paths{{"path", PathUse::Read, PathForm::Single}};

    CHECK(guardedCall(paths, settings, {{"path", "//203.0.113.5/s/x.nif"}}).isError);
#if defined(_WIN32)
    CHECK(guardedCall(paths, settings, {{"path", R"(\\203.0.113.5\s\x.nif)"}}).isError);
    CHECK(guardedCall(paths, settings, {{"path", R"(\\?\UNC\203.0.113.5\s\x.nif)"}}).isError);
    CHECK(guardedCall(paths, settings, {{"path", R"(\\.\pipe\x)"}}).isError);
    CHECK(guardedCall(paths, settings, {{"path", "C:x.nif"}}).isError);
#endif
}

#if defined(_WIN32)
// Detects: an explicitly configured network root must remain usable.
TEST_CASE("Path policy accepts a network path that lexically matches a configured root") {
    fo4ide::settings::Settings configured;
    configured.mo2InstancePath = R"(\\203.0.113.5\s\MO2)";
    const fo4ide::settings::SharedSettings settings{configured};
    const std::vector<PathArgument> paths{{"path", PathUse::Read, PathForm::Single}};

    CHECK_FALSE(guardedCall(paths, settings, {{"path", R"(\\203.0.113.5\s\MO2\overwrite\x.esp)"}}).isError);
    CHECK(guardedCall(paths, settings, {{"path", R"(\\203.0.113.5\s\Other\x.esp)"}}).isError);
}

// Detects: a whole network share must never be an allowed root.
TEST_CASE("Path policy never treats a whole network share as an allowed root") {
    fo4ide::settings::Settings configured;
    configured.mo2InstancePath = R"(\\203.0.113.5\s)";
    const fo4ide::settings::SharedSettings settings{configured};
    const std::vector<PathArgument> paths{{"path", PathUse::Read, PathForm::Single}};

    CHECK(guardedCall(paths, settings, {{"path", R"(\\203.0.113.5\s\x.esp)"}}).isError);
}

// Detects: a trailing separator must not hide a protected file name on a network path, which is never resolved.
TEST_CASE("Path policy checks the protected name of a network write that ends in a separator") {
    fo4ide::settings::Settings configured;
    configured.mo2InstancePath = R"(\\203.0.113.5\s\MO2)";
    const fo4ide::settings::SharedSettings settings{configured};
    const std::vector<PathArgument> paths{{"out", PathUse::Write, PathForm::Single}};

    CHECK_FALSE(guardedCall(paths, settings, {{"out", R"(\\203.0.113.5\s\MO2\out\)"}}).isError);
    CHECK(guardedCall(paths, settings, {{"out", R"(\\203.0.113.5\s\MO2\evil.exe\)"}}).isError);
}
#endif

// Detects: an inline validation contract must not be interpreted as a filename.
TEST_CASE("Path policy skips path checks for an inline validate_plugin contract") {
    const fo4ide::settings::SharedSettings settings{policySettings()};
    const std::vector<PathArgument> paths{{"contract", PathUse::Read, PathForm::SingleOrObject}};

    CHECK_FALSE(guardedCall(paths, settings, {{"contract", R"({"records":[]})"}}).isError);
    CHECK_FALSE(guardedCall(paths, settings, {{"contract", R"(  {"note":"a;..;b"})"}}).isError);
    CHECK(guardedCall(paths, settings, {{"contract", (testRoot() / "Elsewhere" / "contract.json").string()}}).isError);

    const nlohmann::json objectContract{{"contract", nlohmann::json::object({{"records", nlohmann::json::array()}})}};
    CHECK_FALSE(guardedCall(paths, settings, objectContract).isError);
    CHECK(guardedCall(paths, settings, {{"contract", 7}}).isError);
}

// Detects: bare plugin names must be accepted, and rooted names must pass the write checks.
TEST_CASE("Plugin-name policy accepts bare names and checks rooted names with the write rules") {
    const fo4ide::settings::SharedSettings settings{policySettings()};
    const std::vector<PathArgument> paths{{"plugin", PathUse::PluginName, PathForm::Single}};
    const auto overwrite = testRoot() / "MO2" / "overwrite";

    CHECK_FALSE(guardedCall(paths, settings, {{"plugin", "Foo.esp"}}).isError);
    CHECK_FALSE(guardedCall(paths, settings, {{"plugin", (overwrite / "Foo.esp").string()}}).isError);
    CHECK(guardedCall(paths, settings, {{"plugin", (testRoot() / "Elsewhere" / "Foo.esp").string()}}).isError);
    CHECK(guardedCall(paths, settings, {{"plugin", "Foo.esp:stream"}}).isError);
}

// Detects: create_plugin and save_plugin must refuse rooted names outside the roots and network shares.
TEST_CASE("Plugin-name arguments refuse a rooted name outside the allowed roots and a network share") {
    const fo4ide::settings::SharedSettings settings{policySettings()};
    fo4ide::mcp::Registry registry;
    fo4ide::rpc::Dispatcher rpc;
    const std::shared_ptr<spdlog::logger> noLog;
    REQUIRE(registerNativeMcpTools(registry, rpc, noLog, settings, noAutoRoots));

    checkRefused(registry, "create_plugin", "plugin", R"(D:\Elsewhere\Foo.esp)");
    checkRefused(registry, "create_plugin", "plugin", R"(\\203.0.113.5\s\Foo.esp)");
    checkRefused(registry, "save_plugin", "plugin", R"(\\203.0.113.5\s\Foo.esp)");
    checkRefused(registry, "copy_as_override", "source_plugin", R"(\\203.0.113.5\s\Foo.esp)");
    checkRefused(registry, "copy_as_override", "patch_plugin", R"(D:\Elsewhere\Foo.esp)");

    const auto inside = (testRoot() / "MO2" / "overwrite" / "Foo.esp").string();
    CHECK_FALSE(registry.call("create_plugin", {{"plugin", inside}}).text.starts_with("Path refused"));
    CHECK_FALSE(registry.call("save_plugin", {{"plugin", "Foo.esp"}}).text.starts_with("Path refused"));
}

// Detects: every registered plugin-name argument must refuse network paths before they reach the backend.
TEST_CASE("Every registered tool with a plugin-name argument refuses a network plugin name") {
    const fo4ide::settings::SharedSettings settings{policySettings()};
    fo4ide::mcp::Registry registry;
    fo4ide::rpc::Dispatcher rpc;
    const std::shared_ptr<spdlog::logger> noLog;
    REQUIRE(registerNativeMcpTools(registry, rpc, noLog, settings, noAutoRoots));

    std::set<std::string> covered;
    const auto definitions = registry.definitions();
    for (const auto& definition : definitions) {
        const auto& tool = definition.at("name").get_ref<const std::string&>();
        for (const auto& [argument, schema] : definition.at("inputSchema").at("properties").items()) {
            if (!isPluginNameArgument(argument, schema)) continue;
            covered.insert(tool + "." + argument);
            checkRefused(registry, tool, argument, R"(\\203.0.113.5\s\Foo.esp)");
        }
    }
    for (const auto* expected : {"create_plugin.plugin", "save_plugin.plugin", "copy_as_override.source_plugin",
                                 "copy_as_override.patch_plugin", "revert_overrides.bad_plugin", "set_name.plugin"}) {
        INFO(expected);
        CHECK(covered.contains(expected));
    }
}

TEST_CASE("registerCatalogTools registers exactly the expected MCP tool names") {
    const fo4ide::settings::SharedSettings settings{policySettings()};
    fo4ide::mcp::Registry registry;
    fo4ide::rpc::Dispatcher rpc;
    const std::shared_ptr<spdlog::logger> noLog;
    REQUIRE(fo4ide::app::registerCatalogTools(registry, rpc, noLog, settings, noAutoRoots));

    const std::set<std::string> expected{
    "add_element", "add_list_item", "add_scene_action", "add_scene_actor", "add_scene_phase", "archive_compare",
    "archive_extract_all", "archive_extract_file", "archive_extract_selected", "archive_list", "audit_asset_usage", "bake_oc_nif", "batch_patch_records", "change_referencing_records",
    "check_circular_leveled_lists", "check_esl_eligibility", "check_for_errors", "check_quest", "clean_plugin",
    "clean_unused", "clean_unused_masters", "cleanup_cell", "clear_element", "compact_to_esl", "compare_plugin_to",
    "copy_as_disabled_override", "copy_as_new_record", "copy_as_override", "copy_as_override_many", "copy_as_wrapper",
    "copy_element_to_selected", "create_dialogue_branch", "create_dialogue_topic", "create_dialogue_topic_bulk",
    "create_info", "create_merged_patch", "create_scene", "create_seq_file", "deep_copy_as_override",
    "delete_placed_reference", "delete_record", "describe_element", "disable_cell_previs", "duplicate_placed_reference",
    "edit_placed_reference", "edit_quest_script", "find_identical_to_previous_override", "find_unreachable", "fix", "generate_cell_previs", "get_cell_edit_history", "get_cell_land",
    "get_cell_properties", "get_condition_function_params", "get_condition_functions", "get_condition_run_on_types",
    "get_conditions", "get_conditions_at", "get_conflicts", "get_containment_path", "get_dependencies",
    "get_dialogue", "get_geometry_batch", "get_geometry_batch_progress", "get_history", "get_load_order_summary",
    "get_navmesh_info", "get_navmesh_links", "get_navmesh_triangles", "get_navmesh_vertices",
    "get_placed_references", "get_placed_references_at_grid", "get_problems", "get_quest_aliases",
    "get_quest_fragments", "get_quest_objectives", "get_quest_script", "get_quest_stages", "get_record_details",
    "get_record_plugin_matrix", "get_record_type_index", "get_referenced_by", "get_scene", "get_scripts", "get_texture",
    "get_voice_path", "get_world_properties", "graph_compile_to_pex", "graph_compile_to_source", "graph_core_palette",
    "graph_load", "graph_node_signature", "graph_save", "graph_search_palette", "graph_validate",
    "inject_forms_into_master", "load_script", "lod_candidates", "lod_generate", "lod_generate_world", "lod_plan", "lod_preview",
    "lod_verify", "lod_verify_world", "lod_worlds", "loot_cleaning_report", "move_cell_to_grid", "move_element", "move_scene_action",
    "move_scene_phase", "plan_cell_cleanup", "precombine_plan", "previs_resolve_oc_template", "previs_scan", "redo_cell_edit", "remove_element", "remove_element_from_selected",
    "remove_identical_to_master", "remove_identical_to_previous_override", "remove_info", "remove_list_item", "remove_quest_fragment", "remove_scene_action",
    "remove_scene_actor", "remove_scene_phase", "renumber_form_id", "renumber_plugin_form_ids", "reorder",
    "repair_hedr_count", "resolve_asset", "resolve_conflict", "resolve_editorid", "resolve_form_key_labels", "resolve_worldspace", "resolve_worldspace_id",
    "revert_overrides", "scan_broken_refs", "scan_conflicts", "script_analyze", "script_check_text",
    "script_complete", "script_read_script", "script_references", "script_rename", "script_signature_help",
    "script_symbol_at", "script_workspace_symbols", "script_write_script", "search_cells", "search_worldspaces",
    "set_cell_land", "set_cell_properties", "set_components", "set_conditions", "set_conditions_at",
    "set_dialogue_branch_start", "set_info", "set_light", "set_localized_flag", "set_name", "set_next_object_id", "set_record_flags", "set_placed_reference_transform", "set_quest_aliases",
    "set_quest_fragment", "set_quest_objectives", "set_quest_stages", "set_scene", "set_scene_action",
    "set_scene_actor", "set_scene_phase", "set_world_properties", "shape_op", "stamp_cell_previs", "strings_add", "strings_discard",
    "strings_delocalize", "strings_export", "strings_get", "strings_list_tables", "strings_localize", "strings_lookup", "strings_remove", "strings_save",
    "strings_set", "strip_masters_clean", "undo_cell_edit", "update_onam", "write_previs_container",
    "align_references", "browse_assets_categories", "browse_assets_folder", "build_room", "check_overlaps",
    "describe_kit", "get_asset_details", "get_model_bounds", "get_reference_details", "list_base_objects",
    "place_asset", "place_reference", "place_references", "search_assets", "snap_transform",
    };
    std::set<std::string> actual;
    for (const auto& definition : registry.definitions()) actual.insert(definition.at("name").get<std::string>());

    std::vector<std::string> missing;
    std::vector<std::string> unexpected;
    std::set_difference(expected.begin(), expected.end(), actual.begin(), actual.end(), std::back_inserter(missing));
    std::set_difference(actual.begin(), actual.end(), expected.begin(), expected.end(), std::back_inserter(unexpected));
    INFO("missing: " << joined(missing));
    INFO("unexpected: " << joined(unexpected));
    CHECK(missing.empty());
    CHECK(unexpected.empty());
    CHECK(actual.size() == expected.size());
}

TEST_CASE("unsafeScriptFileStem refuses every name that is not one plain file name") {
    using fo4ide::core::unsafeScriptFileStem;
    for (const std::string& name : {std::string("..\\Escape"), std::string("../Escape"), std::string("Ns:Script"),
                                   std::string("A.B"), std::string("Has Space"), std::string("Tab\t"),
                                   std::string("Nul\0X", 5), std::string("Caf\xC3\xA9"), std::string(""),
                                   std::string("NUL"), std::string("con"), std::string("Com1"), std::string("LPT9")}) {
        INFO(name);
        CHECK(unsafeScriptFileStem(name).has_value());
    }
    for (const char* name : {"Fixture", "My_Script2", "_x", "Console", "Com0", "Com10"}) {
        INFO(name);
        CHECK_FALSE(unsafeScriptFileStem(name).has_value());
    }
}

namespace {

class OkFalseBackend final : public fo4ide::rpc::Target {
public:
    fo4ide::Result<nlohmann::json> invoke(std::string_view, const nlohmann::json&) override {
        return nlohmann::json{{"ok", false}, {"limitations", nlohmann::json::array({"no cdx row"})}};
    }
};

}

TEST_CASE("Regression: only generate_cell_previs reports an ok:false reply as an MCP error") {
    const fo4ide::settings::SharedSettings settings{policySettings()};
    fo4ide::rpc::Dispatcher rpc;
    REQUIRE(rpc.registerTarget("backend", std::make_shared<OkFalseBackend>()).has_value());
    fo4ide::mcp::Registry registry;
    const std::shared_ptr<spdlog::logger> noLog;
    REQUIRE(fo4ide::app::registerCatalogTools(registry, rpc, noLog, settings, noAutoRoots));

    const auto generated = registry.call("generate_cell_previs", {{"patch_plugin", "Patch.esp"}, {"cell", "Cell"}});
    CHECK(generated.isError);
    CHECK(generated.text.find("no cdx row") != std::string::npos);
    const auto planned = registry.call("precombine_plan", {{"cell", "Cell"}, {"plugin", "Patch.esp"}});
    CHECK_FALSE(planned.isError);
    CHECK(planned.text.find("no cdx row") != std::string::npos);
}

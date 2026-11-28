#include <fo4/f4se/plugin_emitter.hpp>

#include <fo4/f4se/cpp_emitter.hpp>
#include <fo4/f4se/region_merge.hpp>

namespace fo4::f4se {

std::string PluginEmitter::emitMain(const PluginBinding& plugin) const {
    return plugin.target == F4SETarget::Ng_0_7_8 ? emitNgMain(plugin) : emitOgMain(plugin);
}

std::string PluginEmitter::emitOgMain(const PluginBinding& plugin) const {
    std::string text;
    appendCommonTop(text, plugin);

    text += "extern \"C\"\n{\n";
    text += "\tbool F4SEPlugin_Query(const F4SEInterface * f4se, PluginInfo * info)\n\t{\n";
    text += "\t\tinfo->infoVersion = PluginInfo::kInfoVersion;\n";
    text += "\t\tinfo->name = \"" + plugin.name + "\";\n";
    text += "\t\tinfo->version = " + versionLiteral(plugin) + ";\n\n";
    text += "\t\tg_pluginHandle = f4se->GetPluginHandle();\n\n";
    text += "\t\tif (f4se->isEditor)\n\t\t{\n";
    text += "\t\t\t_MESSAGE(\"loaded in editor, marking as incompatible\");\n";
    text += "\t\t\treturn false;\n\t\t}\n\n";
    text += "\t\tif (f4se->runtimeVersion != " + std::string(kOgRuntimeVersion) +
            ")\n\t\t{\n";
    text += "\t\t\t_MESSAGE(\"unsupported runtime version %08X\", f4se->runtimeVersion);\n";
    text += "\t\t\treturn false;\n\t\t}\n\n";
    text += "\t\treturn true;\n\t}\n\n";

    appendLoad(text, plugin);
    text += "}\n";
    return text;
}

std::string PluginEmitter::emitNgMain(const PluginBinding& plugin) const {
    std::string text;
    appendCommonTop(text, plugin);

    text += "extern \"C\"\n{\n";
    text += "\t__declspec(dllexport) F4SEPluginVersionData F4SEPlugin_Version =\n\t{\n";
    text += "\t\tF4SEPluginVersionData::kVersion,\n";
    text += "\t\t" + versionLiteral(plugin) + ",\n";
    text += "\t\t\"" + plugin.name + "\",\n";
    text += "\t\t\"" + plugin.author + "\",\n";
    text += "\t\t\"\",\n";
    text += "\t\tF4SEPluginVersionData::kAddressIndependence_Signatures,\n";
    text += "\t\tF4SEPluginVersionData::kStructureIndependence_NoStructs,\n";
    text += "\t\t{ 0 },\n";
    text += "\t\t0,\n";
    text += "\t\t0, 0,\n";
    text += "\t\t{ 0 }\n";
    text += "\t};\n\n";

    appendLoad(text, plugin);
    text += "}\n";
    return text;
}

void PluginEmitter::appendCommonTop(std::string& text, const PluginBinding& plugin) {
    text += "#include \"f4se/PluginAPI.h\"\n";
    text += "#include \"f4se_common/f4se_version.h\"\n";
    text += "#include \"f4se/PapyrusVM.h\"\n\n";
    text += "#include \"" + plugin.name + "Registrations.h\"\n\n";
    text += "static PluginHandle g_pluginHandle = kPluginHandle_Invalid;\n";
    text += "static F4SEPapyrusInterface * g_papyrus = nullptr;\n";
    if (!plugin.messages.empty()) {
        text += "static F4SEMessagingInterface * g_messaging = nullptr;\n";
    }
    text += '\n';

    if (plugin.messages.empty()) return;

    for (const auto& subscription : plugin.messages) {
        text += "void " + subscription.handlerName +
                "(F4SEMessagingInterface::Message * message);\n";
    }
    text += '\n';

    text += "static void OnF4SEMessage(F4SEMessagingInterface::Message * message)\n{\n";
    text += "\tswitch (message->type)\n\t{\n";
    for (const auto& subscription : plugin.messages) {
        text += "\tcase F4SEMessagingInterface::" + subscription.messageName + ":\n";
        text += "\t\t" + subscription.handlerName + "(message);\n";
        text += "\t\tbreak;\n";
    }
    text += "\tdefault:\n\t\tbreak;\n\t}\n}\n\n";
}

void PluginEmitter::appendLoad(std::string& text, const PluginBinding& plugin) {
    text += "\tbool F4SEPlugin_Load(const F4SEInterface * f4se)\n\t{\n";
    text += "\t\tg_pluginHandle = f4se->GetPluginHandle();\n\n";
    text += "\t\tg_papyrus = (F4SEPapyrusInterface *)f4se->QueryInterface(kInterface_Papyrus);\n";
    text += "\t\tif (!g_papyrus || !g_papyrus->Register(RegisterAllFuncs))\n\t\t{\n";
    text += "\t\t\t_MESSAGE(\"could not register Papyrus functions\");\n";
    text += "\t\t\treturn false;\n\t\t}\n";

    if (!plugin.messages.empty()) {
        text += '\n';
        text += "\t\tg_messaging = (F4SEMessagingInterface *)f4se->QueryInterface(kInterface_Messaging);\n";
        text += "\t\tif (g_messaging)\n";
        text += "\t\t\tg_messaging->RegisterListener(g_pluginHandle, \"F4SE\", OnF4SEMessage);\n";
    }

    text += "\n\t\t";
    text += RegionMerge::begin("F4SEPlugin_Load");
    text += "\n\t\t";
    text += RegionMerge::end("F4SEPlugin_Load");
    text += "\n\n\t\treturn true;\n\t}\n";
}

std::string PluginEmitter::versionLiteral(const PluginBinding& plugin) {
    return "MAKE_EXE_VERSION(" + std::to_string(plugin.versionMajor) + ", " +
           std::to_string(plugin.versionMinor) + ", " + std::to_string(plugin.versionPatch) + ")";
}

std::string PluginEmitter::emitCMakeLists(const PluginBinding& plugin) const {
    std::string text;
    text += "cmake_minimum_required(VERSION 3.21)\n\n";
    text += "project(" + plugin.name + " VERSION " + std::to_string(plugin.versionMajor) + "." +
            std::to_string(plugin.versionMinor) + "." + std::to_string(plugin.versionPatch) +
            " LANGUAGES CXX)\n\n";
    text += "set(CMAKE_CXX_STANDARD 17)\n";
    text += "set(CMAKE_CXX_STANDARD_REQUIRED ON)\n";
    text += "set(CMAKE_MSVC_RUNTIME_LIBRARY \"MultiThreaded$<$<CONFIG:Debug>:Debug>\")\n\n";
    text += "find_package(common REQUIRED CONFIG)\n";
    text += "find_package(f4se REQUIRED CONFIG)\n\n";
    text += "add_library(" + plugin.name + " SHARED\n";
    text += "\tsrc/main.cpp\n";
    for (const auto& module : plugin.modules) {
        text += "\tsrc/" + CppEmitter::sourceFileName(plugin, module) + "\n";
    }
    text += "\tsrc/" + plugin.name + "Registrations.cpp\n";
    text += ")\n\n";
    text += "target_include_directories(" + plugin.name + " PRIVATE src)\n";
    text += "target_link_libraries(" + plugin.name + " PRIVATE common::common f4se::f4se)\n";
    return text;
}

std::string PluginEmitter::emitReadme(const PluginBinding& plugin) const {
    const auto target = plugin.target == F4SETarget::Ng_0_7_8
                            ? std::string(
                                  "F4SE 0.7.x and later. The plugin exports F4SEPluginVersionData.")
                            : std::string(
                                  "runtime 1.10.163. The plugin exports F4SEPlugin_Query.");

    std::string text;
    text += "# " + plugin.name + "\n\n";
    text += "Generated by FO4IDE from a binding graph. Regenerating overwrites every file\n";
    text += "here except the text between the body markers, which is preserved.\n\n";
    text += "Target: " + target + "\n\n";
    text += "## Building\n\n";
    text += "F4SE builds as two installed packages. From a directory beside this one:\n\n";
    text += "```\n";
    text += "git clone https://github.com/ianpatt/common\n";
    text += "git clone https://github.com/ianpatt/f4se\n";
    text += "cmake -B common/build -S common -DCMAKE_INSTALL_PREFIX=extern\n";
    text += "cmake --build common/build --config Release --target install\n";
    text += "cmake -B f4se/build -S f4se -DCMAKE_INSTALL_PREFIX=extern -DCMAKE_PREFIX_PATH=extern\n";
    text += "cmake --build f4se/build --config Release --target install\n";
    text += "```\n\n";
    text += "Then build this plugin against that prefix:\n\n";
    text += "```\n";
    text += "cmake -B build -S . -DCMAKE_PREFIX_PATH=extern\n";
    text += "cmake --build build --config Release\n";
    text += "```\n\n";
    text += "## Function bodies\n\n";
    text += "Every native is emitted as a stub guarded by `" +
            std::string(CppEmitter::kUnimplementedMacro) +
            "`, which\n";
    text += "fails the build until an implementation is written. Write it between the\n";
    text += "`#pragma region FO4IDE_BODY` and `#pragma endregion FO4IDE_BODY` markers; that text survives regeneration. To build\n";
    text += "with stubs still in place, define `" + std::string(CppEmitter::kAllowStubsMacro) +
            "`.\n";
    return text;
}

}

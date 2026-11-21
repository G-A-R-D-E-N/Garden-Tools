#!/usr/bin/env python3
"""Fail when a new C API header or Radicle.Interop source file appears outside the frozen allow-list.

Requires: run from any directory; the repository root is found from this file. Exit codes: 0 clear, 1 a new file.
Policy: Radicle/docs/FLAT_WRAPPERS_FREEZE.md.
"""

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HEADER_DIR = ROOT / "Radicle" / "include" / "fo4"
CSHARP_DIR = ROOT / "Radicle" / "bindings" / "csharp" / "Radicle.Interop"

HEADERS = frozenset((
    "c_api.h", "c_api_actor_state.h", "c_api_authoring.h", "c_api_behavior.h",
    "c_api_cell.h", "c_api_cell_edit.h", "c_api_collision_names.h", "c_api_collision_plain.h",
    "c_api_components.h", "c_api_conditions.h", "c_api_conflict.h", "c_api_dds.h", "c_api_destructible.h", "c_api_dialogue.h", "c_api_duplicate_formid.h",
    "c_api_env.h", "c_api_f4se.h", "c_api_faction.h", "c_api_gameplay.h", "c_api_graph.h", "c_api_hkx.h",
    "c_api_hkx_behavior.h", "c_api_hkx_plain.h", "c_api_itpo.h", "c_api_localization.h", "c_api_object_template.h", "c_api_placed_create.h", "c_api_plugin_ops.h", "c_api_previs.h", "c_api_record_json.h", "c_api_record_json_import.h", "c_api_search.h", "c_api_loadorder.h", "c_api_lod.h", "c_api_maintenance.h",
    "c_api_loose_plugins.h", "c_api_material_file.h", "c_api_materials.h", "c_api_navmesh.h", "c_api_nif.h", "c_api_nif_anim.h",
    "c_api_nif_mesh.h", "c_api_nif_skin_tools.h", "c_api_npc_visual.h", "c_api_papyrus.h", "c_api_papyrus_analysis.h",
    "c_api_papyrus_compiler.h", "c_api_papyrus_lifecycle.h", "c_api_papyrus_quest.h",
    "c_api_papyrus_runtime.h", "c_api_papyrus_save.h", "c_api_papyrus_source.h", "c_api_patch.h", "c_api_perk.h",
    "c_api_pet_write.h", "c_api_pex.h", "c_api_pex_analysis.h", "c_api_previs.h", "c_api_quest.h", "c_api_records.h",
    "c_api_runtime.h", "c_api_runtime_art.h", "c_api_save.h", "c_api_script_set.h", "c_api_seq.h",
    "c_api_sound.h", "c_api_spawn.h", "c_api_swf.h", "c_api_swf_as3.h", "c_api_swf_verbs.h",
    "c_api_terrain.h", "c_api_ui_records.h", "c_api_vegetation.h", "c_api_vmad.h",
))

CSHARP = frozenset((
    "ActorStateCodec.cs", "ActorValueBase.cs", "Archives.cs", "Behavior.cs", "CellEdits.cs", "CellLookup.cs", "Conditions.cs",
    "Core.cs", "DdsImage.cs", "Destructible.cs", "Dialogue.cs", "EnvMaintenance.cs", "EnvQuestBulk.cs", "FactionReaction.cs", "GameplayRules.cs",
    "HkxBehaviorGraph.cs", "HkxFile.cs", "HkxPlain.cs", "InteractionRecords.cs", "LoadOrder.cs", "Localization.cs", "LodSettings.cs",
    "MaterialFile.cs", "MaterialSwaps.cs", "Messages.cs", "Navmesh.cs", "NavmeshWalk.cs", "NifAnim.cs",
    "NifCollisionOwners.cs", "NifFile.cs", "NifHeader.cs", "NifMesh.cs", "Vocabulary.cs", "NpcGameplay.cs", "NpcVisual.cs", "PackageRules.cs",
    "PapyrusCompiler.cs", "PapyrusQuestLifecycle.cs", "PapyrusRuntime.cs", "PapyrusSave.cs",
    "PerkEffects.cs", "PexCode.cs", "PexFile.cs",
    "PlacedReferences.cs", "PlainCollision.cs", "PluginFile.Authoring.cs",
    "PluginFile.Fragments.cs", "PluginFile.SceneAuthoring.cs", "PluginFile.SceneCamera.cs", "PluginFile.Masters.cs", "PluginFile.PetWrite.cs", "PluginFile.QuestWrites.cs",
    "PluginFile.VmadScripts.cs", "PluginFile.cs", "QuestRuntime.cs",
    "QuestStructure.cs", "RaceSubgraphs.cs", "RadicleEnvironment.cs", "RadicleNative.Init.cs",
    "RadicleNative.g.cs", "RecordFieldAccess.cs", "RecordFields.cs", "RecordReads.cs", "Runtime.cs", "RuntimeNative.cs",
    "ScriptSelection.cs", "ScriptSet.cs", "SkyParameters.cs",
    "SoundRecords.cs", "Spawn.cs", "SwfArt.cs", "SwfEditText.cs", "SwfEditing.cs", "SwfFile.cs",
    "SwfLayout.cs", "SwfMorphs.cs", "SwfPlacementMatrix.cs", "SwfPlacements.cs",
    "UiRecords.cs", "Vegetation.cs", "VmadBinder.cs",
))


def unexpected(found, allowed):
    return sorted(set(found) - set(allowed))


def listed(directory, pattern, skip=()):
    return [path.relative_to(directory).as_posix() for path in directory.rglob(pattern)
            if not any(part in skip for part in path.relative_to(directory).parts)]


def main():
    found = [
        ("Radicle/include/fo4/", unexpected(listed(HEADER_DIR, "c_api*.h"), HEADERS)),
        ("Radicle/bindings/csharp/Radicle.Interop/", unexpected(listed(CSHARP_DIR, "*.cs", ("obj", "bin")), CSHARP)),
    ]
    for prefix, names in found:
        for name in names:
            print("::error::{}{} is a new file outside the flat-wrapper allow-list; use the generic record field API "
                  "(FLAT_WRAPPERS_FREEZE.md under Radicle/docs in the private repo), or add it to the allow-list in this script if it is not a per-record shim.".format(prefix, name))
    return 1 if any(names for _, names in found) else 0


if __name__ == "__main__":
    sys.exit(main())

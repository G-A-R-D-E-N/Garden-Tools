#pragma once

#include <array>
#include <cstddef>
#include <string_view>

namespace fo4ide::http {

inline bool isPluginMutationTarget(std::string_view target) {
    return target == "backend" || target == "masters" || target == "cell" || target == "strings" ||
           target == "assets" || target == "layout";
}

inline std::size_t pluginArgIndex(std::string_view method) {
    if (method == "CopyAsNewRecord" || method == "DeepCopyAsOverride" ||
        method == "ChangeReferencingRecords" || method == "ResolveConflict" ||
        method == "CopyAsOverride" || method == "CopyAsDisabledOverride" ||
        method == "CopyAsWrapper") return 2;
    if (method == "SetPlacedReferenceTransform" || method == "EditPlacedReference" ||
        method == "DuplicatePlacedReference" || method == "DeletePlacedReference" ||
        method == "SetCellProperties" || method == "ApplyCellCleanup" ||
        method == "CreateMergedPatch" || method == "CopyAsOverrideMany" ||
        method == "RevertOverrides") return 1;
    return 0;
}

inline bool isPluginMutatingMethod(std::string_view method) {
    static constexpr std::array exactMethods{
        std::string_view{"PlaceReference"},
        std::string_view{"PlaceReferences"},
        std::string_view{"CreateInteriorCell"},
        std::string_view{"CreatePlacedObject"},
        std::string_view{"DeletePlacedReference"},
        std::string_view{"DuplicatePlacedReference"},
        std::string_view{"EditPlacedReference"},
        std::string_view{"SetPlacedReferenceTransform"},
        std::string_view{"SetCellProperties"},
        std::string_view{"UndoCellEdit"},
        std::string_view{"RedoCellEdit"},
        std::string_view{"CleanupCell"},
        std::string_view{"PlaceAsset"},
        std::string_view{"BuildRoom"},
        std::string_view{"AlignReferences"},
        std::string_view{"EditPlacedReferences"},
        std::string_view{"ChangeReferencingRecords"},
        std::string_view{"CleanPlugin"},
        std::string_view{"CleanUnused"},
        std::string_view{"CleanUnusedMasters"},
        std::string_view{"InsertFormLinkListElement"},
        std::string_view{"RepairHedrCount"},
        std::string_view{"ResolveConflict"},
        std::string_view{"CreatePlugin"},
        std::string_view{"SavePlugin"},
        std::string_view{"SavePatch"}
    };
    static constexpr std::array prefixes{
        std::string_view{"Set"},
        std::string_view{"Create"},
        std::string_view{"Delete"},
        std::string_view{"Add"},
        std::string_view{"Remove"},
        std::string_view{"Edit"},
        std::string_view{"Copy"},
        std::string_view{"DeepCopy"},
        std::string_view{"Move"},
        std::string_view{"Clear"},
        std::string_view{"Update"},
        std::string_view{"Apply"},
        std::string_view{"Save"},
        std::string_view{"Undo"},
        std::string_view{"Redo"},
        std::string_view{"Place"},
        std::string_view{"Duplicate"},
        std::string_view{"Build"},
        std::string_view{"Align"},
        std::string_view{"Cleanup"},
        std::string_view{"Compact"},
        std::string_view{"Renumber"},
        std::string_view{"Inject"},
        std::string_view{"Discard"},
        std::string_view{"Rename"},
        std::string_view{"Revert"}
    };
    for (const auto name : exactMethods) {
        if (method == name) return true;
    }
    for (const auto prefix : prefixes) {
        if (method.starts_with(prefix)) return true;
    }
    return false;
}

}

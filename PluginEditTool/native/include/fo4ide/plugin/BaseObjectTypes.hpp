#pragma once

#include <array>
#include <optional>
#include <string_view>

namespace fo4ide::plugin {

struct PlaceableBaseType {
    std::string_view signature;
    std::string_view name;
};

// Must stay a subset of the xEdit sigBaseObjects list; CMPO and LVLN are excluded.
inline constexpr std::array kPlaceableBaseTypes{
    PlaceableBaseType{"ACTI", "Activator"},
    PlaceableBaseType{"ADDN", "Addon Node"},
    PlaceableBaseType{"ALCH", "Potion"},
    PlaceableBaseType{"AMMO", "Ammo"},
    PlaceableBaseType{"ARMO", "Armor"},
    PlaceableBaseType{"BOOK", "Book"},
    PlaceableBaseType{"CONT", "Container"},
    PlaceableBaseType{"DOOR", "Door"},
    PlaceableBaseType{"FLOR", "Flora"},
    PlaceableBaseType{"FURN", "Furniture"},
    PlaceableBaseType{"HAZD", "Hazard"},
    PlaceableBaseType{"IDLM", "Idle Marker"},
    PlaceableBaseType{"INGR", "Ingredient"},
    PlaceableBaseType{"KEYM", "Key"},
    PlaceableBaseType{"LIGH", "Light"},
    PlaceableBaseType{"LVLI", "Leveled Item"},
    PlaceableBaseType{"MISC", "Misc Item"},
    PlaceableBaseType{"MSTT", "Movable Static"},
    PlaceableBaseType{"NOTE", "Note"},
    PlaceableBaseType{"NPC_", "NPC"},
    PlaceableBaseType{"PROJ", "Projectile"},
    PlaceableBaseType{"SCOL", "Static Collection"},
    PlaceableBaseType{"SCRL", "Scroll"},
    PlaceableBaseType{"STAT", "Static"},
    PlaceableBaseType{"TACT", "Talking Activator"},
    PlaceableBaseType{"TERM", "Terminal"},
    PlaceableBaseType{"TREE", "Tree"},
    PlaceableBaseType{"WEAP", "Weapon"},
};

inline std::optional<std::string_view> placeableBaseTypeName(std::string_view signature) {
    for (const auto& type : kPlaceableBaseTypes) {
        if (type.signature == signature) return type.name;
    }
    return std::nullopt;
}

}  // namespace fo4ide::plugin

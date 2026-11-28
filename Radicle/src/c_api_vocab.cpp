#include "c_api_internal.hpp"
#include "fo4/c_api_env.h"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace {

using namespace radicle_c_detail;

constexpr CopyMessages k_copy_messages{"out_bytes and out_size are required", "failed to allocate a vocabulary name"};

struct RecordClassRow {
  std::uint32_t signature;
  const char* name;
};

struct ValueRow {
  std::uint32_t value;
  const char* name;
};

struct TraitRow {
  std::uint32_t signature;
  std::uint32_t traits;
};

// Invariant: a signature is its four ASCII bytes packed little-endian, the same key a host computes.
constexpr std::uint32_t pack(const char (&s)[5]) {
  return static_cast<std::uint32_t>(static_cast<unsigned char>(s[0])) |
         static_cast<std::uint32_t>(static_cast<unsigned char>(s[1])) << 8 |
         static_cast<std::uint32_t>(static_cast<unsigned char>(s[2])) << 16 |
         static_cast<std::uint32_t>(static_cast<unsigned char>(s[3])) << 24;
}

// Constraint: every string here is the identifier Garden-Engine opencw RecordVocabulary.cs writes today, not an xEdit label; marker type 65 is the engine's one merged identifier and 66 to 99 stay absent, so scene metadata text does not change.
constexpr RecordClassRow k_record_classes[] = {
    {pack("AACT"), "ActionRecord"},
    {pack("ACHR"), "PlacedNpc"},
    {pack("ACTI"), "Activator"},
    {pack("ADDN"), "AddonNode"},
    {pack("AECH"), "AudioEffectChain"},
    {pack("ALCH"), "Ingestible"},
    {pack("AMDL"), "AimModel"},
    {pack("AMMO"), "Ammunition"},
    {pack("ANIO"), "AnimatedObject"},
    {pack("AORU"), "AttractionRule"},
    {pack("ARMA"), "ArmorAddon"},
    {pack("ARMO"), "Armor"},
    {pack("ARTO"), "ArtObject"},
    {pack("ASPC"), "AcousticSpace"},
    {pack("ASTP"), "AssociationType"},
    {pack("AVIF"), "ActorValueInformation"},
    {pack("BNDS"), "BendableSpline"},
    {pack("BOOK"), "Book"},
    {pack("BPTD"), "BodyPartData"},
    {pack("CAMS"), "CameraShot"},
    {pack("CELL"), "Cell"},
    {pack("CLAS"), "Class"},
    {pack("CLFM"), "ColorRecord"},
    {pack("CLMT"), "Climate"},
    {pack("CMPO"), "Component"},
    {pack("COBJ"), "ConstructibleObject"},
    {pack("COLL"), "CollisionLayer"},
    {pack("CONT"), "Container"},
    {pack("CPTH"), "CameraPath"},
    {pack("CSTY"), "CombatStyle"},
    {pack("DEBR"), "Debris"},
    {pack("DFOB"), "DefaultObject"},
    {pack("DIAL"), "DialogTopic"},
    {pack("DLBR"), "DialogBranch"},
    {pack("DLVW"), "DialogView"},
    {pack("DOBJ"), "DefaultObjectManager"},
    {pack("DOOR"), "Door"},
    {pack("DUAL"), "DualCastData"},
    {pack("ECZN"), "EncounterZone"},
    {pack("EFSH"), "EffectShader"},
    {pack("ENCH"), "ObjectEffect"},
    {pack("EQUP"), "EquipType"},
    {pack("EXPL"), "Explosion"},
    {pack("FACT"), "Faction"},
    {pack("FLOR"), "Flora"},
    {pack("FLST"), "FormList"},
    {pack("FSTP"), "Footstep"},
    {pack("FSTS"), "FootstepSet"},
    {pack("FURN"), "Furniture"},
    {pack("GDRY"), "GodRays"},
    {pack("GRAS"), "Grass"},
    {pack("HAZD"), "Hazard"},
    {pack("HDPT"), "HeadPart"},
    {pack("IDLE"), "IdleAnimation"},
    {pack("IDLM"), "IdleMarker"},
    {pack("IMAD"), "ImageSpaceAdapter"},
    {pack("IMGS"), "ImageSpace"},
    {pack("INFO"), "DialogResponses"},
    {pack("INGR"), "Ingredient"},
    {pack("INNR"), "InstanceNamingRules"},
    {pack("IPCT"), "Impact"},
    {pack("IPDS"), "ImpactDataSet"},
    {pack("KEYM"), "Key"},
    {pack("KSSM"), "SoundKeywordMapping"},
    {pack("KYWD"), "Keyword"},
    {pack("LAND"), "Landscape"},
    {pack("LAYR"), "Layer"},
    {pack("LCRT"), "LocationReferenceType"},
    {pack("LCTN"), "Location"},
    {pack("LENS"), "LensFlare"},
    {pack("LGTM"), "LightingTemplate"},
    {pack("LIGH"), "Light"},
    {pack("LSCR"), "LoadScreen"},
    {pack("LTEX"), "LandscapeTexture"},
    {pack("LVLI"), "LeveledItem"},
    {pack("LVLN"), "LeveledNpc"},
    {pack("MATO"), "MaterialObject"},
    {pack("MATT"), "MaterialType"},
    {pack("MESG"), "Message"},
    {pack("MGEF"), "MagicEffect"},
    {pack("MISC"), "MiscItem"},
    {pack("MOVT"), "MovementType"},
    {pack("MSTT"), "MovableStatic"},
    {pack("MSWP"), "MaterialSwap"},
    {pack("MUSC"), "MusicType"},
    {pack("MUST"), "MusicTrack"},
    {pack("NAVI"), "NavigationMeshInfoMap"},
    {pack("NAVM"), "NavigationMesh"},
    {pack("NOCM"), "NavigationMeshObstacleManager"},
    {pack("NOTE"), "Holotape"},
    {pack("NPC_"), "Npc"},
    {pack("OTFT"), "Outfit"},
    {pack("OVIS"), "ObjectVisibilityManager"},
    {pack("PACK"), "Package"},
    {pack("PARW"), "PlacedArrow"},
    {pack("PBAR"), "PlacedBarrier"},
    {pack("PBEA"), "PlacedBeam"},
    {pack("PCON"), "PlacedCone"},
    {pack("PERK"), "Perk"},
    {pack("PFLA"), "PlacedFlame"},
    {pack("PGRE"), "PlacedTrap"},
    {pack("PHZD"), "PlacedHazard"},
    {pack("PKIN"), "PackIn"},
    {pack("PMIS"), "PlacedMissile"},
    {pack("PROJ"), "Projectile"},
    {pack("QUST"), "Quest"},
    {pack("RACE"), "Race"},
    {pack("REFR"), "PlacedObject"},
    {pack("REGN"), "Region"},
    {pack("RELA"), "Relationship"},
    {pack("REVB"), "ReverbParameters"},
    {pack("RFCT"), "VisualEffect"},
    {pack("RFGP"), "ReferenceGroup"},
    {pack("SCCO"), "SceneCollection"},
    {pack("SCEN"), "Scene"},
    {pack("SCOL"), "StaticCollection"},
    {pack("SCSN"), "AudioCategorySnapshot"},
    {pack("SMBN"), "StoryManagerBranchNode"},
    {pack("SMEN"), "StoryManagerEventNode"},
    {pack("SMQN"), "StoryManagerQuestNode"},
    {pack("SNCT"), "SoundCategory"},
    {pack("SNDR"), "SoundDescriptor"},
    {pack("SOPM"), "SoundOutputModel"},
    {pack("SOUN"), "SoundMarker"},
    {pack("SPEL"), "Spell"},
    {pack("SPGD"), "ShaderParticleGeometry"},
    {pack("STAG"), "AnimationSoundTagSet"},
    {pack("STAT"), "Static"},
    {pack("TACT"), "TalkingActivator"},
    {pack("TERM"), "Terminal"},
    {pack("TREE"), "Tree"},
    {pack("TRNS"), "Transform"},
    {pack("TXST"), "TextureSet"},
    {pack("VTYP"), "VoiceType"},
    {pack("WATR"), "Water"},
    {pack("WEAP"), "Weapon"},
    {pack("WRLD"), "Worldspace"},
    {pack("WTHR"), "Weather"},
    {pack("ZOOM"), "Zoom"},
};

constexpr const char* k_levels[] = {
    "Easy",
    "Medium",
    "Hard",
    "VeryHard",
};

constexpr const char* k_primitive_types[] = {
    "None",
    "Box",
    "Sphere",
    "Plane",
    "Line",
    "Ellipsoid",
};

constexpr const char* k_marker_types[] = {
    "Cave",
    "City",
    "DiamondCity",
    "Encampment",
    "FactoryOrIndustrialSite",
    "GovtBuildingOrMonument",
    "MetroStation",
    "MilitaryBase",
    "NaturalLandmark",
    "OfficeOrCivicBuilding",
    "RuinsTown",
    "RuinsUrban",
    "Sanctuary",
    "Settlement",
    "SewerOrUtilityTunnels",
    "Vault",
    "Airfield",
    "BunkerHill",
    "Camper",
    "Car",
    "Church",
    "CountryClub",
    "CustomHouse",
    "DriveIn",
    "ElevatedHighway",
    "FaneuilHall",
    "Farm",
    "FillingStation",
    "Forested",
    "Goodneighbor",
    "Graveyard",
    "Hospital",
    "IndustrialDome",
    "IndustrialStacks",
    "Institute",
    "IrishPride",
    "Junkyard",
    "Observatory",
    "Pier",
    "PondOrLake",
    "Quarry",
    "RadioactiveArea",
    "RadioTower",
    "Salem",
    "School",
    "Shipwreck",
    "Submarine",
    "SwanPond",
    "SynthHead",
    "Town",
    "BrotherhoodofSteel",
    "BrownstoneTownhouse",
    "Bunker",
    "Castle",
    "Skyscraper",
    "Libertalia",
    "LowRiseBuilding",
    "Minutemen",
    "PoliceStation",
    "Prydwen",
    "RailroadFaction",
    "Railroad",
    "Satellite",
    "Sentinel",
    "USSConstitution",
    "MechanistLairRaidersettlementVassalsettlementPotentialVassalsettlement",
};

constexpr ValueRow k_script_property_types[] = {
    {1, "Object"},
    {2, "String"},
    {3, "Int"},
    {4, "Float"},
    {5, "Bool"},
    {6, "Variable"},
    {7, "Struct"},
    {11, "ArrayOfObject"},
    {12, "ArrayOfString"},
    {13, "ArrayOfInt"},
    {14, "ArrayOfFloat"},
    {15, "ArrayOfBool"},
    {16, "ArrayOfVariable"},
    {17, "ArrayOfStruct"},
};

constexpr TraitRow k_traits[] = {
    {pack("AACT"), 1},
    {pack("ACTI"), 1},
    {pack("ALCH"), 1},
    {pack("AMMO"), 1},
    {pack("ARMO"), 1},
    {pack("AVIF"), 1},
    {pack("BOOK"), 1},
    {pack("CELL"), 1},
    {pack("CLAS"), 1},
    {pack("CLFM"), 1},
    {pack("CMPO"), 1},
    {pack("CONT"), 1},
    {pack("DIAL"), 1},
    {pack("DOOR"), 1},
    {pack("ENCH"), 1},
    {pack("EXPL"), 1},
    {pack("FACT"), 1},
    {pack("FLST"), 1},
    {pack("FURN"), 1},
    {pack("HAZD"), 1},
    {pack("HDPT"), 1},
    {pack("INGR"), 1},
    {pack("INNR"), 1},
    {pack("KYWD"), 1},
    {pack("LCTN"), 1},
    {pack("LIGH"), 1},
    {pack("MATT"), 1},
    {pack("MESG"), 1},
    {pack("MGEF"), 1},
    {pack("MISC"), 1},
    {pack("MOVT"), 1},
    {pack("MSTT"), 1},
    {pack("NOTE"), 1},
    {pack("NPC_"), 1},
    {pack("OMOD"), 1},
    {pack("PARW"), 2},
    {pack("PBAR"), 2},
    {pack("PBEA"), 2},
    {pack("PCON"), 2},
    {pack("PERK"), 1},
    {pack("PFLA"), 2},
    {pack("PGRE"), 2},
    {pack("PHZD"), 2},
    {pack("PMIS"), 2},
    {pack("PROJ"), 1},
    {pack("QUST"), 1},
    {pack("RACE"), 1},
    {pack("RFGP"), 1},
    {pack("SCOL"), 1},
    {pack("SNCT"), 1},
    {pack("SPEL"), 1},
    {pack("STAT"), 1},
    {pack("TACT"), 1},
    {pack("TERM"), 1},
    {pack("TREE"), 1},
    {pack("WATR"), 1},
    {pack("WEAP"), 1},
    {pack("WRLD"), 1},
};

template <std::size_t N>
const char* find_value(const ValueRow (&rows)[N], std::uint32_t value) {
  for (const auto& row : rows) {
    if (row.value == value) return row.name;
  }
  return nullptr;
}

template <std::size_t N>
const char* index_name(const char* const (&names)[N], std::uint32_t value) {
  return value < N ? names[value] : nullptr;
}

const char* vocab_name(std::uint32_t table, std::uint32_t value, bool& known_table) {
  known_table = true;
  switch (table) {
    case RADICLE_VOCAB_RECORD_CLASS:
      for (const auto& row : k_record_classes) {
        if (row.signature == value) return row.name;
      }
      return nullptr;
    case RADICLE_VOCAB_LEVEL: return index_name(k_levels, value);
    case RADICLE_VOCAB_PRIMITIVE_TYPE: return index_name(k_primitive_types, value);
    case RADICLE_VOCAB_MARKER_TYPE: return index_name(k_marker_types, value);
    case RADICLE_VOCAB_SCRIPT_PROPERTY_TYPE: return find_value(k_script_property_types, value);
    // Constraint: only value 1 has an identifier; the engine writes every other value as its number.
    case RADICLE_VOCAB_ACTIVATE_PARENT_FLAGS: return value == 1 ? "ParentActivateOnly" : nullptr;
    default: known_table = false; return nullptr;
  }
}

}  // namespace

extern "C" radicle_status radicle_vocab_name(std::uint32_t table,
                                             std::uint32_t value,
                                             std::uint8_t** out_bytes,
                                             std::size_t* out_size) {
  clear_error();
  if (!out_bytes || !out_size) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_bytes and out_size are required");
  *out_bytes = nullptr;
  *out_size = 0;
  bool known_table = false;
  const char* name = vocab_name(table, value, known_table);
  if (!known_table) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "unknown vocabulary table");
  if (name == nullptr) return fail(RADICLE_STATUS_NOT_FOUND, "no vocabulary name for this value");
  return copy_out(std::string_view{name}, out_bytes, out_size, k_copy_messages);
}

extern "C" radicle_status radicle_vocab_signature_traits(std::uint32_t signature, std::uint32_t* out_traits) {
  clear_error();
  if (!out_traits) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_traits is required");
  *out_traits = 0;
  for (const auto& row : k_traits) {
    if (row.signature == signature) *out_traits = row.traits;
  }
  return RADICLE_STATUS_OK;
}

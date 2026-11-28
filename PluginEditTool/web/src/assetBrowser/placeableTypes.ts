export interface PlaceableType {
  signature: string;
  name: string;
}

export const PLACEABLE_TYPES: PlaceableType[] = [
  { signature: 'ACTI', name: 'Activator' },
  { signature: 'ADDN', name: 'Addon Node' },
  { signature: 'ALCH', name: 'Potion' },
  { signature: 'AMMO', name: 'Ammo' },
  { signature: 'ARMO', name: 'Armor' },
  { signature: 'BOOK', name: 'Book' },
  { signature: 'CONT', name: 'Container' },
  { signature: 'DOOR', name: 'Door' },
  { signature: 'FLOR', name: 'Flora' },
  { signature: 'FURN', name: 'Furniture' },
  { signature: 'HAZD', name: 'Hazard' },
  { signature: 'IDLM', name: 'Idle Marker' },
  { signature: 'INGR', name: 'Ingredient' },
  { signature: 'KEYM', name: 'Key' },
  { signature: 'LIGH', name: 'Light' },
  { signature: 'LVLI', name: 'Leveled Item' },
  { signature: 'MISC', name: 'Misc Item' },
  { signature: 'MSTT', name: 'Movable Static' },
  { signature: 'NOTE', name: 'Note' },
  { signature: 'NPC_', name: 'NPC' },
  { signature: 'PROJ', name: 'Projectile' },
  { signature: 'SCOL', name: 'Static Collection' },
  { signature: 'SCRL', name: 'Scroll' },
  { signature: 'STAT', name: 'Static' },
  { signature: 'TACT', name: 'Talking Activator' },
  { signature: 'TERM', name: 'Terminal' },
  { signature: 'TREE', name: 'Tree' },
  { signature: 'WEAP', name: 'Weapon' },
];

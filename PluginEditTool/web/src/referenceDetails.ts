export interface ReferenceLink {
  formKey: string;
  editorId: string | null;
  signature: string | null;
}

export interface ReferenceVec {
  x: number;
  y: number;
  z: number;
}

export interface ReferenceScriptValue {
  object: ReferenceLink | null;
  alias: number;
}

export interface ReferenceScriptMember {
  name: string;
  type: number;
  typeName: string;
  flags: number;
  value: unknown;
}

export interface ReferenceScriptElement {
  object: ReferenceLink | null;
  alias: number;
}

export interface ReferenceScriptProperty {
  name: string;
  type: number;
  typeName: string;
  flags: number;
  elementCount: number | null;
  value: unknown;
  elements: unknown;
}

export interface ReferenceScript {
  name: string;
  flags: number;
  properties: ReferenceScriptProperty[];
}

export interface ReferenceScriptSet {
  version: number;
  objectFormat: number;
  unsupported: boolean;
  fragmentBytes: number;
  scripts: ReferenceScript[];
}

export interface ReferenceError {
  error: string;
}

export interface ReferenceFlags {
  raw: number;
  deleted: boolean;
  persistent: boolean;
  initiallyDisabled: boolean;
  respawns: boolean | null;
  names: { name: string; bit: number }[];
}

export interface ReferenceBase extends ReferenceLink {
  type: string | null;
  model: string | null;
  missing: boolean;
  name?: string | null;
}

export interface ReferenceDetails {
  formKey: string;
  editorId: string | null;
  signature: string;
  winningPlugin: string;
  pluginsWithRecord: string[];
  cell: ReferenceLink | null;
  flags: ReferenceFlags;
  base: ReferenceBase | null;
  baseScripts: ReferenceScriptSet | ReferenceError | null;
  transform: {
    position: ReferenceVec | null;
    rotation: ReferenceVec | null;
    scale: number | null;
  };
  layer: ReferenceLink | null;
  encounterZone: ReferenceLink | null;
  emittance: ReferenceLink | null;
  radius: number | null;
  count: number | null;
  levelModifier: number | null;
  locationReference: ReferenceLink | null;
  locationReferenceTypes: ReferenceLink[];
  attachRef: ReferenceLink | null;
  enableParent: {
    reference: ReferenceLink | null;
    flags: number | null;
    setOppositeToParent: boolean | null;
    popIn: boolean | null;
  } | null;
  ownership: { owner: ReferenceLink | null; noCrime: boolean | null } | null;
  factionRank: number | null;
  linkedReferences: { keyword: ReferenceLink | null; reference: ReferenceLink | null }[];
  activateParents: {
    flags: number | null;
    parents: { reference: ReferenceLink | null; delay: number }[];
  } | null;
  teleport: {
    door: ReferenceLink | null;
    position: ReferenceVec;
    rotation: ReferenceVec;
    flags: number;
    transitionInterior: ReferenceLink | null;
  } | null;
  primitive: {
    bounds: ReferenceVec;
    color: number[];
    type: number;
  } | null;
  spline: {
    slack: number;
    thickness: number;
    halfExtents: ReferenceVec;
    windDetachedEnd: boolean | null;
  } | null;
  roomPortal: {
    width: number;
    height: number;
    position: ReferenceVec;
    rotation: number[];
  } | null;
  room: {
    linkedRoomCount: number;
    flags: number;
    lightingTemplate: ReferenceLink | null;
    imageSpace: ReferenceLink | null;
    linkedRooms: ReferenceLink[];
  } | null;
  portals: { origin: ReferenceLink | null; destination: ReferenceLink | null }[];
  lightData: {
    fov: number;
    fade: number;
    endDistanceCap: number;
    shadowDepthBias: number;
    nearClip: number | null;
    volumetricIntensity: number | null;
  } | null;
  mapMarker: {
    flags: number | null;
    name: string | null;
    nameStringId: number | null;
    type: number | null;
  } | null;
  patrols: {
    idleTime: number;
    idle: ReferenceLink | null;
    topic: ReferenceLink | null;
    topics: { type: number; topic: ReferenceLink | null; subtype: number | null }[];
  }[];
  lock: {
    level: number;
    levelName: string | null;
    key: ReferenceLink | null;
    flags: number;
  } | null;
  comments: string | null;
  openByDefault: boolean;
  ignoredBySandbox: boolean;
  scripts: ReferenceScriptSet | null;
}

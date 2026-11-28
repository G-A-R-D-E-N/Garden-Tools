import type { BaseObjectQuery, PlaceOptions, PlacementTransform } from './assetBrowser/baseObjects';

export interface CellPlacedReference {
  formKey: string;
  editorId?: string;
  recordType: string;
  baseType?: string;
  baseFormKey: string;
  baseEditorId?: string;
  modelPath: string | null;



  decalDiffuse?: string | null;
  decalWidth?: number | null;
  decalHeight?: number | null;





  scolParts?: {
    modelPath: string;
    placements: { x: number; y: number; z: number; rx: number; ry: number; rz: number; scale: number }[];
  }[] | null;
  position: { x: number; y: number; z: number };
  rotation: { x: number; y: number; z: number };
  scale: number;
  isMarker?: boolean;
  light?: CellLightData | null;
  primitive?: { type: number; bounds: { x: number; y: number; z: number }; color?: number[] } | null;
}
export interface CellLightData {
  radius: number;
  color: { r: number; g: number; b: number };
  fade?: number | null;
  flags?: number;
  fov?: number;
  falloffExponent?: number;
  isSpot?: boolean;
  castsShadow?: boolean;
  ambientOnly?: boolean;
  offByDefault?: boolean;
  flicker?: boolean;
  pulse?: boolean;
  nonSpecular?: boolean;
}
export interface CellLightingData {
  ambient: [number, number, number];
  directional: [number, number, number];
  directionalRotationXY: number;
  directionalRotationZ: number;
  directionalFade: number;
  fogNearColor: [number, number, number];
  fogFarColor?: [number, number, number] | null;
  fogNear: number;
  fogFar: number;
  fogClipDistance?: number;
  fogPower?: number;
}
export interface CellTransform {
  x: number; y: number; z: number; rx: number; ry: number; rz: number;
}
export interface CellLockChange {
  level: number;
  key: string | null;
  flags: number;
}
export interface CellOwnershipChange {
  owner: string;
  noCrime: boolean;
  rank?: number | null;
}
export interface CellPrimitiveChange {
  bounds?: { x: number; y: number; z: number };
  color?: number[];
}
export interface CellEditChanges {
  transform?: CellTransform;
  scale?: number | null;
  editorId?: string;
  flags?: number;
  lock?: CellLockChange | null;
  ownership?: CellOwnershipChange | null;
  primitive?: CellPrimitiveChange;
  openByDefault?: boolean;
  ignoredBySandbox?: boolean;
}
export type CellPropertyKind = 'scalar' | 'link' | 'colour' | 'flags';
export type CellPropertyValue = number | string | number[] | string[] | null;
export interface CellPropertyDescriptor {
  name: string;
  label: string;
  kind: CellPropertyKind;
  minimum: number;
  maximum: number;
  multiple: boolean;
  targets: string[];
}
export type CellPropertyChanges = Record<string, CellPropertyValue>;
export interface WorldPropertyDescriptor {
  name: string;
  label: string;
  kind: CellPropertyKind;
  minimum: number;
  maximum: number;
  targets: string[];
}
export interface WorldPropertiesResult {
  formKey: string;
  values: Record<string, CellPropertyValue>;
  descriptors: WorldPropertyDescriptor[];
}
export type WorldPropertyChanges = Record<string, CellPropertyValue>;
export interface CellLandResult {
  cellFormKey: string;
  landFormKey: string;
  width: number;
  height: number;
  offset: number;
  heights: number[];
}
export interface CellPropertiesResult {
  cellFormKey: string;
  cellEditorId: string;
  values: Record<string, CellPropertyValue>;
  descriptors: CellPropertyDescriptor[];
  gridX?: number | null;
  gridY?: number | null;
}
export interface CellReferencesResult {
  cellFormKey?: string;
  cellEditorId?: string;
  cellName?: string;
  interior?: boolean;
  referenceCount?: number;
  withModelCount?: number;
  references?: CellPlacedReference[];
  lighting?: CellLightingData | null;
  error?: string;
}

export interface CellGeoBatchResult {
  count: number;
  geometry: Record<string, unknown>;
}



export interface CellSearchHit {
  FormKey: string;
  EditorID: string;
  Type: string;
  Plugin: string;
  Name: string;
}

export interface CellHost {
  GetPlacedReferences(cellId: string): Promise<string>;

  GetPlacedReferencesAtGrid(worldspace: string, gridX: number, gridY: number): Promise<string>;
  GetReferenceDetails(formKey: string): Promise<string>;
  SearchWorldspaces(query: string, limit: number): Promise<string>;
  ResolveWorldspace(cellId: string): Promise<string>;
  ResolveWorldspaceId(worldspace: string): Promise<string>;

  GetGeometryBatch(relModelPathsJson: string): Promise<string>;
  GetPlugins(): Promise<string>;
  SearchCells(query: string, limit: number): Promise<string>;

  GetTexture(relModelPath: string, relTexPath: string): Promise<string>;

  GetGeometryBatchProgress(): Promise<string>;
  GetCellTimings(): Promise<string>;




  SetPlacedReferenceTransform(formKey: string, patchPlugin: string,
    x: number, y: number, z: number, rx: number, ry: number, rz: number): Promise<string>;

  EditPlacedReference(formKey: string, patchPlugin: string, changes: CellEditChanges): Promise<string>;
  DeletePlacedReference(formKey: string, patchPlugin: string): Promise<string>;
  DuplicatePlacedReference(formKey: string, patchPlugin: string, offset: CellTransform | null): Promise<string>;
  UndoCellEdit(patchPlugin: string): Promise<string>;
  RedoCellEdit(patchPlugin: string): Promise<string>;
  GetCellEditHistory(patchPlugin: string): Promise<string>;
  GetCellProperties(cellFormKey: string): Promise<string>;
  SetCellProperties(cellFormKey: string, patchPlugin: string, changes: CellPropertyChanges): Promise<string>;
  MoveCellToGrid(cellFormKey: string, patchPlugin: string, gridX: number, gridY: number): Promise<string>;
  GetCellLand(cellFormKey: string): Promise<string>;
  SetCellLand(cellFormKey: string, patchPlugin: string, heights: number[]): Promise<string>;
  GetWorldProperties(worldspaceFormKey: string): Promise<string>;
  SetWorldProperties(worldspaceFormKey: string, patchPlugin: string, changes: WorldPropertyChanges): Promise<string>;
  ListBaseObjects(query: BaseObjectQuery): Promise<string>;
  PlaceReference(patchPlugin: string, cellId: string, baseFormKey: string, transform: PlacementTransform,
    options: PlaceOptions): Promise<string>;
  PlaceReferences(patchPlugin: string, cellId: string, placementsJson: string, options: PlaceOptions): Promise<string>;
}

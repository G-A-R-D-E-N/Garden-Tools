import type { CellHost } from './cellHost';
import type { PapyrusHost } from './papyrusHost';
import type { PrevisHost } from './previsClient';
import type { SwfHost } from './swfClient';
export * from './previsClient';
export * from './swfClient';

export type * from './cellHost';
export type * from './papyrusHost';

export interface Backend {

  GetConflicts(): Promise<string>;
  GetConflictMatrix(formKey: string): Promise<string>;
  GetRecordTree(plugin: string, id: string): Promise<string>;
  GetReferencedBy(formKey: string): Promise<string>;
  GetProblems(formKey: string): Promise<string>;
  SearchRecords(query: string, typeFilter: string, regex?: boolean): Promise<string>;
  GetEditablePlugins(): Promise<string>;


  OpenPlugin(plugin: string): Promise<string>;
  CreatePlugin(name: string): Promise<string>;
  CreateRecord(plugin: string, type: string, payload: Record<string, unknown>): Promise<string>;
  SetField(plugin: string, record: string, field: string, value: string): Promise<string>;
  SetComponents(plugin: string, record: string, componentsJson: string): Promise<string>;
  SetQuestAliases(plugin: string, record: string, aliasesJson: string): Promise<string>;
  SetQuestStages(plugin: string, record: string, stagesJson: string): Promise<string>;
  GetQuestStages(plugin: string, formKey: string, context?: object): Promise<string | object>;
  GetQuestObjectives(plugin: string, formKey: string, context?: object): Promise<string | object>;
  GetQuestAliases(plugin: string, formKey: string): Promise<string | object>;
  SetQuestObjectives(plugin: string, record: string, objectivesJson: string): Promise<string>;
  SetQuestFragment(plugin: string, record: string, stage: number, logEntryIndex: number, scriptName: string,
                   outputDirectory: string, importsDirectory: string, actionsJson: string):
    Promise<string | { script: string; function: string; source: string; pex: string }>;
  GetQuestFragments(plugin: string, formKey: string): Promise<string | object>;
  GetQuestScript(scriptName: string, importsDirectory?: string): Promise<string | object>;
  GetDialogue(plugin: string, questFormKey: string): Promise<string | object>;
  CreateDialogueBranch(plugin: string, questFormKey: string, spec: Record<string, unknown>): Promise<string | { formKey: string }>;
  CreateDialogueTopic(plugin: string, questFormKey: string, spec: Record<string, unknown>): Promise<string | { formKey: string }>;
  CreateDialogueTopicBulk(plugin: string, questFormKey: string, spec: Record<string, unknown>): Promise<string | object>;
  SetDialogueBranchStart(plugin: string, branchFormKey: string, topicFormKey: string): Promise<string>;
  CreateInfo(plugin: string, topicFormKey: string, spec: Record<string, unknown>): Promise<string | { formKey: string }>;
  SetInfo(plugin: string, infoFormKey: string, spec: Record<string, unknown>): Promise<string>;
  RemoveInfo(plugin: string, infoFormKey: string): Promise<string>;
  GetScene(plugin: string, sceneFormKey: string): Promise<string | object>;
  CreateScene(plugin: string, questFormKey: string, spec?: Record<string, unknown>): Promise<string | { formKey: string }>;
  SetScene(plugin: string, sceneFormKey: string, spec: Record<string, unknown>): Promise<string>;
  AddScenePhase(plugin: string, sceneFormKey: string, index: number, spec?: Record<string, unknown>): Promise<string>;
  SetScenePhase(plugin: string, sceneFormKey: string, phaseIndex: number, spec: Record<string, unknown>): Promise<string>;
  MoveScenePhase(plugin: string, sceneFormKey: string, fromIndex: number, toIndex: number): Promise<string>;
  RemoveScenePhase(plugin: string, sceneFormKey: string, phaseIndex: number): Promise<string>;
  AddSceneActor(plugin: string, sceneFormKey: string, index: number): Promise<string>;
  SetSceneActor(plugin: string, sceneFormKey: string, actorIndex: number, spec: Record<string, unknown>): Promise<string>;
  RemoveSceneActor(plugin: string, sceneFormKey: string, actorIndex: number): Promise<string>;
  AddSceneAction(plugin: string, sceneFormKey: string, index: number, type: number): Promise<string>;
  SetSceneAction(plugin: string, sceneFormKey: string, actionIndex: number, spec: Record<string, unknown>): Promise<string>;
  MoveSceneAction(plugin: string, sceneFormKey: string, fromIndex: number, toIndex: number): Promise<string>;
  RemoveSceneAction(plugin: string, sceneFormKey: string, actionIndex: number): Promise<string>;
  SetConditions(plugin: string, record: string, conditionsJson: string): Promise<string>;
  AddListItem(plugin: string, record: string, field: string, value: string): Promise<string>;
  RemoveListItem(plugin: string, record: string, field: string, value: string): Promise<string>;
  DeleteRecord(plugin: string, id: string): Promise<string>;


  CopyAsOverride(sourcePlugin: string, id: string, patchPlugin: string, overwrite: boolean, allowLosing?: boolean): Promise<string>;
  CopyAsOverrideMany(itemsJson: string, patchPlugin: string, overwrite: boolean, allowLosing?: boolean): Promise<string>;
  RevertOverrides(badPlugin: string, patchPlugin: string, signature: string,
                  containsComponent: string, apply: boolean, limit: number): Promise<string>;
  CompactToEsl(plugin: string): Promise<string>;
  CleanUnusedMasters(plugin: string, apply: boolean): Promise<string>;
  CheckForErrors(plugin: string, limit?: number): Promise<string>;
  CheckQuest(plugin: string, formKey: string, limit?: number): Promise<string>;
  FindUnreachable(plugin: string, limit: number): Promise<string>;
  LootCleaningReport(plugin: string): Promise<string>;
  UpdateOnam(plugin: string): Promise<string>;
  CopyAsDisabledOverride(sourcePlugin: string, id: string, patchPlugin: string, overwrite: boolean): Promise<string>;
  CopyAsWrapper(sourcePlugin: string, id: string, patchPlugin: string, editorId: string | null): Promise<string>;
  ComparePluginTo(plugin: string, otherPath: string, formKey?: string): Promise<string>;
  CheckEslEligibility(plugin: string): Promise<string>;
  CleanPlugin(plugin: string): Promise<string>;
  RenumberFormId(plugin: string, record: string, newId: string): Promise<string>;


  DescribeElement(plugin: string, record: string, path: string): Promise<string>;
  AddElement(plugin: string, record: string, path: string, template: string): Promise<string>;
  RemoveElement(plugin: string, record: string, path: string): Promise<string>;
  MoveElement(plugin: string, record: string, path: string, delta: number): Promise<string>;
  ClearElement(plugin: string, record: string, path: string): Promise<string>;
  CopyElementToSelected(sourcePlugin: string, sourceFormKey: string, elementPath: string, targetsJson: string): Promise<string>;
  RemoveElementFromSelected(elementPath: string, targetsJson: string): Promise<string>;
  GetConditions(plugin: string, record: string): Promise<string>;
  GetConditionsAt(plugin: string, record: string, path: string): Promise<string>;
  SetConditionsAt(plugin: string, record: string, path: string, conditionsJson: string): Promise<string>;
  GetConditionFunctions(): Promise<string>;
  GetConditionRunOnTypes(): Promise<string>;
  GetConditionFunctionParams(): Promise<string>;
  ResolveFormKeyLabels(formKeysCsv: string): Promise<string>;
  CopyAsNewRecord(sourcePlugin: string, id: string, targetPlugin: string,
                  newEditorId: string): Promise<string>;
  RemoveIdenticalToMaster(plugin: string, apply: boolean): Promise<string>;
  CreateMergedPatch(plugins: string, patchPlugin: string, apply: boolean): Promise<string>;
  AddMasters(plugin: string, mastersCsv: string): Promise<string>;
  RenumberPluginFormIds(plugin: string, start: string, apply: boolean): Promise<string>;
  InjectFormsIntoMaster(plugin: string, master: string, start: string, apply: boolean): Promise<string>;
  CreateSeqFile(plugin: string, outputDir: string): Promise<string>;
  CheckCircularLeveledLists(plugin: string): Promise<string>;
  DeepCopyAsOverride(sourcePlugin: string, id: string, patchPlugin: string, apply: boolean, overwrite: boolean, maxRecords?: number, allowLosing?: boolean): Promise<string>;
  ChangeReferencingRecords(from: string, to: string, patchPlugin: string, apply: boolean): Promise<string>;
  GetLeveledList(formKey: string): Promise<string | object>;
  SetLeveledListSettings(plugin: string, formKey: string, chanceNone: number, maxCount: number, flags: number): Promise<string>;
  EditLeveledListEntry(plugin: string, formKey: string, op: EntryOp, index: number, value?: Record<string, unknown>): Promise<string>;
  GetNpc(formKey: string): Promise<string | object>;
  EditNpcEntries(plugin: string, formKey: string, list: NpcList, op: EntryOp, index: number, value?: Record<string, unknown>): Promise<string>;
  GetFaction(formKey: string): Promise<string | object>;
  EditFactionEntries(plugin: string, formKey: string, list: FactionList, op: EntryOp, index: number, value?: Record<string, unknown>): Promise<string>;
  GetWeaponDamageTypes(formKey: string): Promise<string | object>;
  GetMagicEffects(plugin: string, formKey: string): Promise<string | object>;
  AddMagicEffect(plugin: string, formKey: string, effect: string, magnitude: number,
                 area: number, duration: number): Promise<string>;
  SetMagicEffect(plugin: string, formKey: string, index: number, effect: string, magnitude: number,
                 area: number, duration: number): Promise<string>;
  RemoveMagicEffect(plugin: string, formKey: string, index: number): Promise<string>;


  GetActivePlugins(): Promise<string>;
  GetRecordTypeIndex(): Promise<string>;
  GetPluginRecordTypes(plugin: string): Promise<string>;
  GetRecordsOfType(signature: string, filter: string,
                   limit: number, offset: number): Promise<string>;
  GetRecordsGrid(plugin: string, type: string, limit: number, offset: number): Promise<string>;
  GetContainmentPath(formKey: string): Promise<string>;
  GetRecordDetails(formKey: string): Promise<string>;
  GetRecordPluginMatrix(formKey: string): Promise<string>;
  GetDependencies(formKey: string): Promise<string>;
  GetHistory(formKey: string): Promise<string>;
  GetPluginWorklist(plugin: string, filter?: string, type?: string, limit?: number, offset?: number): Promise<string>;
  GetLoadOrderSummary(): Promise<string>;


  ResolveConflict(formKey: string, winner: string, patch: string, allowLosing?: boolean): Promise<string>;
  SavePatch(patch: string): Promise<string>;
  SavePlugin(plugin: string, path: string): Promise<string>;
}


export type PluginKind = 'master' | 'plugin' | 'light';

export interface ActivePlugin {
  Name: string; Kind: PluginKind; LoadOrder: number; Editable: boolean; Localized: boolean; Size: number;
}
export interface RecordTypeEntry { Type: string; FriendlyName: string; Count: number; }
export interface BreadcrumbNode { Kind: string; Label: string; FormKey: string; }
export interface RecordDetails {
  FormKey: string; FormId: string; EditorId: string; Signature: string; ClassName: string;
  BaseForm: string; BaseFormKey: string; File: string; Winner: string; OverrideCount: number;
}
export interface PluginMatrixRow {
  Plugin: string; LoadOrder: number; Kind: PluginKind; Changes: number; Conflicts: number;
  IsOverride: boolean; IsWinner: boolean; LastModified: string;
}
export interface Dependency {
  Kind: 'link' | 'missing'; FormKey: string; EditorId: string; Type: string; Plugin: string;
}
export interface HistoryEntry {
  Plugin: string; LoadOrder: number; Action: string; ChangedFields: number; LastModified: string;
}
export interface LoadOrderSummary {
  TotalRecords: number; PluginCount: number; Plugins: string[];
}

export type EntryOp = 'add' | 'set' | 'remove';
export type NpcList = 'factions' | 'perks' | 'inventory';
export type FactionList = 'relations' | 'ranks';
export type LeveledListExtra = { owner: string | null; globalOrRank: number; itemCondition: number };
export type LeveledListEntry = {
  level: number; item: string | null; count: number; chanceNone: number; extra: LeveledListExtra | null;
};
export type LeveledList = {
  formKey: string; chanceNone: number; maxCount: number; flags: number;
  useGlobal: string | null; epicLootChance: string | null; entries: LeveledListEntry[];
};
export type NpcFaction = { faction: string | null; rank: number };
export type NpcPerk = { perk: string | null; rank: number };
export type NpcInventoryExtra = { owner: string | null; globalOrRank: number; condition: number };
export type NpcInventoryItem = { item: string | null; count: number; extra: NpcInventoryExtra | null };
export type NpcRecord = { formKey: string; factions: NpcFaction[]; perks: NpcPerk[]; inventory: NpcInventoryItem[] };
export type FactionReaction = 'neutral' | 'enemy' | 'ally' | 'friendly';
export type FactionRelation = { faction: string | null; modifier: number; reaction: FactionReaction };
export type LocalizedTitle = string | { stringId: number } | { rawId: string } | null;
export type FactionRank = { number: number; maleTitle: LocalizedTitle; femaleTitle: LocalizedTitle; hasInsignia: boolean };
export type FactionRecord = { formKey: string; relations: FactionRelation[]; ranks: FactionRank[] };
export type WeaponDamageType = { type: string | null; amount: number; curveTable: string | null };
export type MagicEffectEntry = { index: number; effect: string | null; magnitude: number; area: number; duration: number };
export type MagicEffectList = { formKey: string; count: number; effects: MagicEffectEntry[] };
export type WeaponDamageTypes = { formKey: string; damageTypes: WeaponDamageType[] };
export const TYPED_RECORD_TYPES = ['LVLI', 'LVLN', 'LVSP', 'NPC_', 'FACT', 'WEAP', 'SPEL', 'ALCH', 'ENCH', 'INGR'];


export interface Mo2Status {
  loaded: boolean;
  stale: boolean;
  staleConfig?: boolean;
  staleLoadOrder?: boolean;
  pendingPlugins?: string[];
  profile?: string;
}

export interface AppInterop {
  BrowseForMo2Folder(): Promise<string>;
  OpenMo2Profile(instancePath: string, discardPending?: boolean): Promise<void>;
  LoadEnvironment(discardPending?: boolean): Promise<void>;
  GetMo2Status(): Promise<Mo2Status | string>;
  ReloadMo2Profile(discardPending?: boolean): Promise<Mo2Status | string>;
  ScanConflicts(): Promise<string>;
  ScanBrokenRefs(): Promise<string>;
  RefreshTree(): Promise<string>;
  GetPlugins(): Promise<string>;
  GetChildren(path: string): Promise<string>;
  OpenRecord(path: string): Promise<string>;
}


export type ScalarPresentation =
  | { kind: 'enum'; names: { value: number; name: string }[] }
  | { kind: 'flags'; names: { bit: number; name: string }[] };

export interface ConflictFieldRow {
  Field: string;
  DisplayLabel: string;
  Level: number;
  Values: string[];
  Editable?: boolean[];
  Differs: boolean;
  Statuses: string[];
  Severity: string;
  Kind: 'Value' | 'Flag' | 'FormID';
  Group: string;
  GroupLabel: string;
  IsSummary: boolean;
  HasChildren: boolean;
  EditKind: 'Text' | 'Bool' | 'Enum' | 'Ref';
  EnumOptions: string[] | null;
  RefType: string | null;
  RefTypes: string | null;
  Presentation?: ScalarPresentation;
}

export interface RefByEntry { Plugin: string; FormKey: string; EditorID: string; Type: string; }
export interface RecordProblem { Code?: string; Detail?: string; Severity: string; Description: string; }
export interface SearchHit { FormKey: string; EditorID: string; Type: string; Plugin: string; }

export interface BulkTarget { plugin: string; formKey: string }

export interface ElementActions {
  canCreate?: boolean;
  canAdd: boolean;
  templates: string[];
  elementType: string;
  canRemove: boolean;
  canMoveUp: boolean;
  canMoveDown: boolean;
  canClear: boolean;
  count: number;
  error?: string;
}


export interface ConditionDto {
  _native?: { ctda: number[]; cis1: string | null; cis2: string | null };
  _edited?: string[];
  function: string;
  operator: string;
  value?: number;
  compareGlobal?: string;
  param1?: string | number;
  param2?: string | number;
  cis1?: string | null;
  cis2?: string | null;
  runOn?: string;
  reference?: string;
  flags?: string;
}
export interface ConflictMatrix {
  FormKey: string;
  EditorID: string;
  Type: string;
  Winner: string;
  Plugins: string[];
  Rows: ConflictFieldRow[];
  Level: string;
}


export interface ConflictEntry {
  FormKey: string;
  EditorID: string;
  Type: string;
  Plugins: string[];
  Winner: string;
  InvolvesMod: boolean;
}


export interface FieldNode {
  key: string;
  label: string;
  value: string;
  values: Record<string, string>;
  editKind: "Text" | "Bool" | "Enum";
  enumOptions: string[] | null;
  isSummary: boolean;
  hasChildren: boolean;
  children: FieldNode[];
}



export type NifCollisionShape = 'none' | 'box' | 'hull' | 'mesh';

export interface NifConnectPoint {
  parent: string;
  name: string;
  rotation: number[];
  translation: number[];
  scale: number;
  world?: number[];
}

export interface NifConnectParentsBlock {
  block: number;
  type: 'BSConnectPoint::Parents';
  name: string;
  points: NifConnectPoint[];
}

export interface NifConnectChildrenBlock {
  block: number;
  type: 'BSConnectPoint::Children';
  name: string;
  skinned: boolean;
  names: string[];
}

export type NifConnectBlock = NifConnectParentsBlock | NifConnectChildrenBlock;

export interface NifHost {
  BrowseForFile(title: string, filter: string): Promise<string>;
  BrowseForFolder(title: string): Promise<string>;
  BrowseForSave(title: string, filter: string): Promise<string>;

  Import(objPath: string, outNif: string, material: string, texDiffuse: string,
         texNormal: string, collision: NifCollisionShape, fromBlender: boolean, massKg: number, overwrite: boolean): Promise<string>;
  ImportGltf(gltfPath: string, outNif: string, material: string, texDiffuse: string,
             texNormal: string, collision: NifCollisionShape, skeleton: string, overwrite: boolean): Promise<string>;
  Inspect(nifPath: string): Promise<string>;
  Geo(nifPath: string): Promise<string>;


  GetTexture(nifPath: string, relTexPath: string, textureRoot: string, reconstructZ?: boolean): Promise<string>;
  Verify(nifPath: string): Promise<string>;
  Fix(nifPath: string, outNif: string, check: string | null, overwrite: boolean): Promise<string>;
  ExportGltf(nifPath: string, outGlb: string, overwrite: boolean, materials: boolean, textures: string,
    tangents: boolean, vertexColours: boolean): Promise<string>;
  Tree(nifPath: string): Promise<string>;
  MaterialCompare(nifPath: string, shape: number, textureRoot: string): Promise<string>;
  MaterialTextureEdits(nifPath: string, shape: number, textureRoot: string): Promise<string>;
  MaterialShapes(nifPath: string, material: string): Promise<string>;
  Segments(nifPath: string, block: number): Promise<string>;

  ConnectPoints(nifPath: string): Promise<string>;
  ApplyConnectPoints(nifPath: string, opsJson: string, outNif: string): Promise<string>;

  CollisionInfo(nifPath: string): Promise<string>;
  CollisionGeo(nifPath: string): Promise<string>;
  CollisionSet(nifPath: string, node: string, outNif: string, source: 'donor_nif' | 'kind',
               donorPath: string, kind: 'box' | 'hull' | 'mesh' | '', massKg: number, donorBody: number | undefined,
               overwrite: boolean, expectedSource: string, expectedDonorSource: string): Promise<string>;
  CollisionRemove(nifPath: string, node: string, outNif: string, overwrite: boolean, expectedSource: string): Promise<string>;

  UvGet(nifPath: string): Promise<string>;
  UvTransform(nifPath: string, block: number, channel: number, du: number, dv: number, sx: number, sy: number,
              rotationDegrees: number, outNif: string): Promise<string>;
  UvOp(op: string, argsJson: string): Promise<string>;
  VertexColorGet(nifPath: string): Promise<string>;
  VertexColorSet(nifPath: string, block: number, kind: string, channel: string | null, axis: string | null,
                 value: number | null, color: number[] | null, endColor: number[] | null, vertices: number[] | null,
                 outNif: string, expectedSource?: string): Promise<string>;

  ApplyEdits(nifPath: string, editsJson: string, outNif: string, expectedSource?: string): Promise<string>;
  CreateTextureSet(nifPath: string, block: number, textures: string[], outNif: string, expectedSource?: string): Promise<string>;
  ShapeOp(nifPath: string, outNif: string, op: string, blocksJson: string, creaseDegrees: number,
          overwrite: boolean, ratio: number, maxInfluences?: number, pruneThreshold?: number,
          sourceBlock?: number | null, tris?: number[] | null, distance?: number | null, loopA?: number[] | null,
          loopB?: number[] | null, normal?: number[] | null, offset?: number | null,
          edges?: [number, number][] | null, width?: number | null): Promise<string>;
  OpenFolder(path: string): Promise<string>;
  StageDroppedFile(name: string, base64: string): Promise<string>;
}


export interface HkxBone {
  name: string;
  parent: number;
  translation: [number, number, number];
  rotation: [number, number, number, number];
  scale: [number, number, number];
}
export interface HkxSkeleton { name: string; bones: HkxBone[]; }
export interface HkxPose { duration: number; frame_count: number; bones: HkxBone[]; }
export type HkxPoseSpace = 'local' | 'model';
export interface HkxPoseBoth { duration: number; frame_count: number; local: HkxBone[]; model: HkxBone[]; }
export type HkxSkeletonMatch = 'match' | 'mismatch' | 'unverified';
export interface HkxClipInfo {
  duration: number;
  frame_count: number;
  skeleton_name: string;
  original_skeleton_name: string;
  skeleton_match: HkxSkeletonMatch;
}

export interface HkxAnnotationEntry { time: number; text: string; }
export interface HkxAnnotationTrack { name: string; entries: HkxAnnotationEntry[]; }
export interface HkxAnimationInspect {
  class_name: string;
  kind: string;
  duration: number;
  frame_count: number;
  track_count: number;
  annotation_track_count: number;
  annotation_count: number;
  object_count: number;
  annotations: HkxAnnotationTrack[];
  frame_duration?: number;
  track_name_source?: 'none' | 'explicit' | 'sibling' | 'embedded';
  track_name_skeleton?: string;
  tracks?: { index: number; name?: string; samples: number; translation_animated: boolean; rotation_animated: boolean; scale_animated: boolean }[];
  track?: { index: number; frames: number; shown: number; truncated: boolean; translations: number[]; rotations: number[]; scales: number[] };
}

export interface HkxHost {
  Skeleton(skeletonPath: string): Promise<HkxSkeleton | HkxPose | string>;
  LoadClip(skeletonPath: string, animationPath: string): Promise<HkxClipInfo | string>;
  Pose(skeletonPath: string, animationPath: string, time: number, space: HkxPoseSpace): Promise<HkxSkeleton | HkxPose | string>;
  PoseBoth(skeletonPath: string, animationPath: string, time: number): Promise<HkxPoseBoth | string>;
  InspectAnimation(animationPath: string, track?: number | null, limit?: number | null, skeletonPath?: string): Promise<HkxAnimationInspect | string>;
}


export interface MasterRow {
  index: number;
  name: string;
  size: number | null;
  used: boolean;
}
export interface MastersListResult {
  pluginName?: string;
  masters?: MasterRow[];
  light?: boolean;
  error?: string;
}


export interface MastersHost {
  GetPlugins(): Promise<string>;
  List(plugin: string): Promise<string>;

  Reorder(plugin: string, orderJson: string): Promise<string>;
  SetLight(plugin: string, light: boolean): Promise<string>;
  CleanUnused(plugin: string, apply: boolean): Promise<string>;
  SavePlugin(plugin: string): Promise<string>;
}

export interface StringTableInfo { kind: string; path: string; count: number; source: string; pending?: boolean; }
export interface SkippedStringArchive { path: string; reason: string; }
export interface StringTablesResult { tables: StringTableInfo[]; skippedArchives: SkippedStringArchive[]; }
export interface StringRow { id: number; text: string; }
export interface StringsPage { total: number; offset: number; limit: number; rows: StringRow[]; pending?: boolean; }
export interface StringLookupRow { kind: string; id: number; text: string; pending?: boolean; }

export interface StringsHost {
  ListStringTables(plugin: string, language: string): Promise<string>;
  GetStrings(plugin: string, language: string, table: string, filter: string,
             offset: number, limit: number): Promise<string>;
  LookupString(plugin: string, language: string, id: number): Promise<string>;
  SetString(plugin: string, language: string, table: string, id: number, text: string): Promise<string>;
  AddString(plugin: string, language: string, table: string, text: string): Promise<string>;
  RemoveString(plugin: string, language: string, table: string, id: number): Promise<string>;
  ImportStrings(plugin: string, language: string, table: string, json: string, mode: string): Promise<string>;
  ExportStrings(plugin: string, language: string, table: string): Promise<string>;
  SaveStringTables(plugin: string, language: string): Promise<string>;
  DiscardStringEdits(plugin: string, language: string): Promise<string>;
}

export interface ArchiveEntry { path: string; size: number; }
export interface ArchiveListResult {
  archiveName?: string;
  totalCount?: number;
  shownCount?: number;
  truncated?: boolean;
  entries?: ArchiveEntry[];
  error?: string;
}


export type ArchiveFilterMode = 'simple' | 'wildcard' | 'regex';


export interface ArchiveCompareResult {
  archiveA?: string;
  archiveB?: string;
  added?: string[];
  removed?: string[];
  changed?: string[];
  identicalCount?: number;
  error?: string;
}


export interface ArchiveHost {
  BrowseForFile(title: string, filter: string): Promise<string>;
  BrowseForFolder(title: string): Promise<string>;
  BrowseForSave(title: string, filter: string): Promise<string>;
  List(archivePath: string, filter: string, limit: number, filterMode: ArchiveFilterMode): Promise<string>;
  ExtractFile(archivePath: string, innerPath: string, outPath: string): Promise<string>;

  ExtractSelected(archivePath: string, innerPathsJson: string, outDir: string, overwrite?: boolean): Promise<string>;
  ExtractAll(archivePath: string, outDir: string, filter: string, limit: number, filterMode: ArchiveFilterMode, overwrite?: boolean): Promise<string>;
  Compare(archivePathA: string, archivePathB: string): Promise<string>;

  Pack(sourcePathsJson: string, outputBa2: string, format: string, rootDir: string, compress: boolean): Promise<string>;
  OpenFolder(path: string): Promise<string>;
}

export interface MaterialField {
  name: string;
  section: 'material' | 'header';
  type: 'bool' | 'float' | 'int' | 'string' | 'color';
  value: string;
}
export interface MaterialInspectResult {
  fileName?: string;
  version?: number;
  fields?: MaterialField[];
  error?: string;
}


export interface MaterialHost {
  BrowseForFile(title: string, filter: string): Promise<string>;
  Inspect(path: string): Promise<string>;

  SetFields(path: string, fieldsJson: string, outPath: string): Promise<string>;
  OpenFolder(path: string): Promise<string>;
  StageDroppedFile(name: string, base64: string): Promise<string>;
}


export interface AudioHost {
  BrowseForFile(title: string, filter: string): Promise<string>;
  BrowseForFolder(title: string): Promise<string>;
  BrowseForSave(title: string, filter: string): Promise<string>;


  ConvertToXwm(source: string, output: string, bitrateBps: number): Promise<string>;

  ConvertFromXwm(source: string, output: string, targetExt: string): Promise<string>;

  MakeFuz(audioSource: string, lipPath: string, fuzOutput: string, noLip: boolean): Promise<string>;

  ExtractFuz(fuzPath: string, xwmOutput: string, lipOutput: string, alsoWav: boolean): Promise<string>;
  OpenFolder(path: string): Promise<string>;
  StageDroppedFile(name: string, base64: string): Promise<string>;
}


export interface SettingsHost {
  GetSettings(): Promise<string>;
  SaveSettings(json: string): Promise<string>;
  BrowseFolder(title: string, current: string): Promise<string>;
  BrowseFile(title: string, filter: string, current: string): Promise<string>;
  TestClaude(path: string): Promise<string>;
}



export interface GraphHost {

  GetCorePalette(): Promise<string>;

  SearchPalette(kind: string, query: string, scriptFilter: string, limit: number): Promise<string>;

  GetNodeSignature(nodeType: string): Promise<string>;

  ValidateGraph(documentJson: string): Promise<string>;

  CompileToSource(documentJson: string): Promise<string>;

  CompileToPex(documentJson: string, outputDirectory: string): Promise<string>;
  LoadGraph(path: string): Promise<string>;

  LoadScript(path: string): Promise<string>;
  SaveGraph(path: string, documentJson: string): Promise<string>;
  BrowseForGraph(save: boolean): Promise<string>;

  BrowseForScript(): Promise<string>;

  Refresh(): Promise<void>;
}

declare global {
  interface Window {
    chrome?: {
      webview?: {
        hostObjects?: {
          backend?: Backend;
          appInterop?: AppInterop;
          settings?: SettingsHost;
          papyrus?: PapyrusHost;
          nif?: NifHost;
          hkx?: HkxHost;
          material?: MaterialHost;
          masters?: MastersHost;
          archive?: ArchiveHost;
          audio?: AudioHost;
          cell?: CellHost;
          graph?: GraphHost;
          previs?: PrevisHost;
          strings?: StringsHost;
          swf?: SwfHost;
          lod?: LodHost;
          debugChecks?: DebugChecksHost;
          debugTools?: DebugToolsHost;
        };
        addEventListener(type: string, listener: (event: MessageEvent) => void): void;
        removeEventListener(type: string, listener: (event: MessageEvent) => void): void;
      };
    };
  }
}

export const getBackend = (): Backend | undefined =>
  window.chrome?.webview?.hostObjects?.backend;

export const getAppInterop = (): AppInterop | undefined =>
  window.chrome?.webview?.hostObjects?.appInterop;

export const getPapyrus = (): PapyrusHost | undefined =>
  window.chrome?.webview?.hostObjects?.papyrus;

export const getNif = (): NifHost | undefined =>
  window.chrome?.webview?.hostObjects?.nif;

export const getHkx = (): HkxHost | undefined =>
  window.chrome?.webview?.hostObjects?.hkx;

export const getMaterial = (): MaterialHost | undefined =>
  window.chrome?.webview?.hostObjects?.material;

export const getMasters = (): MastersHost | undefined =>
  window.chrome?.webview?.hostObjects?.masters;

export const getArchive = (): ArchiveHost | undefined =>
  window.chrome?.webview?.hostObjects?.archive;

export const getAudio = (): AudioHost | undefined =>
  window.chrome?.webview?.hostObjects?.audio;

export const getCell = (): CellHost | undefined =>
  window.chrome?.webview?.hostObjects?.cell;

export const getStrings = (): StringsHost | undefined =>
  window.chrome?.webview?.hostObjects?.strings;

export const getGraph = (): GraphHost | undefined =>
  window.chrome?.webview?.hostObjects?.graph;



export interface LodHost {
  Worlds(pluginPaths: string, dataDir: string, archivePaths: string): Promise<string>;
  Plan(pluginPaths: string, dataDir: string, archivePaths: string, ledgerText: string): Promise<string>;
  Preview(pluginPaths: string, dataDir: string, archivePaths: string,
          world: string, level: number, x: number, y: number, object: boolean): Promise<string>;
  Commit(ledgerText: string, commitText: string): Promise<string>;
  Generate(pluginPaths: string, dataDir: string, archivePaths: string,
           ledgerText: string, apply: boolean): Promise<string>;
  GenerateWorld(pluginPaths: string, dataDir: string, archivePaths: string,
                world: string, ledgerText: string, apply: boolean): Promise<string>;
  GenerateScoped(pluginPaths: string, dataDir: string, archivePaths: string, world: string,
                 ledgerText: string, apply: boolean, levelMin: number, levelMax: number): Promise<string>;
  // Invariant: progress and cancel must answer while a scoped bake runs, so they never wait for it.
  Progress(): Promise<string>;
  CancelScoped(): void;
  Verify(pluginPaths: string, dataDir: string, archivePaths: string, ledgerText: string): Promise<string>;
  VerifyWorld(pluginPaths: string, dataDir: string, archivePaths: string, world: string,
              ledgerText: string): Promise<string>;
  Candidates(pluginPaths: string, dataDir: string, archivePaths: string, world: string, level: number): Promise<string>;
}

export const getLod = (): LodHost | undefined =>
  window.chrome?.webview?.hostObjects?.lod;

// Invariant: every call fails unless the host registered debugChecks, which it does only when FO4IDE_DEBUG_CHECKS=1.
export interface DebugChecksHost {
  List(): Promise<string>;
  Results(): Promise<string>;
  Tests(box: string, tier: string): Promise<string>;
  Start(kind: string, box: string, tier: string, test: string, base?: string): Promise<string>;
  Status(): Promise<string>;
  Cancel(): Promise<string>;
}

export const getDebugChecks = (): DebugChecksHost | undefined =>
  window.chrome?.webview?.hostObjects?.debugChecks;

// Invariant: Start takes a JSON object of string params; a tool that writes gets its folder under the key outputDir.
export interface DebugToolsHost {
  Context(): Promise<string>;
  List(): Promise<string>;
  Start(toolId: string, paramsJson: string): Promise<string>;
  Status(): Promise<string>;
  Cancel(): Promise<string>;
}

export const getDebugTools = (): DebugToolsHost | undefined =>
  window.chrome?.webview?.hostObjects?.debugTools;

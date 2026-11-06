



export interface Backend {

  GetConflicts(): Promise<string>;
  GetConflictMatrix(formKey: string): Promise<string>;
  GetRecordTree(plugin: string, id: string): Promise<string>;
  GetReferencedBy(formKey: string): Promise<string>;
  GetProblems(formKey: string): Promise<string>;
  SearchRecords(query: string, typeFilter: string): Promise<string>;
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
  CheckForErrors(plugin: string, limit: number): Promise<string>;
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
  Name: string; Kind: PluginKind; LoadOrder: number; Editable: boolean; Size: number;
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
export type WeaponDamageTypes = { formKey: string; damageTypes: WeaponDamageType[] };
export const TYPED_RECORD_TYPES = ['LVLI', 'LVLN', 'LVSP', 'NPC_', 'FACT', 'WEAP'];


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


export interface PapyrusHost {
  BrowseForFile(title: string, filter: string): Promise<string>;
  BrowseForFolder(title: string): Promise<string>;


  Compile(source: string, output: string, imports: string, flags: string,
          all: boolean, optimize: boolean, release: boolean, compilerPath: string,
          engine: string): Promise<string>;

  Decompile(source: string, output: string, assembly: boolean, write: boolean): Promise<string>;

  OpenFolder(path: string): Promise<string>;

  StageDroppedFile(name: string, base64: string): Promise<string>;


  LookupFunction(script: string, functionName: string): Promise<string>;

  LookupScriptInfo(script: string): Promise<string>;






  Analyze(text: string, path: string): Promise<string>;

  SymbolAt(text: string, path: string, offset: number, imports: string): Promise<string>;

  Complete(text: string, path: string, offset: number, imports: string): Promise<string>;

  SignatureHelp(text: string, path: string, offset: number, imports: string): Promise<string>;

  CheckText(text: string, path: string, revision: number, imports: string): Promise<string>;

  References(text: string, path: string, offset: number, imports: string): Promise<string>;

  Rename(text: string, path: string, offset: number, newName: string, imports: string): Promise<string>;

  WorkspaceSymbols(query: string, limit: number, imports: string): Promise<string>;

  ReadScript(path: string): Promise<string>;

  WriteScript(path: string, text: string): Promise<string>;
}


export interface PapyrusDiagnostic {
  code: string;
  severity: 'error' | 'warning';
  message: string;
  line: number;
  column: number;
  start: number;
  length: number;
}


export interface PapyrusSymbol {
  name: string;
  kind: string;
  signature: string;
  documentation: string | null;
  container: string | null;
  line: number;
  column: number;
  start: number;
  nameStart: number;
  nameLength: number;
  nameLine: number;
}

export interface PapyrusAnalyzeResult {
  script?: string;
  extends?: string | null;
  errorCount?: number;
  diagnostics?: PapyrusDiagnostic[];
  symbols?: PapyrusSymbol[];

  error?: string;
}


export interface PapyrusSymbolResult {
  resolved: boolean;
  name?: string;
  kind?: string;
  signature?: string;
  documentation?: string | null;
  container?: string | null;
  file?: string | null;

  sameFile?: boolean;
  line?: number;
  column?: number;
  start?: number;
  length?: number;
  error?: string;
}


export interface PapyrusCompletionItem {
  label: string;
  kind: string;
  detail: string;
  insertText: string;
  documentation: string | null;
}

export interface PapyrusCompletionResult {
  context?: string;
  replaceStart?: number;
  replaceLength?: number;
  items?: PapyrusCompletionItem[];
  error?: string;
}

export interface PapyrusSignatureParameter {
  label: string;
  documentation: string | null;
}

export interface PapyrusSignature {
  label: string;
  documentation: string | null;
  parameters: PapyrusSignatureParameter[];
}

export interface PapyrusSignatureResult {
  active: boolean;
  signatures?: PapyrusSignature[];
  activeSignature?: number;
  activeParameter?: number;
  error?: string;
}

export interface PapyrusHint {
  code: string;
  severity: 'hint';
  unnecessary: boolean;
  message: string;
  line: number;
  column: number;
  start: number;
  length: number;
}

export interface PapyrusCheckResult {
  revision?: number;
  complete?: boolean;
  errorCount?: number;
  diagnostics?: PapyrusDiagnostic[];
  hints?: PapyrusHint[];
  error?: string;
}

export interface PapyrusReferenceHit {
  file: string | null;
  line: number;
  column: number;
  start: number;
  length: number;
  isDeclaration: boolean;
}

export interface PapyrusReferencesResult {
  resolved?: boolean;
  reason?: string;
  declaration?: { name: string; kind: string; signature: string; file: string | null; line: number; column: number; start: number; length: number };
  references?: PapyrusReferenceHit[];
  error?: string;
}

export interface PapyrusRenameEdit {
  file: string | null;
  start: number;
  length: number;
  newText: string;
}

export interface PapyrusRenameResult {
  ok?: boolean;
  reason?: string;
  edits?: PapyrusRenameEdit[];
  fileRename?: { from: string; to: string } | null;
  error?: string;
}

export interface PapyrusWorkspaceSymbol {
  name: string;
  kind: string;
  signature: string;
  container: string | null;
  file: string | null;
  line: number;
  column: number;
  start: number;
  length: number;
}

export interface PapyrusWorkspaceResult {
  truncated?: boolean;
  results?: PapyrusWorkspaceSymbol[];
  error?: string;
}


export interface NifHost {
  BrowseForFile(title: string, filter: string): Promise<string>;
  BrowseForFolder(title: string): Promise<string>;
  BrowseForSave(title: string, filter: string): Promise<string>;

  Import(objPath: string, outNif: string, material: string, texDiffuse: string,
         texNormal: string, collision: boolean, fromBlender: boolean): Promise<string>;
  Inspect(nifPath: string): Promise<string>;
  Geo(nifPath: string): Promise<string>;


  GetTexture(nifPath: string, relTexPath: string, textureRoot: string): Promise<string>;
  Verify(nifPath: string): Promise<string>;
  Fix(nifPath: string, outNif: string): Promise<string>;
  Tree(nifPath: string): Promise<string>;

  ApplyEdits(nifPath: string, editsJson: string, outNif: string): Promise<string>;
  OpenFolder(path: string): Promise<string>;
  StageDroppedFile(name: string, base64: string): Promise<string>;
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

export interface SwfHost {
  Open(path: string): Promise<string>;
  Characters(): Promise<string>;
  FrameLabels(timeline: number): Promise<string>;
  Frame(timeline: number, frame: number): Promise<string>;
  Timeline(index: number): Promise<string>;
  RenameLabel(timeline: number, labelIndex: number, name: string): Promise<string>;
  Undo(): Promise<string>;
  Redo(): Promise<string>;
  State(): Promise<string>;
  Save(): Promise<string>;
  AddLabel(frame: number, name: string, timeline: number): Promise<string>;
  RemoveLabel(timeline: number, labelIndex: number): Promise<string>;
  Placements(): Promise<string>;
  SetDepth(tagIndex: number, depth: number): Promise<string>;
  PlacementMatrix(tagIndex: number): Promise<string>;
  SetTransform(tagIndex: number, x: number, y: number, scaleX: number, scaleY: number, rotate0: number, rotate1: number): Promise<string>;
  InsertKeyframe(tagIndex: number, frame: number): Promise<string>;
  ReadEditText(tagIndex: number): Promise<string>;
  EditEditText(tagIndex: number, text: string, colorHex: string, x0: number, x1: number, y0: number, y1: number): Promise<string>;
  AbcBlocks(): Promise<string>;
  Disassemble(abc: number, methodIndex: number): Promise<string>;
  ShapeSvg(characterId: number): Promise<string>;
  FrameSvg(frame: number): Promise<string>;
  BitmapPng(tagIndex: number): Promise<string>;
  BitmapJpeg(tagIndex: number): Promise<string>;
  InspectText(): Promise<string>;
  ActionTags(): Promise<string>;
  Avm1Disassemble(tagIndex: number): Promise<string>;
  BrowseForFile(title: string, filter: string): Promise<string>;
  ArchiveSwfs(): Promise<string>;
  OpenArchived(name: string): Promise<string>;
  SaveAs(title: string, filter: string): Promise<string>;
  AbcText(abc: number): Promise<string>;
  ApplyAbcText(abc: number, text: string): Promise<string>;
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

  ExtractSelected(archivePath: string, innerPathsJson: string, outDir: string): Promise<string>;
  ExtractAll(archivePath: string, outDir: string, filter: string, limit: number, filterMode: ArchiveFilterMode): Promise<string>;
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
}
export interface CellReferencesResult {
  cellFormKey?: string;
  cellEditorId?: string;
  cellName?: string;
  interior?: boolean;
  referenceCount?: number;
  withModelCount?: number;
  references?: CellPlacedReference[];
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
  SearchWorldspaces(query: string, limit: number): Promise<string>;
  ResolveWorldspace(cellId: string): Promise<string>;
  ResolveWorldspaceId(worldspace: string): Promise<string>;

  GetGeometryBatch(relModelPathsJson: string): Promise<string>;
  GetPlugins(): Promise<string>;
  SearchCells(query: string, limit: number): Promise<string>;

  GetTexture(relModelPath: string, relTexPath: string): Promise<string>;

  GetGeometryBatchProgress(): Promise<string>;




  SetPlacedReferenceTransform(formKey: string, patchPlugin: string,
    x: number, y: number, z: number, rx: number, ry: number, rz: number): Promise<string>;
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
          material?: MaterialHost;
          masters?: MastersHost;
          archive?: ArchiveHost;
          audio?: AudioHost;
          cell?: CellHost;
          graph?: GraphHost;
          strings?: StringsHost;
          swf?: SwfHost;
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

export const getSwf = (): SwfHost | undefined =>
  window.chrome?.webview?.hostObjects?.swf;

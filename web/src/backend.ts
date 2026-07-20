// Typed access to the C# host objects exposed to the WebView2 page via AddHostObjectToScript.
// Every call is async: the WebView2 host-object proxy returns a Promise for each method.

/** The "backend" bridge (FO4RecordEditor.Services.BackendInterop) — conflicts, records, edits, saves. */
export interface Backend {
  // ---- read ----
  GetConflicts(): Promise<string>;                          // JSON: ConflictEntry[]
  GetConflictMatrix(formKey: string): Promise<string>;      // JSON: ConflictMatrix
  GetRecordTree(plugin: string, id: string): Promise<string>; // JSON: FieldNode[]
  GetReferencedBy(formKey: string): Promise<string>;        // JSON: RefByEntry[]
  GetProblems(formKey: string): Promise<string>;            // JSON: RecordProblem[]
  SearchRecords(query: string, typeFilter: string): Promise<string>;  // JSON: SearchHit[]
  GetEditablePlugins(): Promise<string>;                    // JSON: string[]

  // ---- edit (in-memory until a save) ----
  OpenPlugin(plugin: string): Promise<string>;
  CreatePlugin(name: string): Promise<string>;
  SetField(plugin: string, record: string, field: string, value: string): Promise<string>;
  SetComponents(plugin: string, record: string, componentsJson: string): Promise<string>;
  SetConditions(plugin: string, record: string, conditionsJson: string): Promise<string>;
  AddListItem(plugin: string, record: string, field: string, value: string): Promise<string>;
  RemoveListItem(plugin: string, record: string, field: string, value: string): Promise<string>;
  DeleteRecord(plugin: string, id: string): Promise<string>;
  CopyAsOverride(sourcePlugin: string, id: string, patchPlugin: string): Promise<string>;
  CopyAsOverrideMany(itemsJson: string, patchPlugin: string): Promise<string>;
  RevertOverrides(badPlugin: string, patchPlugin: string, signature: string,
                  containsComponent: string, apply: boolean, limit: number): Promise<string>;
  CompactToEsl(plugin: string): Promise<string>;
  CleanPlugin(plugin: string): Promise<string>;
  RenumberFormId(plugin: string, record: string, newId: string): Promise<string>;

  // ---- save ----
  ResolveConflict(formKey: string, winner: string, patch: string): Promise<string>;
  SavePatch(patch: string): Promise<string>;
  SavePlugin(plugin: string, path: string): Promise<string>;
}

/** The "appInterop" bridge (FO4RecordEditor.Services.AppInterop) — explorer tree + env loading. */
export interface AppInterop {
  BrowseForMo2Folder(): Promise<string>;   // native folder picker; "" if cancelled/invalid
  OpenMo2Profile(instancePath: string): Promise<void>;
  LoadEnvironment(): Promise<void>;
  ScanConflicts(): Promise<string>;          // full load-order conflict scan (cached); returns a summary
  ScanBrokenRefs(): Promise<string>;         // full modlist broken-ref scan; returns full text report
  RefreshTree(): Promise<string>;            // rebuild the tree from current state (incl. AI edits); JSON plugin nodes
  GetPlugins(): Promise<string>;            // JSON: plugin nodes
  GetChildren(path: string): Promise<string>;
  OpenRecord(path: string): Promise<string>;   // resolves the node's FormKey ("" if unresolved)
}

/** xEdit-style conflict matrix from Backend.GetConflictMatrix (matches Models/ConflictMatrix). */
export interface ConflictFieldRow {
  Field: string;
  DisplayLabel: string;
  Level: number;
  Values: string[];      // one per plugin column, parallel to ConflictMatrix.Plugins ("" = unset)
  Differs: boolean;
  Statuses: string[];    // per-plugin status parallel to Values: notdefined|master|identical|win|override|lose|only
  Severity: string;      // row severity: none|override|conflict|critical
  IsSummary: boolean;    // a condition/component entry: collapse its sub-fields by default
  HasChildren: boolean;  // deeper rows belong to this one (show an expand chevron)
  EditKind: 'Text' | 'Bool' | 'Enum' | 'Ref';   // how a value cell edits ('Ref' = record picker)
  EnumOptions: string[] | null;          // dropdown options when EditKind === 'Enum'
  RefType: string | null;                // short display label when EditKind === 'Ref'
  RefTypes: string | null;               // csv of concrete target record types (picker filter)
}

export interface RefByEntry { Plugin: string; FormKey: string; EditorID: string; Type: string; }
export interface RecordProblem { Severity: string; Description: string; }
export interface SearchHit { FormKey: string; EditorID: string; Type: string; Plugin: string; }
export interface ConflictMatrix {
  FormKey: string;
  EditorID: string;
  Type: string;
  Winner: string;        // winning plugin (last in load order)
  Plugins: string[];     // columns, in load order
  Rows: ConflictFieldRow[];
  Level: string;         // record rollup: onlyone|noconflict|override|conflict|critical
}

/** One conflicting record across the load order (matches Models/ConflictEntry). */
export interface ConflictEntry {
  FormKey: string;
  EditorID: string;
  Type: string;
  Plugins: string[];
  Winner: string;
  InvolvesMod: boolean;
}

/** Shape of each node from Backend.GetRecordTree (matches BackendInterop.NodeDto). */
export interface FieldNode {
  key: string;
  label: string;
  value: string;
  values: Record<string, string>;             // value per plugin column
  editKind: "Text" | "Bool" | "Enum";
  enumOptions: string[] | null;               // dropdown options when editKind === "Enum"
  isSummary: boolean;
  hasChildren: boolean;
  children: FieldNode[];
}

/** The "chat" bridge (FO4RecordEditor.Services.ChatInterop) — the Claude AI assistant.
 *  Replies stream back as web messages, not return values: listen on chrome.webview 'message'
 *  for {Type:"AiToken",Text} / {Type:"AiToolStatus",Text} / {Type:"AiDone"[,Stopped]} / {Type:"AiError",Text}. */
export interface Chat {
  SendMessage(sessionId: string, text: string, imagesJson: string): Promise<void>;   // imagesJson: JSON array of data-URL/base64 images
  CancelMessage(sessionId: string): Promise<void>;
  ResetChat(): Promise<void>;
  IsAgentReady(): Promise<boolean>;
  // sessions (persisted under %AppData%\FO4RecordEditor\Chats)
  ListSessions(): Promise<string>;            // JSON: [{id,name,createdAt,count}]
  NewSession(): Promise<string>;              // JSON: {id,name,messages:[]}
  LoadSession(id: string): Promise<string>;   // JSON: {id,name,messages:[{isUser,text}]}
  RenameSession(id: string, name: string): Promise<string>;
  DeleteSession(id: string): Promise<string>; // returns updated ListSessions JSON
  ForkSession(id: string): Promise<string>;   // summarize -> new chat seeded with the summary; JSON session
  GetCommands(): Promise<string>;             // JSON: [{name,args,help}]
}

export interface ChatSessionMeta { id: string; name: string; createdAt: string; count: number; }
export interface ChatSessionMsg { isUser: boolean; text: string; }
export interface ChatSessionFull { id: string; name: string; messages: ChatSessionMsg[]; }
export interface SlashCommand { name: string; args: string; help: string; }

/** The "papyrus" bridge (FO4RecordEditor.Services.PapyrusInterop) — compile/decompile Papyrus. */
export interface PapyrusHost {
  BrowseForFile(title: string, filter: string): Promise<string>;   // native file picker; "" if cancelled
  BrowseForFolder(title: string): Promise<string>;                 // native folder picker; "" if cancelled
  // Compile .psc -> .pex via the CK PapyrusCompiler. Returns compiler output + exit code.
  Compile(source: string, output: string, imports: string, flags: string,
          all: boolean, optimize: boolean, release: boolean, compilerPath: string): Promise<string>;
  // Decompile .pex -> .psc (built-in). For a single file returns source text inline unless write=true.
  Decompile(source: string, output: string, assembly: boolean, write: boolean): Promise<string>;
  // Open a folder (or a file's folder) in Windows Explorer. Returns "" on success.
  OpenFolder(path: string): Promise<string>;
  // Write a dropped file's bytes to a temp path and return it (drag-and-drop helper). "ERR:" on fail.
  StageDroppedFile(name: string, base64: string): Promise<string>;
}

/** The "nif" bridge (FO4RecordEditor.Services.NifInterop) — author/inspect/verify/repair FO4 NIFs. */
export interface NifHost {
  BrowseForFile(title: string, filter: string): Promise<string>;   // native file picker; "" if cancelled
  BrowseForFolder(title: string): Promise<string>;                 // native folder picker; "" if cancelled
  BrowseForSave(title: string, filter: string): Promise<string>;   // native save picker; "" if cancelled
  // Author a static FO4 NIF from an OBJ. Returns niftool's RESULT + verify output.
  Import(objPath: string, outNif: string, material: string, texDiffuse: string,
         texNormal: string, collision: boolean, fromBlender: boolean): Promise<string>;
  Inspect(nifPath: string): Promise<string>;   // JSON summary
  Geo(nifPath: string): Promise<string>;        // full geometry JSON (flat arrays) for the 3D viewer
  // Resolve a NIF texture (game-relative path) to a PNG data URL via texconv; "" if not found.
  // textureRoot: optional user-picked Data/Textures folder tried before auto-resolution.
  GetTexture(nifPath: string, relTexPath: string, textureRoot: string): Promise<string>;
  Verify(nifPath: string): Promise<string>;    // RESULT: + [ok]/[FAIL] checks
  Fix(nifPath: string, outNif: string): Promise<string>;  // repair + report actions
  Tree(nifPath: string): Promise<string>;      // curated editable property tree (JSON) for Edit mode
  // Apply a JSON array of field edits and save. outNif blank = save in place. Returns RESULT text.
  ApplyEdits(nifPath: string, editsJson: string, outNif: string): Promise<string>;
  OpenFolder(path: string): Promise<string>;   // reveal in Explorer; "" on success
  StageDroppedFile(name: string, base64: string): Promise<string>;  // drag-drop helper; "ERR:" on fail
}

/** One row in a MastersListResult (matches WriteService.ListMastersJson's anonymous shape). */
export interface MasterRow {
  index: number;
  name: string;
  size: number | null;   // null = not found on disk / not in the loaded environment
  used: boolean;          // false = save_plugin's Iterate would drop this master
}
export interface MastersListResult {
  pluginName?: string;
  masters?: MasterRow[];
  light?: boolean;        // current ESL/Small header flag
  error?: string;
}

/** The "masters" bridge (FO4RecordEditor.Services.MastersInterop) — master table + ESL flag. */
export interface MastersHost {
  GetPlugins(): Promise<string>;                       // JSON: string[] of plugin names
  List(plugin: string): Promise<string>;                // JSON: MastersListResult
  // orderJson: JSON array of master names in the new order (must be an exact permutation). Writes immediately.
  Reorder(plugin: string, orderJson: string): Promise<string>;
  SetLight(plugin: string, light: boolean): Promise<string>;   // in memory until SavePlugin
  SavePlugin(plugin: string): Promise<string>;
}

/** One entry in an ArchiveListResult (matches ArchiveService.ListArchiveJson's anonymous shape). */
export interface ArchiveEntry { path: string; size: number; }
export interface ArchiveListResult {
  archiveName?: string;
  totalCount?: number;
  shownCount?: number;
  truncated?: boolean;
  entries?: ArchiveEntry[];
  error?: string;
}

/** The "archive" bridge (FO4RecordEditor.Services.ArchiveInterop) — list/extract BA2/BSA contents. */
export interface ArchiveHost {
  BrowseForFile(title: string, filter: string): Promise<string>;   // native file picker; "" if cancelled
  BrowseForFolder(title: string): Promise<string>;                 // native folder picker; "" if cancelled
  List(archivePath: string, filter: string, limit: number): Promise<string>;   // JSON: ArchiveListResult
  ExtractFile(archivePath: string, innerPath: string, outPath: string): Promise<string>;
  // innerPathsJson: JSON array of the panel's selected entry paths.
  ExtractSelected(archivePath: string, innerPathsJson: string, outDir: string): Promise<string>;
  ExtractAll(archivePath: string, outDir: string, filter: string, limit: number): Promise<string>;
  OpenFolder(path: string): Promise<string>;   // reveal in Explorer; "" on success
}

/** One field in a MaterialField list (matches MaterialService.CollectFields' anonymous shape). */
export interface MaterialField {
  name: string;
  section: 'material' | 'header';
  type: 'bool' | 'float' | 'int' | 'string' | 'color';
  value: string;   // 'true'/'false' for bool, a number as text for float/int, 'r, g, b' for color
}
export interface MaterialInspectResult {
  fileName?: string;
  version?: number;
  fields?: MaterialField[];
  error?: string;
}

/** The "material" bridge (FO4RecordEditor.Services.MaterialInterop) — inspect/edit .bgsm shader fields. */
export interface MaterialHost {
  BrowseForFile(title: string, filter: string): Promise<string>;   // native file picker; "" if cancelled
  Inspect(path: string): Promise<string>;    // JSON: MaterialInspectResult
  // fieldsJson: {"FieldName":"newValue", ...}. outPath blank = overwrite in place.
  SetFields(path: string, fieldsJson: string, outPath: string): Promise<string>;
  OpenFolder(path: string): Promise<string>;   // reveal in Explorer; "" on success
  StageDroppedFile(name: string, base64: string): Promise<string>;  // drag-drop helper; "ERR:" on fail
}

/** The "settings" bridge (FO4RecordEditor.Services.SettingsInterop) — the app settings panel. */
export interface SettingsHost {
  GetSettings(): Promise<string>;            // JSON of editable settings
  SaveSettings(json: string): Promise<string>;  // persists + rebuilds AI provider; returns status
  BrowseFolder(title: string, current: string): Promise<string>;  // native folder picker; "" if cancelled
  TestClaude(path: string): Promise<string>; // verifies the Claude Code CLI
}

declare global {
  interface Window {
    chrome?: {
      webview?: {
        hostObjects?: {
          backend?: Backend;
          appInterop?: AppInterop;
          chat?: Chat;
          settings?: SettingsHost;
          papyrus?: PapyrusHost;
          nif?: NifHost;
          material?: MaterialHost;
          masters?: MastersHost;
          archive?: ArchiveHost;
        };
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

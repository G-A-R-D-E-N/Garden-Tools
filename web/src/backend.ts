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
  RevertOverrides(badPlugin: string, patchPlugin: string, signature: string,
                  containsComponent: string, apply: boolean, limit: number): Promise<string>;

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
        };
      };
    };
  }
}

export const getBackend = (): Backend | undefined =>
  window.chrome?.webview?.hostObjects?.backend;

export const getAppInterop = (): AppInterop | undefined =>
  window.chrome?.webview?.hostObjects?.appInterop;

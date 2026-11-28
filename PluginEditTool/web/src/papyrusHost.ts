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

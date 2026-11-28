export interface PrevisIssue {
  code: string;
  detail: string;
}

export interface PrevisCellReport {
  cell: string;
  plugin: string;
  issues: PrevisIssue[];
  outdated: PrevisIssue[];
  skips: string[];
}

export interface PrevisScanSummary {
  cells: number;
  broken: number;
  outdated: number;
  skipped: number;
  cdxFiles: number;
  cdxRows: number;
  csgFiles: number;
  csgRows: number;
  csgChunks: number;
  tomeFiles: number;
  ocNifFiles: number;
  problems: number;
}

export interface PrevisScanResult {
  dataDir: string;
  summary: PrevisScanSummary;
  pendingPlugins: string[];
  problems: string[];
  broken: PrevisCellReport[];
  outdated: PrevisCellReport[];
  skipped: PrevisCellReport[];
  truncated: boolean;
}

export interface PrevisDataFolder {
  dataDir: string;
  source?: string;
}

export interface PrevisPatchStep {
  id: string;
  title: string;
  detail: string;
  status: 'done' | 'partial' | 'manual' | 'blocked';
}

export interface PrevisGenerateGroup {
  ocId: number;
  ocIdText: string;
  modelPath: string;
  instanceCount: number;
  path: string;
}

export interface PrevisGenerateResult {
  cell: string;
  plugin: string;
  dryRun: boolean;
  pcmbStamp: number;
  visiStamp: number;
  meshIds: number;
  references: number;
  groups: PrevisGenerateGroup[];
  cdx?: { path: string; rows: number; source?: string };
  shell?: { path: string; autoResolved: boolean; candidates: number };
  warnings?: string[];
  complete?: boolean;
  ok?: boolean;
  limitations?: string[];
  planSkipped?: { foreignPlugin?: number };
}

export interface PrevisHost {
  Scan(dataDir: string): Promise<PrevisScanResult>;
  GetDataFolder(): Promise<PrevisDataFolder>;
  PatchWorkflow?(): Promise<PrevisPatchStep[]>;
  PatchStamp?(
    patchPlugin: string,
    cell: string,
    sourcePlugin: string,
    pcmb: string,
    visi: string,
    xcriJson: string,
    apply: boolean,
  ): Promise<unknown>;
  PatchDisable?(cell: string, patchPlugin: string, sourcePlugin: string, apply: boolean): Promise<unknown>;
  PatchPlan?(cell: string, plugin: string, minInstances?: number): Promise<unknown>;
  ResolveOcTemplate?(
    cell: string,
    sourcePlugin?: string,
    patchPlugin?: string,
  ): Promise<{
    cell: string;
    cellPrefix: string;
    dataDir: string;
    templates: { path: string; relative: string; plugin: string; ocId: string }[];
    suggestedSourceNif: string;
    suggestedOutputOc: string;
    truncated: boolean;
  }>;
  PatchBake?(
    sourceNif: string,
    outputOc: string,
    placementsOrPlan?: unknown,
    planGroupIndex?: number,
    ocShellTemplate?: string,
  ): Promise<unknown>;
  Generate?(
    patchPlugin: string,
    cell: string,
    sourcePlugin?: string,
    ocShell?: string,
    minInstances?: number,
    apply?: boolean,
  ): Promise<PrevisGenerateResult>;
  PatchWriteContainer?(kind: string, sourcePath: string, outputPath: string): Promise<unknown>;
  PatchSave?(patchPlugin: string): Promise<unknown>;
}



export const getPrevis = (): PrevisHost | undefined =>
  window.chrome?.webview?.hostObjects?.previs;

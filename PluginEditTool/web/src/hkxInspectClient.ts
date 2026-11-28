import type { HkxHost } from './backend';
import './hkxHost';

// Constraint: the additions stay optional, because older host builds and test doubles carry only the pose RPCs.
declare module './backend' {
  interface HkxHost {
    InspectObject?(path: string, object: number, limit?: number | null): Promise<string>;
    ObjectTree?(path: string, depth?: number | null, limit?: number | null): Promise<string>;
    PackfileDiagnostics?(path: string): Promise<string>;
    RepackCheck?(path: string): Promise<string>;
    SearchHkx?(path: string, query: string, limit?: number | null): Promise<string>;
    AuditChannels?(skeletonPath: string, animations: string, limit?: number | null): Promise<string>;
    SymbolUsage?(path: string, limit?: number | null): Promise<string>;
  }
}

export interface HkxObjectLink {
  source_object: number;
  source_offset: number;
  member: string;
  target_section: number;
  target_offset: number;
  target_object: number;
  target_class: string;
  inner: boolean;
}

export interface HkxObjectDetail {
  object: number;
  object_count: number;
  offset: number;
  signature: string;
  class_name: string;
  size: number;
  layout_known: boolean;
  fields: { name: string; offset: number; size: number; hex: string; type?: string; value?: string; reason?: string }[];
  field_total: number;
  outgoing: HkxObjectLink[];
  outgoing_total: number;
  incoming: HkxObjectLink[];
  incoming_total: number;
  truncated: boolean;
  partial: boolean;
  note: string;
}

export interface HkxTreeRow {
  node: number;
  object: number;
  parent: number;
  depth: number;
  class_name: string;
  name: string;
  animation: string;
  offset: number;
  member: string;
  summary?: string;
  repeated: boolean;
  cut: boolean;
}

export interface HkxObjectTree {
  object_count: number;
  roots: number[];
  nodes: HkxTreeRow[];
  unreachable: number;
  truncated: boolean;
  partial: boolean;
  note: string;
}

export interface HkxPackfileReport {
  readable: boolean;
  status?: string;
  file_version?: number;
  pointer_size?: number;
  object_count?: number;
  object_links?: number;
  inner_links?: number;
  external_links?: number;
  unreferenced_objects?: number;
  sections?: { tag: string; size: number; local_fixups: number; global_fixups: number; virtual_fixups: number }[];
  classes?: { name: string; signature: string; count: number; layout_status: string; layout_size: number; shortest_span: number }[];
  issues: string[];
}

export interface HkxRepackReport {
  verdict: 'equal' | 'read_failed' | 'refused' | 'rewrite_failed' | 'reread_failed' | 'differs';
  model_equal: boolean;
  status: string;
  bytes_identical: boolean;
  source_size: number;
  rewritten_size: number;
  first_difference: string;
  reason?: string;
}

export interface HkxSearchHit {
  file: string;
  kind: string;
  text: string;
  object: number;
}

export interface HkxSearchReport {
  file_count: number;
  skipped: number;
  hits: HkxSearchHit[];
  hit_count: number;
  truncated: boolean;
}

export interface HkxChannelFile {
  file: string;
  tracks: number;
  offset_translation: number;
  offset_rotation: number;
  offset_scale: number;
  max_translation_offset: number;
  max_rotation_degrees: number;
  offsets: { track: number; bone_name: string; translation_undriven: boolean; rotation_undriven: boolean; scale_undriven: boolean }[];
  offset_total: number;
}

export interface HkxChannelReport {
  skeleton: string;
  file_count: number;
  audited: number;
  with_offsets: number;
  skipped: number;
  files: HkxChannelFile[];
  skipped_files: { file: string; reason: string }[];
  truncated: boolean;
}

export interface HkxEventUsage {
  event_id: number;
  name: string;
  in_range: boolean;
  no_decoded_site: boolean;
  site_total: number;
  sites: { site: string; node: number; state: number; owner: string; object: number; member: string }[];
}

export interface HkxUsageReport {
  events: HkxEventUsage[];
  variables: { variable_index: number; name: string; in_range: boolean; no_decoded_site: boolean; site_total: number;
               sites: { owner_kind: string; owner_index: number; member_path: string }[] }[];
  states: { state: number; name: string; state_id: number; incoming: number; outgoing: number; is_start: boolean }[];
  partial: boolean;
  note: string;
}

function parse<T>(raw: unknown, required: string[]): T {
  const value = (typeof raw === 'string' ? JSON.parse(raw) : raw) as (Record<string, unknown> & { error?: string }) | null;
  if (value === null || typeof value !== 'object') throw new Error('the host returned no inspection reply');
  if (value.error) throw new Error(value.error);
  for (const key of required) {
    if (!(key in value)) throw new Error(`inspection reply has no ${key}`);
  }
  return value as unknown as T;
}

const missing = (what: string) => new Error(`this host build cannot ${what}`);

export async function readObject(host: HkxHost, path: string, object: number): Promise<HkxObjectDetail> {
  if (!host.InspectObject) throw missing('inspect an HKX object');
  return parse<HkxObjectDetail>(await host.InspectObject(path, object, 200), ['fields', 'incoming', 'outgoing']);
}

export async function readObjectTree(host: HkxHost, path: string): Promise<HkxObjectTree> {
  if (!host.ObjectTree) throw missing('walk an HKX object tree');
  return parse<HkxObjectTree>(await host.ObjectTree(path, null, 2000), ['nodes']);
}

export async function readPackfile(host: HkxHost, path: string): Promise<HkxPackfileReport> {
  if (!host.PackfileDiagnostics) throw missing('diagnose an HKX packfile');
  return parse<HkxPackfileReport>(await host.PackfileDiagnostics(path), ['readable', 'issues']);
}

export async function readRepackCheck(host: HkxHost, path: string): Promise<HkxRepackReport> {
  if (!host.RepackCheck) throw missing('check an HKX packfile round trip');
  return parse<HkxRepackReport>(await host.RepackCheck(path), ['verdict', 'model_equal']);
}

export async function searchHkx(host: HkxHost, path: string, query: string): Promise<HkxSearchReport> {
  if (!host.SearchHkx) throw missing('search HKX files');
  return parse<HkxSearchReport>(await host.SearchHkx(path, query, 500), ['hits']);
}

export async function auditChannels(host: HkxHost, skeletonPath: string, animations: string): Promise<HkxChannelReport> {
  if (!host.AuditChannels) throw missing('audit animation channels');
  return parse<HkxChannelReport>(await host.AuditChannels(skeletonPath, animations, 500), ['files', 'skipped_files']);
}

export async function readSymbolUsage(host: HkxHost, path: string): Promise<HkxUsageReport> {
  if (!host.SymbolUsage) throw missing('list HKX symbol usage');
  return parse<HkxUsageReport>(await host.SymbolUsage(path, 500), ['events', 'states']);
}

const count = (n: number, one: string, many: string) => `${n} ${n === 1 ? one : many}`;

export const describeObject = (d: HkxObjectDetail): string =>
  `object ${d.object} of ${d.object_count}: ${d.class_name}, ${count(d.incoming_total, 'incoming reference', 'incoming references')}, `
  + `${count(d.outgoing_total, 'pointer', 'pointers')}${d.truncated ? ' (lists cut)' : ''}`;

export const describeTree = (t: HkxObjectTree): string =>
  `${count(t.nodes.length, 'row', 'rows')} for ${count(t.object_count, 'object', 'objects')}; ${t.unreachable} not reached${t.truncated ? ' (cut at the row limit)' : ''}`;

export const describePackfile = (r: HkxPackfileReport): string =>
  r.readable ? `${count(r.object_count ?? 0, 'object', 'objects')}, ${count(r.issues.length, 'issue', 'issues')}` : 'the packfile reader refused this file';

const REPACK_TEXT: Record<HkxRepackReport['verdict'], string> = {
  equal: 'the rewrite reads back as the same packfile',
  read_failed: 'the packfile reader refused this file',
  refused: 'the writer does not lay out this packfile (not a corruption)',
  rewrite_failed: 'the writer failed on this packfile',
  reread_failed: 'the rewritten file could not be read back',
  differs: 'the rewrite reads back differently',
};

export const describeRepack = (r: HkxRepackReport): string =>
  `${REPACK_TEXT[r.verdict] ?? r.verdict}${r.first_difference ? `: ${r.first_difference}` : ''}${r.reason ? ` (${r.reason})` : ''}`;

export const describeSearch = (r: HkxSearchReport): string =>
  `${count(r.hits.length, 'hit', 'hits')} in ${count(r.file_count, 'file', 'files')}${r.skipped ? `, ${r.skipped} could not be read` : ''}${r.truncated ? ' (more hits than the limit)' : ''}`;

export const describeChannels = (r: HkxChannelReport): string =>
  `${r.audited} audited, ${r.with_offsets} with an undriven offset, ${r.skipped} skipped`;

export const describeUsage = (r: HkxUsageReport): string =>
  `${count(r.events.length, 'event', 'events')}, ${r.events.filter(e => e.no_decoded_site).length} with no decoded site; ${r.partial ? 'sites the class table does not type are not decoded' : 'complete'}`;

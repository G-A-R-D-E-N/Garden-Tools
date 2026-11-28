import type { HkxHost } from './backend';
import './hkxAnimationIndexHost';

export type HkxAnimationKind = 'animation' | 'behaviour' | 'project' | 'character' | 'skeleton' | 'other';

export interface HkxAnimationProvider {
  label: string;
  origin: 'loose' | 'archive';
  source: string;
  file: string;
}

export interface HkxAnimationEntry {
  path: string;
  kind: HkxAnimationKind;
  project: string;
  race: string;
  character: string;
  folder: string;
  size: number;
  provider: HkxAnimationProvider;
  shadowed: HkxAnimationProvider[];
}

export interface HkxAnimationFacet {
  values: string[];
  hidden: number;
}

export interface HkxAnimationActor {
  actor_root: string;
  skeleton: string | null;
  skeleton_provider: HkxAnimationProvider | null;
  skeleton_candidates: string[];
  skeleton_nif: string | null;
  skeleton_nif_provider: HkxAnimationProvider | null;
  body: string | null;
  body_provider: HkxAnimationProvider | null;
  body_candidates: string[];
  body_candidate_total: number;
  notes: string[];
}

export interface HkxAnimationIndexReply {
  manager: string;
  profile: string;
  data_folder: string;
  sources: HkxAnimationProvider[];
  notes: string[];
  total: number;
  counts: Record<string, number>;
  matched: number;
  offset: number;
  limit: number;
  truncated: boolean;
  entries: HkxAnimationEntry[];
  facets: { projects: HkxAnimationFacet; races: HkxAnimationFacet; characters: HkxAnimationFacet };
  actor?: HkxAnimationActor;
}

export interface HkxAnimationIndexRequest {
  dataFolder: string;
  mo2Folder: string;
  profile: string;
  kind: string;
  project: string;
  race: string;
  character: string;
  query: string;
  resolvePath: string;
  offset: number;
  limit: number;
  refresh: boolean;
}

// Requires the host to carry the animation index RPC; an older host build reports why instead of showing an empty index.
export async function readAnimationIndex(host: HkxHost, request: HkxAnimationIndexRequest): Promise<HkxAnimationIndexReply> {
  if (!host.AnimationIndex) throw new Error('this host build cannot index animations');
  const raw = await host.AnimationIndex(
    request.dataFolder, request.mo2Folder, request.profile, request.kind, request.project, request.race,
    request.character, request.query, request.resolvePath, request.offset, request.limit, request.refresh);
  const reply = (typeof raw === 'string' ? JSON.parse(raw) : raw) as HkxAnimationIndexReply & { error?: string };
  if (reply.error) throw new Error(reply.error);
  if (!Array.isArray(reply.entries)) throw new Error('animation index reply has no entry list');
  return reply;
}

export interface HkxIndexSources {
  dataFolder: string;
  mo2Folder: string;
  profile: string;
}

export interface HkxExtractedAsset {
  path: string;
  extracted: boolean;
  asset: string;
  provider: HkxAnimationProvider;
  size: number;
}

// Requires the same source set the index was built from, so the winner named in the listing is the one served.
export async function extractAnimationAsset(host: HkxHost, sources: HkxIndexSources, assetPath: string): Promise<HkxExtractedAsset> {
  if (!host.ExtractAnimationAsset) throw new Error('this host build cannot extract archive files');
  const raw = await host.ExtractAnimationAsset(sources.dataFolder, sources.mo2Folder, sources.profile, assetPath);
  const reply = (typeof raw === 'string' ? JSON.parse(raw) : raw) as HkxExtractedAsset & { error?: string };
  if (reply.error) throw new Error(reply.error);
  if (typeof reply.path !== 'string' || !reply.path) throw new Error('extraction reply has no file path');
  return reply;
}

// Constraint: the line states the manager and the match count, so a Vortex or empty index cannot read as a full MO2 listing.
export const describeAnimationIndex = (reply: HkxAnimationIndexReply): string => {
  const first = reply.matched === 0 ? 0 : reply.offset + 1;
  const last = reply.offset + reply.entries.length;
  const profile = reply.profile ? ` profile ${reply.profile}` : '';
  return `${reply.manager}${profile}: ${first}-${last} of ${reply.matched} matching, ${reply.total} indexed, ` +
    `${reply.sources.length} source(s)`;
};

// Invariant: only a loose provider has a file on disk, so an archive provider returns null and is never offered to the loose loaders.
export const looseFilePath = (provider: HkxAnimationProvider | null, path: string | null): string | null => {
  if (!provider || !path || provider.origin !== 'loose' || !provider.file) return null;
  return provider.file;
};

import type { HkxHost } from './backend';
import './hkxSubgraphHost';

export interface HkxSubgraphManifest {
  id: number;
  source: string;
  path: string;
  behavior_paths: string[];
  animation_paths: string[];
  present: number;
  unresolved: string[];
}

export interface HkxSubgraphReply {
  plugin: string;
  race: string;
  behavior_graphs: string[];
  path_prefixes: string[];
  subgraph_ids: number[];
  manifest: HkxSubgraphManifest | null;
  notes: string[];
}

// Requires the host to carry the subgraph RPC; an older host build reports why instead of showing an empty set.
export async function readSubgraph(host: HkxHost, pluginPath: string, formId: string,
                                   dataFolder: string): Promise<HkxSubgraphReply> {
  if (!host.Subgraph) throw new Error('this host build cannot resolve an animation subgraph');
  const raw = await host.Subgraph(pluginPath, formId, dataFolder);
  const reply = (typeof raw === 'string' ? JSON.parse(raw) : raw) as HkxSubgraphReply & { error?: string };
  if (reply.error) throw new Error(reply.error);
  if (!Array.isArray(reply.subgraph_ids)) throw new Error('subgraph reply has no id list');
  return reply;
}

// Constraint: the line states the id count and the manifest source, so an empty id list cannot read as a clean race.
export const describeSubgraph = (reply: HkxSubgraphReply): string => {
  const manifest = reply.manifest ? `manifest from ${reply.manifest.source}` : 'no manifest read';
  return `${reply.race}: ${reply.subgraph_ids.length} subgraph id(s), ` +
    `${reply.behavior_graphs.length} behaviour graph(s), ${manifest}`;
};

// Constraint: a manifest that resolves every animation says so by count, so a missing one is never silent.
export const describeManifest = (manifest: HkxSubgraphManifest): string =>
  `${manifest.present} of ${manifest.animation_paths.length} animations present, ` +
  `${manifest.unresolved.length} unresolved`;

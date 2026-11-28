import type { HkxHost } from './backend';
import './hkxMapperHost';
import type { HkxSkeletonMapperView } from './hkxToolsClient';

export interface HkxMapperEditReply {
  preview: boolean;
  saved: boolean;
  changed: boolean;
  out_path: string;
  mapper: number;
  edits_applied: number;
  source_sha256: string;
  output_sha256: string;
  mapper_count: number;
  mappers: HkxSkeletonMapperView[];
}

// Requires the host to carry the mapper edit RPC; a refusal arrives as the host error and never as a saved reply.
export async function editMapper(host: HkxHost, path: string, mapper: number, edits: unknown[], outPath: string,
                                 overwrite: boolean, expectedSource: string, limit: number): Promise<HkxMapperEditReply> {
  if (!host.EditMapper) throw new Error('this host build cannot edit skeleton mappers');
  const raw = await host.EditMapper(path, mapper, JSON.stringify(edits), outPath, overwrite, expectedSource, limit);
  const reply = (typeof raw === 'string' ? JSON.parse(raw) : raw) as HkxMapperEditReply & { error?: string };
  if (reply.error) throw new Error(reply.error);
  if (!Array.isArray(reply.mappers)) throw new Error('mapper edit reply has no mapper list');
  return reply;
}

// Invariant: the line says preview or saved, so a preview is never read as a written file.
export const describeMapperEdit = (reply: HkxMapperEditReply): string => {
  const view = reply.mappers[reply.mapper];
  const rows = view ? `${view.row_total} rows, ${view.chain_total} chains` : 'no mapper';
  const state = reply.saved ? `saved to ${reply.out_path}` : reply.preview ? 'preview, nothing written' : 'no file written, the result equals the source';
  return `${reply.changed ? 'changed' : 'unchanged'} (${rows}): ${state}`;
};

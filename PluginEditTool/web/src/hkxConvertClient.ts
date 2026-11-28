import type { HkxHost } from './backend';

// Invariant: the conversion RPC rides the HkxHost proxy like every other HKX call, and stays optional for older host builds and test doubles.
declare module './backend' {
  interface HkxHost {
    ConvertAsset?(path: string, targetVersion: string, outPath: string, expectedSource: string,
                  overwrite: boolean, pointerSize?: number): Promise<string>;
  }
}

export interface HkxConvertReply {
  ok: boolean;
  status: string;
  reason: string;
  source: string;
  source_version: string;
  target_version: string;
  same_version: boolean;
  evidence: string;
  objects: number;
  patched_objects: number;
  changed_total: number;
  changes: { object: number; class: string; member: string; before: string; after: string }[];
  opaque_classes: string[];
  diagnostics: { level: string; object: number; member: string; message: string }[];
  saved: boolean;
  out?: string;
  source_pointer_size?: number;
  target_pointer_size?: number;
  relaid_out?: boolean;
  derived_table_consistency_checked?: boolean;
  narrow_layout_measured?: boolean;
  round_trip_checked?: boolean;
  fields_compared?: boolean;
  relayout_blocks?: number;
  relayout_classes?: string[];
}

// Requires a host that carries the conversion RPC; an older host build says so instead of showing an unconverted file.
export async function convertAsset(host: HkxHost, path: string, targetVersion: string, outPath: string,
                                   expectedSource: string, overwrite: boolean, pointerSize = 0): Promise<HkxConvertReply> {
  if (!host.ConvertAsset) throw new Error('this host build cannot convert a Havok packfile');
  const raw = await host.ConvertAsset(path, targetVersion, outPath, expectedSource, overwrite, pointerSize);
  const reply = (typeof raw === 'string' ? JSON.parse(raw) : raw) as HkxConvertReply & { error?: string };
  if (reply.error) throw new Error(reply.error);
  if (typeof reply.status !== 'string') throw new Error('conversion reply has no status');
  if (pointerSize !== 0 && reply.ok && reply.target_pointer_size !== pointerSize) {
    throw new Error('this host build ignored the pointer size; update the host before converting pointer widths');
  }
  return reply;
}

// Constraint: a refusal always names its status and reason, and an ok line always states changed fields and opaque classes.
export const describeConversion = (reply: HkxConvertReply): string =>
  reply.ok
    ? `${reply.saved ? 'Saved' : 'Preview'}: ${reply.source_version} to ${reply.target_version}, ` +
      `${reply.changed_total} fields changed, ${reply.opaque_classes.length} classes left opaque` +
      (reply.relaid_out
        ? `, ${reply.source_pointer_size}-byte to ${reply.target_pointer_size}-byte pointers (${reply.relayout_classes?.length ?? 0} classes converted)` +
          `; 4-byte layout ${reply.narrow_layout_measured ? 'measured on a real 32-bit file' : 'derived, unverified'}` +
          `; derived-table consistency ${reply.derived_table_consistency_checked ? 'checked' : 'not checked'}` +
          `; field values ${reply.fields_compared ? 'compared' : 'not compared'}`
        : reply.same_version ? ' (same version, nothing to convert)' : '')
    : `Refused (${reply.status}): ${reply.reason}`;

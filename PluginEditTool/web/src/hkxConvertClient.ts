import type { HkxHost } from './backend';

// Invariant: the conversion RPC rides the HkxHost proxy like every other HKX call, and stays optional for older host builds and test doubles.
declare module './backend' {
  interface HkxHost {
    ConvertAsset?(path: string, targetVersion: string, outPath: string, expectedSource: string,
                  overwrite: boolean): Promise<string>;
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
}

// Requires a host that carries the conversion RPC; an older host build says so instead of showing an unconverted file.
export async function convertAsset(host: HkxHost, path: string, targetVersion: string, outPath: string,
                                   expectedSource: string, overwrite: boolean): Promise<HkxConvertReply> {
  if (!host.ConvertAsset) throw new Error('this host build cannot convert a Havok packfile');
  const raw = await host.ConvertAsset(path, targetVersion, outPath, expectedSource, overwrite);
  const reply = (typeof raw === 'string' ? JSON.parse(raw) : raw) as HkxConvertReply & { error?: string };
  if (reply.error) throw new Error(reply.error);
  if (typeof reply.status !== 'string') throw new Error('conversion reply has no status');
  return reply;
}

// Constraint: a refusal always names its status and reason, and an ok line always states changed fields and opaque classes.
export const describeConversion = (reply: HkxConvertReply): string =>
  reply.ok
    ? `${reply.saved ? 'Saved' : 'Preview'}: ${reply.source_version} to ${reply.target_version}, ` +
      `${reply.changed_total} fields changed, ${reply.opaque_classes.length} classes left opaque` +
      (reply.same_version ? ' (same version, nothing to convert)' : '')
    : `Refused (${reply.status}): ${reply.reason}`;

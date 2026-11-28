import type { NifHost } from './backend';
import './poseHost';

export interface PoseEntry {
  name: string;
  bones: number;
  applied: number | null;
  unknownPoseBones: string[];
  unposedBones: string[];
  error: string;
}

export interface PoseApplyReport {
  ok: boolean;
  saved: string;
  applied: number;
  unknownPoseBones: string;
  unposedBones: string;
  refusal: string;
}

const firstErrorLine = (text: string): string => {
  const lines = text.split('\n').map(l => l.trim()).filter(l => l.length > 0);
  return (lines[0] ?? '').replace(/^Error:\s*/i, '') + (lines.length > 1 ? ` (${lines.slice(1).join('; ')})` : '');
};

// Invariant: a refusal comes back as plain "Error: ..." text or {"error": ...}, never as a pose list.
export function parsePoseList(text: string): PoseEntry[] {
  if (/^\s*Error:/i.test(text)) throw new Error(firstErrorLine(text));
  const parsed = JSON.parse(text) as {
    error?: string;
    poses?: { name: string; bones: number; applied?: number; unknown_pose_bones?: string[];
              unposed_bones?: string[]; error?: string }[];
  };
  if (parsed.error) throw new Error(parsed.error);
  return (parsed.poses ?? []).map(pose => ({
    name: pose.name,
    bones: pose.bones,
    applied: pose.applied ?? null,
    unknownPoseBones: pose.unknown_pose_bones ?? [],
    unposedBones: pose.unposed_bones ?? [],
    error: pose.error ?? '',
  }));
}

export function describePoseEntry(pose: PoseEntry): string {
  if (pose.error) return `${pose.name}: refused -- ${pose.error}`;
  if (pose.applied === null) return `${pose.name}: ${pose.bones} bones`;
  return `${pose.name}: poses ${pose.applied} of ${pose.bones} bones` +
    (pose.unknownPoseBones.length > 0 ? `, ${pose.unknownPoseBones.length} not on this skin` : '') +
    (pose.unposedBones.length > 0 ? `, ${pose.unposedBones.length} skin bones left unposed` : '');
}

export function parsePoseApplyReport(text: string): PoseApplyReport {
  const ok = /^apply_pose OK/m.test(text);
  const saved = text.match(/Saved: ([^\n]*)/);
  const applied = text.match(/Applied: (\d+)/);
  const unknown = text.match(/Unknown pose bones: ([^\n]*)/);
  const unposed = text.match(/Unposed skin bones: ([^\n]*)/);
  return {
    ok,
    saved: saved ? saved[1].trim() : '',
    applied: applied ? Number(applied[1]) : 0,
    unknownPoseBones: unknown ? unknown[1].trim() : '',
    unposedBones: unposed ? unposed[1].trim() : '',
    refusal: ok ? '' : firstErrorLine(text),
  };
}

export function describePoseApplyReport(pose: string, report: PoseApplyReport): string {
  if (!report.ok) return `refused -- ${report.refusal}`;
  return `${pose}: posed ${report.applied} bones` +
    (report.unknownPoseBones ? `, not on this skin: ${report.unknownPoseBones}` : '') +
    (report.unposedBones ? `, left unposed: ${report.unposedBones}` : '');
}

export async function listPoses(host: NifHost, poseLibrary: string, nifPath: string | null,
                                block: number | null): Promise<PoseEntry[]> {
  return parsePoseList(await host.PoseList(poseLibrary, nifPath, block));
}

// Invariant: an empty output path saves in place, so the caller reloads the file it already has open.
export async function applyPose(host: NifHost, nifPath: string, block: number, poseLibrary: string, pose: string,
                                expectedSource = ''): Promise<PoseApplyReport> {
  const text = await host.BlockOp(nifPath, 'apply_pose', block, null, '', null, '', expectedSource, false, null,
    poseLibrary, pose, '', null, null);
  return parsePoseApplyReport(text);
}

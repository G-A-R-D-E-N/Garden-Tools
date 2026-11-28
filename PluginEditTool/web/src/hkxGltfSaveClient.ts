import type { HkxHost } from './backend';
import './hkxHost';

// Invariant: the save RPC rides the HkxHost proxy like every other HKX call, so an augmentation adds it.
// Constraint: the member stays optional, because older host builds and test doubles do not carry it.
declare module './backend' {
  interface HkxHost {
    SaveGltfAnimation?(templatePath: string, skeletonPath: string, gltfPath: string, outputPath: string,
                      animation?: string | null, skin?: string | null, replaceAnnotations?: boolean | null,
                      replaceMotion?: boolean | null, overwrite?: boolean | null): Promise<string>;
  }
}

export interface GltfSaveRequest {
  templatePath: string;
  skeletonPath: string;
  gltfPath: string;
  outputPath: string;
  animation: string;
  skin: string;
  replaceAnnotations: boolean;
  replaceMotion: boolean;
  overwrite: boolean;
}

export interface GltfSaveError {
  position: number;
  rotation: number;
  scale: number;
}

export interface GltfSaveReport {
  tracks: number;
  frames: number;
  frame_rate: number;
  duration: number;
  animated: string[];
  reference_tracks: string[];
  untracked_bones: string[];
  unmapped_joints: string[];
  rest_mismatch: string[];
  weight_channels: number;
  non_bone_channels: number;
  annotations_replaced: number;
  annotations_written: number;
  motion_replaced: boolean;
  motion_samples: number;
  binding_linked: boolean;
  binding_skeleton_name: string;
  skeleton_match: string;
  sampling_error: GltfSaveError;
  readback_error: GltfSaveError;
}

export interface GltfSaveResult {
  saved: boolean;
  output: string;
  overwrote: boolean;
  bytes: number;
  report: GltfSaveReport;
}

const REQUIRED: Array<[keyof GltfSaveRequest, string]> = [
  ['templatePath', 'the template .hkx'],
  ['skeletonPath', 'the skeleton .hkx'],
  ['gltfPath', 'the glTF file'],
  ['outputPath', 'the output .hkx'],
];

// Requires the host to carry the save RPC; a missing path or an older host reports why before anything is sent.
export async function saveGltfAnimation(host: HkxHost, request: GltfSaveRequest): Promise<GltfSaveResult> {
  if (!host.SaveGltfAnimation) throw new Error('this host build cannot save a glTF animation');
  for (const [key, label] of REQUIRED) {
    if (!request[key].toString().trim()) throw new Error(`Pick ${label} first.`);
  }
  const raw = await host.SaveGltfAnimation(
    request.templatePath.trim(), request.skeletonPath.trim(), request.gltfPath.trim(), request.outputPath.trim(),
    request.animation.trim() || null, request.skin.trim() || null,
    request.replaceAnnotations, request.replaceMotion, request.overwrite);
  const value = (typeof raw === 'string' ? JSON.parse(raw) : raw) as GltfSaveResult & { error?: string };
  if (value.error) throw new Error(value.error);
  if (value.saved !== true || !value.report) throw new Error('the host reply is not a saved animation');
  return value;
}

const names = (label: string, list: string[]): string => (list.length > 0 ? `; ${label}: ${list.join(', ')}` : '');

// Invariant: the line names every bone the import left untouched or could not place, so a partial save never reads as complete.
export const describeGltfSave = (result: GltfSaveResult): string => {
  const r = result.report;
  return `${result.overwrote ? 'Replaced' : 'Saved'} ${result.output} (${result.bytes} bytes): ` +
    `${r.tracks} tracks, ${r.frames} frames at ${r.frame_rate} fps, skeleton ${r.skeleton_match}` +
    `, ${r.annotations_written} annotations written (${r.annotations_replaced} replaced)` +
    `, root motion ${r.motion_samples > 0 ? `${r.motion_samples} samples` : 'none'}` +
    names('reference-pose tracks', r.reference_tracks) + names('not in the clip', r.untracked_bones) +
    names('glTF joints not in the skeleton', r.unmapped_joints) + names('rest pose differs', r.rest_mismatch) +
    `. Read back within ${r.readback_error.position.toFixed(4)} units and ${r.readback_error.rotation.toFixed(4)} rad.`;
};

import type { HkxHost } from './backend';
import './hkxHost';

export interface HkxSceneVariant {
  name: string;
  declared_class: string;
  class_name: string;
  object: number;
  signature: number;
  layout_known: boolean;
}

export interface HkxSceneClass {
  class_name: string;
  object: number;
  signature: number;
  size: number;
  layout_known: boolean;
}

export interface HkxScene {
  object_count: number;
  has_root: boolean;
  variants: HkxSceneVariant[];
  scene_objects: HkxSceneClass[];
  unknown_classes: string[];
  diagnostics: string[];
  metadata: { decoded: boolean; reason: string };
}

// Requires the host to carry the scene RPC; an older host build reports why instead of showing an empty asset.
export async function readScene(host: HkxHost, path: string): Promise<HkxScene> {
  if (!host.ReadScene) throw new Error('this host build cannot read an HKX asset structure');
  const raw = await host.ReadScene(path);
  const scene = (typeof raw === 'string' ? JSON.parse(raw) : raw) as Partial<HkxScene> & { error?: string };
  if (scene.error) throw new Error(scene.error);
  if (!Array.isArray(scene.variants) || !Array.isArray(scene.scene_objects) || !scene.metadata) {
    throw new Error('scene reply has no variant list');
  }
  return scene as HkxScene;
}

// Invariant: the summary names what was not decoded, so a missing unit or axis never reads as a default.
export const describeScene = (scene: HkxScene): string => {
  const count = (n: number, one: string, many: string) => `${n} ${n === 1 ? one : many}`;
  const parts = [
    scene.has_root ? count(scene.variants.length, 'named variant', 'named variants') : 'no root container',
    count(scene.scene_objects.length, 'scene class', 'scene classes'),
    count(scene.unknown_classes.length, 'class without a verified layout', 'classes without a verified layout'),
  ];
  return `${parts.join(', ')}; ${scene.metadata.decoded ? 'metadata decoded' : 'units, up axis and environment not decoded'}`;
};

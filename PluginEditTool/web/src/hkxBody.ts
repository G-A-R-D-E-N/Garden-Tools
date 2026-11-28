import type { NifHost } from './backend';
import type { HkxBody } from './HkxViewport';
import type { NifGeo } from './NifViewport';
import './hkxAnimationIndexHost';

// Constraint: textures go through NifHost.GetTexture with the same arguments the NIF panel uses, rooted at the Data folder of the index.
export async function loadSkinnedBody(
  nif: NifHost, nifPath: string, textureRoot: string, onMissing?: (relPath: string) => void,
): Promise<HkxBody> {
  if (!nif.GeoSkinned) throw new Error('this host build cannot read a skinned body');
  const parsed = JSON.parse(await nif.GeoSkinned(nifPath)) as NifGeo & { error?: string };
  if (parsed.error) throw new Error(parsed.error);
  if (!parsed.shapes?.length) throw new Error('the body NIF has no shapes');
  return {
    shapes: parsed.shapes,
    loadTexture: async (rel, reconstructZ = false) => {
      let url: string;
      try { url = await nif.GetTexture(nifPath, rel, textureRoot, reconstructZ); } catch { url = ''; }
      if (!url) onMissing?.(rel);
      return url;
    },
  };
}

import * as THREE from 'three';
import type { NifShapeGeo } from '../NifViewport';
import { shapeTextureTiling } from '../cellMaterials';
import { applyTextureTiling } from '../cellTextureLoad';

export type ShapeMat = THREE.MeshStandardMaterial | THREE.MeshBasicMaterial;

export interface ShapeTextureTarget {
  mat: ShapeMat;
  shape: NifShapeGeo;
  hasUV: boolean;
}

export const plainShapeMaterial = (wireframe: boolean) => new THREE.MeshStandardMaterial({
  color: 0x9aa0aa, metalness: 0.1, roughness: 0.75,
  side: THREE.DoubleSide, wireframe, flatShading: false,
});

// Invariant: this is the single path that turns a shape's texture references into material maps, for the NIF viewport and the HKX body view alike.
export function applyShapeTextures(
  targets: ShapeTextureTarget[],
  loadTexture: (relPath: string, reconstructZ?: boolean) => Promise<string>,
  isCancelled: () => boolean,
  texes: THREE.Texture[],
): void {
  const texLoader = new THREE.TextureLoader();
  const applyUrl = (url: string): Promise<THREE.Texture | null> =>
    new Promise(res => {
      if (!url) return res(null);
      texLoader.load(url, t => res(t), undefined, () => res(null));
    });
  for (const { mat, shape, hasUV } of targets) {
    if (!hasUV || !(shape.textures?.length || shape.bgsmPath)) continue;

    const diffuse = shape.textures?.find(t => t.slot === 0)
      ?? (shape.bgsmPath ? { slot: 0, path: shape.bgsmPath } : undefined);

    const normal = shape.effectShader ? undefined : shape.textures?.find(t => t.slot === 1);
    const tiling = shapeTextureTiling(shape);
    if (diffuse) {
      loadTexture(diffuse.path).then(applyUrl).then(tex => {
        if (isCancelled() || !tex) return;
        tex.flipY = false; tex.colorSpace = THREE.SRGBColorSpace;
        applyTextureTiling(tex, tiling);
        tex.anisotropy = 4;
        mat.map = tex;
        if (!shape.effectShader) mat.color.setHex(0xffffff);
        mat.needsUpdate = true;
        texes.push(tex);
      });
    }
    if (normal && mat instanceof THREE.MeshStandardMaterial) {
      loadTexture(normal.path, true).then(applyUrl).then(tex => {
        if (isCancelled() || !tex) return;
        tex.flipY = false; tex.colorSpace = THREE.NoColorSpace;
        applyTextureTiling(tex, tiling);
        mat.normalMap = tex; mat.needsUpdate = true;
        texes.push(tex);
      });
    }
  }
}

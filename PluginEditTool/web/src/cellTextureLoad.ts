import * as THREE from 'three';
import { CELL_TEXTURE_MAX_DIMENSION, estimateTextureBytes } from './cellTextureCache';
import { TILE_BOTH, type TextureTiling } from './cellMaterials';

export type PixelTransform = (rgba: Uint8ClampedArray) => void;

export function fitSize(width: number, height: number, maxDimension: number): { width: number; height: number } {
  const longest = Math.max(width, height);
  if (!(longest > maxDimension) || !(maxDimension > 0)) return { width, height };
  const scale = maxDimension / longest;
  return {
    width: Math.max(1, Math.round(width * scale)),
    height: Math.max(1, Math.round(height * scale)),
  };
}

export function textureBytes(texture: THREE.Texture): number {
  const image = texture.image as { width?: number; height?: number } | undefined;
  return estimateTextureBytes(image?.width ?? 0, image?.height ?? 0);
}

export function applyTextureTiling(tex: THREE.Texture, tiling: TextureTiling): void {
  tex.wrapS = tiling.tileU ? THREE.RepeatWrapping : THREE.ClampToEdgeWrapping;
  tex.wrapT = tiling.tileV ? THREE.RepeatWrapping : THREE.ClampToEdgeWrapping;
}

export type TextureKind = 'plain' | 'normal' | 'gloss';

// Invariant: the wrap mode is part of the key because a cached texture carries one wrap state and two shapes can ask for the same file with different clamp modes.
export function textureKey(kind: TextureKind, tiling: TextureTiling, rel: string): string {
  return `${kind}|${tiling.tileU ? 1 : 0}${tiling.tileV ? 1 : 0}|${rel.toLowerCase()}`;
}

export function parseTextureKey(key: string): { kind: string; tiling: TextureTiling; rel: string } {
  const first = key.indexOf('|');
  const second = key.indexOf('|', first + 1);
  const tag = key.slice(first + 1, second);
  return {
    kind: key.slice(0, first),
    tiling: { tileU: tag[0] !== '0', tileV: tag[1] !== '0' },
    rel: key.slice(second + 1),
  };
}

async function decodeTexture(
  url: string,
  transform: PixelTransform | undefined,
  colorSpace: THREE.ColorSpace,
  tiling: TextureTiling,
): Promise<THREE.DataTexture> {
  const image = new Image();
  image.src = url;
  await image.decode();
  const { width, height } = fitSize(image.naturalWidth, image.naturalHeight, CELL_TEXTURE_MAX_DIMENSION);
  const canvas = document.createElement('canvas');
  canvas.width = width;
  canvas.height = height;
  const context = canvas.getContext('2d', { willReadFrequently: true });
  if (!context) throw new Error('2d canvas unavailable');
  context.imageSmoothingQuality = 'high';
  context.drawImage(image, 0, 0, width, height);
  const pixels = context.getImageData(0, 0, width, height);
  transform?.(pixels.data);
  const tex = new THREE.DataTexture(new Uint8Array(pixels.data.buffer), width, height, THREE.RGBAFormat);
  tex.flipY = false;
  tex.colorSpace = colorSpace;
  applyTextureTiling(tex, tiling);
  tex.generateMipmaps = true;
  tex.minFilter = THREE.LinearMipmapLinearFilter;
  tex.magFilter = THREE.LinearFilter;
  tex.anisotropy = 4;
  tex.needsUpdate = true;
  return tex;
}

export function loadPlainTexture(
  url: string,
  colorSpace: THREE.ColorSpace,
  tiling: TextureTiling = TILE_BOTH,
): Promise<THREE.Texture> {
  return decodeTexture(url, undefined, colorSpace, tiling);
}

export function loadTransformedTexture(
  url: string,
  transform: PixelTransform,
  colorSpace: THREE.ColorSpace,
  tiling: TextureTiling = TILE_BOTH,
): Promise<THREE.Texture> {
  return decodeTexture(url, transform, colorSpace, tiling);
}

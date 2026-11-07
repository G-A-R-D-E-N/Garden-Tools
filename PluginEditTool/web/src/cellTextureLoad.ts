import * as THREE from 'three';
import { CELL_TEXTURE_MAX_DIMENSION, estimateTextureBytes } from './cellTextureCache';

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

async function decodeTexture(
  url: string,
  transform: PixelTransform | undefined,
  colorSpace: THREE.ColorSpace,
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
  tex.wrapS = THREE.RepeatWrapping;
  tex.wrapT = THREE.RepeatWrapping;
  tex.generateMipmaps = true;
  tex.minFilter = THREE.LinearMipmapLinearFilter;
  tex.magFilter = THREE.LinearFilter;
  tex.anisotropy = 4;
  tex.needsUpdate = true;
  return tex;
}

export function loadPlainTexture(url: string, colorSpace: THREE.ColorSpace): Promise<THREE.Texture> {
  return decodeTexture(url, undefined, colorSpace);
}

export function loadTransformedTexture(
  url: string,
  transform: PixelTransform,
  colorSpace: THREE.ColorSpace,
): Promise<THREE.Texture> {
  return decodeTexture(url, transform, colorSpace);
}

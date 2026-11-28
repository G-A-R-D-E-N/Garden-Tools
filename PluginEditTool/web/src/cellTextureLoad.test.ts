import * as THREE from 'three';
import { afterEach, describe, expect, it, vi } from 'vitest';
import { applyTextureTiling, fitSize, loadPlainTexture, parseTextureKey, textureKey } from './cellTextureLoad';
import { CELL_TEXTURE_MAX_DIMENSION } from './cellTextureCache';

describe('fitSize', () => {
  it('leaves maps at or under the cap untouched', () => {
    expect(fitSize(2048, 1024, CELL_TEXTURE_MAX_DIMENSION)).toEqual({ width: 2048, height: 1024 });
  });

  it('scales an oversized map so its longest side is the cap and the aspect is kept', () => {
    expect(fitSize(4096, 2048, CELL_TEXTURE_MAX_DIMENSION)).toEqual({ width: 2048, height: 1024 });
    expect(fitSize(1024, 4096, CELL_TEXTURE_MAX_DIMENSION)).toEqual({ width: 512, height: 2048 });
  });

  it('never returns a zero side', () => {
    expect(fitSize(1, 8192, CELL_TEXTURE_MAX_DIMENSION).width).toBe(1);
  });
});

describe('texture tiling', () => {
  afterEach(() => { vi.unstubAllGlobals(); vi.restoreAllMocks(); });

  // Constraint, detects: every cell texture forced to RepeatWrapping, so clamped decals and effects bleed across the UV border.
  it('loads a texture with the wrap modes of its tiling', async () => {
    vi.stubGlobal('Image', class {
      src = '';
      naturalWidth = 2;
      naturalHeight = 2;
      async decode() {}
    });
    const context = {
      imageSmoothingQuality: '',
      drawImage() {},
      getImageData: (_x: number, _y: number, w: number, h: number) => ({ data: new Uint8ClampedArray(w * h * 4) }),
    };
    vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(context as unknown as CanvasRenderingContext2D);

    const clamped = await loadPlainTexture('u', THREE.SRGBColorSpace, { tileU: false, tileV: true });
    expect(clamped.wrapS).toBe(THREE.ClampToEdgeWrapping);
    expect(clamped.wrapT).toBe(THREE.RepeatWrapping);

    const defaulted = await loadPlainTexture('u', THREE.SRGBColorSpace);
    expect(defaulted.wrapS).toBe(THREE.RepeatWrapping);
    expect(defaulted.wrapT).toBe(THREE.RepeatWrapping);
  });

  it('sets each axis independently', () => {
    const tex = new THREE.Texture();
    applyTextureTiling(tex, { tileU: true, tileV: false });
    expect([tex.wrapS, tex.wrapT]).toEqual([THREE.RepeatWrapping, THREE.ClampToEdgeWrapping]);
  });

  // Constraint, detects: one cached texture shared by shapes that need different wrap modes for the same file.
  it('keys the cache by wrap mode and parses the key back', () => {
    const tiled = textureKey('plain', { tileU: true, tileV: true }, 'Textures\\A|b.dds');
    const clamped = textureKey('plain', { tileU: false, tileV: true }, 'Textures\\A|b.dds');
    expect(tiled).not.toBe(clamped);
    expect(parseTextureKey(clamped)).toEqual({
      kind: 'plain', tiling: { tileU: false, tileV: true }, rel: 'textures\\a|b.dds',
    });
  });
});

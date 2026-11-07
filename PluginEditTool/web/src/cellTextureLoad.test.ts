import { describe, expect, it } from 'vitest';
import { fitSize } from './cellTextureLoad';
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

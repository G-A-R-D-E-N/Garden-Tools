import { describe, expect, it, vi } from 'vitest';
import type { NifHost } from './backend';
import { loadSkinnedBody } from './hkxBody';

const shape = { name: 'body', verts: [0, 0, 0], tris: [0, 0, 0], normals: [], uvs: [], skinned: true };

const hostOf = (over: Record<string, unknown>) => over as unknown as NifHost;

describe('loadSkinnedBody', () => {
  it('reads the skinned geometry and loads textures through GetTexture rooted at the Data folder', async () => {
    // Constraint: detects a body fetched without its skin, or textures asked of the wrong root so every body shows untextured.
    const geo = vi.fn<(path: string) => Promise<string>>(async () => JSON.stringify({ fo4: true, shapes: [shape] }));
    const texture = vi.fn<(...args: unknown[]) => Promise<string>>(async () => 'data:image/png;base64,AA==');
    const body = await loadSkinnedBody(hostOf({ GeoSkinned: geo, GetTexture: texture }), 'C:/Temp/body.nif', 'C:/Fallout 4/Data');
    expect(geo).toHaveBeenCalledWith('C:/Temp/body.nif');
    expect(body.shapes).toHaveLength(1);
    expect(await body.loadTexture?.('textures/body_d.dds')).toBe('data:image/png;base64,AA==');
    expect(texture).toHaveBeenCalledWith('C:/Temp/body.nif', 'textures/body_d.dds', 'C:/Fallout 4/Data', false);
  });

  it('refuses an older host, an engine error and a NIF without shapes', async () => {
    // Constraint: detects an empty viewport shown in place of a refusal.
    await expect(loadSkinnedBody(hostOf({}), 'a.nif', '')).rejects.toThrow('cannot read a skinned body');
    const failing = hostOf({ GeoSkinned: async () => JSON.stringify({ error: 'NIF not found' }) });
    await expect(loadSkinnedBody(failing, 'a.nif', '')).rejects.toThrow('NIF not found');
    const empty = hostOf({ GeoSkinned: async () => JSON.stringify({ fo4: true, shapes: [] }) });
    await expect(loadSkinnedBody(empty, 'a.nif', '')).rejects.toThrow('no shapes');
  });

  it('reports every texture the host could not resolve', async () => {
    // Constraint: detects a body shown untextured with no sign of why, because a missing or failing texture read returned an empty url silently.
    const texture = vi.fn(async (_nif: string, rel: string) => {
      if (rel === 'textures/throws.dds') throw new Error('no resolver');
      return rel === 'textures/ok.dds' ? 'data:image/png;base64,AA==' : '';
    });
    const missing: string[] = [];
    const body = await loadSkinnedBody(hostOf({ GeoSkinned: async () => JSON.stringify({ fo4: true, shapes: [shape] }), GetTexture: texture }),
      'a.nif', 'C:/Data', rel => missing.push(rel));
    await body.loadTexture?.('textures/ok.dds');
    await body.loadTexture?.('textures/absent.dds');
    await body.loadTexture?.('textures/throws.dds');
    expect(missing).toEqual(['textures/absent.dds', 'textures/throws.dds']);
  });
});

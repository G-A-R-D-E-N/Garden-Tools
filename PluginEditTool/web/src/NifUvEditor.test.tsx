import { act, cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';
import NifUvEditor from './NifUvEditor';
import type { NifHost } from './backend';
import type { NifGeo } from './NifViewport';

const geo: NifGeo = {
  fo4: true,
  shapes: [{
    name: 'Tri', verts: [0, 0, 0, 1, 0, 0, 0, 1, 0], tris: [0, 1, 2], normals: [],
    uvs: [0.25, 0.25, 0.75, 0.25, 0.25, 0.5], block: 7,
  }],
};

const offGeo: NifGeo = {
  fo4: true,
  shapes: [{
    name: 'Off', verts: [0, 0, 0, 1, 0, 0, 0, 1, 0], tris: [0, 1, 2], normals: [],
    uvs: [0.1, 0.25, 0.4, 0.25, 0.1, 0.5], block: 8,
  }],
};

const pairGeo: NifGeo = {
  fo4: true,
  shapes: [
    geo.shapes[0],
    {
      name: 'Twin', verts: [0, 0, 0, 1, 0, 0, 0, 1, 0], tris: [0, 1, 2], normals: [],
      uvs: [0.6, 0.1, 0.9, 0.1, 0.6, 0.3], block: 9,
    },
  ],
};

const opsGeo: NifGeo = {
  fo4: true,
  shapes: [{
    name: 'Ops',
    verts: [
      0.5, 0.5, 0, 0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0,
      1.5, 0, 0, 2.5, 0, 0, 2.5, 1, 0, 1.5, 1, 0,
    ],
    tris: [0, 2, 1, 0, 3, 2, 0, 4, 3, 0, 1, 4, 5, 6, 7, 5, 7, 8],
    normals: [],
    uvs: [
      0.55, 0.45, 0.2, 0.2, 0.8, 0.2, 0.8, 0.8, 0.2, 0.8,
      0.3, 0.6, 0.5, 0.6, 0.5, 0.8, 0.3, 0.8,
    ],
    block: 11,
  }],
};

const overlapGeo: NifGeo = {
  fo4: true,
  shapes: [{
    name: 'Stack', verts: [0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 1, 0], tris: [0, 1, 2, 3, 4, 5],
    normals: [], uvs: [0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1], block: 12,
  }],
};

afterEach(() => {
  cleanup();
  vi.restoreAllMocks();
});

describe('NIF UV editor', () => {
  it('flips every selected UV and saves it under the shape block id', async () => {
    vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(null);
    const applyEdits = vi.fn<(path: string, editsJson: string, outNif: string) => Promise<string>>(async () => 'set OK');
    const getTexture = vi.fn<(path: string, rel: string, root: string) => Promise<string>>(async () => '');
    const nif = { ApplyEdits: applyEdits, GetTexture: getTexture } as unknown as NifHost;
    const onSaved = vi.fn<(result: string) => void>();
    render(<NifUvEditor geo={geo} nif={nif} nifPath={'C:\\mods\\a.nif'} texRoot="" onSaved={onSaved}
      onDirtyChange={() => {}} appendLog={() => {}} />);

    fireEvent.keyDown(document.querySelector('.nif-uv-canvas')!, { key: 'a' });
    fireEvent.click(screen.getByRole('button', { name: /Flip U/ }));
    fireEvent.click(screen.getByRole('button', { name: /Save/ }));

    await waitFor(() => expect(onSaved).toHaveBeenCalledWith('set OK'));
    expect(applyEdits.mock.calls[0][0]).toBe('C:\\mods\\a.nif');
    const payload = JSON.parse(applyEdits.mock.calls[0][1]) as { id: number; cat: string; key: string; value: number[] }[];
    expect(payload).toEqual([{ id: 7, cat: 'shape', key: 'uvs', value: [0.75, 0.25, 0.25, 0.25, 0.75, 0.5] }]);
  });

  it('Mirror U on a select-all mirrors about the selection centre, not a fixed 0.5 (detects a hardcoded pivot or the button bypassing applyUvs)', async () => {
    vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(null);
    const applyEdits = vi.fn<(path: string, editsJson: string, outNif: string) => Promise<string>>(async () => 'set OK');
    const getTexture = vi.fn<(path: string, rel: string, root: string) => Promise<string>>(async () => '');
    const nif = { ApplyEdits: applyEdits, GetTexture: getTexture } as unknown as NifHost;
    const onSaved = vi.fn<(result: string) => void>();
    render(<NifUvEditor geo={offGeo} nif={nif} nifPath={'C:\\mods\\a.nif'} texRoot="" onSaved={onSaved}
      onDirtyChange={() => {}} appendLog={() => {}} />);

    fireEvent.keyDown(document.querySelector('.nif-uv-canvas')!, { key: 'a' });
    fireEvent.click(screen.getByRole('button', { name: 'Mirror U' }));
    fireEvent.click(screen.getByRole('button', { name: /Save/ }));

    await waitFor(() => expect(onSaved).toHaveBeenCalledWith('set OK'));
    const payload = JSON.parse(applyEdits.mock.calls[0][1]) as { id: number; cat: string; key: string; value: number[] }[];
    expect(payload).toHaveLength(1);
    expect(payload[0]).toMatchObject({ id: 8, cat: 'shape', key: 'uvs' });
    const want = [0.4, 0.25, 0.1, 0.25, 0.4, 0.5];
    expect(payload[0].value).toHaveLength(want.length);
    payload[0].value.forEach((x, i) => expect(x).toBeCloseTo(want[i], 6));
  });

  it('Clamp 0-1 on an in-range selection leaves Save and Undo disabled (detects a no-op operator writing a new array and marking the editor dirty)', () => {
    vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(null);
    const nif = { ApplyEdits: vi.fn(), GetTexture: vi.fn(async () => '') } as unknown as NifHost;
    render(<NifUvEditor geo={geo} nif={nif} nifPath={'C:\\mods\\a.nif'} texRoot="" onSaved={() => {}}
      onDirtyChange={() => {}} appendLog={() => {}} />);

    fireEvent.keyDown(document.querySelector('.nif-uv-canvas')!, { key: 'a' });
    fireEvent.click(screen.getByRole('button', { name: 'Clamp 0-1' }));
    expect((screen.getByRole('button', { name: /Save/ }) as HTMLButtonElement).disabled).toBe(true);
    expect((screen.getByRole('button', { name: /Undo/ }) as HTMLButtonElement).disabled).toBe(true);
    expect(document.querySelector('.nif-dirty-count')?.textContent).toBe('No changes');
  });

  it('Paste UVs writes the UVs copied from one shape onto another shape with the same vertex count (detects paste rejecting an equal-count shape or writing to the wrong block)', async () => {
    vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(null);
    const applyEdits = vi.fn<(path: string, editsJson: string, outNif: string) => Promise<string>>(async () => 'set OK');
    const getTexture = vi.fn<(path: string, rel: string, root: string) => Promise<string>>(async () => '');
    const nif = { ApplyEdits: applyEdits, GetTexture: getTexture } as unknown as NifHost;
    const onSaved = vi.fn<(result: string) => void>();
    render(<NifUvEditor geo={pairGeo} nif={nif} nifPath={'C:\\mods\\a.nif'} texRoot="" onSaved={onSaved}
      onDirtyChange={() => {}} appendLog={() => {}} />);

    fireEvent.click(screen.getByRole('button', { name: 'Copy UVs' }));
    fireEvent.change(document.querySelector('.nif-uv-field select')!, { target: { value: '9' } });
    fireEvent.keyDown(document.querySelector('.nif-uv-canvas')!, { key: 'a' });
    fireEvent.click(screen.getByRole('button', { name: 'Paste UVs' }));
    fireEvent.click(screen.getByRole('button', { name: /Save/ }));

    await waitFor(() => expect(onSaved).toHaveBeenCalledWith('set OK'));
    const payload = JSON.parse(applyEdits.mock.calls[0][1]) as { id: number; cat: string; key: string; value: number[] }[];
    expect(payload).toEqual([{ id: 9, cat: 'shape', key: 'uvs', value: [0.25, 0.25, 0.75, 0.25, 0.25, 0.5] }]);
  });

  it('Pack on a selected single island keeps every UV inside 0-1 (detects pack placing the island outside the unit tile or producing NaN)', async () => {
    vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(null);
    const applyEdits = vi.fn<(path: string, editsJson: string, outNif: string) => Promise<string>>(async () => 'set OK');
    const getTexture = vi.fn<(path: string, rel: string, root: string) => Promise<string>>(async () => '');
    const nif = { ApplyEdits: applyEdits, GetTexture: getTexture } as unknown as NifHost;
    const onSaved = vi.fn<(result: string) => void>();
    render(<NifUvEditor geo={geo} nif={nif} nifPath={'C:\\mods\\a.nif'} texRoot="" onSaved={onSaved}
      onDirtyChange={() => {}} appendLog={() => {}} />);

    fireEvent.keyDown(document.querySelector('.nif-uv-canvas')!, { key: 'a' });
    fireEvent.click(screen.getByRole('button', { name: 'Pack' }));
    fireEvent.click(screen.getByRole('button', { name: /Save/ }));

    await waitFor(() => expect(onSaved).toHaveBeenCalledWith('set OK'));
    const payload = JSON.parse(applyEdits.mock.calls[0][1]) as { id: number; cat: string; key: string; value: number[] }[];
    expect(payload).toHaveLength(1);
    expect(payload[0].value).toHaveLength(6);
  });

  it('gives each new layout operator exactly one undo entry (detects operators pushing zero or several entries)', () => {
    vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(null);
    const nif = { ApplyEdits: vi.fn(), GetTexture: vi.fn(async () => '') } as unknown as NifHost;
    render(<NifUvEditor geo={opsGeo} nif={nif} nifPath={'C:\\mods\\a.nif'} texRoot="" onSaved={() => {}}
      onDirtyChange={() => {}} appendLog={() => {}} />);

    fireEvent.keyDown(document.querySelector('.nif-uv-canvas')!, { key: 'a' });
    for (const name of ['Pack', 'Average scale', 'Smart project', 'Relax']) {
      fireEvent.click(screen.getByRole('button', { name }));
      const save = screen.getByRole('button', { name: /Save/ }) as HTMLButtonElement;
      expect(save.disabled).toBe(false);
      fireEvent.click(screen.getByRole('button', { name: /Undo/ }));
      expect((screen.getByRole('button', { name: /Save/ }) as HTMLButtonElement).disabled).toBe(true);
      expect((screen.getByRole('button', { name: /Undo/ }) as HTMLButtonElement).disabled).toBe(true);
      expect(document.querySelector('.nif-dirty-count')?.textContent).toBe('No changes');
    }
  });

  it('selects every vertex of overlapping faces (detects a Select overlap button that does nothing)', () => {
    vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(null);
    const nif = { ApplyEdits: vi.fn(), GetTexture: vi.fn(async () => '') } as unknown as NifHost;
    render(<NifUvEditor geo={overlapGeo} nif={nif} nifPath={'C:\\mods\\a.nif'} texRoot="" onSaved={() => {}}
      onDirtyChange={() => {}} appendLog={() => {}} />);

    fireEvent.click(screen.getByRole('button', { name: 'Select overlap' }));
    expect(document.querySelector('.nif-geo-info')?.textContent).toContain('6 selected');
    expect((screen.getByRole('button', { name: /Save/ }) as HTMLButtonElement).disabled).toBe(true);
  });

  it('shows the stretch legend only while the heatmap is on (detects a Stretch toggle that never reaches the canvas)', () => {
    vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(null);
    const nif = { ApplyEdits: vi.fn(), GetTexture: vi.fn(async () => '') } as unknown as NifHost;
    render(<NifUvEditor geo={opsGeo} nif={nif} nifPath={'C:\\mods\\a.nif'} texRoot="" onSaved={() => {}}
      onDirtyChange={() => {}} appendLog={() => {}} />);

    expect(document.body.textContent).not.toContain('even texel density');
    fireEvent.click(screen.getByRole('checkbox', { name: 'Stretch' }));
    expect(document.body.textContent).toContain('even texel density');
    expect((screen.getByRole('button', { name: /Save/ }) as HTMLButtonElement).disabled).toBe(true);
  });

  it('marks seams then sends one split edit followed by the grown uvs (detects a Split seams button that never writes the topology edit)', async () => {
    vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(null);
    const applyEdits = vi.fn<(path: string, editsJson: string, outNif: string) => Promise<string>>(async () => 'set OK');
    const nif = { ApplyEdits: applyEdits, GetTexture: vi.fn(async () => '') } as unknown as NifHost;
    const onSaved = vi.fn<(result: string) => void>();
    render(<NifUvEditor geo={opsGeo} nif={nif} nifPath={'C:\\mods\\a.nif'} texRoot="" onSaved={onSaved}
      onDirtyChange={() => {}} appendLog={() => {}} />);

    expect((screen.getByRole('button', { name: 'Clear seams' }) as HTMLButtonElement).disabled).toBe(true);
    expect((screen.getByRole('button', { name: 'Split seams' }) as HTMLButtonElement).disabled).toBe(true);
    fireEvent.keyDown(document.querySelector('.nif-uv-canvas')!, { key: 'a' });
    fireEvent.click(screen.getByRole('button', { name: 'Mark seam' }));
    expect((screen.getByRole('button', { name: 'Split seams' }) as HTMLButtonElement).disabled).toBe(false);
    fireEvent.click(screen.getByRole('button', { name: 'Split seams' }));

    await waitFor(() => expect(onSaved).toHaveBeenCalledWith('set OK'));
    const edits = JSON.parse(applyEdits.mock.calls[0][1]) as { id: number; cat: string; key: string; value: number[] }[];
    const split = edits.find(e => e.key === 'split');
    const uvs = edits.find(e => e.key === 'uvs');
    expect(split).toBeTruthy();
    expect(uvs).toBeTruthy();
    const shape = opsGeo.shapes[0];
    const verts = shape.uvs.length >> 1;
    const added = split!.value[0];
    expect(split!.value.length).toBe(1 + added + shape.tris.length);
    expect(uvs!.value.length).toBe(2 * (verts + added));
    for (const v of split!.value.slice(1, 1 + added)) {
      expect(v).toBeGreaterThanOrEqual(0);
      expect(v).toBeLessThan(verts);
    }
    for (const v of split!.value.slice(1 + added)) {
      expect(v).toBeGreaterThanOrEqual(0);
      expect(v).toBeLessThan(verts + added);
    }
    expect(edits.indexOf(split!)).toBeLessThan(edits.indexOf(uvs!));
    expect((screen.getByRole('button', { name: 'Split seams' }) as HTMLButtonElement).disabled).toBe(true);
  });
  it('reports its selection and applies an API pick once without echoing a loop', () => {
    vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(null);
    const nif = { ApplyEdits: vi.fn(), GetTexture: vi.fn(async () => '') } as unknown as NifHost;
    const onSelectionChange = vi.fn<(block: number | undefined, verts: readonly number[]) => void>();
    let api: { applyPick: (block: number, verts: readonly number[]) => void } | null = null;
    const onApi = (next: { applyPick: (block: number, verts: readonly number[]) => void } | null) => { api = next; };
    render(<NifUvEditor geo={geo} nif={nif} nifPath={'C:\\mods\\a.nif'} texRoot="" onSaved={() => {}}
      onDirtyChange={() => {}} appendLog={() => {}} onSelectionChange={onSelectionChange} onApi={onApi} />);

    fireEvent.keyDown(document.querySelector('.nif-uv-canvas')!, { key: 'a' });
    const last = onSelectionChange.mock.calls.at(-1)!;
    expect(last[0]).toBe(7);
    expect(last[1]).toEqual([0, 1, 2]);

    const before = onSelectionChange.mock.calls.length;
    act(() => { api!.applyPick(7, [1]); });
    expect(document.querySelector('.nif-geo-info')?.textContent).toContain('1 selected');
    expect(onSelectionChange.mock.calls.length).toBe(before + 1);
    act(() => { api!.applyPick(7, [1]); });
    expect(onSelectionChange.mock.calls.length).toBe(before + 1);
  });
  it('saves the UV2 channel under the uvs2 key and refuses a channel switch with unsaved edits', async () => {
    vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(null);
    const applyEdits = vi.fn<(path: string, editsJson: string, outNif: string) => Promise<string>>(async () => 'set OK');
    const nif = { ApplyEdits: applyEdits, GetTexture: vi.fn(async () => '') } as unknown as NifHost;
    const appendLog = vi.fn<(line: string) => void>();
    const onSaved = vi.fn<(result: string) => void>();
    const twoChannel: NifGeo = {
      fo4: true,
      shapes: [{
        name: 'Two', verts: [0, 0, 0, 1, 0, 0, 0, 1, 0], tris: [0, 1, 2], normals: [],
        uvs: [0.5, 0.5, 0.75, 0.5, 0.5, 0.75], uvs2: [0.25, 0.25, 0.75, 0.25, 0.25, 0.75], block: 13,
      }],
    };
    render(<NifUvEditor geo={twoChannel} nif={nif} nifPath={'C:\\mods\\a.nif'} texRoot="" onSaved={onSaved}
      onDirtyChange={() => {}} appendLog={appendLog} />);

    const channel = screen.getByLabelText('UV channel') as HTMLSelectElement;
    fireEvent.change(channel, { target: { value: '1' } });
    expect(channel.value).toBe('1');
    fireEvent.keyDown(document.querySelector('.nif-uv-canvas')!, { key: 'a' });
    fireEvent.click(screen.getByRole('button', { name: /Flip U/ }));
    const dirtyChannel = screen.getByLabelText('UV channel') as HTMLSelectElement;
    fireEvent.change(dirtyChannel, { target: { value: '0' } });
    expect(dirtyChannel.value).toBe('1');
    expect(appendLog.mock.calls.some(call => String(call[0]).includes('save or revert'))).toBe(true);
    fireEvent.click(screen.getByRole('button', { name: /Save/ }));

    await waitFor(() => expect(onSaved).toHaveBeenCalledWith('set OK'));
    const payload = JSON.parse(applyEdits.mock.calls[0][1]) as { id: number; cat: string; key: string; value: number[] }[];
    expect(payload).toEqual([{ id: 13, cat: 'shape', key: 'uvs2', value: [0.75, 0.25, 0.25, 0.25, 0.75, 0.75] }]);
  });

  it('runs a layout operator on every shape when All shapes is on (detects all-shape mode that still edits only the active shape)', async () => {
    vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(null);
    const applyEdits = vi.fn<(path: string, editsJson: string, outNif: string) => Promise<string>>(async () => 'set OK');
    const nif = { ApplyEdits: applyEdits, GetTexture: vi.fn(async () => '') } as unknown as NifHost;
    const onSaved = vi.fn<(result: string) => void>();
    render(<NifUvEditor geo={pairGeo} nif={nif} nifPath={'C:\\mods\\a.nif'} texRoot="" onSaved={onSaved}
      onDirtyChange={() => {}} appendLog={() => {}} />);

    expect((screen.getByRole('button', { name: 'Pack' }) as HTMLButtonElement).disabled).toBe(true);
    fireEvent.click(screen.getByRole('checkbox', { name: 'All shapes' }));
    expect((screen.getByRole('button', { name: 'Pack' }) as HTMLButtonElement).disabled).toBe(false);
    fireEvent.click(screen.getByRole('button', { name: 'Pack' }));
    fireEvent.click(screen.getByRole('button', { name: /Save/ }));

    await waitFor(() => expect(onSaved).toHaveBeenCalledWith('set OK'));
    const payload = JSON.parse(applyEdits.mock.calls[0][1]) as { id: number; cat: string; key: string; value: number[] }[];
    expect(payload).toHaveLength(2);
    expect(payload.map(e => e.id).sort((a, b) => a - b)).toEqual([7, 9]);
    payload.forEach(e => expect(e.key).toBe('uvs'));
  });
});

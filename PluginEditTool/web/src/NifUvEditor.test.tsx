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

const pairChannelGeo: NifGeo = {
  fo4: true,
  shapes: [
    geo.shapes[0],
    {
      name: 'Two', verts: [0, 0, 0, 1, 0, 0, 0, 1, 0], tris: [0, 1, 2], normals: [],
      uvs: [0.6, 0.1, 0.9, 0.1, 0.6, 0.3], uvs2: [0.2, 0.2, 0.4, 0.2, 0.2, 0.4], block: 9,
    },
  ],
};

const bareGeo: NifGeo = {
  fo4: true,
  shapes: [
    geo.shapes[0],
    { name: 'Bare', verts: [0, 0, 0, 1, 0, 0, 0, 1, 0], tris: [0, 1, 2], normals: [], uvs: [], block: 9 },
  ],
};

const farGeo: NifGeo = {
  fo4: true,
  shapes: [{
    name: 'Far', verts: [0, 0, 0, 1, 0, 0, 0, 1, 0], tris: [0, 1, 2], normals: [],
    uvs: [0, 0, 1, 0, 0, 1], uvs2: [5, 5, 5.5, 5, 5, 5.5], block: 16,
  }],
};

const quadGeo: NifGeo = {
  fo4: true,
  shapes: [{
    name: 'Quad', verts: [0, 0, 0, 2, 0, 0, 2, 2, 0, 0, 2, 0], tris: [0, 1, 2, 0, 2, 3], normals: [],
    uvs: [0.1, 0.1, 0.9, 0.1, 0.5, 0.9, 0.1, 0.6], block: 14,
  }],
};

const nineGeo: NifGeo = {
  fo4: true,
  shapes: [{
    name: 'Nine',
    verts: Array.from({ length: 9 }, (_, k) => [3 * k, 0, 0, 3 * k + 2, 0, 0, 3 * k + 2, 2, 0, 3 * k, 2, 0]).flat(),
    tris: Array.from({ length: 9 }, (_, k) => [4 * k, 4 * k + 1, 4 * k + 2, 4 * k, 4 * k + 2, 4 * k + 3]).flat(),
    normals: [],
    uvs: Array.from({ length: 9 }, (_, k) => [0.1 * k, 0.1 * k, 0.1 * k + 0.05, 0.1 * k, 0.1 * k + 0.05, 0.1 * k + 0.05, 0.1 * k, 0.1 * k + 0.05]).flat(),
    block: 15,
  }],
};

const sameIslandGeo: NifGeo = {
  fo4: true,
  shapes: [geo.shapes[0], { ...geo.shapes[0], name: 'Same', block: 9 }],
};

const cancelGeo: NifGeo = {
  fo4: true,
  shapes: [
    geo.shapes[0],
    {
      name: 'Cancel', verts: [0, 0, 0, 1, 0, 0, 0, 1, 0], tris: [0, 1, 2, 0, 2, 1], normals: [],
      uvs: [0.6, 0.1, 0.9, 0.1, 0.6, 0.3], block: 21,
    },
  ],
};

const cancelOnlyGeo: NifGeo = { fo4: true, shapes: [cancelGeo.shapes[1]] };

type UvArgs = { uvs: number[]; tris: number[]; selection: number[]; vertex_count: number } & Record<string, unknown>;
type UvOps = Record<string, (args: UvArgs) => object>;
type Edit = { id: number; cat: string; key: string; value: number[] };

const zeroIslands = (a: UvArgs) => ({ ids: Array.from({ length: a.vertex_count }, () => 0) });

function makeHost(ops: UvOps = {}) {
  const handlers: UvOps = { islands: zeroIslands, ...ops };
  const applyEdits = vi.fn<(path: string, editsJson: string, outNif: string) => Promise<string>>(async () => 'set OK');
  const uvOp = vi.fn(async (op: string, argsJson: string) => {
    const handler = handlers[op];
    return JSON.stringify(handler ? handler(JSON.parse(argsJson) as UvArgs) : { error: `no mock for ${op}` });
  });
  const nif = { ApplyEdits: applyEdits, GetTexture: vi.fn(async () => ''), UvOp: uvOp } as unknown as NifHost;
  return { nif, applyEdits, uvOp };
}

const callsOf = (uvOp: ReturnType<typeof makeHost>['uvOp'], op: string) =>
  uvOp.mock.calls.filter(c => c[0] === op).map(c => JSON.parse(c[1]) as UvArgs);
const editsOf = (applyEdits: ReturnType<typeof makeHost>['applyEdits']) => JSON.parse(applyEdits.mock.calls[0][1]) as Edit[];
const idle = () => waitFor(() => expect((document.querySelector('.nif-uv-body') as HTMLFieldSetElement).disabled).toBe(false));
const button = (name: string | RegExp) => screen.getByRole('button', { name }) as HTMLButtonElement;
const canvasEl = () => document.querySelector('.nif-uv-canvas')!;

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

  it('Clamp 0-1 on an in-range selection leaves Save and Undo disabled when the host answers float32 values (detects a no-op operator marking the editor dirty)', async () => {
    vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(null);
    const { nif, uvOp } = makeHost({ clamp_tile: a => ({ uvs: a.uvs.map(x => Math.fround(x)) }) });
    render(<NifUvEditor geo={offGeo} nif={nif} nifPath={'C:\\mods\\a.nif'} texRoot="" onSaved={() => {}}
      onDirtyChange={() => {}} appendLog={() => {}} />);

    fireEvent.keyDown(canvasEl(), { key: 'a' });
    fireEvent.click(button('Clamp 0-1'));
    await idle();
    expect(callsOf(uvOp, 'clamp_tile')).toHaveLength(1);
    expect(button(/Save/).disabled).toBe(true);
    expect(button(/Undo/).disabled).toBe(true);
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

  it('Pack sends the selected island to the host and saves the UVs it answers (detects a result that is never applied or a request without the selection)', async () => {
    vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(null);
    const packed = [0.1, 0.1, 0.9, 0.1, 0.1, 0.9];
    const { nif, applyEdits, uvOp } = makeHost({ pack: () => ({ fits: true, uvs: packed }) });
    const onSaved = vi.fn<(result: string) => void>();
    render(<NifUvEditor geo={geo} nif={nif} nifPath={'C:\\mods\\a.nif'} texRoot="" onSaved={onSaved}
      onDirtyChange={() => {}} appendLog={() => {}} />);

    fireEvent.keyDown(canvasEl(), { key: 'a' });
    fireEvent.click(button('Pack'));
    await idle();
    fireEvent.click(button(/Save/));

    await waitFor(() => expect(onSaved).toHaveBeenCalledWith('set OK'));
    expect(callsOf(uvOp, 'pack')).toEqual([expect.objectContaining({ uvs: geo.shapes[0].uvs, tris: [0, 1, 2], selection: [0, 1, 2], margin: 0.01 })]);
    expect(editsOf(applyEdits)).toEqual([{ id: 7, cat: 'shape', key: 'uvs', value: packed }]);
  });

  it('logs a host failure, changes nothing and unlocks the editor (detects a rejected operation that leaves the editor locked or dirty)', async () => {
    vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(null);
    const { nif } = makeHost({ pack: () => ({ error: 'boom' }) });
    const appendLog = vi.fn<(line: string) => void>();
    render(<NifUvEditor geo={geo} nif={nif} nifPath={'C:\\mods\\a.nif'} texRoot="" onSaved={() => {}}
      onDirtyChange={() => {}} appendLog={appendLog} />);

    fireEvent.keyDown(canvasEl(), { key: 'a' });
    fireEvent.click(button('Pack'));
    await idle();
    expect(appendLog.mock.calls.some(call => String(call[0]).includes('pack failed -- boom'))).toBe(true);
    expect(button(/Save/).disabled).toBe(true);
  });

  it('shows the no-UV fallback without calling the host when no shape has UVs (detects the render crashing on a missing shape)', () => {
    const { nif, uvOp } = makeHost();
    render(<NifUvEditor geo={{ fo4: true, shapes: [{ ...geo.shapes[0], uvs: [] }] }} nif={nif} nifPath="a.nif" texRoot=""
      onSaved={() => {}} onDirtyChange={() => {}} appendLog={() => {}} />);

    expect(screen.getByText(/No shapes with UVs/)).toBeTruthy();
    expect(uvOp).not.toHaveBeenCalled();
  });

  it('locks the editor while an operation is pending and applies its answer afterwards (detects an edit or key landing between the request and the reply)', async () => {
    vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(null);
    const { nif, uvOp } = makeHost();
    let answer!: (text: string) => void;
    uvOp.mockImplementation((op: string) => (op === 'pack'
      ? new Promise<string>(resolve => { answer = resolve; })
      : Promise.resolve(JSON.stringify({ ids: [0, 0, 0] }))));
    render(<NifUvEditor geo={geo} nif={nif} nifPath={'C:\\mods\\a.nif'} texRoot="" onSaved={() => {}}
      onDirtyChange={() => {}} appendLog={() => {}} />);

    fireEvent.keyDown(canvasEl(), { key: 'a' });
    fireEvent.click(button('Pack'));
    expect((document.querySelector('.nif-uv-body') as HTMLFieldSetElement).disabled).toBe(true);
    fireEvent.keyDown(canvasEl(), { key: 'a', altKey: true });
    expect(document.querySelector('.nif-geo-info')?.textContent).toContain('3 selected');

    await act(async () => { answer(JSON.stringify({ fits: true, uvs: [0.1, 0.1, 0.9, 0.1, 0.1, 0.9] })); });
    await idle();
    expect(button(/Save/).disabled).toBe(false);
  });

  it('gives each new layout operator exactly one undo entry (detects operators pushing zero or several entries)', async () => {
    vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(null);
    const moved = (a: UvArgs) => ({ uvs: a.uvs.map(x => x * 0.5) });
    const { nif } = makeHost({
      islands: () => ({ ids: [0, 0, 0, 0, 0, 1, 1, 1, 1] }),
      pack: a => ({ fits: true, ...moved(a) }),
      average_scale: moved,
      smart_project: a => ({ fits: true, projected: 3, skipped: 0, ...moved(a) }),
      relax: moved,
    });
    render(<NifUvEditor geo={opsGeo} nif={nif} nifPath={'C:\\mods\\a.nif'} texRoot="" onSaved={() => {}}
      onDirtyChange={() => {}} appendLog={() => {}} />);

    fireEvent.keyDown(canvasEl(), { key: 'a' });
    await waitFor(() => expect(button('Average scale').disabled).toBe(false));
    for (const name of ['Pack', 'Average scale', 'Smart project', 'Relax']) {
      fireEvent.click(button(name));
      await idle();
      expect(button(/Save/).disabled).toBe(false);
      fireEvent.click(button(/Undo/));
      expect(button(/Save/).disabled).toBe(true);
      expect(button(/Undo/).disabled).toBe(true);
      expect(document.querySelector('.nif-dirty-count')?.textContent).toBe('No changes');
    }
  });

  it('selects every vertex of overlapping faces (detects a Select overlap button that does nothing)', async () => {
    vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(null);
    const { nif, uvOp } = makeHost({ overlapping_faces: () => ({ faces: [0, 1] }) });
    render(<NifUvEditor geo={overlapGeo} nif={nif} nifPath={'C:\\mods\\a.nif'} texRoot="" onSaved={() => {}}
      onDirtyChange={() => {}} appendLog={() => {}} />);

    fireEvent.click(button('Select overlap'));
    await waitFor(() => expect(document.querySelector('.nif-geo-info')?.textContent).toContain('6 selected'));
    expect(callsOf(uvOp, 'overlapping_faces')).toEqual([expect.objectContaining({ tris: [0, 1, 2, 3, 4, 5], hidden: [] })]);
    expect(button(/Save/).disabled).toBe(true);
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
    const plannedTris = [0, 2, 9, 0, 3, 2, 0, 4, 3, 0, 1, 4, 5, 6, 7, 5, 7, 10];
    const { nif, applyEdits, uvOp } = makeHost({
      seams_in_selection: () => ({ pairs: [0, 1, 2, 1] }),
      split_plan: () => ({ sources: [1, 2], tris: plannedTris }),
    });
    const onSaved = vi.fn<(result: string) => void>();
    render(<NifUvEditor geo={opsGeo} nif={nif} nifPath={'C:\\mods\\a.nif'} texRoot="" onSaved={onSaved}
      onDirtyChange={() => {}} appendLog={() => {}} />);

    expect(button('Clear seams').disabled).toBe(true);
    expect(button('Split seams').disabled).toBe(true);
    fireEvent.keyDown(canvasEl(), { key: 'a' });
    fireEvent.click(button('Mark seam'));
    await waitFor(() => expect(button('Split seams').disabled).toBe(false));
    fireEvent.click(button('Split seams'));

    await waitFor(() => expect(onSaved).toHaveBeenCalledWith('set OK'));
    const [marked] = callsOf(uvOp, 'seams_in_selection');
    expect([...marked.selection].sort((a, b) => a - b)).toEqual([0, 1, 2, 3, 4, 5, 6, 7, 8]);
    expect(marked.hidden).toEqual([]);
    expect(callsOf(uvOp, 'split_plan')).toEqual([expect.objectContaining({ vertex_count: 9, tris: opsGeo.shapes[0].tris, seams: [0, 1, 1, 2] })]);
    const edits = editsOf(applyEdits);
    const split = edits.find(e => e.key === 'split');
    const uvs = edits.find(e => e.key === 'uvs');
    expect(split!.value).toEqual([2, 1, 2, ...plannedTris]);
    expect(uvs!.value).toHaveLength(2 * (9 + 2));
    expect(uvs!.value.slice(18)).toEqual([...opsGeo.shapes[0].uvs.slice(2, 4), ...opsGeo.shapes[0].uvs.slice(4, 6)]);
    expect(edits.indexOf(split!)).toBeLessThan(edits.indexOf(uvs!));
    expect(button('Split seams').disabled).toBe(true);
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

  it('locks UV edits from Save until the parent reload finishes (detects an edit made during the save that the reload remount discards)', async () => {
    vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(null);
    let respond!: (text: string) => void;
    const applyEdits = vi.fn<(path: string, editsJson: string, outNif: string) => Promise<string>>(
      () => new Promise<string>(resolve => { respond = resolve; }));
    const nif = { ApplyEdits: applyEdits, GetTexture: vi.fn(async () => '') } as unknown as NifHost;
    let reloaded!: () => void;
    const onSaved = vi.fn<(result: string) => Promise<void>>(() => new Promise<void>(resolve => { reloaded = () => resolve(); }));
    render(<NifUvEditor geo={geo} nif={nif} nifPath={'C:\\mods\\a.nif'} texRoot="" onSaved={onSaved}
      onDirtyChange={() => {}} appendLog={() => {}} />);
    const body = () => document.querySelector('.nif-uv-body') as HTMLFieldSetElement;

    fireEvent.keyDown(document.querySelector('.nif-uv-canvas')!, { key: 'a' });
    fireEvent.click(screen.getByRole('button', { name: 'Flip U' }));
    fireEvent.click(screen.getByRole('button', { name: /Save/ }));
    await waitFor(() => expect(applyEdits).toHaveBeenCalledTimes(1));
    expect(body().disabled).toBe(true);
    fireEvent.click(screen.getByRole('button', { name: 'Flip V' }));

    respond('set OK');
    await waitFor(() => expect(onSaved).toHaveBeenCalledWith('set OK'));
    expect(body().disabled).toBe(true);
    reloaded();
    await waitFor(() => expect(body().disabled).toBe(false));

    fireEvent.click(screen.getByRole('button', { name: /Save/ }));
    await waitFor(() => expect(applyEdits).toHaveBeenCalledTimes(2));
    expect(applyEdits.mock.calls[1][1]).toBe(applyEdits.mock.calls[0][1]);
  });

  it('locks UV edits from Split seams until the parent reload finishes (detects a split followed by an edit the reload discards)', async () => {
    vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(null);
    let respond!: (text: string) => void;
    const { nif, applyEdits } = makeHost({
      seams_in_selection: () => ({ pairs: [0, 1] }),
      split_plan: () => ({ sources: [1], tris: opsGeo.shapes[0].tris }),
    });
    applyEdits.mockImplementation(() => new Promise<string>(resolve => { respond = resolve; }));
    let reloaded!: () => void;
    const onSaved = vi.fn<(result: string) => Promise<void>>(() => new Promise<void>(resolve => { reloaded = () => resolve(); }));
    render(<NifUvEditor geo={opsGeo} nif={nif} nifPath={'C:\\mods\\a.nif'} texRoot="" onSaved={onSaved}
      onDirtyChange={() => {}} appendLog={() => {}} />);
    const body = () => document.querySelector('.nif-uv-body') as HTMLFieldSetElement;

    fireEvent.keyDown(canvasEl(), { key: 'a' });
    fireEvent.click(button('Mark seam'));
    await waitFor(() => expect(button('Split seams').disabled).toBe(false));
    fireEvent.click(button('Split seams'));
    await waitFor(() => expect(applyEdits).toHaveBeenCalledTimes(1));
    expect(body().disabled).toBe(true);
    fireEvent.click(button('Flip U'));

    respond('set OK');
    await waitFor(() => expect(onSaved).toHaveBeenCalledWith('set OK'));
    expect(body().disabled).toBe(true);
    reloaded();
    await waitFor(() => expect(body().disabled).toBe(false));
  });

  it('names Average scale per shape while All shapes is on (detects a per-shape operator that reads as a cross-shape average)', () => {
    vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(null);
    const nif = { ApplyEdits: vi.fn(), GetTexture: vi.fn(async () => '') } as unknown as NifHost;
    render(<NifUvEditor geo={pairGeo} nif={nif} nifPath={'C:\\mods\\a.nif'} texRoot="" onSaved={() => {}}
      onDirtyChange={() => {}} appendLog={() => {}} />);

    expect(screen.getByRole('button', { name: 'Average scale' })).toBeTruthy();
    fireEvent.click(screen.getByRole('checkbox', { name: 'All shapes' }));
    expect(screen.getByRole('button', { name: 'Average scale per shape' })).toBeTruthy();
    expect(screen.queryByRole('button', { name: 'Average scale' })).toBeNull();
  });

  it('runs a layout operator on every shape when All shapes is on (detects all-shape mode that still edits only the active shape)', async () => {
    vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(null);
    const { nif, applyEdits } = makeHost({ pack: a => ({ fits: true, uvs: a.uvs.map(x => x * 0.5) }) });
    const onSaved = vi.fn<(result: string) => void>();
    render(<NifUvEditor geo={pairGeo} nif={nif} nifPath={'C:\\mods\\a.nif'} texRoot="" onSaved={onSaved}
      onDirtyChange={() => {}} appendLog={() => {}} />);

    expect(button('Pack').disabled).toBe(true);
    fireEvent.click(screen.getByRole('checkbox', { name: 'All shapes' }));
    expect(button('Pack into one shared tile').disabled).toBe(false);
    fireEvent.click(button('Pack into one shared tile'));
    await idle();
    fireEvent.click(button(/Save/));

    await waitFor(() => expect(onSaved).toHaveBeenCalledWith('set OK'));
    const payload = editsOf(applyEdits);
    expect(payload).toHaveLength(2);
    expect(payload.map(e => e.id).sort((a, b) => a - b)).toEqual([7, 9]);
    payload.forEach(e => expect(e.key).toBe('uvs'));
  });

  it('sends both shapes to one pack call with offset triangles and splits the answer back per shape when All shapes is on (detects per-shape packing that stacks them on the same 0-1 place)', async () => {
    vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(null);
    const { nif, applyEdits, uvOp } = makeHost({
      pack: a => ({ fits: true, uvs: a.uvs.map((x, i) => (i < a.uvs.length / 2 ? x * 0.4 : 0.5 + x * 0.4)) }),
    });
    const onSaved = vi.fn<(result: string) => void>();
    render(<NifUvEditor geo={sameIslandGeo} nif={nif} nifPath={'C:\\mods\\a.nif'} texRoot="" onSaved={onSaved}
      onDirtyChange={() => {}} appendLog={() => {}} />);

    fireEvent.click(screen.getByRole('checkbox', { name: 'All shapes' }));
    fireEvent.click(button('Pack into one shared tile'));
    await idle();
    fireEvent.click(button(/Save/));

    await waitFor(() => expect(onSaved).toHaveBeenCalledWith('set OK'));
    const sent = callsOf(uvOp, 'pack');
    expect(sent).toHaveLength(1);
    expect(sent[0].uvs).toHaveLength(12);
    expect(sent[0].tris).toEqual([0, 1, 2, 3, 4, 5]);
    expect(sent[0].selection).toEqual([0, 1, 2, 3, 4, 5]);
    const payload = editsOf(applyEdits);
    payload.sort((a, b) => a.id - b.id);
    expect(payload.map(e => e.id)).toEqual([7, 9]);
    const box = (value: number[]) => {
      const us = value.filter((_, i) => i % 2 === 0);
      const vs = value.filter((_, i) => i % 2 === 1);
      return { minU: Math.min(...us), maxU: Math.max(...us), minV: Math.min(...vs), maxV: Math.max(...vs) };
    };
    const a = box(payload[0].value);
    const b = box(payload[1].value);
    expect(a.maxU <= b.minU || b.maxU <= a.minU || a.maxV <= b.minV || b.maxV <= a.minV).toBe(true);
  });

  it('reports the skipped cancelling-normal island of one shape while the projectable shape still saves (detects an all-shape smart project that drops the skipped count)', async () => {
    vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(null);
    const { nif, applyEdits } = makeHost({
      smart_project: a => (a.tris.length === 6
        ? { fits: true, projected: 0, skipped: 1, uvs: a.uvs }
        : { fits: true, projected: 3, skipped: 0, uvs: a.uvs.map(x => x * 0.5) }),
    });
    const onSaved = vi.fn<(result: string) => void>();
    const appendLog = vi.fn<(line: string) => void>();
    render(<NifUvEditor geo={cancelGeo} nif={nif} nifPath={'C:\\mods\\a.nif'} texRoot="" onSaved={onSaved}
      onDirtyChange={() => {}} appendLog={appendLog} />);

    fireEvent.click(screen.getByRole('checkbox', { name: 'All shapes' }));
    fireEvent.click(button('Smart project per shape'));
    await idle();
    expect(appendLog.mock.calls.some(call => String(call[0]).includes('skipped 1 island(s) with cancelling normals on Cancel'))).toBe(true);
    fireEvent.click(button(/Save/));

    await waitFor(() => expect(onSaved).toHaveBeenCalledWith('set OK'));
    expect(editsOf(applyEdits).map(e => e.id)).toEqual([7]);
  });

  it('refuses All-shapes Smart project when no shape has a projectable island (detects a silent no-op with no message)', async () => {
    vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(null);
    const { nif } = makeHost({ smart_project: a => ({ fits: true, projected: 0, skipped: 1, uvs: a.uvs }) });
    const appendLog = vi.fn<(line: string) => void>();
    render(<NifUvEditor geo={cancelOnlyGeo} nif={nif} nifPath={'C:\\mods\\a.nif'} texRoot="" onSaved={() => {}}
      onDirtyChange={() => {}} appendLog={appendLog} />);

    fireEvent.click(screen.getByRole('checkbox', { name: 'All shapes' }));
    fireEvent.click(button('Smart project per shape'));
    await idle();
    expect(appendLog.mock.calls.some(call => String(call[0]).includes('no shape has a projectable island'))).toBe(true);
    expect(button(/Save/).disabled).toBe(true);
  });

  it('clears the marked seams when an automatic channel switch changes the shape (detects seams carried onto another shape)', async () => {
    vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(null);
    const { nif } = makeHost({ seams_in_selection: () => ({ pairs: [0, 1] }) });
    render(<NifUvEditor geo={pairChannelGeo} nif={nif} nifPath={'C:\\mods\\a.nif'} texRoot="" onSaved={() => {}}
      onDirtyChange={() => {}} appendLog={() => {}} />);

    fireEvent.keyDown(canvasEl(), { key: 'a' });
    fireEvent.click(button('Mark seam'));
    await waitFor(() => expect(button('Split seams').disabled).toBe(false));
    fireEvent.change(screen.getByLabelText('UV channel'), { target: { value: '1' } });
    expect((document.querySelector('.nif-uv-field select') as HTMLSelectElement).value).toBe('9');
    expect(button('Split seams').disabled).toBe(true);
  });

  it('clears the marked seams when a viewport pick moves to another shape (detects seams carried over by a pick)', async () => {
    vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(null);
    const { nif } = makeHost({ seams_in_selection: () => ({ pairs: [0, 1] }) });
    let api: { applyPick: (block: number, verts: readonly number[]) => boolean } | null = null;
    render(<NifUvEditor geo={pairChannelGeo} nif={nif} nifPath={'C:\\mods\\a.nif'} texRoot="" onSaved={() => {}}
      onDirtyChange={() => {}} appendLog={() => {}} onApi={next => { api = next; }} />);

    fireEvent.keyDown(canvasEl(), { key: 'a' });
    fireEvent.click(button('Mark seam'));
    await waitFor(() => expect(button('Split seams').disabled).toBe(false));
    act(() => { api!.applyPick(9, [0, 1]); });
    expect((document.querySelector('.nif-uv-field select') as HTMLSelectElement).value).toBe('9');
    expect(document.querySelector('.nif-geo-info')?.textContent).toContain('2 selected');
    expect(button('Split seams').disabled).toBe(true);
  });

  it('rejects a viewport pick on a shape without UVs and keeps the current selection (detects a fallback to the first UV shape that keeps the picked vertex)', () => {
    vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(null);
    const nif = { ApplyEdits: vi.fn(), GetTexture: vi.fn(async () => '') } as unknown as NifHost;
    let api: { applyPick: (block: number, verts: readonly number[]) => boolean } | null = null;
    render(<NifUvEditor geo={bareGeo} nif={nif} nifPath={'C:\\mods\\a.nif'} texRoot="" onSaved={() => {}}
      onDirtyChange={() => {}} appendLog={() => {}} onApi={next => { api = next; }} />);

    fireEvent.keyDown(document.querySelector('.nif-uv-canvas')!, { key: 'a' });
    let accepted = true;
    act(() => { accepted = api!.applyPick(9, [0]); });
    expect(accepted).toBe(false);
    expect(document.querySelector('.nif-geo-info')?.textContent).toContain('3 selected');
    expect((document.querySelector('.nif-uv-field select') as HTMLSelectElement).value).toBe('7');
  });

  it('draws UV2 inside the canvas after switching to it (detects a view framed on UV0 that leaves UV2 off screen)', () => {
    const strokeRect = vi.fn();
    const ctx = new Proxy({ strokeRect }, { get: (t, k) => (k in t ? Reflect.get(t, k) : vi.fn()) });
    vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(ctx as unknown as CanvasRenderingContext2D);
    const nif = { ApplyEdits: vi.fn(), GetTexture: vi.fn(async () => '') } as unknown as NifHost;
    render(<NifUvEditor geo={farGeo} nif={nif} nifPath={'C:\\mods\\a.nif'} texRoot="" onSaved={() => {}}
      onDirtyChange={() => {}} appendLog={() => {}} />);

    fireEvent.change(screen.getByLabelText('UV channel'), { target: { value: '1' } });
    const [px, py, s] = strokeRect.mock.calls.at(-1)! as number[];
    expect(5 * s + px).toBeGreaterThanOrEqual(0);
    expect(5.5 * s + px).toBeLessThanOrEqual(600);
    expect(5 * s + py).toBeGreaterThanOrEqual(0);
    expect(5.5 * s + py).toBeLessThanOrEqual(400);
  });

  it('refuses a channel switch while a transform is open (detects a switch that drops the open transform and its preview)', () => {
    vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(null);
    const nif = { ApplyEdits: vi.fn(), GetTexture: vi.fn(async () => '') } as unknown as NifHost;
    const appendLog = vi.fn<(line: string) => void>();
    render(<NifUvEditor geo={pairChannelGeo} nif={nif} nifPath={'C:\\mods\\a.nif'} texRoot="" onSaved={() => {}}
      onDirtyChange={() => {}} appendLog={appendLog} />);

    fireEvent.keyDown(document.querySelector('.nif-uv-canvas')!, { key: 'a' });
    fireEvent.keyDown(document.querySelector('.nif-uv-canvas')!, { key: 'g' });
    fireEvent.change(screen.getByLabelText('UV channel'), { target: { value: '1' } });
    expect((screen.getByLabelText('UV channel') as HTMLSelectElement).value).toBe('0');
    expect(appendLog.mock.calls.some(call => String(call[0]).includes('finish or cancel the transform'))).toBe(true);
  });

  it('applies an unwrap that uses every visible vertex when nothing is selected (detects an unwrap result discarded by the empty-selection guard)', async () => {
    vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(null);
    const unwrapped = [0.1, 0.1, 0.9, 0.1, 0.9, 0.9, 0.1, 0.9];
    const { nif, applyEdits, uvOp } = makeHost({ lscm_unwrap: () => ({ unwrapped: 1, skipped: 0, uvs: unwrapped }) });
    const onSaved = vi.fn<(result: string) => void>();
    let api: { applyPick: (block: number, verts: readonly number[]) => boolean } | null = null;
    render(<NifUvEditor geo={quadGeo} nif={nif} nifPath={'C:\\mods\\a.nif'} texRoot="" onSaved={onSaved}
      onDirtyChange={() => {}} appendLog={() => {}} onApi={next => { api = next; }} />);

    act(() => { api!.applyPick(14, [0]); });
    fireEvent.click(button('Pin'));
    act(() => { api!.applyPick(14, [1]); });
    fireEvent.click(button('Pin'));
    fireEvent.keyDown(canvasEl(), { key: 'a', altKey: true });
    expect(document.querySelector('.nif-geo-info')?.textContent).toContain('0 selected');
    fireEvent.click(button('Unwrap'));
    await idle();
    fireEvent.click(button(/Save/));

    await waitFor(() => expect(onSaved).toHaveBeenCalledWith('set OK'));
    const quad = quadGeo.shapes[0].uvs;
    const [sent] = callsOf(uvOp, 'lscm_unwrap');
    expect([...sent.selection].sort((a, b) => a - b)).toEqual([0, 1, 2, 3]);
    expect(sent).toMatchObject({ pin_vertices: [0, 1], pin_uvs: quad.slice(0, 4), uvs: quad, tris: [0, 1, 2, 0, 2, 3] });
    expect(editsOf(applyEdits)).toEqual([{ id: 14, cat: 'shape', key: 'uvs', value: unwrapped }]);
  });

  it('reports an open transform as pending and keeps Save disabled until it ends (detects a clean report that lets navigation drop the transform)', () => {
    vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(null);
    const nif = { ApplyEdits: vi.fn(), GetTexture: vi.fn(async () => '') } as unknown as NifHost;
    const onDirtyChange = vi.fn<(dirty: boolean) => void>();
    render(<NifUvEditor geo={geo} nif={nif} nifPath={'C:\\mods\\a.nif'} texRoot="" onSaved={() => {}}
      onDirtyChange={onDirtyChange} appendLog={() => {}} />);

    fireEvent.keyDown(document.querySelector('.nif-uv-canvas')!, { key: 'a' });
    fireEvent.keyDown(document.querySelector('.nif-uv-canvas')!, { key: 'g' });
    expect(onDirtyChange.mock.calls.at(-1)?.[0]).toBe(true);
    expect((screen.getByRole('button', { name: /Save/ }) as HTMLButtonElement).disabled).toBe(true);
    expect(document.querySelector('.nif-dirty-count')?.textContent).toBe('No changes');
    fireEvent.keyDown(document.querySelector('.nif-uv-canvas')!, { key: 'Escape' });
    expect(onDirtyChange.mock.calls.at(-1)?.[0]).toBe(false);
  });

  it('refuses Split seams while a transform is open (detects a split that writes the pre-transform UVs)', async () => {
    vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(null);
    const { nif, applyEdits } = makeHost({ seams_in_selection: () => ({ pairs: [0, 1] }) });
    const appendLog = vi.fn<(line: string) => void>();
    render(<NifUvEditor geo={opsGeo} nif={nif} nifPath={'C:\\mods\\a.nif'} texRoot="" onSaved={() => {}}
      onDirtyChange={() => {}} appendLog={appendLog} />);

    fireEvent.keyDown(canvasEl(), { key: 'a' });
    fireEvent.click(button('Mark seam'));
    await waitFor(() => expect(button('Split seams').disabled).toBe(false));
    fireEvent.keyDown(canvasEl(), { key: 'g' });
    fireEvent.click(button('Split seams'));
    expect(applyEdits).not.toHaveBeenCalled();
    expect(appendLog.mock.calls.some(call => String(call[0]).includes('splitting seams'))).toBe(true);
  });

  it('refuses Pack when the islands do not fit at the requested margin and applies nothing (detects an unpacked result reported as a finished pack)', async () => {
    vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(null);
    const { nif, uvOp } = makeHost({ pack: a => ({ fits: false, uvs: a.uvs }) });
    const appendLog = vi.fn<(line: string) => void>();
    render(<NifUvEditor geo={nineGeo} nif={nif} nifPath={'C:\\mods\\a.nif'} texRoot="" onSaved={() => {}}
      onDirtyChange={() => {}} appendLog={appendLog} />);

    fireEvent.keyDown(canvasEl(), { key: 'a' });
    fireEvent.change(screen.getByLabelText('Pack margin'), { target: { value: '0.25' } });
    fireEvent.click(button('Pack'));
    await idle();
    expect(callsOf(uvOp, 'pack')).toEqual([expect.objectContaining({ margin: 0.25 })]);
    expect(appendLog.mock.calls.some(call => String(call[0]).includes('do not fit'))).toBe(true);
    expect(button(/Save/).disabled).toBe(true);
  });

  it('refuses Smart project when the islands do not fit at the requested margin (detects a partial projection applied as a finished smart project)', async () => {
    vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(null);
    const { nif } = makeHost({ smart_project: a => ({ fits: false, projected: 0, skipped: 0, uvs: a.uvs }) });
    const appendLog = vi.fn<(line: string) => void>();
    render(<NifUvEditor geo={nineGeo} nif={nif} nifPath={'C:\\mods\\a.nif'} texRoot="" onSaved={() => {}}
      onDirtyChange={() => {}} appendLog={appendLog} />);

    fireEvent.keyDown(canvasEl(), { key: 'a' });
    fireEvent.change(screen.getByLabelText('Pack margin'), { target: { value: '0.25' } });
    fireEvent.click(button('Smart project'));
    await idle();
    expect(appendLog.mock.calls.some(call => String(call[0]).includes('do not fit'))).toBe(true);
    expect(button(/Save/).disabled).toBe(true);
  });

  it('ignores canvas mouse input while a save is running (detects a click that clears the selection the reload then discards)', async () => {
    vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(null);
    let respond!: (text: string) => void;
    const applyEdits = vi.fn<(path: string, editsJson: string, outNif: string) => Promise<string>>(
      () => new Promise<string>(resolve => { respond = resolve; }));
    const nif = { ApplyEdits: applyEdits, GetTexture: vi.fn(async () => '') } as unknown as NifHost;
    render(<NifUvEditor geo={geo} nif={nif} nifPath={'C:\\mods\\a.nif'} texRoot="" onSaved={() => {}}
      onDirtyChange={() => {}} appendLog={() => {}} />);
    const canvas = document.querySelector('.nif-uv-canvas')!;

    fireEvent.keyDown(canvas, { key: 'a' });
    fireEvent.click(screen.getByRole('button', { name: 'Flip U' }));
    fireEvent.click(screen.getByRole('button', { name: /Save/ }));
    await waitFor(() => expect(applyEdits).toHaveBeenCalledTimes(1));
    fireEvent.mouseDown(canvas, { clientX: 599, clientY: 399, button: 0 });
    fireEvent.mouseUp(canvas, { clientX: 599, clientY: 399, button: 0 });
    expect(document.querySelector('.nif-geo-info')?.textContent).toContain('3 selected');
    await act(async () => { respond('set OK'); });
  });
});

import { act, cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import type { ConfirmRequest } from './useDialogs';
import NifPanel from './NifPanel';

const dialogs = vi.hoisted(() => ({ confirm: vi.fn<(req: ConfirmRequest) => Promise<boolean>>() }));
type ViewportProps = {
  connectPoints?: { world: number[]; active: boolean }[];
  highlight?: unknown;
  onPickVertex?: (block: number, vertex: number) => void;
};
const viewport = vi.hoisted(() => ({
  render: vi.fn<(props: ViewportProps) => null>(),
}));
vi.mock('./useDialogs', () => ({ useDialogs: () => ({ confirm: dialogs.confirm }) }));
vi.mock('./NifViewport', () => ({
  default: (props: ViewportProps) => {
    viewport.render(props);
    return null;
  },
}));

const A = 'C:\\mods\\a.nif';
const B = 'C:\\mods\\b.nif';
const C = 'C:\\mods\\c.nif';
const MA = 'C:\\mods\\a.bgsm';
const MB = 'C:\\mods\\b.bgsm';

const geo = vi.fn<(path: string) => Promise<string>>();
const tree = vi.fn<(path: string) => Promise<string>>();
const applyEdits = vi.fn<(path: string, editsJson: string, outNif: string, expectedSource?: string) => Promise<string>>();
const stage = vi.fn<(name: string, base64: string) => Promise<string>>();
const browseSave = vi.fn<(title: string, filter: string) => Promise<string>>();
const inspect = vi.fn<(path: string) => Promise<string>>();
const setFields = vi.fn<(path: string, fieldsJson: string, outPath: string) => Promise<string>>();
const connectPoints = vi.fn<(path: string) => Promise<string>>();
const applyConnectPoints = vi.fn<(path: string, opsJson: string, outNif: string) => Promise<string>>();
let connectJson = '[]';
const uvOp = vi.fn<(op: string, argsJson: string) => Promise<string>>();
const vertexColorGet = vi.fn<(path: string) => Promise<string>>();
const vertexColorSet = vi.fn<(...args: unknown[]) => Promise<string>>();
const PAINT_GEO = JSON.stringify({
  fo4: true,
  shapes: [{
    name: 'Tri', verts: [0, 0, 0, 1, 0, 0, 0, 1, 0], tris: [0, 1, 2], normals: [],
    uvs: [0.25, 0.25, 0.75, 0.25, 0.25, 0.5], block: 7,
  }],
});
const PAINT_INFO = JSON.stringify({ shapes: [{ block: 7, name: 'Tri', vertices: 3, color: { present: true } }] });
const QUAD_GEO = JSON.stringify({
  fo4: true,
  shapes: [{
    name: 'Quad', verts: [0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0], tris: [0, 1, 2, 0, 2, 3], normals: [],
    uvs: [0.25, 0.25, 0.75, 0.25, 0.75, 0.75, 0.25, 0.75], block: 7,
  }],
});
const MIXED_GEO = JSON.stringify({
  fo4: true,
  shapes: [
    { name: 'Tri', verts: [0, 0, 0, 1, 0, 0, 0, 1, 0], tris: [0, 1, 2], normals: [], uvs: [0.25, 0.25, 0.75, 0.25, 0.25, 0.5], block: 7 },
    { name: 'Bare', verts: [0, 0, 0, 1, 0, 0, 0, 1, 0], tris: [0, 1, 2], normals: [], uvs: [], block: 9 },
  ],
});

const CONNECT_BLOCKS = [{
  block: 2, type: 'BSConnectPoint::Parents', name: 'CPA',
  points: [
    { parent: 'Chest', name: 'P-Muzzle', rotation: [1, 0, 0, 0], translation: [1, 2, 3], scale: 1, world: [11, 2, 3] },
    { parent: '', name: 'P-Grip', rotation: [1, 0, 0, 0], translation: [-4, 0.25, 8], scale: 2, world: [-4, 0.25, 8] },
  ],
}];

const labelOf = (path: string) => (path.endsWith('a.nif') || path.endsWith('a.bgsm') ? 'Alpha' : 'Beta');

function treeFor(name: string) {
  return JSON.stringify({
    fo4: true, file: name, source: `${name}-sha`,
    blocks: [{ id: 1, type: 'NiNode', name, group: 'Nodes', cat: '',
      fields: [{ key: 'name', label: 'Name', type: 'string', value: name }] }],
  });
}

function fieldsFor(value: string) {
  return JSON.stringify({
    fileName: 'skin.bgsm', version: 2,
    fields: [{ name: 'Texture', section: 'material', type: 'string', value }],
  });
}

function stubBridge() {
  vi.stubGlobal('chrome', {
    webview: {
      addEventListener: vi.fn(),
      removeEventListener: vi.fn(),
      hostObjects: {
        nif: {
          StageDroppedFile: stage, BrowseForSave: browseSave, Geo: geo, Tree: tree, ApplyEdits: applyEdits,
          ConnectPoints: connectPoints, ApplyConnectPoints: applyConnectPoints,
          VertexColorGet: vertexColorGet, VertexColorSet: vertexColorSet, UvOp: uvOp,
        },
        material: { Inspect: inspect, SetFields: setFields },
      },
    },
  });
}

function drop(name: string) {
  fireEvent.drop(document.querySelector('.papyrus-drop')!, { dataTransfer: { files: [new File(['payload'], name)] } });
}

function pathInput(pattern: RegExp) {
  return screen.getByPlaceholderText(pattern) as HTMLInputElement;
}

async function loadNif(path: string) {
  fireEvent.change(pathInput(/drag a \.nif here/), { target: { value: path } });
  fireEvent.click(document.querySelector('.nif-load-btn')!);
  await screen.findByDisplayValue(labelOf(path));
}

async function loadMaterial(path: string) {
  fireEvent.change(pathInput(/drag a \.bgsm/), { target: { value: path } });
  fireEvent.click(document.querySelector('.nif-load-btn')!);
  await screen.findByDisplayValue(labelOf(path));
}

function editField(current: string, value: string) {
  fireEvent.change(screen.getByDisplayValue(current), { target: { value } });
}

function save() {
  fireEvent.click(document.querySelector('.nif-save-btn')!);
}

function closeButton() {
  return document.querySelector('.papyrus-close')!;
}

function overlay() {
  return document.querySelector('.papyrus-overlay')!;
}

beforeEach(() => {
  vi.clearAllMocks();
  localStorage.clear();
  dialogs.confirm.mockResolvedValue(true);
  tree.mockImplementation(async p => treeFor(labelOf(p)));
  stage.mockImplementation(async name => 'C:\\staged\\' + name);
  browseSave.mockResolvedValue('C:\\mods\\kept.nif');
  geo.mockResolvedValue(JSON.stringify({ shapes: [] }));
  applyEdits.mockResolvedValue('set OK');
  uvOp.mockImplementation(async op => JSON.stringify(
    op === 'seams_in_selection' ? { pairs: [0, 1] } : op === 'split_plan' ? { sources: [1], tris: [0, 1, 2, 0, 2, 3] } : { ids: [0, 0, 0, 0] }));
  inspect.mockImplementation(async p => fieldsFor(labelOf(p)));
  setFields.mockResolvedValue('Set 1 field(s)');
  connectJson = '[]';
  connectPoints.mockImplementation(async () => connectJson);
  applyConnectPoints.mockResolvedValue('connect points OK (in-process)');
  stubBridge();
});

function lastViewportProps() {
  const calls = viewport.render.mock.calls;
  return calls[calls.length - 1][0];
}

afterEach(() => {
  cleanup();
  vi.unstubAllGlobals();
});

describe('NIF connect point markers in the edit viewport', () => {
  it('passes the parent points to the viewport and highlights the focused point', async () => {
    connectJson = JSON.stringify(CONNECT_BLOCKS);
    localStorage.setItem('nif.mode', 'edit');
    render(<NifPanel onClose={() => {}} />);
    await loadNif(A);
    fireEvent.focusIn(await screen.findByDisplayValue('P-Grip'));
    await waitFor(() => expect(lastViewportProps().connectPoints).toEqual([
      { world: [11, 2, 3], active: false },
      { world: [-4, 0.25, 8], active: true },
    ]));
  });

  it('refreshes the viewport markers after a connect point save', async () => {
    connectJson = JSON.stringify(CONNECT_BLOCKS);
    localStorage.setItem('nif.mode', 'edit');
    render(<NifPanel onClose={() => {}} />);
    await loadNif(A);
    const muzzle = await screen.findByDisplayValue('P-Muzzle');
    await waitFor(() => expect(lastViewportProps().connectPoints?.[0].world).toEqual([11, 2, 3]));
    connectJson = JSON.stringify([{
      ...CONNECT_BLOCKS[0],
      points: [{ ...CONNECT_BLOCKS[0].points[0], translation: [5, 2, 3], world: [15, 2, 3] }, CONNECT_BLOCKS[0].points[1]],
    }]);
    fireEvent.change(muzzle, { target: { value: 'P-Renamed' } });
    fireEvent.click(document.querySelector('.nif-cp-actions .papyrus-run')!);
    await waitFor(() => expect(lastViewportProps().connectPoints?.[0].world).toEqual([15, 2, 3]));
  });

  it('draws no marker for a point whose parent is unresolved or ambiguous', async () => {
    connectJson = JSON.stringify([{
      ...CONNECT_BLOCKS[0],
      points: [
        { parent: 'Ghost', name: 'P-Lost', rotation: [1, 0, 0, 0], translation: [0, 0, 0], scale: 1, resolution: 'unresolved' },
        { parent: 'Twin', name: 'P-Twin', rotation: [1, 0, 0, 0], translation: [0, 0, 0], scale: 1, resolution: 'ambiguous' },
        CONNECT_BLOCKS[0].points[0],
      ],
    }]);
    localStorage.setItem('nif.mode', 'edit');
    render(<NifPanel onClose={() => {}} />);
    await loadNif(A);
    await waitFor(() => expect(lastViewportProps().connectPoints).toEqual([{ world: [11, 2, 3], active: false }]));
  });
});

describe('NIF editor keeps edits with the file they belong to (GT-080)', () => {
  it('drops the previous file edits when another file is loaded', async () => {
    localStorage.setItem('nif.mode', 'edit');
    render(<NifPanel onClose={() => {}} />);
    await loadNif(A);
    editField('Alpha', 'Alpha edited');
    expect(screen.getByText('1 unsaved change')).toBeTruthy();

    await loadNif(B);
    expect(screen.queryByDisplayValue('Alpha edited')).toBeNull();
    expect(screen.getByText('No changes')).toBeTruthy();

    editField('Beta', 'Beta edited');
    save();
    await waitFor(() => expect(applyEdits).toHaveBeenCalled());
    expect(applyEdits.mock.calls[0][0]).toBe(B);
  });

  it('saves to the file loaded into the editor even when its geometry failed to load', async () => {
    localStorage.setItem('nif.mode', 'edit');
    render(<NifPanel onClose={() => {}} />);
    await loadNif(A);
    geo.mockRejectedValueOnce(new Error('no geometry'));
    await loadNif(B);
    editField('Beta', 'Beta edited');
    save();
    await waitFor(() => expect(applyEdits).toHaveBeenCalled());
    expect(applyEdits.mock.calls[0][0]).toBe(B);
  });

  it('keeps the save target when the path input is changed without loading', async () => {
    localStorage.setItem('nif.mode', 'edit');
    geo.mockRejectedValue(new Error('no geometry'));
    render(<NifPanel onClose={() => {}} />);
    await loadNif(A);
    editField('Alpha', 'Alpha edited');
    fireEvent.change(pathInput(/drag a \.nif here/), { target: { value: C } });
    save();
    await waitFor(() => expect(applyEdits).toHaveBeenCalled());
    expect(applyEdits.mock.calls[0][0]).toBe(A);
  });

  it('shows the loaded save target next to the save button', async () => {
    localStorage.setItem('nif.mode', 'edit');
    render(<NifPanel onClose={() => {}} />);
    await loadNif(A);
    expect(screen.getByText('Save target: a.nif')).toBeTruthy();
    expect(screen.getByTitle(A)).toBeTruthy();
  });

  it('keeps the edit and shows the ApplyEdits error when a save fails', async () => {
    localStorage.setItem('nif.mode', 'edit');
    render(<NifPanel onClose={() => {}} />);
    await loadNif(A);
    editField('Alpha', 'Alpha edited');
    applyEdits.mockResolvedValueOnce('Error: file is locked');
    save();
    await screen.findByText('Save failed -- Error: file is locked');
    expect(screen.getByText('1 unsaved change')).toBeTruthy();
    expect(screen.getByDisplayValue('Alpha edited')).toBeTruthy();
    expect(tree).toHaveBeenCalledTimes(1);
  });

  it('drops the previous material edits when another material is loaded', async () => {
    localStorage.setItem('nif.mode', 'materials');
    render(<NifPanel onClose={() => {}} />);
    await loadMaterial(MA);
    editField('Alpha', 'Alpha edited');
    expect(screen.getByText('1 unsaved change')).toBeTruthy();

    await loadMaterial(MB);
    expect(screen.queryByDisplayValue('Alpha edited')).toBeNull();

    editField('Beta', 'Beta edited');
    save();
    await waitFor(() => expect(setFields).toHaveBeenCalled());
    expect(setFields.mock.calls[0][0]).toBe(MB);
  });

  it('saves and reloads the loaded material when the path input is changed without loading', async () => {
    localStorage.setItem('nif.mode', 'materials');
    render(<NifPanel onClose={() => {}} />);
    await loadMaterial(MA);
    editField('Alpha', 'Alpha edited');

    fireEvent.change(pathInput(/drag a \.bgsm/), { target: { value: MB } });
    save();
    await waitFor(() => expect(setFields).toHaveBeenCalled());
    expect(setFields.mock.calls[0][0]).toBe(MA);
    await waitFor(() => expect(inspect).toHaveBeenCalledTimes(2));
    expect(inspect.mock.calls[1][0]).toBe(MA);
    expect(inspect).not.toHaveBeenCalledWith(MB);
  });
});

describe('NIF panel close with unsaved edits (GT-088)', () => {
  it('asks before closing a dirty editor and keeps the edit when cancelled', async () => {
    localStorage.setItem('nif.mode', 'edit');
    dialogs.confirm.mockResolvedValue(false);
    const onClose = vi.fn();
    render(<NifPanel onClose={onClose} />);
    await loadNif(A);
    editField('Alpha', 'Alpha edited');

    fireEvent.click(closeButton());
    await waitFor(() => expect(dialogs.confirm).toHaveBeenCalledTimes(1));
    expect(onClose).not.toHaveBeenCalled();
    expect(screen.getByDisplayValue('Alpha edited')).toBeTruthy();
  });

  it('closes after the user confirms discarding the dirty edit', async () => {
    localStorage.setItem('nif.mode', 'edit');
    const onClose = vi.fn();
    render(<NifPanel onClose={onClose} />);
    await loadNif(A);
    editField('Alpha', 'Alpha edited');

    fireEvent.click(closeButton());
    await waitFor(() => expect(onClose).toHaveBeenCalledTimes(1));
    expect(dialogs.confirm).toHaveBeenCalledTimes(1);
  });

  it('does not close silently on a backdrop click over a dirty editor', async () => {
    localStorage.setItem('nif.mode', 'edit');
    dialogs.confirm.mockResolvedValue(false);
    const onClose = vi.fn();
    render(<NifPanel onClose={onClose} />);
    await loadNif(A);
    editField('Alpha', 'Alpha edited');

    fireEvent.click(overlay());
    await waitFor(() => expect(dialogs.confirm).toHaveBeenCalledTimes(1));
    expect(onClose).not.toHaveBeenCalled();
  });

  it('asks before closing over unsaved material edits', async () => {
    localStorage.setItem('nif.mode', 'materials');
    dialogs.confirm.mockResolvedValue(false);
    const onClose = vi.fn();
    render(<NifPanel onClose={onClose} />);
    await loadMaterial(MA);
    editField('Alpha', 'Alpha edited');

    fireEvent.click(closeButton());
    await waitFor(() => expect(dialogs.confirm).toHaveBeenCalledTimes(1));
    expect(onClose).not.toHaveBeenCalled();
  });

  it('closes a clean panel without asking', async () => {
    localStorage.setItem('nif.mode', 'edit');
    const onClose = vi.fn();
    render(<NifPanel onClose={onClose} />);
    await loadNif(A);

    fireEvent.click(closeButton());
    await waitFor(() => expect(onClose).toHaveBeenCalledTimes(1));
    expect(dialogs.confirm).not.toHaveBeenCalled();
  });
});

describe('NIF load and mode switch over unsaved edits (GT-088)', () => {
  it('keeps the current file and edits when a tree load over a dirty editor is cancelled', async () => {
    localStorage.setItem('nif.mode', 'edit');
    dialogs.confirm.mockResolvedValue(false);
    render(<NifPanel onClose={() => {}} />);
    await loadNif(A);
    editField('Alpha', 'Alpha edited');

    fireEvent.change(pathInput(/drag a \.nif here/), { target: { value: B } });
    fireEvent.click(document.querySelector('.nif-load-btn')!);
    await waitFor(() => expect(dialogs.confirm).toHaveBeenCalledTimes(1));
    expect(tree).not.toHaveBeenCalledWith(B);
    expect(screen.getByDisplayValue('Alpha edited')).toBeTruthy();
  });

  it('loads the new file clean after the user confirms the discard', async () => {
    localStorage.setItem('nif.mode', 'edit');
    render(<NifPanel onClose={() => {}} />);
    await loadNif(A);
    editField('Alpha', 'Alpha edited');

    fireEvent.change(pathInput(/drag a \.nif here/), { target: { value: B } });
    fireEvent.click(document.querySelector('.nif-load-btn')!);
    await screen.findByDisplayValue('Beta');
    expect(dialogs.confirm).toHaveBeenCalledTimes(1);
    expect(screen.queryByDisplayValue('Alpha edited')).toBeNull();
    expect(screen.getByText('No changes')).toBeTruthy();
  });

  it('keeps the current material and edits when a material load over a dirty editor is cancelled', async () => {
    localStorage.setItem('nif.mode', 'materials');
    dialogs.confirm.mockResolvedValue(false);
    render(<NifPanel onClose={() => {}} />);
    await loadMaterial(MA);
    editField('Alpha', 'Alpha edited');

    fireEvent.change(pathInput(/drag a \.bgsm/), { target: { value: MB } });
    fireEvent.click(document.querySelector('.nif-load-btn')!);
    await waitFor(() => expect(dialogs.confirm).toHaveBeenCalledTimes(1));
    expect(inspect).not.toHaveBeenCalledWith(MB);
    expect(screen.getByDisplayValue('Alpha edited')).toBeTruthy();
  });

  it('loads the new material clean after the user confirms the discard', async () => {
    localStorage.setItem('nif.mode', 'materials');
    render(<NifPanel onClose={() => {}} />);
    await loadMaterial(MA);
    editField('Alpha', 'Alpha edited');

    fireEvent.change(pathInput(/drag a \.bgsm/), { target: { value: MB } });
    fireEvent.click(document.querySelector('.nif-load-btn')!);
    await screen.findByDisplayValue('Beta');
    expect(dialogs.confirm).toHaveBeenCalledTimes(1);
    expect(screen.queryByDisplayValue('Alpha edited')).toBeNull();
  });

  it('keeps the edit when a mode switch over a dirty editor is cancelled', async () => {
    localStorage.setItem('nif.mode', 'edit');
    dialogs.confirm.mockResolvedValue(false);
    render(<NifPanel onClose={() => {}} />);
    await loadNif(A);
    editField('Alpha', 'Alpha edited');

    fireEvent.click(screen.getByRole('button', { name: 'View' }));
    await waitFor(() => expect(dialogs.confirm).toHaveBeenCalledTimes(1));
    expect(screen.getByDisplayValue('Alpha edited')).toBeTruthy();
    expect(screen.getByText('1 unsaved change')).toBeTruthy();
  });

  it('switches mode and drops the edit after the user confirms the switch', async () => {
    localStorage.setItem('nif.mode', 'edit');
    render(<NifPanel onClose={() => {}} />);
    await loadNif(A);
    editField('Alpha', 'Alpha edited');

    fireEvent.click(screen.getByRole('button', { name: 'Materials' }));
    await screen.findByPlaceholderText(/drag a \.bgsm/);
    expect(dialogs.confirm).toHaveBeenCalledTimes(1);
    expect(screen.queryByDisplayValue('Alpha edited')).toBeNull();
  });

  it('keeps the open tree and its edits when a same-path reload fails', async () => {
    localStorage.setItem('nif.mode', 'edit');
    render(<NifPanel onClose={() => {}} />);
    await loadNif(A);
    editField('Alpha', 'Alpha edited');

    tree.mockRejectedValueOnce(new Error('file is locked'));
    fireEvent.click(document.querySelector('.nif-load-btn')!);
    await screen.findByText('Could not load tree: file is locked');
    expect(dialogs.confirm).toHaveBeenCalledTimes(1);
    expect(screen.getByDisplayValue('Alpha edited')).toBeTruthy();
    expect(screen.getByText('1 unsaved change')).toBeTruthy();
  });
});

describe('NIF UV split while a vertex paint is pending', () => {
  it('makes the UV split inert from the paint request until the reload after it', async () => {
    const canvas = vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(null);
    let reload: (value: string) => void = () => {};
    geo.mockResolvedValueOnce(PAINT_GEO)
      .mockImplementationOnce(() => new Promise<string>(resolve => { reload = resolve; }));
    vertexColorGet.mockResolvedValue(PAINT_INFO);
    let finish: (value: string) => void = () => {};
    vertexColorSet.mockImplementationOnce(() => new Promise<string>(resolve => { finish = resolve; }));
    const split = () => document.querySelector('.nif-uv-split')!;
    localStorage.setItem('nif.mode', 'uv');
    render(<NifPanel onClose={() => {}} />);
    fireEvent.change(pathInput(/drag a \.nif here/), { target: { value: A } });
    fireEvent.click(screen.getByRole('button', { name: 'Load & UV' }));
    await waitFor(() => expect((screen.getByRole('button', { name: 'Fill all' }) as HTMLButtonElement).disabled).toBe(false));
    expect(split().hasAttribute('inert')).toBe(false);

    fireEvent.click(screen.getByRole('button', { name: 'Fill all' }));
    await waitFor(() => expect(split().hasAttribute('inert')).toBe(true));
    await act(async () => { finish('vertex color OK (in-process)'); });

    await waitFor(() => expect(geo).toHaveBeenCalledTimes(2));
    expect(split().hasAttribute('inert')).toBe(true);
    await act(async () => { reload(PAINT_GEO); });
    await waitFor(() => expect(split().hasAttribute('inert')).toBe(false));
    canvas.mockRestore();
  });

  it('locks vertex paint while a UV seam split saves (detects paint editable under a split whose reload has not landed)', async () => {
    const canvas = vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(null);
    geo.mockResolvedValue(QUAD_GEO);
    vertexColorGet.mockResolvedValue(PAINT_INFO);
    let respond: (value: string) => void = () => {};
    applyEdits.mockImplementationOnce(() => new Promise<string>(resolve => { respond = resolve; }));
    localStorage.setItem('nif.mode', 'uv');
    render(<NifPanel onClose={() => {}} />);
    fireEvent.change(pathInput(/drag a \.nif here/), { target: { value: A } });
    fireEvent.click(screen.getByRole('button', { name: 'Load & UV' }));
    await waitFor(() => expect((screen.getByRole('button', { name: 'Fill all' }) as HTMLButtonElement).disabled).toBe(false));

    fireEvent.keyDown(document.querySelector('.nif-uv-canvas')!, { key: 'a' });
    fireEvent.click(screen.getByRole('button', { name: 'Mark seam' }));
    await waitFor(() => expect((screen.getByRole('button', { name: 'Split seams' }) as HTMLButtonElement).disabled).toBe(false));
    fireEvent.click(screen.getByRole('button', { name: 'Split seams' }));
    await waitFor(() => expect(applyEdits).toHaveBeenCalledTimes(1));
    expect((screen.getByRole('button', { name: 'Fill all' }) as HTMLButtonElement).disabled).toBe(true);

    await act(async () => { respond('set OK'); });
    await waitFor(() => expect(geo).toHaveBeenCalledTimes(2));
    await waitFor(() => expect((screen.getByRole('button', { name: 'Fill all' }) as HTMLButtonElement).disabled).toBe(false));
    canvas.mockRestore();
  });
});

describe('NIF loads superseded while pending (audit 140)', () => {
  it('ignores a tree response that a later drop superseded', async () => {
    localStorage.setItem('nif.mode', 'edit');
    let finishA: (value: string) => void = () => {};
    tree.mockImplementationOnce(() => new Promise<string>(resolve => { finishA = resolve; }));
    render(<NifPanel onClose={() => {}} />);
    fireEvent.change(pathInput(/drag a \.nif here/), { target: { value: A } });
    fireEvent.click(document.querySelector('.nif-load-btn')!);
    await waitFor(() => expect(tree).toHaveBeenCalledWith(A));

    drop('b.nif');
    await screen.findByDisplayValue('Beta');
    await act(async () => { finishA(treeFor('Alpha')); });
    expect(screen.getByDisplayValue('Beta')).toBeTruthy();
    expect(screen.queryByDisplayValue('Alpha')).toBeNull();
  });

  it('ignores a geometry response that a later load superseded', async () => {
    localStorage.setItem('nif.mode', 'view');
    let finishA: (value: string) => void = () => {};
    const oneShape = JSON.stringify({ shapes: [{ name: 'One', verts: [0, 0, 0], tris: [0, 1, 2], normals: [], uvs: [], block: 1 }] });
    const twoShapes = JSON.stringify({ shapes: [
      { name: 'One', verts: [0, 0, 0], tris: [0, 1, 2], normals: [], uvs: [], block: 1 },
      { name: 'Two', verts: [0, 0, 0], tris: [0, 1, 2], normals: [], uvs: [], block: 2 },
    ] });
    geo.mockImplementationOnce(() => new Promise<string>(resolve => { finishA = resolve; }))
      .mockResolvedValueOnce(twoShapes);
    render(<NifPanel onClose={() => {}} />);
    fireEvent.change(pathInput(/drag a \.nif here/), { target: { value: A } });
    fireEvent.click(screen.getByRole('button', { name: 'Load & View' }));
    await waitFor(() => expect(geo).toHaveBeenCalledWith(A));

    drop('b.nif');
    await screen.findByText(/^2 shape\(s\)/);
    await act(async () => { finishA(oneShape); });
    expect(screen.getByText(/^2 shape\(s\)/)).toBeTruthy();
    expect(screen.queryByText(/^1 shape\(s\)/)).toBeNull();
  });

  it('makes the open editor inert while a replacement tree load is pending', async () => {
    localStorage.setItem('nif.mode', 'edit');
    render(<NifPanel onClose={() => {}} />);
    await loadNif(A);
    let finishB: (value: string) => void = () => {};
    tree.mockImplementationOnce(() => new Promise<string>(resolve => { finishB = resolve; }));
    fireEvent.change(pathInput(/drag a \.nif here/), { target: { value: B } });
    fireEvent.click(document.querySelector('.nif-load-btn')!);
    await waitFor(() => expect(document.querySelector('.nif-edit-editorwrap')!.hasAttribute('inert')).toBe(true));

    await act(async () => { finishB(treeFor('Beta')); });
    await waitFor(() => expect(document.querySelector('.nif-edit-editorwrap')!.hasAttribute('inert')).toBe(false));
    expect(screen.getByDisplayValue('Beta')).toBeTruthy();
  });
});

describe('NIF dropped copy is saved only as a new file (audit 141)', () => {
  it('refuses plain Save for a dropped NIF and saves through Save As to the chosen path', async () => {
    localStorage.setItem('nif.mode', 'edit');
    render(<NifPanel onClose={() => {}} />);
    drop('a.nif');
    await screen.findByDisplayValue('Alpha');
    editField('Alpha', 'Alpha edited');

    expect(screen.getByText(/Temporary copy -- use Save As/)).toBeTruthy();
    expect((document.querySelector('.nif-save-btn') as HTMLButtonElement).disabled).toBe(true);

    fireEvent.click(screen.getByRole('button', { name: 'Save As…' }));
    await waitFor(() => expect(applyEdits).toHaveBeenCalledTimes(1));
    expect(applyEdits.mock.calls[0][0]).toBe('C:\\staged\\a.nif');
    expect(applyEdits.mock.calls[0][2]).toBe('C:\\mods\\kept.nif');
  });
});

describe('NIF tree save sends the loaded source fingerprint (audit 145)', () => {
  it('sends the fingerprint of the tree the editor loaded with the save', async () => {
    localStorage.setItem('nif.mode', 'edit');
    render(<NifPanel onClose={() => {}} />);
    await loadNif(A);
    editField('Alpha', 'Alpha edited');
    save();
    await waitFor(() => expect(applyEdits).toHaveBeenCalledTimes(1));
    expect(applyEdits.mock.calls[0][3]).toBe('Alpha-sha');
  });

  it('reloads the saved tree without asking to discard the edits that were just written (detects a discard prompt after an Edit-mode save)', async () => {
    localStorage.setItem('nif.mode', 'edit');
    render(<NifPanel onClose={() => {}} />);
    await loadNif(A);
    editField('Alpha', 'Alpha edited');
    save();
    await waitFor(() => expect(applyEdits).toHaveBeenCalledTimes(1));
    await waitFor(() => expect(tree).toHaveBeenCalledTimes(2));
    expect(dialogs.confirm).not.toHaveBeenCalled();
  });

  it('sends the fingerprint read after a connect point save, not the one from before it', async () => {
    connectJson = JSON.stringify(CONNECT_BLOCKS);
    localStorage.setItem('nif.mode', 'edit');
    render(<NifPanel onClose={() => {}} />);
    await loadNif(A);
    fireEvent.change(await screen.findByDisplayValue('P-Muzzle'), { target: { value: 'P-Renamed' } });
    tree.mockImplementation(async p => JSON.stringify({ ...JSON.parse(treeFor(labelOf(p))), source: 'Alpha-after-connect' }));
    fireEvent.click(document.querySelector('.nif-cp-actions .papyrus-run')!);
    await waitFor(() => expect(applyConnectPoints).toHaveBeenCalledTimes(1));
    await waitFor(() => expect(tree).toHaveBeenCalledTimes(2));
    expect(dialogs.confirm).not.toHaveBeenCalled();
    editField('Alpha', 'Alpha edited');
    save();
    await waitFor(() => expect(applyEdits).toHaveBeenCalledTimes(1));
    expect(applyEdits.mock.calls[0][3]).toBe('Alpha-after-connect');
  });
});

describe('NIF UV seam split sends the geometry fingerprint (audit 145)', () => {
  it('sends the digest of the geometry read with a split, and the digest of the reload after it with the next split', async () => {
    const canvas = vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(null);
    geo.mockResolvedValueOnce(JSON.stringify({ ...JSON.parse(QUAD_GEO), source: 'quad-sha' }))
      .mockResolvedValue(JSON.stringify({ ...JSON.parse(QUAD_GEO), source: 'quad-after-split' }));
    vertexColorGet.mockResolvedValue(PAINT_INFO);
    localStorage.setItem('nif.mode', 'uv');
    render(<NifPanel onClose={() => {}} />);
    fireEvent.change(pathInput(/drag a \.nif here/), { target: { value: A } });
    fireEvent.click(screen.getByRole('button', { name: 'Load & UV' }));
    await waitFor(() => expect((screen.getByRole('button', { name: 'Fill all' }) as HTMLButtonElement).disabled).toBe(false));

    fireEvent.keyDown(document.querySelector('.nif-uv-canvas')!, { key: 'a' });
    fireEvent.click(screen.getByRole('button', { name: 'Mark seam' }));
    await waitFor(() => expect((screen.getByRole('button', { name: 'Split seams' }) as HTMLButtonElement).disabled).toBe(false));
    fireEvent.click(screen.getByRole('button', { name: 'Split seams' }));
    await waitFor(() => expect(applyEdits).toHaveBeenCalledTimes(1));
    expect(applyEdits.mock.calls[0][3]).toBe('quad-sha');
    await waitFor(() => expect(geo).toHaveBeenCalledTimes(2));
    await waitFor(() => expect((screen.getByRole('button', { name: 'Fill all' }) as HTMLButtonElement).disabled).toBe(false));

    fireEvent.keyDown(document.querySelector('.nif-uv-canvas')!, { key: 'a' });
    fireEvent.click(screen.getByRole('button', { name: 'Mark seam' }));
    await waitFor(() => expect((screen.getByRole('button', { name: 'Split seams' }) as HTMLButtonElement).disabled).toBe(false));
    fireEvent.click(screen.getByRole('button', { name: 'Split seams' }));
    await waitFor(() => expect(applyEdits).toHaveBeenCalledTimes(2));
    expect(applyEdits.mock.calls[1][3]).toBe('quad-after-split');
    canvas.mockRestore();
  });
});

describe('NIF viewport pick on a shape without UVs', () => {
  it('clears the paint selection when the picked shape has no UV layout (detects a same-numbered vertex on the UV shape being selected)', async () => {
    const canvas = vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(null);
    geo.mockResolvedValue(MIXED_GEO);
    vertexColorGet.mockResolvedValue(PAINT_INFO);
    localStorage.setItem('nif.mode', 'uv');
    render(<NifPanel onClose={() => {}} />);
    fireEvent.change(pathInput(/drag a \.nif here/), { target: { value: A } });
    fireEvent.click(screen.getByRole('button', { name: 'Load & UV' }));
    await waitFor(() => expect(viewport.render.mock.calls.at(-1)?.[0].highlight).toEqual({ block: 7, verts: [] }));
    act(() => { viewport.render.mock.calls.at(-1)![0].onPickVertex!(9, 0); });
    expect(viewport.render.mock.calls.at(-1)![0].highlight).toBeNull();
    canvas.mockRestore();
  });
});

describe('NIF vertex paint refresh after a saved write (audit 180)', () => {
  it('reports the save, not a failed paint, and reloads the geometry when the shape list refresh is rejected', async () => {
    const canvas = vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(null);
    geo.mockResolvedValue(PAINT_GEO);
    vertexColorGet.mockResolvedValue(PAINT_INFO);
    vertexColorSet.mockResolvedValueOnce('vertex color OK (in-process)');
    localStorage.setItem('nif.mode', 'uv');
    render(<NifPanel onClose={() => {}} />);
    fireEvent.change(pathInput(/drag a \.nif here/), { target: { value: A } });
    fireEvent.click(screen.getByRole('button', { name: 'Load & UV' }));
    await waitFor(() => expect((screen.getByRole('button', { name: 'Fill all' }) as HTMLButtonElement).disabled).toBe(false));

    vertexColorGet.mockRejectedValueOnce(new Error('bridge gone'));
    fireEvent.click(screen.getByRole('button', { name: 'Fill all' }));
    expect(await screen.findByText('Saved, but the view could not be refreshed')).toBeTruthy();
    expect(screen.queryByText(/Paint failed/)).toBeNull();
    await waitFor(() => expect(geo).toHaveBeenCalledTimes(2));
    canvas.mockRestore();
  });
});

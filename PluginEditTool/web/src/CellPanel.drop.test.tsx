import { act, cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import CellPanel from './CellPanel';
import type { CellTextureStats } from './CellViewport';
import type { BaseObjectDragPayload } from './assetBrowser/dragPayload';

type DropHandler = (payload: BaseObjectDragPayload, point: { x: number; y: number; z: number }) => void;
type TextureStatsHandler = (stats: CellTextureStats) => void;

const bridge = vi.hoisted(() => ({
  grid: vi.fn<(...args: unknown[]) => Promise<string>>(),
  refs: vi.fn<(...args: unknown[]) => Promise<string>>(),
  geometry: vi.fn<(...args: unknown[]) => Promise<string>>(),
  place: vi.fn<(...args: unknown[]) => Promise<string>>(),
  save: vi.fn<(...args: unknown[]) => Promise<string>>(),
  pickPlugin: vi.fn<(...args: unknown[]) => Promise<unknown>>(),
  active: vi.fn<(...args: unknown[]) => Promise<string>>(),
  create: vi.fn<(...args: unknown[]) => Promise<string>>(),
  viewport: {
    onPlaceDrop: null as DropHandler | null,
    onTextureStats: null as TextureStatsHandler | null,
  },
}));

vi.mock('./backend', () => ({
  getCell: () => ({
    GetPlugins: async () => '["Fallout4.esm"]',
    SearchCells: async () => '[]',
    SearchWorldspaces: async () => '[]',
    GetPlacedReferencesAtGrid: bridge.grid,
    GetPlacedReferences: bridge.refs,
    GetGeometryBatch: bridge.geometry,
    GetGeometryBatchProgress: async () => '{"done":0,"total":0}',
    GetCellProperties: async () => '{"values":{},"descriptors":[]}',
    GetCellEditHistory: async () => '{"undoCount":1,"redoCount":0}',
    PlaceReferences: bridge.place,
  }),
  getBackend: () => ({ SavePlugin: bridge.save, GetActivePlugins: bridge.active, CreatePlugin: bridge.create }),
}));
vi.mock('./useDialogs', () => ({ useDialogs: () => ({ pickPlugin: bridge.pickPlugin }) }));
vi.mock('./CellViewport', () => ({
  default: (props: { onPlaceDrop?: DropHandler; onTextureStats?: TextureStatsHandler }) => {
    bridge.viewport.onPlaceDrop = props.onPlaceDrop ?? null;
    bridge.viewport.onTextureStats = props.onTextureStats ?? null;
    return null;
  },
}));

const oldRef = {
  formKey: '0A1:Fallout4.esm', editorId: 'OldCrate', recordType: 'REFR', baseFormKey: '0B1:Fallout4.esm',
  modelPath: 'Old.nif', position: { x: 0, y: 0, z: 0 }, rotation: { x: 0, y: 0, z: 0 }, scale: 1,
};
const newRef = {
  ...oldRef, formKey: '0ABC:CellEdits.esp', editorId: 'NewCrate', baseFormKey: '0B2:Fallout4.esm',
  modelPath: 'New.nif', position: { x: 10, y: 20, z: 30 },
};

function geometryReply(json: unknown): Promise<string> {
  const paths = JSON.parse(String(json)) as string[];
  const geometry: Record<string, { shapes: unknown[] }> = {};
  for (const p of paths) geometry[p] = { shapes: [] };
  return Promise.resolve(JSON.stringify({ count: paths.length, geometry }));
}

beforeEach(() => {
  vi.resetAllMocks();
  localStorage.clear();
  localStorage.setItem('cell.pickMode', 'grid');
  localStorage.setItem('cell.worldspace', '00003C:Fallout4.esm');
  localStorage.setItem('cell.gridX', '1');
  localStorage.setItem('cell.gridY', '0');
  bridge.viewport.onPlaceDrop = null;
  bridge.viewport.onTextureStats = null;
  bridge.grid.mockResolvedValue(JSON.stringify({
    cellFormKey: '0C1:Fallout4.esm', cellEditorId: 'TestCell', references: [oldRef], withModelCount: 1,
  }));
  bridge.refs.mockResolvedValue(JSON.stringify({ cellFormKey: '0C1:Fallout4.esm', references: [oldRef, newRef] }));
  bridge.geometry.mockImplementation(geometryReply);
  bridge.place.mockResolvedValue(JSON.stringify({
    plugin: 'CellEdits.esp', cellFormKey: '0C1:Fallout4.esm', created: ['0ABC:CellEdits.esp'], references: [],
  }));
  bridge.save.mockResolvedValue('');
  bridge.pickPlugin.mockResolvedValue({ target: 'CellEdits.esp', extra: '' });
  bridge.active.mockResolvedValue(JSON.stringify([{ Name: 'Fallout4.esm', Kind: 'master', LoadOrder: 0, Editable: false }]));
  bridge.create.mockResolvedValue("Created new plugin 'CellEdits.esp' in MO2 overwrite.");
});

afterEach(() => { cleanup(); localStorage.clear(); });

describe('Cell Viewer drop placement', () => {
  it('places a dropped object, fetches its unseen model and logs the created key', async () => {
    render(<CellPanel onClose={() => {}} />);
    fireEvent.click(screen.getByRole('button', { name: /Load Cell/ }));
    expect(await screen.findByText('Loaded 1 reference(s), all 1 unique mesh(es) resolved.')).toBeTruthy();
    await waitFor(() => expect(bridge.viewport.onPlaceDrop).not.toBeNull());

    const payload: BaseObjectDragPayload = {
      items: [{ formKey: '0B2:Fallout4.esm', signature: 'STAT', modelPath: 'New.nif', editorId: 'NewCrate' }],
    };
    await act(async () => { bridge.viewport.onPlaceDrop?.(payload, { x: 10, y: 20, z: 30 }); });

    await waitFor(() => expect(bridge.place).toHaveBeenCalledTimes(1));
    expect(bridge.place.mock.calls[0][0]).toBe('CellEdits.esp');
    expect(bridge.place.mock.calls[0][1]).toBe('0C1:Fallout4.esm');
    await waitFor(() => expect(bridge.geometry).toHaveBeenLastCalledWith(JSON.stringify(['New.nif'])));

    fireEvent.click(screen.getByRole('button', { name: /^Log \(/ }));
    expect(await screen.findByText(/PlaceReferences ok: created 0ABC:CellEdits\.esp/)).toBeTruthy();
    expect(screen.getByText(/geometry: converting 1 mesh/)).toBeTruthy();
  });

  it('raises the unseen log badge for texture failures only while another sidebar tab is open', async () => {
    const { container } = render(<CellPanel onClose={() => {}} />);
    fireEvent.click(screen.getByRole('button', { name: /Load Cell/ }));
    expect(await screen.findByText('Loaded 1 reference(s), all 1 unique mesh(es) resolved.')).toBeTruthy();
    await waitFor(() => expect(bridge.viewport.onTextureStats).not.toBeNull());
    expect(container.querySelector('.cell-sidebar-tab-dot')).toBeNull();

    await act(async () => {
      bridge.viewport.onTextureStats?.({
        ok: 1,
        resolveFail: 2,
        decodeFail: 0,
        noPath: 0,
        firstFailure: 'textures/missing.dds',
        report: { materialNotFound: 0, noPath: new Map(), missing: new Map() },
      });
    });

    expect(container.querySelector('.cell-sidebar-tab-dot')).not.toBeNull();

    fireEvent.click(screen.getByRole('button', { name: /^Log \(/ }));
    expect(container.querySelector('.cell-sidebar-tab-dot')).toBeNull();
    expect(await screen.findByText(/Textures: 1 loaded, 2 not found/)).toBeTruthy();
    await act(async () => {
      bridge.viewport.onTextureStats?.({
        ok: 0,
        resolveFail: 3,
        decodeFail: 0,
        noPath: 0,
        firstFailure: 'textures/other.dds',
        report: { materialNotFound: 0, noPath: new Map(), missing: new Map() },
      });
    });

    expect(await screen.findByText(/Textures: 0 loaded, 3 not found/)).toBeTruthy();
    expect(container.querySelector('.cell-sidebar-tab-dot')).toBeNull();
  });
});

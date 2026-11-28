import { cleanup, fireEvent, render, screen, within } from '@testing-library/react';
import { afterEach, expect, it, vi } from 'vitest';
import AssetTile from './AssetTile';
import type { BaseObjectItem } from './baseObjects';
import TileGrid, { type TileGridProps } from './TileGrid';

vi.mock('./previewRenderer', async importOriginal => {
  const actual = await importOriginal<typeof import('./previewRenderer')>();
  return { ...actual, createPreviewRenderer: () => ({ render: async () => null }) };
});

afterEach(() => { cleanup(); });

function item(n: number, patch: Partial<BaseObjectItem> = {}): BaseObjectItem {
  return {
    formKey: `F${n}`,
    hexId: `0000000${n}`,
    editorId: `Obj00${n}`,
    name: `Object ${n}`,
    signature: 'STAT',
    typeName: 'Static',
    modelPath: `Meshes/object${n}.nif`,
    winningPlugin: 'Fallout4.esm',
    deleted: false,
    hasModel: true,
    ...patch,
  };
}

function tileProps(overrides: Partial<Parameters<typeof AssetTile>[0]> = {}) {
  return {
    item: item(1),
    style: {},
    selected: false,
    active: false,
    onClick: vi.fn(),
    onContextMenu: vi.fn(),
    onDragStart: vi.fn(),
    onDragEnd: vi.fn(),
    ...overrides,
  };
}

it('shows the editor ID, the plugin and the record badge', () => {
  render(<AssetTile {...tileProps()} />);
  const tile = screen.getByRole('option', { name: 'Obj001' });
  expect(within(tile).getByText('Obj001')).toBeTruthy();
  expect(within(tile).getByText('Fallout4.esm')).toBeTruthy();
  expect(within(tile).getByText('Statics')).toBeTruthy();
  expect(within(tile).queryByText('No model')).toBeNull();
});

it('marks a model-less object and falls back to its Form ID for the plugin line', () => {
  render(<AssetTile {...tileProps({ item: item(2, { modelPath: '', winningPlugin: '' }) })} />);
  const tile = screen.getByRole('option', { name: 'Obj002' });
  expect(within(tile).getByText('No model')).toBeTruthy();
  expect(within(tile).getByText('00000002')).toBeTruthy();
  expect(tile.getAttribute('title')).toBe('No model');
});

it('reflects selection, the active item and deleted records in its state', () => {
  render(<AssetTile {...tileProps({ selected: true, active: true, item: item(3, { deleted: true }) })} />);
  const tile = screen.getByRole('option', { name: 'Obj003' });
  expect(tile.getAttribute('aria-selected')).toBe('true');
  expect(tile.classList.contains('selected')).toBe(true);
  expect(tile.classList.contains('active')).toBe(true);
  expect(tile.classList.contains('deleted')).toBe(true);
});

it('reports click, context menu and the drag lifecycle with the item', () => {
  const props = tileProps();
  render(<AssetTile {...props} />);
  const tile = screen.getByRole('option', { name: 'Obj001' });

  fireEvent.click(tile);
  expect(props.onClick).toHaveBeenCalledWith(expect.objectContaining({ editorId: 'Obj001' }), expect.anything());

  fireEvent.contextMenu(tile);
  expect(props.onContextMenu).toHaveBeenCalledTimes(1);

  fireEvent.dragStart(tile, { dataTransfer: { setData: vi.fn(), effectAllowed: 'all' } });
  expect(props.onDragStart).toHaveBeenCalledWith(expect.objectContaining({ formKey: 'F1' }), expect.anything());

  fireEvent.dragEnd(tile);
  expect(props.onDragEnd).toHaveBeenCalledTimes(1);
});

function gridProps(overrides: Partial<TileGridProps> = {}): TileGridProps {
  return {
    items: [item(1), item(2), item(3)],
    tileSize: 144,
    loading: false,
    selectedKeys: new Set<string>(),
    activeKey: null,
    onRowClick: vi.fn(),
    onRowContext: vi.fn(),
    onRowDragStart: vi.fn(),
    onRowDragEnd: vi.fn(),
    onNavigate: vi.fn(),
    onKeyDown: vi.fn(),
    ...overrides,
  };
}

it('marks selected tiles and the active tile in the grid', () => {
  render(<TileGrid {...gridProps({ selectedKeys: new Set(['F2']), activeKey: 'F2' })} />);
  expect(screen.getByRole('option', { name: 'Obj002' }).getAttribute('aria-selected')).toBe('true');
  expect(screen.getByRole('option', { name: 'Obj001' }).getAttribute('aria-selected')).toBe('false');
  expect(screen.getByRole('option', { name: 'Obj002' }).classList.contains('active')).toBe(true);
});

it('routes the arrow keys to navigation and everything else to the parent handler', () => {
  const props = gridProps();
  render(<TileGrid {...props} />);
  const grid = screen.getByRole('listbox', { name: 'Base objects' });

  fireEvent.keyDown(grid, { key: 'ArrowRight' });
  expect(props.onNavigate).toHaveBeenLastCalledWith(1, false);

  fireEvent.keyDown(grid, { key: 'ArrowLeft', shiftKey: true });
  expect(props.onNavigate).toHaveBeenLastCalledWith(-1, true);

  fireEvent.keyDown(grid, { key: 'ArrowDown' });
  expect(props.onNavigate).toHaveBeenLastCalledWith(1, false);

  fireEvent.keyDown(grid, { key: 'Enter' });
  expect(props.onKeyDown).toHaveBeenCalledTimes(1);
  expect(props.onNavigate).toHaveBeenCalledTimes(3);
});

it('reserves the full height for every row but mounts only a window of tiles', () => {
  const many = Array.from({ length: 200 }, (_, i) => item(i + 1));
  const { container } = render(<TileGrid {...gridProps({ items: many })} />);
  const spacer = container.querySelector('.ab-tiles-spacer') as HTMLElement;
  expect(spacer.style.height).toBe(`${200 * (144 + 44 + 8)}px`);
  const mounted = screen.getAllByRole('option');
  expect(mounted.length).toBeGreaterThan(0);
  expect(mounted.length).toBeLessThan(200);
});

it('shows the empty message only when nothing is loading', () => {
  const { rerender } = render(<TileGrid {...gridProps({ items: [], loading: true })} />);
  expect(screen.queryByText('No objects match the filter.')).toBeNull();
  rerender(<TileGrid {...gridProps({ items: [], loading: false })} />);
  expect(screen.getByText('No objects match the filter.')).toBeTruthy();
});

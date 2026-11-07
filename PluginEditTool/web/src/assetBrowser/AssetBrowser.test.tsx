import { act, cleanup, fireEvent, render, screen, waitFor, within } from '@testing-library/react';
import { afterEach, beforeEach, expect, it, vi } from 'vitest';
import AssetBrowser from './AssetBrowser';
import type { BaseObjectItem, BaseObjectQuery } from './baseObjects';
import { BASE_OBJECT_MIME, decodeDragPayload } from './dragPayload';

beforeEach(() => { localStorage.clear(); });

afterEach(() => {
  cleanup();
  vi.useRealTimers();
});

function item(n: number): BaseObjectItem {
  return {
    formKey: `F${n}`,
    hexId: n.toString(16).toUpperCase().padStart(8, '0'),
    editorId: `Obj${String(n).padStart(3, '0')}`,
    name: `Object ${n}`,
    signature: 'STAT',
    typeName: 'Static',
    modelPath: `Meshes/object${n}.nif`,
    winningPlugin: 'Fallout4.esm',
    deleted: false,
    hasModel: true,
  };
}

function makeHost(total: number) {
  const ListBaseObjects = vi.fn(async (query: BaseObjectQuery) => {
    const offset = query.offset ?? 0;
    const limit = query.limit ?? 100;
    const all = Array.from({ length: total }, (_, i) => item(i + 1));
    return JSON.stringify({
      total,
      offset,
      limit,
      sortBy: query.sortBy ?? 'editorId',
      sortDir: query.sortDir ?? 'asc',
      typeCounts: { STAT: total },
      items: all.slice(offset, offset + limit),
    });
  });
  return { ListBaseObjects };
}

function rowOf(text: string): HTMLElement {
  return screen.getByText(text).closest('[role="row"]') as HTMLElement;
}

it('loads the first page and shows the count', async () => {
  const host = makeHost(3);
  render(<AssetBrowser host={host} onPlace={() => {}} onPreview={() => {}} />);
  expect(await screen.findByText('Obj001')).toBeTruthy();
  expect(screen.getByText('3 of 3 objects')).toBeTruthy();
  expect(host.ListBaseObjects).toHaveBeenCalledWith(expect.objectContaining({ offset: 0, limit: 100 }));
});

it('waits 250 ms after typing before querying', async () => {
  vi.useFakeTimers();
  const host = makeHost(3);
  render(<AssetBrowser host={host} onPlace={() => {}} onPreview={() => {}} />);
  await act(async () => { await vi.advanceTimersByTimeAsync(0); });
  expect(host.ListBaseObjects).toHaveBeenCalledTimes(1);

  fireEvent.change(screen.getByLabelText('Search objects'), { target: { value: 'obj' } });
  await act(async () => { await vi.advanceTimersByTimeAsync(249); });
  expect(host.ListBaseObjects).toHaveBeenCalledTimes(1);

  await act(async () => { await vi.advanceTimersByTimeAsync(1); });
  expect(host.ListBaseObjects).toHaveBeenCalledTimes(2);
  expect(host.ListBaseObjects).toHaveBeenLastCalledWith(expect.objectContaining({ query: 'obj' }));
});

it('toggles the sort direction when the same header is clicked again', async () => {
  const host = makeHost(3);
  render(<AssetBrowser host={host} onPlace={() => {}} onPreview={() => {}} />);
  await screen.findByText('Obj001');

  fireEvent.click(screen.getByRole('button', { name: 'Sort by name' }));
  await waitFor(() => expect(host.ListBaseObjects).toHaveBeenLastCalledWith(expect.objectContaining({ sortBy: 'name', sortDir: 'asc' })));

  fireEvent.click(screen.getByRole('button', { name: 'Sort by name' }));
  await waitFor(() => expect(host.ListBaseObjects).toHaveBeenLastCalledWith(expect.objectContaining({ sortBy: 'name', sortDir: 'desc' })));
  expect(screen.getByRole('button', { name: 'Sort by name' }).getAttribute('aria-sort')).toBe('descending');
});

it('places the selected row when Enter is pressed in the table', async () => {
  const onPlace = vi.fn();
  render(<AssetBrowser host={makeHost(3)} onPlace={onPlace} onPreview={() => {}} />);
  await screen.findByText('Obj001');

  fireEvent.click(rowOf('Obj002'));
  fireEvent.keyDown(screen.getByRole('grid'), { key: 'Enter' });
  expect(onPlace).toHaveBeenCalledWith([expect.objectContaining({ editorId: 'Obj002' })], 'viewCentre');
});

it('opens the object menu, copies the Form ID and closes on Escape', async () => {
  const writeText = vi.fn(async () => {});
  Object.defineProperty(navigator, 'clipboard', { value: { writeText }, configurable: true });
  render(<AssetBrowser host={makeHost(3)} onPlace={() => {}} onPreview={() => {}} />);
  await screen.findByText('Obj001');

  fireEvent.contextMenu(rowOf('Obj001'));
  const menu = screen.getByRole('menu', { name: 'Object actions' });
  expect(within(menu).getByRole('menuitem', { name: 'Place at view centre' })).toBeTruthy();
  fireEvent.keyDown(menu, { key: 'Escape' });
  expect(screen.queryByRole('menu')).toBeNull();

  fireEvent.contextMenu(rowOf('Obj001'));
  fireEvent.click(within(screen.getByRole('menu')).getByRole('menuitem', { name: 'Copy Form ID' }));
  expect(writeText).toHaveBeenCalledWith('00000001');
  expect(screen.queryByRole('menu')).toBeNull();
});

it('writes the dragged rows into the base-object drag payload', async () => {
  const setData = vi.fn();
  render(<AssetBrowser host={makeHost(3)} onPlace={() => {}} onPreview={() => {}} />);
  await screen.findByText('Obj003');

  fireEvent.dragStart(rowOf('Obj003'), { dataTransfer: { setData, effectAllowed: 'all' } });
  expect(setData).toHaveBeenCalledWith(BASE_OBJECT_MIME, expect.any(String));
  const payload = decodeDragPayload(setData.mock.calls[0][1] as string);
  expect(payload?.items.map(i => i.editorId)).toEqual(['Obj003']);
});

it('loads the next page of 100 when Load more is pressed', async () => {
  const host = makeHost(150);
  render(<AssetBrowser host={host} onPlace={() => {}} onPreview={() => {}} />);
  await screen.findByText('Obj001');
  expect(screen.getByText('100 of 150 objects')).toBeTruthy();

  fireEvent.click(screen.getByRole('button', { name: 'Load more' }));
  await waitFor(() => expect(screen.getByText('150 of 150 objects')).toBeTruthy());
  expect(host.ListBaseObjects).toHaveBeenLastCalledWith(expect.objectContaining({ offset: 100, limit: 100 }));
  expect(screen.queryByRole('button', { name: 'Load more' })).toBeNull();
});

it('toggles a search field and filters by a type with its count', async () => {
  const host = makeHost(3);
  render(<AssetBrowser host={host} onPlace={() => {}} onPreview={() => {}} />);
  await screen.findByText('Obj001');

  expect(screen.getByRole('button', { name: 'Model path' }).getAttribute('aria-pressed')).toBe('false');
  fireEvent.click(screen.getByRole('button', { name: 'Model path' }));
  expect(screen.getByRole('button', { name: 'Model path' }).getAttribute('aria-pressed')).toBe('true');
  await waitFor(() => expect(host.ListBaseObjects).toHaveBeenLastCalledWith(expect.objectContaining({ scopes: ['editorId', 'name', 'hexId', 'modelPath'] })));

  expect(screen.getByRole('button', { name: 'All (3)' })).toBeTruthy();
  fireEvent.click(screen.getByRole('button', { name: 'Static (3)' }));
  await waitFor(() => expect(host.ListBaseObjects).toHaveBeenLastCalledWith(expect.objectContaining({ type: 'STAT' })));
});

it('clears the search with Escape and offers past searches', async () => {
  const host = makeHost(3);
  render(<AssetBrowser host={host} onPlace={() => {}} onPreview={() => {}} />);
  const search = screen.getByLabelText('Search objects') as HTMLInputElement;

  fireEvent.change(search, { target: { value: 'Obj002' } });
  fireEvent.keyDown(search, { key: 'Enter' });
  await waitFor(() => expect(host.ListBaseObjects).toHaveBeenLastCalledWith(expect.objectContaining({ query: 'Obj002' })));

  fireEvent.keyDown(search, { key: 'Escape' });
  expect(search.value).toBe('');

  fireEvent.focus(search);
  fireEvent.click(screen.getByRole('button', { name: 'Obj002' }));
  expect(search.value).toBe('Obj002');
});

it('saves the chosen snap grid', async () => {
  render(<AssetBrowser host={makeHost(1)} onPlace={() => {}} onPreview={() => {}} />);
  fireEvent.change(screen.getByLabelText('Snap grid'), { target: { value: '16' } });
  expect(localStorage.getItem('fo4ide.assetBrowser.snapGrid')).toBe('16');
});

it('selects every loaded row with Ctrl+A and copies Form IDs with Ctrl+C', async () => {
  const writeText = vi.fn(async () => {});
  Object.defineProperty(navigator, 'clipboard', { value: { writeText }, configurable: true });
  render(<AssetBrowser host={makeHost(3)} onPlace={() => {}} onPreview={() => {}} />);
  await screen.findByText('Obj001');

  const grid = screen.getByRole('grid');
  fireEvent.keyDown(grid, { key: 'a', ctrlKey: true });
  expect(grid.querySelectorAll('[aria-selected="true"]').length).toBe(3);

  fireEvent.keyDown(grid, { key: 'c', ctrlKey: true });
  expect(writeText).toHaveBeenCalledWith('00000001\n00000002\n00000003');
});

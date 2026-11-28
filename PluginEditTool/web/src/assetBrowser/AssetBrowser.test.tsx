import { act, cleanup, fireEvent, render, screen, waitFor, within } from '@testing-library/react';
import { afterEach, beforeEach, expect, it, vi } from 'vitest';
import AssetBrowser, { type AssetBrowserProps } from './AssetBrowser';
import type { BaseObjectItem, BaseObjectQuery } from './baseObjects';
import { BASE_OBJECT_MIME, decodeDragPayload } from './dragPayload';
import { useDrawerState } from './useDrawerState';

vi.mock('./previewRenderer', async importOriginal => {
  const actual = await importOriginal<typeof import('./previewRenderer')>();
  return { ...actual, createPreviewRenderer: () => ({ render: async () => 'data:image/png;base64,AAAA' }) };
});

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
    const limit = query.limit ?? 200;
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

type Host = ReturnType<typeof makeHost>;
type BrowserProps = Omit<AssetBrowserProps, 'drawer'>;

function Harness(props: BrowserProps) {
  const drawer = useDrawerState();
  return (
    <>
      <button type="button" onClick={drawer.openDrawer}>Open browser</button>
      <AssetBrowser {...props} drawer={drawer} />
    </>
  );
}

function renderOpen(host: Host, overrides: Partial<BrowserProps> = {}) {
  render(<Harness host={host} onPlace={() => {}} onPreview={() => {}} {...overrides} />);
  fireEvent.click(screen.getByRole('button', { name: 'Open browser' }));
}

function showList() {
  fireEvent.click(screen.getByRole('button', { name: 'List' }));
}

function rowOf(text: string): HTMLElement {
  return within(screen.getByRole('grid')).getByText(text).closest('[role="row"]') as HTMLElement;
}

function tile(name: string): HTMLElement {
  return screen.getByRole('option', { name });
}

it('loads the first page of 200 and shows the count', async () => {
  const host = makeHost(3);
  renderOpen(host);
  expect(await screen.findByRole('option', { name: 'Obj001' })).toBeTruthy();
  expect(screen.getByText('3 of 3 objects')).toBeTruthy();
  expect(host.ListBaseObjects).toHaveBeenCalledWith(expect.objectContaining({ offset: 0, limit: 200 }));
});

it('shows the indexing state, not no-match, while the first page is pending', async () => {
  const ListBaseObjects = vi.fn<(query: BaseObjectQuery) => Promise<string>>(() => new Promise<string>(() => {}));
  renderOpen({ ListBaseObjects });
  expect(await screen.findByRole('status')).toBeTruthy();
  expect(screen.getByRole('status').textContent).toContain('Indexing objects (first run takes up to ~30 s)');
  expect(screen.getAllByText(/Indexing objects/)).toHaveLength(2);
  expect(screen.queryByText('No objects match the filter.')).toBeNull();
  expect(screen.queryByText(/of 0 objects/)).toBeNull();
});

it('shows no-match and a zero count only after a finished empty scan', async () => {
  renderOpen(makeHost(0));
  expect(await screen.findByText('No objects match the filter.')).toBeTruthy();
  expect(screen.getByText('0 of 0 objects')).toBeTruthy();
  expect(screen.queryByRole('status')).toBeNull();
});

it('shows the scan error with a retry that reruns the scan and clears the error', async () => {
  const good = makeHost(2);
  let calls = 0;
  const ListBaseObjects = vi.fn(async (query: BaseObjectQuery) => {
    calls += 1;
    if (calls === 1) return JSON.stringify({ error: 'Native index unavailable.' });
    return good.ListBaseObjects(query);
  });
  renderOpen({ ListBaseObjects });
  expect((await screen.findByRole('alert')).textContent).toContain('Native index unavailable.');
  expect(screen.getByText('Scan failed')).toBeTruthy();
  expect(screen.queryByText('No objects match the filter.')).toBeNull();
  fireEvent.click(screen.getByRole('button', { name: 'Retry' }));
  expect(await screen.findByRole('option', { name: 'Obj001' })).toBeTruthy();
  expect(screen.getByText('2 of 2 objects')).toBeTruthy();
  expect(screen.queryByRole('alert')).toBeNull();
  expect(ListBaseObjects).toHaveBeenCalledTimes(2);
});

it('does not query while the drawer is closed', async () => {
  const host = makeHost(3);
  render(<Harness host={host} onPlace={() => {}} onPreview={() => {}} />);
  await act(async () => { await Promise.resolve(); });
  expect(host.ListBaseObjects).not.toHaveBeenCalled();
});

it('waits 250 ms after typing before querying', async () => {
  vi.useFakeTimers();
  const host = makeHost(3);
  renderOpen(host);
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
  renderOpen(host);
  await screen.findByRole('option', { name: 'Obj001' });
  showList();

  fireEvent.click(screen.getByRole('button', { name: 'Sort by name' }));
  await waitFor(() => expect(host.ListBaseObjects).toHaveBeenLastCalledWith(expect.objectContaining({ sortBy: 'name', sortDir: 'asc' })));

  fireEvent.click(screen.getByRole('button', { name: 'Sort by name' }));
  await waitFor(() => expect(host.ListBaseObjects).toHaveBeenLastCalledWith(expect.objectContaining({ sortBy: 'name', sortDir: 'desc' })));
  expect(screen.getByRole('button', { name: 'Sort by name' }).getAttribute('aria-sort')).toBe('descending');
});

it('places the selected row when Enter is pressed in the table', async () => {
  const onPlace = vi.fn();
  renderOpen(makeHost(3), { onPlace });
  await screen.findByRole('option', { name: 'Obj001' });
  showList();

  fireEvent.click(rowOf('Obj002'));
  fireEvent.keyDown(screen.getByRole('grid'), { key: 'Enter' });
  expect(onPlace).toHaveBeenCalledWith([expect.objectContaining({ editorId: 'Obj002' })], 'viewCentre');
});

it('places the selected tile when Enter is pressed in the tile grid', async () => {
  const onPlace = vi.fn();
  renderOpen(makeHost(3), { onPlace });
  await screen.findByRole('option', { name: 'Obj001' });

  fireEvent.click(tile('Obj002'));
  fireEvent.keyDown(screen.getByRole('listbox', { name: 'Base objects' }), { key: 'Enter' });
  expect(onPlace).toHaveBeenCalledWith([expect.objectContaining({ editorId: 'Obj002' })], 'viewCentre');
});

it('moves the selection with the arrow keys in the tile grid', async () => {
  renderOpen(makeHost(3));
  await screen.findByRole('option', { name: 'Obj001' });

  fireEvent.click(tile('Obj001'));
  fireEvent.keyDown(screen.getByRole('listbox', { name: 'Base objects' }), { key: 'ArrowDown' });
  expect(tile('Obj002').getAttribute('aria-selected')).toBe('true');
  expect(tile('Obj001').getAttribute('aria-selected')).toBe('false');
});

it('selects tiles with Ctrl-click and Shift-click', async () => {
  renderOpen(makeHost(3));
  await screen.findByRole('option', { name: 'Obj001' });

  fireEvent.click(tile('Obj001'));
  fireEvent.click(tile('Obj003'), { ctrlKey: true });
  expect(tile('Obj001').getAttribute('aria-selected')).toBe('true');
  expect(tile('Obj002').getAttribute('aria-selected')).toBe('false');
  expect(tile('Obj003').getAttribute('aria-selected')).toBe('true');

});

it('selects a range of tiles with Shift-click from the anchor', async () => {
  renderOpen(makeHost(3));
  await screen.findByRole('option', { name: 'Obj001' });

  fireEvent.click(tile('Obj001'));
  fireEvent.click(tile('Obj003'), { shiftKey: true });
  expect(tile('Obj001').getAttribute('aria-selected')).toBe('true');
  expect(tile('Obj002').getAttribute('aria-selected')).toBe('true');
  expect(tile('Obj003').getAttribute('aria-selected')).toBe('true');
});

it('opens the object menu, copies the Form ID and closes on Escape', async () => {
  const writeText = vi.fn(async () => {});
  Object.defineProperty(navigator, 'clipboard', { value: { writeText }, configurable: true });
  renderOpen(makeHost(3));
  await screen.findByRole('option', { name: 'Obj001' });
  showList();

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

it('sends a tile to the preview from its object menu', async () => {
  const onPreview = vi.fn();
  renderOpen(makeHost(3), { onPreview });
  await screen.findByRole('option', { name: 'Obj002' });

  fireEvent.contextMenu(tile('Obj002'));
  fireEvent.click(within(screen.getByRole('menu')).getByRole('menuitem', { name: 'Preview at view centre' }));
  expect(onPreview).toHaveBeenCalledWith(expect.objectContaining({ editorId: 'Obj002' }));
});

it('writes the dragged rows into the base-object drag payload', async () => {
  const setData = vi.fn();
  renderOpen(makeHost(3));
  await screen.findByRole('option', { name: 'Obj001' });
  showList();

  fireEvent.dragStart(rowOf('Obj003'), { dataTransfer: { setData, effectAllowed: 'all' } });
  expect(setData).toHaveBeenCalledWith(BASE_OBJECT_MIME, expect.any(String));
  const payload = decodeDragPayload(setData.mock.calls[0][1] as string);
  expect(payload?.items.map(i => i.editorId)).toEqual(['Obj003']);
});

it('drags every selected tile together', async () => {
  const setData = vi.fn();
  renderOpen(makeHost(3));
  await screen.findByRole('option', { name: 'Obj001' });

  fireEvent.click(tile('Obj001'));
  fireEvent.click(tile('Obj002'), { ctrlKey: true });
  fireEvent.dragStart(tile('Obj002'), { dataTransfer: { setData, effectAllowed: 'all' } });
  const payload = decodeDragPayload(setData.mock.calls[0][1] as string);
  expect(payload?.items.map(i => i.editorId)).toEqual(['Obj001', 'Obj002']);
});

it('scans every page of the catalogue, not just the first one', async () => {
  const host = makeHost(450);
  renderOpen(host);
  await waitFor(() => expect(screen.getByText('450 of 450 objects')).toBeTruthy());
  expect(host.ListBaseObjects.mock.calls.map(([query]) => [query.offset, query.limit])).toEqual([[0, 200], [200, 200], [400, 200]]);
});

it('toggles a search field and filters by a record type from the category tree', async () => {
  const host = makeHost(3);
  renderOpen(host);
  await screen.findByRole('option', { name: 'Obj001' });

  expect(screen.getByRole('button', { name: 'Model path' }).getAttribute('aria-pressed')).toBe('false');
  fireEvent.click(screen.getByRole('button', { name: 'Model path' }));
  expect(screen.getByRole('button', { name: 'Model path' }).getAttribute('aria-pressed')).toBe('true');
  await waitFor(() => expect(host.ListBaseObjects).toHaveBeenLastCalledWith(expect.objectContaining({ scopes: ['editorId', 'name', 'hexId', 'modelPath'] })));

  expect(screen.getByRole('button', { name: /^All/ })).toBeTruthy();
  fireEvent.click(screen.getByRole('button', { name: /^Statics/ }));
  await waitFor(() => expect(host.ListBaseObjects).toHaveBeenCalledWith(expect.objectContaining({ type: 'STAT' })));
});

it('hides and shows the category tree from the footer', async () => {
  renderOpen(makeHost(3));
  await screen.findByRole('option', { name: 'Obj001' });

  fireEvent.click(screen.getByRole('button', { name: 'Hide categories' }));
  expect(screen.queryByRole('tree')).toBeNull();
  fireEvent.click(screen.getByRole('button', { name: 'Show categories' }));
  expect(screen.getByRole('tree', { name: 'Categories' })).toBeTruthy();
});

it('stars an object into the Favourites category', async () => {
  renderOpen(makeHost(3));
  fireEvent.click(await screen.findByRole('option', { name: 'Obj001' }));

  fireEvent.click(screen.getByRole('button', { name: 'Add to favourites' }));
  await waitFor(() => expect(screen.getByRole('button', { name: /Favourites/ }).textContent).toContain('1'));
  expect(screen.getByRole('button', { name: 'Remove from favourites' })).toBeTruthy();
});

it('shows rendered previews on the tiles and in the detail pane', async () => {
  renderOpen(makeHost(3));
  await screen.findByRole('option', { name: 'Obj001' });
  await waitFor(() => expect(document.querySelector('.ab-tile img')).not.toBeNull());

  fireEvent.click(tile('Obj001'));
  expect(await screen.findByRole('img', { name: /Model preview\. Drag to orbit/ })).toBeTruthy();
});

it('clears the search with Escape and offers past searches', async () => {
  const host = makeHost(3);
  renderOpen(host);
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
  renderOpen(makeHost(1));
  fireEvent.change(screen.getByLabelText('Snap grid'), { target: { value: '16' } });
  expect(localStorage.getItem('fo4ide.assetBrowser.snapGrid')).toBe('16');
});

it('selects every loaded row with Ctrl+A and copies Form IDs with Ctrl+C', async () => {
  const writeText = vi.fn(async () => {});
  Object.defineProperty(navigator, 'clipboard', { value: { writeText }, configurable: true });
  renderOpen(makeHost(3));
  await screen.findByRole('option', { name: 'Obj001' });
  showList();

  const grid = screen.getByRole('grid');
  fireEvent.keyDown(grid, { key: 'a', ctrlKey: true });
  expect(grid.querySelectorAll('[aria-selected="true"]').length).toBe(3);

  fireEvent.keyDown(grid, { key: 'c', ctrlKey: true });
  expect(writeText).toHaveBeenCalledWith('00000001\n00000002\n00000003');
});

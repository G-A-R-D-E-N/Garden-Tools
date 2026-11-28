import { act, cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';
import type { ArchiveEntry, ArchiveHost } from './backend';
import ArchivePanel from './ArchivePanel';

const listing = (entries: ArchiveEntry[], extra: Record<string, unknown> = {}) => JSON.stringify({
  archiveName: 'A.ba2',
  totalCount: entries.length,
  shownCount: entries.length,
  truncated: false,
  entries,
  ...extra,
});

function installHost(overrides: Partial<ArchiveHost> = {}): ArchiveHost {
  const host: ArchiveHost = {
    BrowseForFile: vi.fn(async () => ''),
    BrowseForFolder: vi.fn(async () => ''),
    BrowseForSave: vi.fn(async () => ''),
    List: vi.fn(async () => listing([])),
    ExtractFile: vi.fn(async () => ''),
    ExtractSelected: vi.fn(async () => 'Extracted 1 file'),
    ExtractAll: vi.fn(async () => 'Extracted 2 files'),
    Compare: vi.fn(async () => JSON.stringify({ added: [], removed: [], changed: [], identicalCount: 0 })),
    Pack: vi.fn(async () => 'RESULT: success'),
    OpenFolder: vi.fn(async () => ''),
    ...overrides,
  };
  Object.assign(window, { chrome: { webview: { hostObjects: { archive: host } } } });
  return host;
}

function renderPanel(path = 'Data/A.ba2') {
  localStorage.setItem('archive.archivePath', path);
  render(<ArchivePanel onClose={() => {}} />);
  return {
    pathInput: screen.getByPlaceholderText('Path to a .ba2/.bsa…') as HTMLInputElement,
    filterInput: screen.getByPlaceholderText('Filter (contains)…') as HTMLInputElement,
  };
}

const button = (label: string) => screen.getByText(label).closest('button') as HTMLButtonElement;

afterEach(() => {
  cleanup();
  localStorage.clear();
  Reflect.deleteProperty(window, 'chrome');
});

describe('ArchivePanel listing', () => {
  it('renders the listed entries with their counts and enables extraction once a row is selected', async () => {
    const host = installHost({
      List: vi.fn(async () => listing(
        [{ path: 'meshes/a.nif', size: 2048 }, { path: 'textures/b.dds', size: 4096 }],
        { totalCount: 5, shownCount: 2, truncated: true },
      )),
    });
    renderPanel();

    fireEvent.click(screen.getByRole('button', { name: 'List' }));

    await screen.findByText('meshes/a.nif');
    expect(screen.getByText('textures/b.dds')).toBeTruthy();
    expect(screen.getByText(/2 of 5 entries shown/)).toBeTruthy();
    expect(screen.getByText(/narrow with a filter/)).toBeTruthy();
    expect(screen.getByText('4,096 B')).toBeTruthy();
    expect(host.List).toHaveBeenCalledWith('Data/A.ba2', '', 2000, 'simple');
    expect(button('Extract All').disabled).toBe(false);
    expect(button('Extract Selected (0)').disabled).toBe(true);

    const rowCheckbox = screen.getByText('meshes/a.nif').closest('tr')!.querySelector('input')!;
    fireEvent.click(rowCheckbox);
    await waitFor(() => expect(button('Extract Selected (1)').disabled).toBe(false));
  });
});

describe('ArchivePanel archive path changes', () => {
  it('clears the list, extraction banner and folder link when the path is edited', async () => {
    const host = installHost({
      List: vi.fn(async () => listing([{ path: 'meshes/a.nif', size: 10 }])),
      BrowseForFolder: vi.fn(async () => 'D:/out'),
      ExtractAll: vi.fn(async () => 'Extracted 2 files'),
    });
    const { pathInput } = renderPanel();

    fireEvent.click(screen.getByRole('button', { name: 'List' }));
    await screen.findByText('meshes/a.nif');
    fireEvent.click(screen.getByText(/^Extract All/));
    await screen.findByText('Extracted 2 files');
    expect(host.ExtractAll).toHaveBeenCalledWith('Data/A.ba2', 'D:/out', '', 0, 'simple', false);
    expect(screen.getByText('Open folder')).toBeTruthy();

    fireEvent.change(pathInput, { target: { value: 'Data/B.ba2' } });

    expect(screen.queryByText('meshes/a.nif')).toBeNull();
    expect(screen.queryByText('Extracted 2 files')).toBeNull();
    expect(screen.queryByText('Open folder')).toBeNull();
    expect(screen.queryByText(/entr(y|ies) shown/)).toBeNull();
    expect(screen.getByText('Pick a .ba2/.bsa, then List.')).toBeTruthy();
  });

  it('discards a List response that arrives for the path the user already left', async () => {
    let resolveList: (value: string) => void = () => {};
    installHost({ List: vi.fn(() => new Promise<string>(resolve => { resolveList = resolve; })) });
    const { pathInput } = renderPanel();

    fireEvent.click(screen.getByRole('button', { name: 'List' }));
    expect(button('Loading…').disabled).toBe(true);

    fireEvent.change(pathInput, { target: { value: 'Data/B.ba2' } });
    await act(async () => { resolveList(listing([{ path: 'meshes/stale.nif', size: 1 }])); });

    expect(screen.queryByText('meshes/stale.nif')).toBeNull();
    expect(screen.queryByText(/entr(y|ies) shown/)).toBeNull();
    await waitFor(() => expect(button('List').disabled).toBe(false));
    expect(screen.getByText('Pick a .ba2/.bsa, then List.')).toBeTruthy();
  });
});

describe('ArchivePanel out-of-order List responses', () => {
  it('keeps the newer list when an older slower List resolves after it', async () => {
    let resolveSlow: (value: string) => void = () => {};
    const host = installHost({
      List: vi.fn((_path: string, filter: string) => (filter === ''
        ? new Promise<string>(resolve => { resolveSlow = resolve; })
        : Promise.resolve(listing([{ path: 'meshes/new.nif', size: 2 }])))),
    });
    const { filterInput } = renderPanel();

    fireEvent.click(screen.getByRole('button', { name: 'List' }));
    fireEvent.change(filterInput, { target: { value: 'nif' } });
    fireEvent.keyDown(filterInput, { key: 'Enter' });

    await screen.findByText('meshes/new.nif');
    expect(host.List).toHaveBeenLastCalledWith('Data/A.ba2', 'nif', 2000, 'simple');

    await act(async () => { resolveSlow(listing([{ path: 'meshes/old.nif', size: 3 }])); });

    expect(screen.queryByText('meshes/old.nif')).toBeNull();
    expect(screen.getByText('meshes/new.nif')).toBeTruthy();
    expect(button('List').disabled).toBe(false);
  });

  it('ignores an older List resolving while a newer one is still in flight and stays busy', async () => {
    const pending: Array<(value: string) => void> = [];
    const host = installHost({ List: vi.fn(() => new Promise<string>(resolve => { pending.push(resolve); })) });
    const { filterInput } = renderPanel();

    fireEvent.click(screen.getByRole('button', { name: 'List' }));
    fireEvent.change(filterInput, { target: { value: 'nif' } });
    fireEvent.keyDown(filterInput, { key: 'Enter' });
    expect(host.List).toHaveBeenCalledTimes(2);

    await act(async () => { pending[0](listing([{ path: 'meshes/old.nif', size: 3 }])); });
    expect(screen.queryByText('meshes/old.nif')).toBeNull();
    expect(screen.getByText('Loading…')).toBeTruthy();

    await act(async () => { pending[1](listing([{ path: 'meshes/new.nif', size: 2 }])); });
    await screen.findByText('meshes/new.nif');
    expect(button('List').disabled).toBe(false);
  });

  it('ignores an older List rejection after a path edit and clears busy', async () => {
    let rejectSlow: (reason: unknown) => void = () => {};
    installHost({ List: vi.fn(() => new Promise<string>((_resolve, reject) => { rejectSlow = reject; })) });
    const { pathInput } = renderPanel();

    fireEvent.click(screen.getByRole('button', { name: 'List' }));
    fireEvent.change(pathInput, { target: { value: 'Data/B.ba2' } });
    await act(async () => { rejectSlow(new Error('disk offline')); });

    expect(screen.queryByText(/Could not read archive/)).toBeNull();
    expect(button('List').disabled).toBe(false);
  });
});

describe('ArchivePanel stale filter extraction', () => {
  it('shows a visible warning and refuses Extract All when filter changes after listing', async () => {
    const host = installHost({
      List: vi.fn(async () => listing([{ path: 'meshes/a.nif', size: 10 }])),
      BrowseForFolder: vi.fn(async () => 'D:/out'),
      ExtractAll: vi.fn(async () => 'Extracted 1 file'),
    });
    const { filterInput } = renderPanel();

    fireEvent.click(screen.getByRole('button', { name: 'List' }));
    await screen.findByText('meshes/a.nif');
    fireEvent.click(screen.getByText(/^Extract All/));
    await screen.findByText('Extracted 1 file');

    fireEvent.change(filterInput, { target: { value: 'dds' } });
    fireEvent.click(screen.getByText(/^Extract All/));

    expect(host.ExtractAll).toHaveBeenCalledTimes(1);
    expect(screen.getByText('The archive or filter changed after this list was loaded. List again before extracting.')).toBeTruthy();
    expect(screen.queryByText('Open folder')).toBeNull();
  });

  it('guards Extract Selected on stale filter, clears lastOutDir on non-Extracted result, and ignores Enter mid-extraction', async () => {
    let finishExtract: (value: string) => void = () => {};
    const host = installHost({
      List: vi.fn(async () => listing([{ path: 'meshes/a.nif', size: 10 }])),
      BrowseForFolder: vi.fn(async () => 'D:/out'),
      ExtractSelected: vi.fn(async () => 'Nothing matching selection'),
      ExtractAll: vi.fn(() => new Promise<string>(resolve => { finishExtract = resolve; })),
    });
    const { filterInput } = renderPanel();

    fireEvent.click(screen.getByRole('button', { name: 'List' }));
    await screen.findByText('meshes/a.nif');
    const rowCheckbox = screen.getByText('meshes/a.nif').closest('tr')!.querySelector('input')!;
    fireEvent.click(rowCheckbox);
    fireEvent.click(button('Extract Selected (1)'));
    await screen.findByText('Nothing matching selection');
    expect(host.ExtractSelected).toHaveBeenCalledWith('Data/A.ba2', JSON.stringify(['meshes/a.nif']), 'D:/out', false);
    expect(screen.queryByText('Open folder')).toBeNull();

    fireEvent.change(filterInput, { target: { value: 'dds' } });
    fireEvent.click(button('Extract Selected (1)'));
    expect(host.ExtractSelected).toHaveBeenCalledTimes(1);
    expect(screen.getByText('The archive or filter changed after this list was loaded. List again before extracting.')).toBeTruthy();

    fireEvent.change(filterInput, { target: { value: '' } });
    fireEvent.click(screen.getByText(/^Extract All/));
    await waitFor(() => expect(host.ExtractAll).toHaveBeenCalledTimes(1));

    fireEvent.keyDown(filterInput, { key: 'Enter' });
    expect(host.List).toHaveBeenCalledTimes(1);

    await act(async () => { finishExtract('Extracted 42 files'); });
    await screen.findByText('Extracted 42 files');
    expect(screen.getByText('Open folder')).toBeTruthy();
    expect(button('List').disabled).toBe(false);
  });
});

describe('ArchivePanel extraction in flight', () => {
  it('reports the Extract Selected result when the path is edited while files are still being written', async () => {
    let finishExtract: (value: string) => void = () => {};
    const host = installHost({
      List: vi.fn(async () => listing([{ path: 'meshes/a.nif', size: 10 }])),
      BrowseForFolder: vi.fn(async () => 'D:/out'),
      ExtractSelected: vi.fn(() => new Promise<string>(resolve => { finishExtract = resolve; })),
    });
    const { pathInput } = renderPanel();

    fireEvent.click(screen.getByRole('button', { name: 'List' }));
    await screen.findByText('meshes/a.nif');
    fireEvent.click(screen.getByText('meshes/a.nif').closest('tr')!.querySelector('input')!);
    fireEvent.click(button('Extract Selected (1)'));

    await waitFor(() => expect(host.ExtractSelected).toHaveBeenCalledTimes(1));
    expect(host.ExtractSelected).toHaveBeenCalledWith('Data/A.ba2', JSON.stringify(['meshes/a.nif']), 'D:/out', false);
    expect(button('Extract All').disabled).toBe(true);
    expect(button('Loading…').disabled).toBe(true);
    fireEvent.click(button('Extract All'));
    fireEvent.click(button('Loading…'));
    expect(host.ExtractAll).toHaveBeenCalledTimes(0);
    expect(host.List).toHaveBeenCalledTimes(1);

    fireEvent.change(pathInput, { target: { value: 'Data/B.ba2' } });
    expect(screen.queryByText('meshes/a.nif')).toBeNull();

    await act(async () => { finishExtract('Extracted 1 file'); });
    await screen.findByText('Extracted 1 file');
    expect(screen.getByText('Open folder')).toBeTruthy();
    await waitFor(() => expect(button('List').disabled).toBe(false));
  });

  it('drops the folder link from an earlier extraction when the next Extract Selected fails', async () => {
    const host = installHost({
      List: vi.fn(async () => listing([{ path: 'meshes/a.nif', size: 10 }])),
      BrowseForFolder: vi.fn(async () => 'D:/out'),
      ExtractSelected: vi.fn()
        .mockResolvedValueOnce('Extracted 1 file')
        .mockRejectedValueOnce(new Error('disk full')),
    });
    renderPanel();

    fireEvent.click(screen.getByRole('button', { name: 'List' }));
    await screen.findByText('meshes/a.nif');
    fireEvent.click(screen.getByText('meshes/a.nif').closest('tr')!.querySelector('input')!);
    fireEvent.click(button('Extract Selected (1)'));

    await screen.findByText('Extracted 1 file');
    expect(screen.getByText('Open folder')).toBeTruthy();
    expect(host.ExtractSelected).toHaveBeenCalledTimes(1);

    fireEvent.click(button('Extract Selected (1)'));

    await screen.findByText('Error: disk full');
    expect(screen.queryByText('Extracted 1 file')).toBeNull();
    expect(screen.queryByText('Open folder')).toBeNull();
    await waitFor(() => expect(button('List').disabled).toBe(false));
  });
});

describe('ArchivePanel compare staleness', () => {
  it('flags a comparison that resolved for paths the inputs have since left', async () => {
    let resolveCompare: (value: string) => void = () => {};
    const host = installHost({ Compare: vi.fn(() => new Promise<string>(resolve => { resolveCompare = resolve; })) });
    renderPanel();

    fireEvent.click(screen.getByText('Compare with another archive'));
    const bInput = screen.getByPlaceholderText('Path to the archive to compare against…');
    fireEvent.change(bInput, { target: { value: 'Data/B.ba2' } });
    fireEvent.click(screen.getByText('Compare'));
    expect(host.Compare).toHaveBeenCalledWith('Data/A.ba2', 'Data/B.ba2');

    fireEvent.change(bInput, { target: { value: 'Data/C.ba2' } });
    await act(async () => {
      resolveCompare(JSON.stringify({ added: ['meshes/x.nif'], removed: [], changed: [], identicalCount: 3 }));
    });

    expect(screen.getByText('+1 added')).toBeTruthy();
    expect(screen.getByText('3 identical')).toBeTruthy();
    expect(screen.getByText('These results were produced for different paths. Compare again to refresh.')).toBeTruthy();
    expect(button('Compare').disabled).toBe(false);
  });

  it('releases the compare busy state when the comparison fails', async () => {
    let rejectCompare: (reason: unknown) => void = () => {};
    installHost({ Compare: vi.fn(() => new Promise<string>((_resolve, reject) => { rejectCompare = reject; })) });
    renderPanel();

    fireEvent.click(screen.getByText('Compare with another archive'));
    fireEvent.change(screen.getByPlaceholderText('Path to the archive to compare against…'), { target: { value: 'Data/B.ba2' } });
    fireEvent.click(screen.getByText('Compare'));
    expect(screen.getByText('Comparing…')).toBeTruthy();

    await act(async () => { rejectCompare(new Error('compare offline')); });

    await screen.findByText('Could not compare: compare offline');
    expect(button('Compare').disabled).toBe(false);
  });
});

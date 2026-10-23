import { cleanup, fireEvent, render, screen, waitFor, within } from '@testing-library/react';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { DialogProvider } from './dialogs';
import MainShell from './MainShell';

const pickPlugin = vi.hoisted(() => vi.fn());
vi.mock('./useDialogs', async importOriginal => {
  const original = await importOriginal<typeof import('./useDialogs')>();
  return { ...original, useDialogs: () => ({ ...original.useDialogs(), pickPlugin }) };
});

const getPlugins = vi.fn<() => Promise<string>>();
const loadEnvironment = vi.fn<(discard?: boolean) => Promise<string>>();
const copyMany = vi.fn<() => Promise<string>>();
const searchRecords = vi.fn<(q: string) => Promise<string>>();
const getMatrix = vi.fn<(key: string) => Promise<string>>();
const getLoadOrder = vi.fn<() => Promise<string>>();

const pluginRow = { Key: 'PluginA.esp', Name: 'PluginA.esp', Kind: 'plugin', FilePath: '', HasChildren: false, IsRecordNode: false };
const weaponHit = { Plugin: 'Master.esm', FormKey: '000800:Master.esm', EditorID: 'WeaponOne', Type: 'WEAP' };
const armorHit = { Plugin: 'Master.esm', FormKey: '000900:Master.esm', EditorID: 'ArmorOne', Type: 'ARMO' };
const bowHit = { Plugin: 'Master.esm', FormKey: '000A00:Master.esm', EditorID: 'BowOne', Type: 'WEAP' };
const axeHit = { Plugin: 'Master.esm', FormKey: '000B00:Master.esm', EditorID: 'AxeOne', Type: 'WEAP' };
const searchHits = [weaponHit, armorHit, bowHit, axeHit];
const conflictRow = {
  FormKey: '000800:Master.esm', EditorID: 'TestWeapon', Type: 'WEAP', Winner: 'Later.esp',
  Plugins: ['Master.esm', 'Later.esp'], Rows: [], Level: 'conflict', Count: 2,
};

function stubBridge() {
  vi.stubGlobal('chrome', {
    webview: {
      addEventListener: vi.fn(),
      removeEventListener: vi.fn(),
      hostObjects: {
        appInterop: { GetPlugins: getPlugins, GetMo2Status: vi.fn(async () => JSON.stringify({})), LoadEnvironment: loadEnvironment },
        backend: {
          GetRecordTypeIndex: vi.fn(async () => '[]'),
          GetLoadOrderSummary: getLoadOrder,
          GetConflicts: vi.fn(async () => JSON.stringify([conflictRow])),
          CopyAsOverrideMany: copyMany,
          SearchRecords: searchRecords,
          GetConflictMatrix: getMatrix,
          GetEditablePlugins: vi.fn(async () => '["Patch.esp"]'),
          GetContainmentPath: vi.fn(async () => '[]'),
          GetProblems: vi.fn(async () => '[]'),
          GetReferencedBy: vi.fn(async () => '[]'),
          GetDependencies: vi.fn(async () => '[]'),
          GetHistory: vi.fn(async () => '[]'),
        },
      },
    },
  });
}

async function openBatchCopy() {
  localStorage.setItem('treeMode', 'conflicts');
  render(<DialogProvider><MainShell /></DialogProvider>);
  fireEvent.click(await screen.findByRole('checkbox'));
  fireEvent.click(screen.getByRole('button', { name: /Copy selected/ }));
}

beforeEach(() => {
  vi.clearAllMocks();
  localStorage.clear();
  getPlugins.mockResolvedValue('[]');
  loadEnvironment.mockResolvedValue('ok');
  copyMany.mockResolvedValue(JSON.stringify({ total: 0, ok: 0, failed: 0 }));
  searchRecords.mockResolvedValue('[]');
  getLoadOrder.mockResolvedValue(JSON.stringify({ Plugins: ['Master.esm'] }));
  getMatrix.mockReset().mockImplementation(async key => JSON.stringify({
    FormKey: key, EditorID: 'Rec', Type: 'WEAP', Winner: 'Master.esm', Plugins: ['Master.esm'], Rows: [], Level: 'conflict',
  }));
  pickPlugin.mockResolvedValue({ target: 'Patch.esp', extra: '' });
  stubBridge();
});

afterEach(() => {
  cleanup();
  vi.unstubAllGlobals();
});

describe('startup through appInterop', () => {
  it('loads the plugin list', async () => {
    getPlugins.mockResolvedValue(JSON.stringify([pluginRow]));
    render(<DialogProvider><MainShell /></DialogProvider>);
    await waitFor(() => expect(getPlugins).toHaveBeenCalled());
    await screen.findByText('PluginA.esp');
    expect(screen.queryByText(/No plugins loaded/)).toBeNull();
  });

  it('shows the host message when the plugin list fails', async () => {
    getPlugins.mockRejectedValue(new Error('bridge down'));
    render(<DialogProvider><MainShell /></DialogProvider>);
    await screen.findByText('bridge down');
  });

  it('shows a plain object rejection as its message', async () => {
    getPlugins.mockRejectedValue({ message: 'host said no' });
    render(<DialogProvider><MainShell /></DialogProvider>);
    await screen.findByText('host said no');
  });
});

describe('environment reload', () => {
  it('asks before discarding pending edits and retries with the flag', async () => {
    loadEnvironment.mockRejectedValueOnce(new Error('PluginHasPendingChanges: PluginA.esp is dirty'));
    render(<DialogProvider><MainShell /></DialogProvider>);
    fireEvent.click(await screen.findByRole('button', { name: 'Load Env' }));
    await screen.findByText('Discard pending edits?');
    fireEvent.click(screen.getByRole('button', { name: 'Discard and reload' }));
    await waitFor(() => expect(loadEnvironment).toHaveBeenCalledTimes(2));
    expect(loadEnvironment).toHaveBeenNthCalledWith(1);
    expect(loadEnvironment).toHaveBeenNthCalledWith(2, true);
    expect(getPlugins).toHaveBeenCalledTimes(2);
  });

  it('keeps the plugins when the discard is cancelled', async () => {
    loadEnvironment.mockRejectedValue(new Error('PluginHasPendingChanges: PluginA.esp is dirty'));
    render(<DialogProvider><MainShell /></DialogProvider>);
    fireEvent.click(await screen.findByRole('button', { name: 'Load Env' }));
    await screen.findByText('Discard pending edits?');
    fireEvent.click(screen.getByRole('button', { name: 'Cancel' }));
    await waitFor(() => expect(screen.queryByText('Discard pending edits?')).toBeNull());
    expect(loadEnvironment).toHaveBeenCalledTimes(1);
    expect(getPlugins).toHaveBeenCalledTimes(1);
  });

  it('shows the reload failure text', async () => {
    loadEnvironment.mockRejectedValue(new Error('env exploded'));
    render(<DialogProvider><MainShell /></DialogProvider>);
    fireEvent.click(await screen.findByRole('button', { name: 'Load Env' }));
    await screen.findByText('env exploded');
  });
});

describe('batch copy failure display', () => {
  it.each([
    [new Error('disk full'), 'Batch copy failed: disk full'],
    ['nope', 'Batch copy failed: nope'],
    [{ message: 'host said no' }, 'Batch copy failed: host said no'],
    [null, 'Batch copy failed: null'],
  ])('reports %o', async (failure, expected) => {
    copyMany.mockRejectedValue(failure);
    await openBatchCopy();
    await screen.findByText(expected);
  });

  it('lists every failed record with its reason', async () => {
    copyMany.mockResolvedValue(JSON.stringify({
      total: 3, ok: 1, failed: 2,
      failures: [
        { formKey: '000800:Master.esm', source: 'Later.esp', reason: 'first reason' },
        { formKey: '000900:Master.esm', source: 'Later.esp', reason: 'second reason' },
      ],
    }));
    await openBatchCopy();
    await screen.findByText(/Copied 1\/3 into Patch\.esp\. 2 failed/);
    const list = await screen.findByText(/2 of 3 records could not be copied into Patch\.esp/);
    expect(list.textContent).toContain('000800:Master.esm (TestWeapon): first reason');
    expect(list.textContent).toContain('000900:Master.esm: second reason');
  });
});

describe('sidebar record search', () => {
  it('drops the earlier query results when the query falls below two characters', async () => {
    searchRecords.mockImplementation(async (q: string) =>
      JSON.stringify(q === 'weapon' ? [weaponHit] : [armorHit]));
    getPlugins.mockResolvedValue(JSON.stringify([pluginRow]));
    render(<DialogProvider><MainShell /></DialogProvider>);

    const input = await screen.findByPlaceholderText('Search plugins & records…');
    fireEvent.change(input, { target: { value: 'weapon' } });
    await screen.findByText('WeaponOne');

    fireEvent.change(input, { target: { value: 'w' } });
    expect(screen.queryByText('WeaponOne')).toBeNull();

    fireEvent.change(input, { target: { value: 'armor' } });
    expect(screen.queryByText('WeaponOne')).toBeNull();
    expect(screen.getByText('Searching…')).not.toBeNull();

    await screen.findByText('ArmorOne');
    expect(screen.queryByText('WeaponOne')).toBeNull();
  });

  it('reports a failed search instead of showing no results', async () => {
    searchRecords.mockRejectedValue(new Error('index offline'));
    getPlugins.mockResolvedValue(JSON.stringify([pluginRow]));
    render(<DialogProvider><MainShell /></DialogProvider>);
    fireEvent.change(await screen.findByPlaceholderText('Search plugins & records…'), { target: { value: 'weapon' } });
    await screen.findByText('Search failed: index offline');
  });
});

describe('sidebar record multi-select', () => {
  const hitRow = (name: string) => screen.getByText(name, { selector: '.sr-id' }).closest('.sidebar-result') as HTMLElement;
  const isSelected = (name: string) => hitRow(name).classList.contains('selected');
  const resultsPanel = () => document.querySelector('.sidebar-results') as HTMLElement;
  const button = (name: RegExp) => screen.getByRole('button', { name }) as HTMLButtonElement;
  const searchInput = () => screen.getByPlaceholderText('Search plugins & records…');

  async function openRecordSearch(query: string) {
    searchRecords.mockImplementation(async (q: string) => JSON.stringify(q === 'armor' ? [armorHit] : searchHits));
    getPlugins.mockResolvedValue(JSON.stringify([pluginRow]));
    render(<DialogProvider><MainShell /></DialogProvider>);
    fireEvent.change(await screen.findByPlaceholderText('Search plugins & records…'), { target: { value: query } });
    await screen.findByText('WeaponOne', { selector: '.sr-id' });
  }

  it('ctrl toggles one record, shift selects a range and ctrl+shift adds a range', async () => {
    await openRecordSearch('weapon');
    fireEvent.click(hitRow('WeaponOne'), { ctrlKey: true });
    fireEvent.click(hitRow('BowOne'), { ctrlKey: true });
    expect(isSelected('WeaponOne') && isSelected('BowOne') && !isSelected('ArmorOne')).toBe(true);
    fireEvent.click(hitRow('WeaponOne'), { ctrlKey: true });
    expect(isSelected('WeaponOne')).toBe(false);
    expect(screen.getByText('1 selected (0 hidden)')).toBeTruthy();
    expect(getMatrix).not.toHaveBeenCalled();

    fireEvent.click(hitRow('ArmorOne'), { ctrlKey: true });
    fireEvent.click(hitRow('AxeOne'), { shiftKey: true });
    expect(['ArmorOne', 'BowOne', 'AxeOne'].every(isSelected) && !isSelected('WeaponOne')).toBe(true);
    expect(screen.getByText('3 selected (0 hidden)')).toBeTruthy();

    fireEvent.click(hitRow('WeaponOne'), { ctrlKey: true, shiftKey: true });
    expect(['WeaponOne', 'ArmorOne', 'BowOne', 'AxeOne'].every(isSelected)).toBe(true);
    expect(screen.getByText('4 selected (0 hidden)')).toBeTruthy();
  });

  it('a plain click selects only the clicked record and opens it', async () => {
    await openRecordSearch('weapon');
    fireEvent.click(hitRow('WeaponOne'), { ctrlKey: true });
    fireEvent.click(hitRow('BowOne'));
    expect(isSelected('BowOne') && !isSelected('WeaponOne')).toBe(true);
    await waitFor(() => expect(getMatrix).toHaveBeenCalledWith(bowHit.FormKey));
  });

  it('keeps hidden records selected across a search change and counts them as hidden', async () => {
    await openRecordSearch('weapon');
    fireEvent.click(hitRow('WeaponOne'), { ctrlKey: true });
    fireEvent.click(hitRow('BowOne'), { ctrlKey: true });
    fireEvent.change(searchInput(), { target: { value: 'armor' } });
    await waitFor(() => expect(screen.queryByText('WeaponOne', { selector: '.sr-id' })).toBeNull());
    expect(screen.getByText('2 selected (2 hidden)')).toBeTruthy();
    fireEvent.click(hitRow('ArmorOne'), { ctrlKey: true });
    expect(screen.getByText('3 selected (2 hidden)')).toBeTruthy();
    fireEvent.change(searchInput(), { target: { value: 'weapon' } });
    await screen.findByText('WeaponOne', { selector: '.sr-id' });
    expect(isSelected('WeaponOne') && isSelected('BowOne')).toBe(true);
    expect(screen.getByText('3 selected (0 hidden)')).toBeTruthy();
  });

  it('copies every selected record, hidden ones included, through CopyAsOverrideMany', async () => {
    copyMany.mockResolvedValue(JSON.stringify({ total: 3, ok: 3, failed: 0 }));
    await openRecordSearch('weapon');
    fireEvent.click(hitRow('WeaponOne'), { ctrlKey: true });
    fireEvent.click(hitRow('BowOne'), { ctrlKey: true });
    fireEvent.change(searchInput(), { target: { value: 'armor' } });
    await waitFor(() => expect(screen.queryByText('WeaponOne', { selector: '.sr-id' })).toBeNull());
    fireEvent.click(hitRow('ArmorOne'), { ctrlKey: true });
    fireEvent.click(button(/Copy selected as override/));
    await waitFor(() => expect(copyMany).toHaveBeenCalledWith(
      JSON.stringify([
        { formKey: '000800:Master.esm', source: 'Master.esm' },
        { formKey: '000A00:Master.esm', source: 'Master.esm' },
        { formKey: '000900:Master.esm', source: 'Master.esm' },
      ]),
      'Patch.esp', false, false,
    ));
    expect(await screen.findByText(/Copied 3\/3 into Patch\.esp/)).toBeTruthy();
    expect(isSelected('ArmorOne')).toBe(false);
    expect(screen.queryByText(/selected \(/)).toBeNull();
  });

  it('enables Compare selected for two or more records and loads each one in pick order', async () => {
    await openRecordSearch('weapon');
    fireEvent.click(hitRow('WeaponOne'), { ctrlKey: true });
    expect(button(/Compare selected/).disabled).toBe(true);
    fireEvent.click(hitRow('BowOne'), { ctrlKey: true });
    expect(button(/Compare selected/).disabled).toBe(false);
    fireEvent.click(hitRow('AxeOne'), { ctrlKey: true });
    expect(button(/Compare selected/).disabled).toBe(false);
    fireEvent.click(hitRow('AxeOne'), { ctrlKey: true });
    fireEvent.click(button(/Compare selected/));
    await waitFor(() => expect(getMatrix).toHaveBeenCalledWith(bowHit.FormKey));
    expect(getMatrix.mock.calls.map(([key]) => key)).toEqual([weaponHit.FormKey, bowHit.FormKey]);
  });

  it('selects rows with their checkboxes without opening them', async () => {
    await openRecordSearch('weapon');
    fireEvent.click(screen.getByRole('checkbox', { name: 'WeaponOne' }));
    fireEvent.click(screen.getByRole('checkbox', { name: 'BowOne' }));
    expect(isSelected('WeaponOne') && isSelected('BowOne')).toBe(true);
    expect(screen.getByText('2 selected (0 hidden)')).toBeTruthy();
    expect(getMatrix).not.toHaveBeenCalled();
  });

  it('leaves out picks from plugins that are no longer loaded', async () => {
    getLoadOrder.mockResolvedValue(JSON.stringify({ Plugins: ['Other.esp'] }));
    await openRecordSearch('weapon');
    fireEvent.click(hitRow('WeaponOne'), { ctrlKey: true });
    fireEvent.click(hitRow('BowOne'), { ctrlKey: true });
    expect(screen.queryByText(/selected \(/)).toBeNull();
    expect(screen.queryByRole('button', { name: /Copy selected/ })).toBeNull();
    expect(copyMany).not.toHaveBeenCalled();
  });

  it('Escape and Clear empty the record selection', async () => {
    await openRecordSearch('weapon');
    fireEvent.click(hitRow('WeaponOne'), { ctrlKey: true });
    fireEvent.click(hitRow('BowOne'), { ctrlKey: true });
    fireEvent.keyDown(resultsPanel(), { key: 'Escape' });
    expect(isSelected('WeaponOne') || isSelected('BowOne')).toBe(false);
    fireEvent.click(hitRow('WeaponOne'), { ctrlKey: true });
    fireEvent.click(within(resultsPanel()).getByRole('button', { name: 'Clear' }));
    expect(isSelected('WeaponOne')).toBe(false);
    expect(screen.queryByText(/selected \(/)).toBeNull();
  });
});

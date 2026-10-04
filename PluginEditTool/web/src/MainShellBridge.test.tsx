import { cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { DialogProvider } from './dialogs';
import MainShell from './MainShell';

const pickPlugin = vi.hoisted(() => vi.fn());
vi.mock('./dialogs', async importOriginal => {
  const original = await importOriginal<typeof import('./dialogs')>();
  return { ...original, useDialogs: () => ({ ...original.useDialogs(), pickPlugin }) };
});

const getPlugins = vi.fn<() => Promise<string>>();
const loadEnvironment = vi.fn<(discard?: boolean) => Promise<string>>();
const copyMany = vi.fn<() => Promise<string>>();

const pluginRow = { Key: 'PluginA.esp', Name: 'PluginA.esp', Kind: 'plugin', FilePath: '', HasChildren: false, IsRecordNode: false };
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
          GetLoadOrderSummary: vi.fn(async () => JSON.stringify({ Plugins: [] })),
          GetConflicts: vi.fn(async () => JSON.stringify([conflictRow])),
          CopyAsOverrideMany: copyMany,
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
});

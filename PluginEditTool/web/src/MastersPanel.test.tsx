import { act, cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';
import type { MastersHost } from './backend';
import MastersPanel from './MastersPanel';

const listing = JSON.stringify({
  pluginName: 'Mod.esp', light: false,
  masters: [{ name: 'Fallout4.esm', size: 1, used: true }, { name: 'Unused.esm', size: 1, used: false }],
});

function installHost(): MastersHost {
  const host: MastersHost = {
    GetPlugins: vi.fn(async () => JSON.stringify(['Mod.esp', 'Other.esp'])),
    List: vi.fn(async () => listing),
    Reorder: vi.fn(async () => ''),
    SetLight: vi.fn(async () => ''),
    CleanUnused: vi.fn(async (_plugin: string, apply: boolean) => apply
      ? 'Removed unused master(s) from Mod.esp: Unused.esm. SavePlugin to persist.'
      : 'DRY RUN: would remove unused master(s) from Mod.esp: Unused.esm.'),
    SavePlugin: vi.fn(async () => 'Saved Mod.esp'),
  };
  Object.assign(window, { chrome: { webview: { hostObjects: { masters: host } } } });
  return host;
}

afterEach(() => {
  cleanup();
  localStorage.clear();
  Reflect.deleteProperty(window, 'chrome');
});

describe('MastersPanel Clean Masters', () => {
  it('previews unused masters before removing them and enables Save Plugin afterwards', async () => {
    const host = installHost();
    localStorage.setItem('masters.plugin', 'Mod.esp');
    render(<MastersPanel onClose={() => {}} />);

    fireEvent.click(screen.getByText('Load'));
    await screen.findByText('Unused.esm');
    const save = screen.getByText('Save Plugin').closest('button')!;
    expect(save.disabled).toBe(true);

    fireEvent.click(screen.getByText('Clean Masters'));
    await screen.findByText('DRY RUN: would remove unused master(s) from Mod.esp: Unused.esm.');
    expect(host.CleanUnused).toHaveBeenCalledWith('Mod.esp', false);

    fireEvent.click(screen.getByText('Remove them'));
    await waitFor(() => expect(host.CleanUnused).toHaveBeenCalledWith('Mod.esp', true));
    await waitFor(() => expect(screen.getByText('Save Plugin').closest('button')!.disabled).toBe(false));
  });
});

describe('MastersPanel Load button after a switch while an operation is pending', () => {
  const pendingForOther = () => new Promise<string>(() => {});

  it('keeps the Load button busy when a light toggle for the previous plugin resolves after a switch', async () => {
    const host = installHost();
    let resolveSetLight: (value: string) => void = () => {};
    host.SetLight = vi.fn(() => new Promise<string>(resolve => { resolveSetLight = resolve; }));
    host.List = vi.fn(async (name: string) => (name === 'Mod.esp' ? listing : pendingForOther()));
    localStorage.setItem('masters.plugin', 'Mod.esp');
    render(<MastersPanel onClose={() => {}} />);

    fireEvent.click(screen.getByText('Load'));
    await screen.findByText('Fallout4.esm');
    fireEvent.click(screen.getByRole('checkbox'));
    expect(host.SetLight).toHaveBeenCalledWith('Mod.esp', true);

    await screen.findByRole('option', { name: 'Other.esp' });
    fireEvent.change(screen.getByRole('combobox'), { target: { value: 'Other.esp' } });
    fireEvent.click(screen.getByText('Load'));
    await screen.findByText('Loading…');
    await act(async () => { resolveSetLight('Could not set the ESL flag on Mod.esp'); });

    expect((screen.getByText('Loading…').closest('button') as HTMLButtonElement).disabled).toBe(true);
  });

  it('keeps the Load button busy when a reorder for the previous plugin resolves after a switch', async () => {
    const host = installHost();
    let resolveReorder: (value: string) => void = () => {};
    host.Reorder = vi.fn(() => new Promise<string>(resolve => { resolveReorder = resolve; }));
    const twoMasters = JSON.stringify({
      pluginName: 'Mod.esp', light: false,
      masters: [{ name: 'Fallout4.esm', size: 1, used: true }, { name: 'DLC.esm', size: 2, used: true }],
    });
    host.List = vi.fn(async (name: string) => (name === 'Mod.esp' ? twoMasters : pendingForOther()));
    localStorage.setItem('masters.plugin', 'Mod.esp');
    render(<MastersPanel onClose={() => {}} />);

    fireEvent.click(screen.getByText('Load'));
    await screen.findByText('DLC.esm');
    fireEvent.click(screen.getAllByTitle('Move down')[0]);
    fireEvent.click(screen.getByText('Apply New Order (writes immediately)'));
    expect(host.Reorder).toHaveBeenCalledWith('Mod.esp', JSON.stringify(['DLC.esm', 'Fallout4.esm']));

    await screen.findByRole('option', { name: 'Other.esp' });
    fireEvent.change(screen.getByRole('combobox'), { target: { value: 'Other.esp' } });
    fireEvent.click(screen.getByText('Load'));
    await screen.findByText('Loading…');
    await act(async () => { resolveReorder('Wrote Mod.esp'); });

    expect((screen.getByText('Loading…').closest('button') as HTMLButtonElement).disabled).toBe(true);
  });

  it('keeps the Load button busy when a clean preview for the previous plugin resolves after a switch', async () => {
    const host = installHost();
    let resolveClean: (value: string) => void = () => {};
    host.CleanUnused = vi.fn(() => new Promise<string>(resolve => { resolveClean = resolve; }));
    host.List = vi.fn(async (name: string) => (name === 'Mod.esp' ? listing : pendingForOther()));
    localStorage.setItem('masters.plugin', 'Mod.esp');
    render(<MastersPanel onClose={() => {}} />);

    fireEvent.click(screen.getByText('Load'));
    await screen.findByText('Unused.esm');
    fireEvent.click(screen.getByText('Clean Masters'));
    expect(host.CleanUnused).toHaveBeenCalledWith('Mod.esp', false);

    await screen.findByRole('option', { name: 'Other.esp' });
    fireEvent.change(screen.getByRole('combobox'), { target: { value: 'Other.esp' } });
    fireEvent.click(screen.getByText('Load'));
    await screen.findByText('Loading…');
    await act(async () => { resolveClean('DRY RUN: would remove unused master(s) from Mod.esp: Unused.esm.'); });

    expect((screen.getByText('Loading…').closest('button') as HTMLButtonElement).disabled).toBe(true);
  });
});

describe('MastersPanel plugin list', () => {
  it('shows a failed plugin list read instead of an empty list', async () => {
    const host = installHost();
    host.GetPlugins = vi.fn(async () => { throw new Error('list offline'); });
    render(<MastersPanel onClose={() => {}} />);
    await screen.findByText('Could not list plugins: list offline');
  });
});

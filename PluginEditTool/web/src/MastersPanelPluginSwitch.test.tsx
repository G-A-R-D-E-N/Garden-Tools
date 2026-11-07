import { act, cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';
import type { MastersHost } from './backend';
import MastersPanel from './MastersPanel';

const listings: Record<string, string> = {
  'Mod.esp': JSON.stringify({
    pluginName: 'Mod.esp', light: false,
    masters: [{ name: 'Fallout4.esm', size: 1, used: true }],
  }),
  'Other.esp': JSON.stringify({
    pluginName: 'Other.esp', light: true,
    masters: [{ name: 'Other2.esm', size: 2, used: true }],
  }),
};

function installHost(): MastersHost {
  const host: MastersHost = {
    GetPlugins: vi.fn(async () => JSON.stringify(['Mod.esp', 'Other.esp'])),
    List: vi.fn(async (name: string) => listings[name]),
    Reorder: vi.fn(async () => 'Wrote Mod.esp'),
    SetLight: vi.fn(async (_plugin: string, light: boolean) => light ? 'Set the ESL flag on Other.esp' : 'Cleared the ESL flag on Other.esp'),
    CleanUnused: vi.fn(async () => ''),
    SavePlugin: vi.fn(async () => 'Saved Other.esp'),
  };
  Object.assign(window, { chrome: { webview: { hostObjects: { masters: host } } } });
  return host;
}

afterEach(() => {
  cleanup();
  localStorage.clear();
  Reflect.deleteProperty(window, 'chrome');
});

describe('MastersPanel plugin selection', () => {
  it('drops the loaded masters, order and light flag when another plugin is selected', async () => {
    const host = installHost();
    localStorage.setItem('masters.plugin', 'Mod.esp');
    render(<MastersPanel onClose={() => {}} />);

    await screen.findByRole('option', { name: 'Other.esp' });
    fireEvent.click(screen.getByText('Load'));
    await screen.findByText('Fallout4.esm');
    expect(screen.getByText('Light plugin (ESL)')).toBeTruthy();

    fireEvent.change(screen.getByRole('combobox'), { target: { value: 'Other.esp' } });

    expect(screen.queryByText('Fallout4.esm')).toBeNull();
    expect(screen.queryByText('Light plugin (ESL)')).toBeNull();
    expect(screen.queryByText('Apply New Order (writes immediately)')).toBeNull();
    expect(screen.queryByText('Save Plugin')).toBeNull();
    expect(host.SetLight).not.toHaveBeenCalled();
    expect(host.Reorder).not.toHaveBeenCalled();
    expect(host.SavePlugin).not.toHaveBeenCalled();
  });

  it('acts on the newly loaded plugin state after Load', async () => {
    const host = installHost();
    localStorage.setItem('masters.plugin', 'Mod.esp');
    render(<MastersPanel onClose={() => {}} />);

    await screen.findByRole('option', { name: 'Other.esp' });
    fireEvent.click(screen.getByText('Load'));
    await screen.findByText('Fallout4.esm');

    fireEvent.change(screen.getByRole('combobox'), { target: { value: 'Other.esp' } });
    fireEvent.click(screen.getByText('Load'));
    await screen.findByText('Other2.esm');

    const light = screen.getByRole('checkbox') as HTMLInputElement;
    expect(light.checked).toBe(true);
    fireEvent.click(light);
    expect(host.SetLight).toHaveBeenCalledWith('Other.esp', false);
    expect(host.List).toHaveBeenLastCalledWith('Other.esp');
  });

  it('ignores a Load that resolves after another plugin was selected', async () => {
    const host = installHost();
    let resolveList: (value: string) => void = () => {};
    host.List = vi.fn(() => new Promise<string>(resolve => { resolveList = resolve; }));
    localStorage.setItem('masters.plugin', 'Mod.esp');
    render(<MastersPanel onClose={() => {}} />);

    await screen.findByRole('option', { name: 'Other.esp' });
    fireEvent.click(screen.getByText('Load'));
    fireEvent.change(screen.getByRole('combobox'), { target: { value: 'Other.esp' } });

    await act(async () => { resolveList(listings['Mod.esp']); });

    expect(screen.queryByText('Fallout4.esm')).toBeNull();
    expect(screen.queryByText('Light plugin (ESL)')).toBeNull();
    expect(screen.queryByText('Save Plugin')).toBeNull();
    expect(screen.getByText('Pick a plugin, then Load.')).toBeTruthy();
    expect((screen.getByText('Load').closest('button') as HTMLButtonElement).disabled).toBe(false);
  });

  it('skips the reload and banner when the plugin changes while Save Plugin is pending', async () => {
    const host = installHost();
    let resolveSave: (value: string) => void = () => {};
    host.SavePlugin = vi.fn(() => new Promise<string>(resolve => { resolveSave = resolve; }));
    localStorage.setItem('masters.plugin', 'Mod.esp');
    render(<MastersPanel onClose={() => {}} />);

    await screen.findByRole('option', { name: 'Other.esp' });
    fireEvent.click(screen.getByText('Load'));
    await screen.findByText('Fallout4.esm');
    fireEvent.click(screen.getByRole('checkbox'));
    await waitFor(() => expect((screen.getByText('Save Plugin').closest('button') as HTMLButtonElement).disabled).toBe(false));

    fireEvent.click(screen.getByText('Save Plugin').closest('button')!);
    fireEvent.change(screen.getByRole('combobox'), { target: { value: 'Other.esp' } });

    await act(async () => { resolveSave('Saved Mod.esp'); });

    expect(screen.queryByText('Saved Mod.esp')).toBeNull();
    expect(screen.queryByText('Fallout4.esm')).toBeNull();
    expect(screen.queryByText('Light plugin (ESL)')).toBeNull();
    expect(screen.getByText('Pick a plugin, then Load.')).toBeTruthy();
    expect(host.List).toHaveBeenCalledTimes(1);
  });

  it('skips the reload and banner when the plugin changes while Apply New Order is pending', async () => {
    const host = installHost();
    let resolveReorder: (value: string) => void = () => {};
    host.Reorder = vi.fn(() => new Promise<string>(resolve => { resolveReorder = resolve; }));
    const twoMasters = JSON.stringify({
      pluginName: 'Mod.esp', light: false,
      masters: [{ name: 'Fallout4.esm', size: 1, used: true }, { name: 'DLC.esm', size: 2, used: true }],
    });
    host.List = vi.fn(async (name: string) => (name === 'Mod.esp' ? twoMasters : listings[name]));
    localStorage.setItem('masters.plugin', 'Mod.esp');
    render(<MastersPanel onClose={() => {}} />);

    await screen.findByRole('option', { name: 'Other.esp' });
    fireEvent.click(screen.getByText('Load'));
    await screen.findByText('DLC.esm');
    fireEvent.click(screen.getAllByTitle('Move down')[0]);
    fireEvent.click(screen.getByText('Apply New Order (writes immediately)'));
    expect(host.Reorder).toHaveBeenCalledWith('Mod.esp', JSON.stringify(['DLC.esm', 'Fallout4.esm']));

    fireEvent.change(screen.getByRole('combobox'), { target: { value: 'Other.esp' } });
    await act(async () => { resolveReorder('Wrote Mod.esp'); });

    expect(screen.queryByText('Wrote Mod.esp')).toBeNull();
    expect(screen.queryByText('DLC.esm')).toBeNull();
    expect(screen.getByText('Pick a plugin, then Load.')).toBeTruthy();
    expect(host.List).toHaveBeenCalledTimes(1);
  });

  it('skips the clean preview and banner when the plugin changes while Clean Masters is pending', async () => {
    const host = installHost();
    let resolveClean: (value: string) => void = () => {};
    host.CleanUnused = vi.fn(() => new Promise<string>(resolve => { resolveClean = resolve; }));
    localStorage.setItem('masters.plugin', 'Mod.esp');
    render(<MastersPanel onClose={() => {}} />);

    await screen.findByRole('option', { name: 'Other.esp' });
    fireEvent.click(screen.getByText('Load'));
    await screen.findByText('Fallout4.esm');
    fireEvent.click(screen.getByText('Clean Masters'));
    expect(host.CleanUnused).toHaveBeenCalledWith('Mod.esp', false);

    fireEvent.change(screen.getByRole('combobox'), { target: { value: 'Other.esp' } });
    fireEvent.click(screen.getByText('Load'));
    await screen.findByText('Other2.esm');
    await act(async () => { resolveClean('DRY RUN: would remove Fallout4.esm'); });

    expect(screen.queryByText('DRY RUN: would remove Fallout4.esm')).toBeNull();
    expect(screen.queryByText('Remove them')).toBeNull();
    expect(screen.getByText('Other2.esm')).toBeTruthy();
  });

  it('keeps the newly loaded light flag when a failed SetLight for the previous plugin resolves after a switch', async () => {
    const host = installHost();
    let resolveSetLight: (value: string) => void = () => {};
    host.SetLight = vi.fn(() => new Promise<string>(resolve => { resolveSetLight = resolve; }));
    localStorage.setItem('masters.plugin', 'Mod.esp');
    render(<MastersPanel onClose={() => {}} />);

    await screen.findByRole('option', { name: 'Other.esp' });
    fireEvent.click(screen.getByText('Load'));
    await screen.findByText('Fallout4.esm');
    fireEvent.click(screen.getByRole('checkbox'));
    expect(host.SetLight).toHaveBeenCalledWith('Mod.esp', true);

    fireEvent.change(screen.getByRole('combobox'), { target: { value: 'Other.esp' } });
    fireEvent.click(screen.getByText('Load'));
    await screen.findByText('Other2.esm');
    await act(async () => { resolveSetLight('Could not set the ESL flag on Mod.esp'); });

    expect((screen.getByRole('checkbox') as HTMLInputElement).checked).toBe(true);
    expect(screen.getByText('Other2.esm')).toBeTruthy();
  });

  it('sends the newly loaded light state after a SetLight for the previous plugin rejects following a switch', async () => {
    const host = installHost();
    let rejectSetLight: (reason: Error) => void = () => {};
    host.SetLight = vi.fn(() => new Promise<string>((_, reject) => { rejectSetLight = reject; }));
    localStorage.setItem('masters.plugin', 'Mod.esp');
    render(<MastersPanel onClose={() => {}} />);

    await screen.findByRole('option', { name: 'Other.esp' });
    fireEvent.click(screen.getByText('Load'));
    await screen.findByText('Fallout4.esm');
    fireEvent.click(screen.getByRole('checkbox'));

    fireEvent.change(screen.getByRole('combobox'), { target: { value: 'Other.esp' } });
    fireEvent.click(screen.getByText('Load'));
    await screen.findByText('Other2.esm');
    await act(async () => { rejectSetLight(new Error('file locked')); });

    fireEvent.click(screen.getByRole('checkbox'));
    expect(host.SetLight).toHaveBeenLastCalledWith('Other.esp', false);
  });
});

import { cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';
import type { MastersHost } from './backend';
import MastersPanel from './MastersPanel';

const listing = JSON.stringify({
  pluginName: 'Mod.esp', light: false,
  masters: [{ name: 'Fallout4.esm', size: 1, used: true }, { name: 'Unused.esm', size: 1, used: false }],
});

function installHost(): MastersHost {
  const host: MastersHost = {
    GetPlugins: vi.fn(async () => JSON.stringify(['Mod.esp'])),
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

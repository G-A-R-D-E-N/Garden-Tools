import { describe, expect, it, vi } from 'vitest';
import { isPatchLoaded, preparePatchPlugin } from './cellPatchTarget';

function backendWith(active: unknown, create: (name: string) => Promise<string> = async () => "Created new plugin 'ConflictPatch.esp'.") {
  return {
    GetActivePlugins: vi.fn(async () => (typeof active === 'string' ? active : JSON.stringify(active))),
    CreatePlugin: vi.fn(create),
  };
}

const FALLOUT = { Name: 'Fallout4.esm', Kind: 'master', LoadOrder: 0, Editable: false };
const MOD = { Name: 'PrewarVault111.esp', Kind: 'plugin', LoadOrder: 1, Editable: true };

describe('preparePatchPlugin', () => {
  it('creates a plugin that is not in the load order, before reporting success', async () => {
    const backend = backendWith([FALLOUT, MOD]);
    await expect(preparePatchPlugin(backend, 'ConflictPatch.esp')).resolves.toBeNull();
    expect(backend.CreatePlugin).toHaveBeenCalledExactlyOnceWith('ConflictPatch.esp');
    expect(backend.CreatePlugin.mock.invocationCallOrder[0]).toBeGreaterThan(backend.GetActivePlugins.mock.invocationCallOrder[0]);
  });

  it('keeps an already loaded writable plugin without creating it', async () => {
    const backend = backendWith([FALLOUT, MOD]);
    await expect(preparePatchPlugin(backend, 'prewarvault111.ESP')).resolves.toBeNull();
    expect(backend.CreatePlugin).not.toHaveBeenCalled();
  });

  it('refuses a loaded game plugin and does not try to create it', async () => {
    const backend = backendWith([FALLOUT]);
    const failure = await preparePatchPlugin(backend, 'Fallout4.esm');
    expect(failure).toContain('game plugin');
    expect(backend.CreatePlugin).not.toHaveBeenCalled();
  });

  it('reports a creation failure and never reports success', async () => {
    const backend = backendWith([FALLOUT], async () => {
      throw new Error('NewPluginRootUnavailable: No trusted MO2 overwrite directory is configured.');
    });
    const failure = await preparePatchPlugin(backend, 'ConflictPatch.esp');
    expect(failure).toBe('Could not create ConflictPatch.esp: NewPluginRootUnavailable: No trusted MO2 overwrite directory is configured.');
  });

  it('does not create anything when the load order cannot be read', async () => {
    const backend = backendWith('not json');
    const failure = await preparePatchPlugin(backend, 'ConflictPatch.esp');
    expect(failure).toMatch(/^Could not read the load order: /);
    expect(backend.CreatePlugin).not.toHaveBeenCalled();
  });

  it('reports a missing backend', async () => {
    await expect(preparePatchPlugin(undefined, 'ConflictPatch.esp')).resolves.toBe('Backend not available.');
  });
});

describe('isPatchLoaded', () => {
  it('reports whether the name is in the load order, ignoring case', async () => {
    const backend = backendWith([FALLOUT, MOD]);
    await expect(isPatchLoaded(backend, 'prewarvault111.esp')).resolves.toBe(true);
    await expect(isPatchLoaded(backend, 'Gone.esp')).resolves.toBe(false);
  });

  it('returns null when the load order is unavailable, so the stored patch is kept', async () => {
    const backend = backendWith('not json');
    await expect(isPatchLoaded(backend, 'Gone.esp')).resolves.toBeNull();
    await expect(isPatchLoaded(undefined, 'Gone.esp')).resolves.toBeNull();
  });
});

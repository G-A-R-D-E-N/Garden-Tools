import { act, cleanup, render, screen } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';
import PluginTargetDialog from './PluginTargetDialog';

const hosts = (active: () => Promise<string>, plugins: () => Promise<string>) => {
  (window as unknown as { chrome: unknown }).chrome = {
    webview: { hostObjects: { backend: { GetActivePlugins: active }, cell: { GetPlugins: plugins } } },
  };
};
const settle = () => act(async () => { await vi.advanceTimersByTimeAsync(1600); });

afterEach(() => {
  cleanup();
  vi.useRealTimers();
  delete (window as unknown as { chrome?: unknown }).chrome;
});

describe('PluginTargetDialog load order', () => {
  it.each([
    ['plain names', '["Fallout4.esm","Patch.esp"]'],
    ['Key nodes', '[{"Key":"Fallout4.esm"},{"Key":"Patch.esp"}]'],
  ])('falls back to the cell plugin list (%s) when GetActivePlugins stalls', async (_shape, list) => {
    vi.useFakeTimers();
    hosts(() => new Promise<string>(() => {}), async () => list);
    render(<PluginTargetDialog request={{ title: 'Pick' }} onResolve={() => {}} />);
    await settle();
    expect(screen.getByText('Fallout4.esm')).toBeTruthy();
    expect(screen.getByText('Patch.esp')).toBeTruthy();
    expect(screen.queryByText(/No plugins are loaded/)).toBeNull();
  });

  it('leaves no timeout timer behind once the active list answers', async () => {
    vi.useFakeTimers();
    hosts(async () => JSON.stringify([{ Name: 'Fallout4.esm', Kind: 'master', LoadOrder: 0, Editable: false }]),
      async () => '[]');
    render(<PluginTargetDialog request={{ title: 'Pick' }} onResolve={() => {}} />);
    await act(async () => { await vi.advanceTimersByTimeAsync(0); });
    expect(screen.getByText('Fallout4.esm')).toBeTruthy();
    expect(vi.getTimerCount()).toBe(0);
  });
});

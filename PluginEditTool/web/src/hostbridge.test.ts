import { afterEach, expect, it, vi } from 'vitest';

afterEach(() => {
  vi.unstubAllGlobals();
  vi.resetModules();
});

function fetchMock(rpcResponse: unknown) {
  return vi.fn((url: string) => (url === '/events' ? new Promise<never>(() => {}) : Promise.resolve(rpcResponse)));
}

it.each([true, false])('uses HTTP RPC on loopback with WebView2 present: %s', async (hasWebView) => {
  vi.stubGlobal('window', {
    location: new URL('http://127.0.0.1:49152/#/main'),
    chrome: hasWebView ? { webview: { hostObjects: {} } } : undefined,
  });
  const fetchRpc = fetchMock({
    ok: true,
    json: async () => ({ ok: true, value: ['Smoke.esp'] }),
  });
  vi.stubGlobal('fetch', fetchRpc);

  await import('./hostbridge');

  expect(Reflect.get(window, '__FO4IDE_HTTP_BRIDGE__')).toBe(true);
  expect(await window.chrome?.webview?.hostObjects?.appInterop?.GetPlugins()).toEqual(['Smoke.esp']);
  expect(fetchRpc).toHaveBeenCalledWith('/rpc', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ target: 'appInterop', method: 'GetPlugins', args: [] }),
  });
});

it('includes RPC error type in thrown host-object failures', async () => {
  vi.stubGlobal('window', {
    location: new URL('http://127.0.0.1:49152/#/main'),
    chrome: undefined,
  });
  vi.stubGlobal('fetch', fetchMock({
    ok: true,
    json: async () => ({
      ok: false,
      type: 'PluginHasPendingChanges',
      error: 'Pending plugin edits would be discarded',
    }),
  }));

  await import('./hostbridge');

  await expect(window.chrome?.webview?.hostObjects?.appInterop?.LoadEnvironment())
    .rejects.toThrow('appInterop.LoadEnvironment: PluginHasPendingChanges: Pending plugin edits would be discarded');
});

it('preserves the legacy host objects on app.local', async () => {
  const legacyWebview = { hostObjects: {} };
  vi.stubGlobal('window', {
    location: new URL('https://app.local/index.html#/main'),
    chrome: { webview: legacyWebview },
  });
  const fetchStub = vi.fn();
  vi.stubGlobal('fetch', fetchStub);

  await import('./hostbridge');

  expect(window.chrome?.webview).toBe(legacyWebview);
  expect(fetchStub).not.toHaveBeenCalled();
});

it('exposes the swf host object over HTTP RPC', async () => {
  vi.stubGlobal('window', {
    location: new URL('http://127.0.0.1:49152/#/main'),
    chrome: undefined,
  });
  const fetchRpc = fetchMock({
    ok: true,
    json: async () => ({ ok: true, value: '{"revision":0,"undo":0,"redo":0,"dirty":false}' }),
  });
  vi.stubGlobal('fetch', fetchRpc);

  await import('./hostbridge');

  expect(await window.chrome?.webview?.hostObjects?.swf?.State()).toBe(
    '{"revision":0,"undo":0,"redo":0,"dirty":false}',
  );
  expect(fetchRpc).toHaveBeenCalledWith('/rpc', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ target: 'swf', method: 'State', args: [] }),
  });
});

it('exposes the strings host object over HTTP RPC', async () => {
  vi.stubGlobal('window', {
    location: new URL('http://127.0.0.1:49152/#/main'),
    chrome: undefined,
  });
  const fetchRpc = fetchMock({
    ok: true,
    json: async () => ({ ok: true, value: '[]' }),
  });
  vi.stubGlobal('fetch', fetchRpc);

  await import('./hostbridge');

  expect(await window.chrome?.webview?.hostObjects?.strings?.ListStringTables('Mod.esp', 'en')).toBe('[]');
  expect(fetchRpc).toHaveBeenCalledWith('/rpc', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ target: 'strings', method: 'ListStringTables', args: ['Mod.esp', 'en'] }),
  });
});

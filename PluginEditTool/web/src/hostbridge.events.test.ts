import { afterEach, expect, it, vi } from 'vitest';

afterEach(() => {
  vi.unstubAllGlobals();
  vi.resetModules();
  sessionStorage.clear();
});

it('takes the token from the hash, strips it from the URL, and streams events with the header', async () => {
  const replaceState = vi.fn();
  vi.stubGlobal('window', {
    location: new URL('http://127.0.0.1:49152/#/main?t=abc123'),
    history: { replaceState },
    chrome: undefined,
  });
  const fetchStub = vi.fn(() => new Promise<never>(() => {}));
  vi.stubGlobal('fetch', fetchStub);

  await import('./hostbridge');

  expect(replaceState).toHaveBeenCalledWith(null, '', '/#/main');
  expect(sessionStorage.getItem('fo4ide.token')).toBe('abc123');
  expect(fetchStub).toHaveBeenCalledWith('/events', { headers: { 'X-FO4IDE-Token': 'abc123' } });
});

it('sends the hash token as X-FO4IDE-Token on host RPC calls', async () => {
  vi.stubGlobal('window', {
    location: new URL('http://127.0.0.1:49152/#/main?t=abc'),
    history: { replaceState: vi.fn() },
    chrome: undefined,
  });
  const fetchStub = vi.fn((url: string) => (url === '/rpc'
    ? Promise.resolve({ ok: true, json: async () => ({ ok: true, value: null }) })
    : new Promise<never>(() => {})));
  vi.stubGlobal('fetch', fetchStub);

  await import('./hostbridge');
  await window.chrome?.webview?.hostObjects?.appInterop?.GetPlugins();

  expect(fetchStub).toHaveBeenCalledWith('/rpc', expect.objectContaining({
    headers: { 'Content-Type': 'application/json', 'X-FO4IDE-Token': 'abc' },
  }));
});

it('reuses the stored token when the URL carries none', async () => {
  sessionStorage.setItem('fo4ide.token', 'stored');
  vi.stubGlobal('window', {
    location: new URL('http://127.0.0.1:49152/#/main'),
    chrome: undefined,
  });
  const fetchStub = vi.fn(() => new Promise<never>(() => {}));
  vi.stubGlobal('fetch', fetchStub);

  await import('./hostbridge');

  expect(fetchStub).toHaveBeenCalledWith('/events', { headers: { 'X-FO4IDE-Token': 'stored' } });
});

it('dispatches SSE data blocks split across chunks to host message listeners', async () => {
  vi.stubGlobal('window', {
    location: new URL('http://127.0.0.1:49152/#/main'),
    chrome: undefined,
  });
  const encoder = new TextEncoder();
  let release!: () => void;
  const gate = new Promise<void>(resolve => { release = resolve; });
  const body = new ReadableStream<Uint8Array>({
    start(controller) {
      controller.enqueue(encoder.encode('data: {"Type":"SetStatus"'));
      gate.then(() => controller.enqueue(encoder.encode(',"Text":"Loading"}\n\n: keep-alive\n\n')));
    },
  });
  vi.stubGlobal('fetch', vi.fn().mockResolvedValue({ ok: true, status: 200, body }));

  await import('./hostbridge');

  const delivered = new Promise<unknown>(resolve => {
    window.chrome?.webview?.addEventListener('message', ev => resolve(ev.data));
  });
  release();
  expect(await delivered).toEqual({ Type: 'SetStatus', Text: 'Loading' });
});











type Listener = (e: { data: unknown }) => void;

const HOST_OBJECTS = [
  'backend', 'appInterop', 'settings', 'papyrus',
  'nif', 'material', 'masters', 'archive', 'audio', 'cell', 'graph', 'strings', 'swf',
] as const;

const TOKEN_KEY = 'fo4ide.token';

function takeToken(): string {
  const [route, query = ''] = window.location.hash.split('?');
  const params = new URLSearchParams(query);
  const fromUrl = params.get('t');
  if (fromUrl !== null) {
    params.delete('t');
    const rest = params.toString();
    const hash = rest ? `${route}?${rest}` : route;
    window.history.replaceState(null, '', `${window.location.pathname}${window.location.search}${hash}`);
  }
  try {
    if (fromUrl) sessionStorage.setItem(TOKEN_KEY, fromUrl);
    return fromUrl || sessionStorage.getItem(TOKEN_KEY) || '';
  } catch {
    return fromUrl || '';
  }
}

const TOKEN = takeToken();

async function streamEvents(onMessage: (raw: string) => void): Promise<void> {
  const res = await fetch('/events', { headers: { 'X-FO4IDE-Token': TOKEN } });
  if (!res.ok || !res.body) throw new Error(`events: HTTP ${res.status}`);
  const reader = res.body.getReader();
  const decoder = new TextDecoder();
  let buffer = '';
  for (;;) {
    const { value, done } = await reader.read();
    if (done) return;
    buffer += decoder.decode(value, { stream: true });
    for (let end = buffer.indexOf('\n\n'); end >= 0; end = buffer.indexOf('\n\n')) {
      const block = buffer.slice(0, end);
      buffer = buffer.slice(end + 2);
      const lines = block.split('\n').filter(line => line.startsWith('data:'));
      if (lines.length) onMessage(lines.map(line => line.slice(5).replace(/^ /, '')).join('\n'));
    }
  }
}

async function pumpEvents(onMessage: (raw: string) => void): Promise<void> {
  await streamEvents(onMessage).catch(() => undefined);
  setTimeout(() => pumpEvents(onMessage), 1000);
}

async function call(target: string, method: string, args: unknown[]): Promise<unknown> {
  const headers: Record<string, string> = { 'Content-Type': 'application/json' };
  if (TOKEN) headers['X-FO4IDE-Token'] = TOKEN;
  const res = await fetch('/rpc', {
    method: 'POST',
    headers,
    body: JSON.stringify({ target, method, args }),
  });
  if (!res.ok) throw new Error(`${target}.${method}: HTTP ${res.status} ${res.statusText}`);
  const body = await res.json();
  if (!body.ok) {
    const type = typeof body.type === 'string' && body.type ? `${body.type}: ` : '';
    throw new Error(`${target}.${method}: ${type}${body.error ?? 'unknown error'}`);
  }
  return body.value;
}


function hostObject(name: string) {
  return new Proxy(function () {} as unknown as Record<string, unknown>, {
    get(_t, prop) {
      if (typeof prop !== 'string') return undefined;

      if (prop === 'then' || prop === Symbol.toStringTag as unknown as string) return undefined;
      return (...args: unknown[]) => call(name, prop, args);
    },
  });
}

function install() {
  const listeners = new Set<Listener>();

  const webview = {
    hostObjects: Object.fromEntries(HOST_OBJECTS.map(n => [n, hostObject(n)])),
    addEventListener: (type: string, fn: Listener) => { if (type === 'message') listeners.add(fn); },
    removeEventListener: (type: string, fn: Listener) => { if (type === 'message') listeners.delete(fn); },
    postMessage: () => {  },
  };

  const host = window as unknown as { chrome?: object };
  host.chrome = { ...host.chrome, webview };
  (window as unknown as { __FO4IDE_HTTP_BRIDGE__: boolean }).__FO4IDE_HTTP_BRIDGE__ = true;


  const dispatch = (raw: string) => {
    let data: unknown;
    try { data = JSON.parse(raw); } catch { data = raw; }
    for (const fn of listeners) {
      try { fn({ data }); } catch (err) { console.error('host message listener failed', err); }
    }
  };
  pumpEvents(dispatch);
}

if (window.location.protocol === 'http:' || !window.chrome?.webview) install();

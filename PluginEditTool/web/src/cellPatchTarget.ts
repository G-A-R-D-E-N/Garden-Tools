import type { ActivePlugin, Backend } from './backend';
import { errorText } from './errors';

export type PatchBackend = Pick<Backend, 'GetActivePlugins' | 'CreatePlugin'>;

const sameName = (a: string, b: string): boolean => a.toLowerCase() === b.toLowerCase();

async function loadOrder(backend: PatchBackend): Promise<ActivePlugin[]> {
  return JSON.parse(await backend.GetActivePlugins()) as ActivePlugin[];
}

export async function preparePatchPlugin(backend: PatchBackend | undefined, name: string): Promise<string | null> {
  if (!backend) return 'Backend not available.';
  let plugins: ActivePlugin[];
  try {
    plugins = await loadOrder(backend);
  } catch (e) {
    return 'Could not read the load order: ' + errorText(e);
  }
  const loaded = plugins.find(p => sameName(p.Name, name));
  if (loaded) return loaded.Editable ? null : `${loaded.Name} is a game plugin and cannot be written to.`;
  try {
    await backend.CreatePlugin(name);
    return null;
  } catch (e) {
    return `Could not create ${name}: ${errorText(e)}`;
  }
}

export async function isPatchLoaded(backend: PatchBackend | undefined, name: string): Promise<boolean | null> {
  if (!backend) return null;
  try {
    return (await loadOrder(backend)).some(p => sameName(p.Name, name));
  } catch {
    return null;
  }
}

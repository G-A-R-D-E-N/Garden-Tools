import { useEffect, useState } from 'react';
import { getBackend } from '../backend';

export function isWritablePlugin(plugin: string, editablePlugins: readonly string[]): boolean {
  const wanted = plugin.toLowerCase();
  return editablePlugins.some(name => name.toLowerCase() === wanted);
}

interface LoadedPlugins { plugin: string; names: string[] }

export function useWritableTarget(plugin: string): { readOnly: boolean; editablePlugins: string[] } {
  const [loaded, setLoaded] = useState<LoadedPlugins | null>(null);
  useEffect(() => {
    let live = true;
    const backend = getBackend();
    if (!backend || plugin === '') return () => { live = false; };
    Promise.resolve()
      .then(() => backend.GetEditablePlugins())
      .then(raw => {
        const names = JSON.parse(raw) as unknown;
        if (live && Array.isArray(names)) {
          setLoaded({ plugin, names: names.filter((name): name is string => typeof name === 'string') });
        }
      })
      .catch(() => { if (live) setLoaded(null); });
    return () => { live = false; };
  }, [plugin]);
  const editablePlugins = loaded !== null && loaded.plugin === plugin ? loaded.names : null;
  return {
    readOnly: editablePlugins !== null && plugin !== '' && !isWritablePlugin(plugin, editablePlugins),
    editablePlugins: editablePlugins ?? [],
  };
}

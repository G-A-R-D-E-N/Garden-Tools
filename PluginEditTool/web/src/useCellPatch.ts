import { useRef } from 'react';
import { getBackend, getCell } from './backend';
import { errorText } from './errors';
import { LS, setLS } from './cellStorage';
import { pluginNameOf } from './util/pluginNames';
import type { TargetRequest, TargetResult } from './PluginTargetDialog';

const loadedPluginNames = async (): Promise<string[]> => {
  const c = getCell();
  if (!c) return [];
  try {
    const parsed = JSON.parse(await c.GetPlugins()) as unknown;
    return Array.isArray(parsed) ? parsed.map(pluginNameOf) : [];
  } catch {
    return [];
  }
};

export default function useCellPatch(
  pickPlugin: (request: TargetRequest) => Promise<TargetResult | null>,
  appendLog: (line: string) => void,
) {
  const patchRef = useRef<string | null>(null);

  const openPatchPlugin = async (target: string): Promise<boolean> => {
    const names = await loadedPluginNames();
    if (names.some(name => name.toLowerCase() === target.toLowerCase())) return true;
    const backend = getBackend();
    if (!backend) {
      appendLog('✗ cell bridge not available');
      return false;
    }
    try {
      appendLog('• ' + await backend.CreatePlugin(target));
      return true;
    } catch (e) {
      appendLog('✗ could not create ' + target + ': ' + errorText(e));
      return false;
    }
  };

  const ensurePatch = async (): Promise<string | null> => {
    if (patchRef.current) return patchRef.current;
    const picked = await pickPlugin({
      title: 'Save cell edits into',
      description: 'Cell edits made this session are written here.',
      confirmLabel: 'Save into', defaultTarget: LS('gizmoPatch', 'CellEdits.esp'),
    });
    if (!picked) return null;
    if (!(await openPatchPlugin(picked.target))) return null;
    patchRef.current = picked.target;
    setLS('gizmoPatch', picked.target);
    return picked.target;
  };

  return { patchRef, openPatchPlugin, ensurePatch };
}

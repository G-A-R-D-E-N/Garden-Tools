import { useEffect, useLayoutEffect, useRef } from 'react';
import { subscribePluginChanged, type PluginChangedEvent } from './pluginChangedBus';

const DEBOUNCE_MS = 150;
const RETRY_MS = 150;

export interface ExternalCellChangeOptions {
  cellFormKey: string | null | undefined;
  gizmoPatchRef: { current: string | null };
  isBusy: () => boolean;
  onChange: (event: PluginChangedEvent) => Promise<void> | void;
}

export function pluginOfFormKey(formKey: string): string {
  return formKey.slice(formKey.indexOf(':') + 1);
}

function fileNameOf(path: string): string {
  return path.split(/[\\/]/).pop()?.toLowerCase() ?? '';
}

export function eventTouchesCell(plugin: string, cellFormKey: string, patch: string | null): boolean {
  if (!plugin) return true;
  const wanted = fileNameOf(plugin.includes(':') ? pluginOfFormKey(plugin) : plugin);
  return [patch ?? '', pluginOfFormKey(cellFormKey)]
    .some(name => name !== '' && fileNameOf(name) === wanted);
}

export function useExternalCellChanges(options: ExternalCellChangeOptions): void {
  const optionsRef = useRef(options);
  useLayoutEffect(() => { optionsRef.current = options; });
  const cellFormKey = options.cellFormKey;

  useEffect(() => {
    if (!cellFormKey) return undefined;
    let timer: ReturnType<typeof setTimeout> | undefined;

    const deliver = (event: PluginChangedEvent) => {
      const current = optionsRef.current;
      if (current.isBusy()) {
        timer = setTimeout(() => deliver(event), RETRY_MS);
        return;
      }
      void current.onChange(event);
    };

    const unsubscribe = subscribePluginChanged(event => {
      const current = optionsRef.current;
      if (!current.cellFormKey) return;
      if (!eventTouchesCell(event.Plugin, current.cellFormKey, current.gizmoPatchRef.current)) return;
      clearTimeout(timer);
      timer = setTimeout(() => deliver(event), DEBOUNCE_MS);
    });

    return () => {
      unsubscribe();
      clearTimeout(timer);
    };
  }, [cellFormKey]);
}

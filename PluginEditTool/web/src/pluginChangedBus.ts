export interface PluginChangedEvent {
  Type: 'PluginChanged';
  Target: string;
  Method: string;
  Plugin: string;
}

type PluginChangedListener = (event: PluginChangedEvent) => void;

const listeners = new Set<PluginChangedListener>();

export function subscribePluginChanged(listener: PluginChangedListener): () => void {
  listeners.add(listener);
  return () => {
    listeners.delete(listener);
  };
}

export function emitPluginChanged(event: PluginChangedEvent): void {
  for (const listener of [...listeners]) listener(event);
}

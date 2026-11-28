import { describe, expect, it, vi } from 'vitest';
import { emitPluginChanged, subscribePluginChanged, type PluginChangedEvent } from './pluginChangedBus';

const sample: PluginChangedEvent = { Type: 'PluginChanged', Target: 'cell', Method: 'PlaceReference', Plugin: 'Patch.esp' };

describe('pluginChangedBus', () => {
  it('delivers each emitted event to every subscriber', () => {
    const first = vi.fn();
    const second = vi.fn();
    const offFirst = subscribePluginChanged(first);
    const offSecond = subscribePluginChanged(second);

    emitPluginChanged(sample);

    expect(first).toHaveBeenCalledTimes(1);
    expect(first).toHaveBeenCalledWith(sample);
    expect(second).toHaveBeenCalledWith(sample);
    offFirst();
    offSecond();
  });

  it('stops delivering to a listener after it unsubscribes', () => {
    const listener = vi.fn();
    const off = subscribePluginChanged(listener);

    off();
    emitPluginChanged(sample);

    expect(listener).not.toHaveBeenCalled();
  });

  it('delivers nothing when there are no subscribers', () => {
    expect(() => emitPluginChanged(sample)).not.toThrow();
  });
});

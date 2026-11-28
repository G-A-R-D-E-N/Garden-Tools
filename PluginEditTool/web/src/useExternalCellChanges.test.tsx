import { act, cleanup, renderHook } from '@testing-library/react';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { emitPluginChanged, type PluginChangedEvent } from './pluginChangedBus';
import { eventTouchesCell, pluginOfFormKey, useExternalCellChanges } from './useExternalCellChanges';

const eventFor = (Plugin: string): PluginChangedEvent => ({ Type: 'PluginChanged', Target: 'cell', Method: 'PlaceReference', Plugin });

function mountCell(cellFormKey: string | null, patch: string | null = null) {
  const gizmoPatchRef = { current: patch };
  const busy = { value: false };
  const onChange = vi.fn();
  const hook = renderHook(
    ({ key }: { key: string | null }) => useExternalCellChanges({
      cellFormKey: key,
      gizmoPatchRef,
      isBusy: () => busy.value,
      onChange,
    }),
    { initialProps: { key: cellFormKey } },
  );
  return { ...hook, gizmoPatchRef, busy, onChange };
}

describe('pluginOfFormKey and eventTouchesCell', () => {
  it('reads the plugin part of a form key', () => {
    expect(pluginOfFormKey('00ABCD:Patch.esp')).toBe('Patch.esp');
    expect(pluginOfFormKey('Patch.esp')).toBe('Patch.esp');
  });

  it('matches the cell plugin, the gizmo patch, an empty plugin, and a full path ignoring case', () => {
    expect(eventTouchesCell('', '00ABCD:Patch.esp', null)).toBe(true);
    expect(eventTouchesCell('Patch.esp', '00ABCD:Patch.esp', null)).toBe(true);
    expect(eventTouchesCell('CellEdits.esp', '00ABCD:Patch.esp', 'CellEdits.esp')).toBe(true);
    expect(eventTouchesCell('E:\\Data\\patch.ESP', '00ABCD:Patch.esp', null)).toBe(true);
    expect(eventTouchesCell('Other.esp', '00ABCD:Patch.esp', 'CellEdits.esp')).toBe(false);
  });

  it('resolves a form-key plugin value to its plugin file', () => {
    expect(eventTouchesCell('000ABC:Patch.esp', '00ABCD:Base.esm', 'Patch.esp')).toBe(true);
    expect(eventTouchesCell('000ABC:patch.ESP', '00ABCD:Patch.esp', null)).toBe(true);
    expect(eventTouchesCell('000ABC:Other.esp', '00ABCD:Patch.esp', 'CellEdits.esp')).toBe(false);
  });
});

describe('useExternalCellChanges', () => {
  beforeEach(() => {
    vi.useFakeTimers();
  });

  afterEach(() => {
    cleanup();
    vi.useRealTimers();
  });

  it('delivers a matching change once after the 150 ms debounce', () => {
    const { onChange } = mountCell('00ABCD:Patch.esp');

    act(() => emitPluginChanged(eventFor('Patch.esp')));
    act(() => vi.advanceTimersByTime(149));
    expect(onChange).not.toHaveBeenCalled();

    act(() => vi.advanceTimersByTime(1));
    expect(onChange).toHaveBeenCalledTimes(1);
    expect(onChange).toHaveBeenCalledWith(eventFor('Patch.esp'));
  });

  it('ignores a change to a plugin the cell does not use', () => {
    const { onChange } = mountCell('00ABCD:Patch.esp', 'CellEdits.esp');

    act(() => emitPluginChanged(eventFor('Other.esp')));
    act(() => vi.advanceTimersByTime(1000));

    expect(onChange).not.toHaveBeenCalled();
  });

  it('matches the gizmo patch when it differs from the cell plugin', () => {
    const { onChange } = mountCell('00ABCD:Fallout4.esm', 'CellEdits.esp');

    act(() => emitPluginChanged(eventFor('CellEdits.esp')));
    act(() => vi.advanceTimersByTime(150));

    expect(onChange).toHaveBeenCalledTimes(1);
  });

  it('treats an empty plugin as a match', () => {
    const { onChange } = mountCell('00ABCD:Patch.esp');

    act(() => emitPluginChanged(eventFor('')));
    act(() => vi.advanceTimersByTime(150));

    expect(onChange).toHaveBeenCalledTimes(1);
  });

  it('never delivers while no cell is loaded', () => {
    const { onChange } = mountCell(null, 'CellEdits.esp');

    act(() => emitPluginChanged(eventFor('CellEdits.esp')));
    act(() => vi.advanceTimersByTime(1000));

    expect(onChange).not.toHaveBeenCalled();
  });

  it('collapses a burst of changes into one delivery', () => {
    const { onChange } = mountCell('00ABCD:Patch.esp');

    act(() => emitPluginChanged(eventFor('Patch.esp')));
    act(() => vi.advanceTimersByTime(100));
    act(() => emitPluginChanged(eventFor('Patch.esp')));
    act(() => vi.advanceTimersByTime(100));
    act(() => emitPluginChanged(eventFor('Patch.esp')));
    act(() => vi.advanceTimersByTime(149));
    expect(onChange).not.toHaveBeenCalled();

    act(() => vi.advanceTimersByTime(1));
    expect(onChange).toHaveBeenCalledTimes(1);
  });

  it('retries while another edit is busy and delivers when it finishes', () => {
    const { onChange, busy } = mountCell('00ABCD:Patch.esp');
    busy.value = true;

    act(() => emitPluginChanged(eventFor('Patch.esp')));
    act(() => vi.advanceTimersByTime(450));
    expect(onChange).not.toHaveBeenCalled();

    busy.value = false;
    act(() => vi.advanceTimersByTime(150));
    expect(onChange).toHaveBeenCalledTimes(1);
  });

  it('stops listening after unmount', () => {
    const { onChange, unmount } = mountCell('00ABCD:Patch.esp');
    unmount();

    act(() => emitPluginChanged(eventFor('Patch.esp')));
    act(() => vi.advanceTimersByTime(1000));

    expect(onChange).not.toHaveBeenCalled();
  });

  it('cancels a pending delivery when the cell changes', () => {
    const { onChange, rerender } = mountCell('00ABCD:Patch.esp');

    act(() => emitPluginChanged(eventFor('Patch.esp')));
    rerender({ key: null });
    act(() => vi.advanceTimersByTime(1000));

    expect(onChange).not.toHaveBeenCalled();
  });
});

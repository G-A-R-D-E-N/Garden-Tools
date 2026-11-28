import { fireEvent, renderHook, cleanup } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';
import { isEditableTarget, undoRedoKey, useCellUndoShortcuts } from './useCellUndoShortcuts';

const key = (k: string, mods: { ctrlKey?: boolean; metaKey?: boolean; altKey?: boolean; shiftKey?: boolean; repeat?: boolean } = {}) => ({
  key: k, ctrlKey: false, metaKey: false, altKey: false, shiftKey: false, repeat: false, ...mods,
});

afterEach(cleanup);

describe('undoRedoKey', () => {
  const cases: [string, Parameters<typeof key>[1], 'undo' | 'redo' | null][] = [
    ['z', { ctrlKey: true }, 'undo'],
    ['Z', { ctrlKey: true }, 'undo'],
    ['z', { metaKey: true }, 'undo'],
    ['z', { ctrlKey: true, shiftKey: true }, 'redo'],
    ['Z', { metaKey: true, shiftKey: true }, 'redo'],
    ['y', { ctrlKey: true }, 'redo'],
    ['y', { ctrlKey: true, shiftKey: true }, null],
    ['z', {}, null],
    ['z', { ctrlKey: true, altKey: true }, null],
    ['z', { ctrlKey: true, repeat: true }, null],
    ['d', { ctrlKey: true }, null],
  ];
  it.each(cases)('maps %s with %o to %s', (k, mods, expected) => {
    expect(undoRedoKey(key(k, mods))).toBe(expected);
  });
});

describe('isEditableTarget', () => {
  it('treats text fields, selects and contenteditable regions as editable', () => {
    const wrap = document.createElement('div');
    wrap.setAttribute('contenteditable', 'true');
    const inner = document.createElement('span');
    wrap.appendChild(inner);
    document.body.appendChild(wrap);
    for (const el of [document.createElement('input'), document.createElement('textarea'), document.createElement('select'), inner]) {
      expect(isEditableTarget(el)).toBe(true);
    }
    wrap.remove();
  });

  it('treats buttons, plain elements, the window and null as not editable', () => {
    expect(isEditableTarget(document.createElement('button'))).toBe(false);
    expect(isEditableTarget(document.createElement('div'))).toBe(false);
    expect(isEditableTarget(window)).toBe(false);
    expect(isEditableTarget(null)).toBe(false);
  });
});

describe('useCellUndoShortcuts', () => {
  it('calls onUndo once for Ctrl+Z and onRedo once for Ctrl+Shift+Z and Ctrl+Y', () => {
    const onUndo = vi.fn();
    const onRedo = vi.fn();
    renderHook(() => useCellUndoShortcuts(onUndo, onRedo));
    fireEvent.keyDown(window, key('z', { ctrlKey: true }));
    expect(onUndo).toHaveBeenCalledTimes(1);
    expect(onRedo).not.toHaveBeenCalled();
    fireEvent.keyDown(window, key('z', { ctrlKey: true, shiftKey: true }));
    expect(onRedo).toHaveBeenCalledTimes(1);
    fireEvent.keyDown(window, key('y', { ctrlKey: true }));
    expect(onRedo).toHaveBeenCalledTimes(2);
    expect(onUndo).toHaveBeenCalledTimes(1);
  });

  it('prevents the default action for a handled shortcut', () => {
    renderHook(() => useCellUndoShortcuts(() => {}, () => {}));
    expect(fireEvent.keyDown(window, key('z', { ctrlKey: true }))).toBe(false);
  });

  it('ignores the shortcut while an input has focus so native text undo still works', () => {
    const onUndo = vi.fn();
    renderHook(() => useCellUndoShortcuts(onUndo, () => {}));
    const input = document.createElement('input');
    document.body.appendChild(input);
    fireEvent.keyDown(input, key('z', { ctrlKey: true }));
    expect(onUndo).not.toHaveBeenCalled();
    input.remove();
  });

  it('uses the latest callbacks after a rerender', () => {
    const first = vi.fn();
    const second = vi.fn();
    const { rerender } = renderHook(({ cb }) => useCellUndoShortcuts(cb, () => {}), { initialProps: { cb: first } });
    rerender({ cb: second });
    fireEvent.keyDown(window, key('z', { ctrlKey: true }));
    expect(first).not.toHaveBeenCalled();
    expect(second).toHaveBeenCalledTimes(1);
  });
});

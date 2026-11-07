import { useEffect } from 'react';
import { act, cleanup, fireEvent, render, renderHook, screen } from '@testing-library/react';
import { afterEach, expect, it, vi } from 'vitest';
import { useUndoStack } from './useUndoStack';

afterEach(cleanup);

it('Detects: undo then redo do not restore the edited value, so Ctrl+Z and Ctrl+Y would revert or replay the wrong change', () => {
  let value = 'before';
  const { result } = renderHook(() => useUndoStack());

  act(() => {
    value = 'after';
    result.current.push({ label: 'rename', undo: () => { value = 'before'; }, redo: () => { value = 'after'; } });
  });
  act(() => result.current.undo());
  expect(value).toBe('before');
  expect(result.current.canRedo).toBe(true);

  act(() => result.current.redo());
  expect(value).toBe('after');
});

function Harness({ undo, redo }: { undo: () => void; redo: () => void }) {
  const { push, onKeyDown, scopeRef } = useUndoStack();
  useEffect(() => {
    push({ label: 'edit', undo, redo });
  }, [push, undo, redo]);
  return (
    <>
      <button type="button">Outside</button>
      <section ref={scopeRef} tabIndex={-1} aria-label="Editor" onKeyDown={onKeyDown}>
        <button type="button">Inside</button>
        <input aria-label="Name" />
        <select aria-label="Kind"><option>one</option></select>
        <div contentEditable suppressContentEditableWarning>notes</div>
      </section>
    </>
  );
}

it('Detects: Ctrl+Z pressed outside an editor screen still undoes the last edit, so changes happen while the user works elsewhere', () => {
  const undo = vi.fn();
  render(<Harness undo={undo} redo={vi.fn()} />);

  fireEvent.keyDown(screen.getByText('Outside'), { key: 'z', ctrlKey: true });
  expect(undo).not.toHaveBeenCalled();

  fireEvent.keyDown(screen.getByText('Inside'), { key: 'z', ctrlKey: true });
  expect(undo).toHaveBeenCalledOnce();
});

it('Detects: the editor screen does not take focus on open, so Ctrl+Z after opening a screen does nothing until a control is clicked', () => {
  render(<Harness undo={vi.fn()} redo={vi.fn()} />);
  expect(document.activeElement).toBe(screen.getByRole('region', { name: 'Editor' }));
});

it('Detects: Ctrl+Y does nothing after an undo, so the keyboard redo shortcut would be dead', () => {
  const redo = vi.fn();
  render(<Harness undo={vi.fn()} redo={redo} />);
  const inside = screen.getByText('Inside');

  fireEvent.keyDown(inside, { key: 'z', ctrlKey: true });
  fireEvent.keyDown(inside, { key: 'y', ctrlKey: true });
  expect(redo).toHaveBeenCalledOnce();
});

it('Detects: Ctrl+Shift+Z does nothing after an undo, so users who expect the common redo shortcut get no redo', () => {
  const redo = vi.fn();
  render(<Harness undo={vi.fn()} redo={redo} />);
  const inside = screen.getByText('Inside');

  fireEvent.keyDown(inside, { key: 'z', ctrlKey: true });
  fireEvent.keyDown(inside, { key: 'Z', ctrlKey: true, shiftKey: true });
  expect(redo).toHaveBeenCalledOnce();
});

it('Detects: Ctrl+Z pressed in a text box, a select or a rich-text field would undo the last plugin edit instead of that field', () => {
  const undo = vi.fn();
  render(<Harness undo={undo} redo={vi.fn()} />);

  fireEvent.keyDown(screen.getByLabelText('Name'), { key: 'z', ctrlKey: true });
  fireEvent.keyDown(screen.getByLabelText('Kind'), { key: 'z', ctrlKey: true });
  fireEvent.keyDown(screen.getByText('notes'), { key: 'z', ctrlKey: true });
  expect(undo).not.toHaveBeenCalled();
});

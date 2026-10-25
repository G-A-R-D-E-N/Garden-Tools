import { act, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { describe, expect, it, vi } from 'vitest';
import SwfPanel from './SwfPanel';

function stubBridge(overrides: Record<string, unknown> = {}) {
  const swf = {
    Open: vi.fn(async () => JSON.stringify({ revision: 0, undo: 0, redo: 0, dirty: false })),
    Characters: vi.fn(async () => JSON.stringify({ characters: [{ id: 11, kind: 'EditText', tagIndex: 0, name: '', imported: '' }] })),
    FrameLabels: vi.fn(async () => JSON.stringify({ labels: [{ name: 'start', tagIndex: 1, spriteId: 0, insideSprite: false }] })),
    Frame: vi.fn(async () => JSON.stringify({ rows: [{ depth: 1, character: 11, name: '', x: 10, y: 20, scaleX: 1, scaleY: 1, rotation: 0, instanceId: 7 }] })),
    RenameLabel: vi.fn(async () => JSON.stringify({ changed: true, revision: 1, undo: 1, redo: 0, dirty: true })),
    Undo: vi.fn(async () => JSON.stringify({ changed: true, revision: 2, undo: 0, redo: 1, dirty: true })),
    Redo: vi.fn(async () => JSON.stringify({ changed: true, revision: 3, undo: 1, redo: 0, dirty: true })),
    State: vi.fn(async () => JSON.stringify({ revision: 0, undo: 0, redo: 0, dirty: false })),
    Save: vi.fn(async () => JSON.stringify({ saved: 'Interface/Test.swf' })),
    AddLabel: vi.fn(async () => JSON.stringify({ changed: true, revision: 5, undo: 3, redo: 0, dirty: true })),
    ReadEditText: vi.fn(async () => JSON.stringify({ id: 11, text: 'hello', color: 'FF112233', x0: 0, x1: 400, y0: 0, y1: 200 })),
    EditEditText: vi.fn(async () => JSON.stringify({ changed: true, revision: 4, undo: 2, redo: 0, dirty: true })),
    ...overrides,
  };
  vi.stubGlobal('chrome', { webview: { hostObjects: { swf } } });
  return swf;
}

describe('SwfPanel', () => {
  it('selects, renames, undoes, redoes and saves through the history', async () => {
    const swf = stubBridge();
    render(<SwfPanel onClose={() => {}} />);

    fireEvent.change(screen.getByPlaceholderText('Interface/Test.swf'), { target: { value: 'Interface/Test.swf' } });
    await act(async () => { fireEvent.click(screen.getByText('Open')); });
    await waitFor(() => expect(swf.Characters).toHaveBeenCalled());
    expect(screen.getByText(/EditText #11/)).toBeTruthy();
    expect(screen.getByText(/root · start/)).toBeTruthy();

    fireEvent.click(screen.getByText(/root · start/));
    fireEvent.change(screen.getByDisplayValue('start'), { target: { value: 'menu' } });
    await act(async () => { fireEvent.click(screen.getByText('Rename')); });
    await waitFor(() => expect(swf.RenameLabel).toHaveBeenCalledWith(1, 'menu'));

    await act(async () => { fireEvent.click(screen.getByTitle('Undo')); });
    await waitFor(() => expect(swf.Undo).toHaveBeenCalled());
    await act(async () => { fireEvent.click(screen.getByTitle('Redo')); });
    await waitFor(() => expect(swf.Redo).toHaveBeenCalled());
    await act(async () => { fireEvent.click(screen.getByTitle('Save')); });
    await waitFor(() => expect(swf.Save).toHaveBeenCalled());
  });

  it('shows a Radicle refusal verbatim', async () => {
    stubBridge({ RenameLabel: vi.fn(async () => JSON.stringify({ error: 'a frame label with that name already exists on this timeline' })) });
    render(<SwfPanel onClose={() => {}} />);
    fireEvent.change(screen.getByPlaceholderText('Interface/Test.swf'), { target: { value: 'Interface/Test.swf' } });
    await act(async () => { fireEvent.click(screen.getByText('Open')); });
    fireEvent.click(await screen.findByText(/root · start/));
    await act(async () => { fireEvent.click(screen.getByText('Rename')); });
    expect(await screen.findByText(/already exists on this timeline/)).toBeTruthy();
  });

  it('edits edit-text fields and keeps them in the history', async () => {
    const swf = stubBridge();
    render(<SwfPanel onClose={() => {}} />);
    fireEvent.change(screen.getByPlaceholderText('Interface/Test.swf'), { target: { value: 'Interface/Test.swf' } });
    await act(async () => { fireEvent.click(screen.getByText('Open')); });
    await act(async () => { fireEvent.click(await screen.findByText(/EditText #11/)); });
    await waitFor(() => expect(swf.ReadEditText).toHaveBeenCalledWith(0));

    const textField = await screen.findByDisplayValue('hello');
    fireEvent.change(textField, { target: { value: 'hello world' } });
    await act(async () => { fireEvent.click(screen.getByText('Apply')); });
    await waitFor(() => expect(swf.EditEditText).toHaveBeenCalledWith(0, 'hello world', 'FF112233', 0, 400, 0, 200));
  });
});
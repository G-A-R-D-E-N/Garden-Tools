import { act, cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import type { ConfirmRequest } from './useDialogs';
import PapyrusPanel from './PapyrusPanel';

const dialogs = vi.hoisted(() => ({ confirm: vi.fn<(req: ConfirmRequest) => Promise<boolean>>() }));
vi.mock('./useDialogs', () => ({ useDialogs: () => ({ confirm: dialogs.confirm }) }));

const SCRIPT_A = 'C:\\scripts\\a.psc';
const SCRIPT_B = 'C:\\scripts\\b.psc';
const SCRIPT_C = 'C:\\scripts\\c.psc';
const texts: Record<string, string> = {
  [SCRIPT_A]: 'abcdefghijk',
  [SCRIPT_B]: 'bbbbbbbbbb',
  [SCRIPT_C]: 'cccccccccc',
};

const readScript = vi.fn<(path: string) => Promise<string>>();
const symbolAt = vi.fn<(text: string, path: string, offset: number, imports: string) => Promise<string>>();
const complete = vi.fn<(text: string, path: string, offset: number, imports: string) => Promise<string>>();
const signatureHelp = vi.fn<(text: string, path: string, offset: number, imports: string) => Promise<string>>();
const checkText = vi.fn<(text: string, path: string, revision: number, imports: string) => Promise<string>>();

const symbolIn = (file: string, start: number) => JSON.stringify({
  resolved: true, name: 'Target', signature: 'Target()', file, sameFile: false, start, length: 3,
});

const checkReply = (revision: number, message: string) => JSON.stringify({
  revision, complete: true, errorCount: 1,
  diagnostics: [{ code: 'PAP0030', severity: 'error', message, line: 1, column: 1, start: 0, length: 1 }],
});

const getOwnerReply = JSON.stringify({
  context: 'member', replaceStart: 4, replaceLength: 2,
  items: [{ label: 'GetOwner', kind: 'Function', detail: 'Function GetOwner()', insertText: 'GetOwner', documentation: null }],
});

function stubBridge() {
  vi.stubGlobal('chrome', {
    webview: {
      addEventListener: vi.fn(),
      removeEventListener: vi.fn(),
      hostObjects: {
        papyrus: {
          ReadScript: readScript,
          SymbolAt: symbolAt,
          Complete: complete,
          SignatureHelp: signatureHelp,
          CheckText: checkText,
          Analyze: vi.fn(async () => JSON.stringify({ diagnostics: [], symbols: [] })),
          WriteScript: vi.fn(async () => ''),
        },
      },
    },
  });
}

function sourceInput() {
  return screen.getByPlaceholderText(/Path, or drag a/) as HTMLInputElement;
}

function editor() {
  return document.querySelector('.papyrus-editor') as HTMLTextAreaElement;
}

async function settle() {
  await act(async () => { await new Promise(resolve => setTimeout(resolve, 0)); });
}

async function openScript(path: string) {
  fireEvent.change(sourceInput(), { target: { value: path } });
  fireEvent.click(screen.getByRole('button', { name: 'Open in editor' }));
  await waitFor(() => expect(editor().value).toBe(texts[path]));
}

beforeEach(() => {
  vi.clearAllMocks();
  localStorage.clear();
  localStorage.setItem('papyrus.mode', 'analyze');
  dialogs.confirm.mockResolvedValue(true);
  readScript.mockImplementation(async path => texts[path] ?? 'ERR: missing');
  symbolAt.mockResolvedValue(symbolIn(SCRIPT_B, 0));
  complete.mockResolvedValue(JSON.stringify({ context: 'general', replaceStart: 0, replaceLength: 0, items: [] }));
  signatureHelp.mockResolvedValue(JSON.stringify({ active: false }));
  checkText.mockImplementation(async (_text, _path, revision) =>
    JSON.stringify({ revision, complete: true, errorCount: 0, diagnostics: [] }));
  stubBridge();
});

afterEach(() => {
  cleanup();
  vi.unstubAllGlobals();
});

describe('Papyrus open over unsaved edits (GT-088)', () => {
  it('asks before opening another script over a dirty buffer and keeps it when cancelled', async () => {
    dialogs.confirm.mockResolvedValue(false);
    render(<PapyrusPanel onClose={() => {}} />);
    await openScript(SCRIPT_A);
    fireEvent.change(editor(), { target: { value: 'A edited' } });

    fireEvent.change(sourceInput(), { target: { value: SCRIPT_B } });
    fireEvent.click(screen.getByRole('button', { name: 'Open in editor' }));
    await waitFor(() => expect(dialogs.confirm).toHaveBeenCalledTimes(1));
    await settle();
    expect(readScript).toHaveBeenCalledTimes(1);
    expect(editor().value).toBe('A edited');
  });

  it('opens the other script after the user confirms discarding the edit', async () => {
    render(<PapyrusPanel onClose={() => {}} />);
    await openScript(SCRIPT_A);
    fireEvent.change(editor(), { target: { value: 'A edited' } });

    fireEvent.change(sourceInput(), { target: { value: SCRIPT_B } });
    fireEvent.click(screen.getByRole('button', { name: 'Open in editor' }));
    await waitFor(() => expect(editor().value).toBe(texts[SCRIPT_B]));
    expect(dialogs.confirm).toHaveBeenCalledTimes(1);
  });

  it('opens a clean buffer without asking', async () => {
    render(<PapyrusPanel onClose={() => {}} />);
    await openScript(SCRIPT_A);

    fireEvent.change(sourceInput(), { target: { value: SCRIPT_B } });
    fireEvent.click(screen.getByRole('button', { name: 'Open in editor' }));
    await waitFor(() => expect(editor().value).toBe(texts[SCRIPT_B]));
    expect(dialogs.confirm).not.toHaveBeenCalled();
  });
});

describe('Papyrus go-to-definition over unsaved edits (GT-088)', () => {
  it('asks before following a definition into another file over a dirty buffer', async () => {
    dialogs.confirm.mockResolvedValue(false);
    render(<PapyrusPanel onClose={() => {}} />);
    await openScript(SCRIPT_A);
    fireEvent.change(editor(), { target: { value: 'A edited' } });

    fireEvent.keyDown(editor(), { key: 'F12' });
    await waitFor(() => expect(dialogs.confirm).toHaveBeenCalledTimes(1));
    await settle();
    expect(readScript).toHaveBeenCalledTimes(1);
    expect(editor().value).toBe('A edited');
  });

  it('follows the definition after the user confirms discarding the edit', async () => {
    render(<PapyrusPanel onClose={() => {}} />);
    await openScript(SCRIPT_A);
    fireEvent.change(editor(), { target: { value: 'A edited' } });

    fireEvent.keyDown(editor(), { key: 'F12' });
    await waitFor(() => expect(readScript).toHaveBeenLastCalledWith(SCRIPT_B));
    await waitFor(() => expect(editor().value).toBe(texts[SCRIPT_B]));
  });
});

describe('Papyrus F12 resolves the symbol at the caret (GT-095)', () => {
  it('follows the symbol under the caret even when a previous symbol was stored', async () => {
    symbolAt.mockImplementation(async (_text, _path, offset) =>
      offset < 5 ? symbolIn(SCRIPT_B, 0) : symbolIn(SCRIPT_C, 0));
    render(<PapyrusPanel onClose={() => {}} />);
    await openScript(SCRIPT_A);

    editor().setSelectionRange(0, 0);
    fireEvent.click(editor());
    await settle();

    editor().setSelectionRange(8, 8);
    fireEvent.keyDown(editor(), { key: 'F12' });
    await waitFor(() => expect(readScript).toHaveBeenLastCalledWith(SCRIPT_C));
    expect(readScript).not.toHaveBeenCalledWith(SCRIPT_B);
  });
});

async function openCompletionAtCaret(caret: number) {
  fireEvent.change(editor(), { target: { value: 'Foo.Ge)' } });
  editor().setSelectionRange(caret, caret);
  fireEvent.keyDown(editor(), { key: ' ', ctrlKey: true });
  await screen.findByText('GetOwner');
}

describe('Papyrus live check and completion (editor help)', () => {
  it('drops a CheckText reply for an older revision that arrives after a newer one', async () => {
    const replies = new Map<number, (json: string) => void>();
    checkText.mockImplementation((_text, _path, revision) =>
      new Promise<string>(resolve => { replies.set(revision, resolve); }));
    render(<PapyrusPanel onClose={() => {}} />);
    await openScript(SCRIPT_A);
    await waitFor(() => expect(replies.has(1)).toBe(true));

    fireEvent.change(editor(), { target: { value: 'A edited' } });
    await waitFor(() => expect(replies.has(2)).toBe(true));

    await act(async () => { replies.get(2)!(checkReply(2, 'Newer problem')); });
    await screen.findByText('Newer problem');
    await act(async () => { replies.get(1)!(checkReply(1, 'Older problem')); });
    await settle();
    expect(screen.queryByText('Older problem')).toBeNull();
    expect(screen.getByText('Newer problem')).toBeTruthy();
  });

  it('coalesces rapid typing into one CheckText call with the final text', async () => {
    render(<PapyrusPanel onClose={() => {}} />);
    await openScript(SCRIPT_A);
    checkText.mockClear();

    fireEvent.change(editor(), { target: { value: 'A e' } });
    fireEvent.change(editor(), { target: { value: 'A ed' } });
    fireEvent.change(editor(), { target: { value: 'A edited' } });
    await waitFor(() => expect(checkText).toHaveBeenCalled());
    await act(async () => { await new Promise(resolve => setTimeout(resolve, 400)); });
    expect(checkText).toHaveBeenCalledTimes(1);
    expect(checkText.mock.calls[0][0]).toBe('A edited');
  });

  it('accepting a completion replaces exactly the replace range and keeps the text around it', async () => {
    complete.mockResolvedValue(getOwnerReply);
    render(<PapyrusPanel onClose={() => {}} />);
    await openScript(SCRIPT_A);
    await openCompletionAtCaret(6);
    expect(complete).toHaveBeenLastCalledWith('Foo.Ge)', SCRIPT_A, 6, '');

    fireEvent.keyDown(editor(), { key: 'Enter' });
    expect(editor().value).toBe('Foo.GetOwner)');
    expect(screen.queryByText('GetOwner')).toBeNull();
  });

  it('Escape closes the completion popup and leaves the text alone', async () => {
    complete.mockResolvedValue(getOwnerReply);
    render(<PapyrusPanel onClose={() => {}} />);
    await openScript(SCRIPT_A);
    await openCompletionAtCaret(6);

    fireEvent.keyDown(editor(), { key: 'Escape' });
    expect(screen.queryByText('GetOwner')).toBeNull();
    expect(editor().value).toBe('Foo.Ge)');
  });

  it('drops a CheckText reply whose echoed revision is not the one it was sent with', async () => {
    checkText.mockImplementation(async () => checkReply(99, 'Wrong revision problem'));
    render(<PapyrusPanel onClose={() => {}} />);
    await openScript(SCRIPT_A);
    await waitFor(() => expect(checkText).toHaveBeenCalled());
    await settle();
    expect(screen.queryByText('Wrong revision problem')).toBeNull();
  });

  it('a dot typed inside a comment does not open the completion popup', async () => {
    complete.mockResolvedValue(JSON.stringify({ ...JSON.parse(getOwnerReply), context: 'general' }));
    render(<PapyrusPanel onClose={() => {}} />);
    await openScript(SCRIPT_A);
    fireEvent.change(editor(), { target: { value: '; Foo.' } });
    await waitFor(() => expect(complete).toHaveBeenCalledWith('; Foo.', SCRIPT_A, 6, ''));
    await settle();
    expect(screen.queryByText('GetOwner')).toBeNull();
  });

  it('a dot typed after a member target opens the completion popup', async () => {
    complete.mockResolvedValue(getOwnerReply);
    render(<PapyrusPanel onClose={() => {}} />);
    await openScript(SCRIPT_A);
    fireEvent.change(editor(), { target: { value: 'Foo.' } });
    await screen.findByText('GetOwner');
  });

  it('sends a UTF-8 byte offset and splices an accepted item at the UTF-16 caret', async () => {
    complete.mockResolvedValue(JSON.stringify({ ...JSON.parse(getOwnerReply), replaceStart: 7, replaceLength: 2 }));
    render(<PapyrusPanel onClose={() => {}} />);
    await openScript(SCRIPT_A);
    fireEvent.change(editor(), { target: { value: '—Foo.Ge)' } });
    editor().setSelectionRange(7, 7);
    fireEvent.keyDown(editor(), { key: ' ', ctrlKey: true });
    await screen.findByText('GetOwner');
    expect(complete).toHaveBeenLastCalledWith('—Foo.Ge)', SCRIPT_A, 9, '');

    fireEvent.keyDown(editor(), { key: 'Enter' });
    expect(editor().value).toBe('—Foo.GetOwner)');
  });

  it('Escape clears the signature bar', async () => {
    signatureHelp.mockResolvedValue(JSON.stringify({
      active: true, activeSignature: 0, activeParameter: 0,
      signatures: [{ label: 'Function Pick(Int first)', documentation: null, parameters: [{ label: 'Int first', documentation: null }] }],
    }));
    render(<PapyrusPanel onClose={() => {}} />);
    await openScript(SCRIPT_A);
    fireEvent.change(editor(), { target: { value: 'Pick(' } });
    await waitFor(() => expect(document.querySelector('.papyrus-signature')).not.toBeNull());

    fireEvent.keyDown(editor(), { key: 'Escape' });
    expect(document.querySelector('.papyrus-signature')).toBeNull();
  });
});

describe('Papyrus close over unsaved edits (GT-088)', () => {
  it('asks before closing a dirty buffer and keeps it when the × is cancelled', async () => {
    dialogs.confirm.mockResolvedValue(false);
    const onClose = vi.fn();
    render(<PapyrusPanel onClose={onClose} />);
    await openScript(SCRIPT_A);
    fireEvent.change(editor(), { target: { value: 'A edited' } });

    fireEvent.click(document.querySelector('.papyrus-close')!);
    await waitFor(() => expect(dialogs.confirm).toHaveBeenCalledTimes(1));
    expect(onClose).not.toHaveBeenCalled();
    expect(editor().value).toBe('A edited');
  });

  it('closes after the user confirms discarding the buffer', async () => {
    const onClose = vi.fn();
    render(<PapyrusPanel onClose={onClose} />);
    await openScript(SCRIPT_A);
    fireEvent.change(editor(), { target: { value: 'A edited' } });

    fireEvent.click(document.querySelector('.papyrus-close')!);
    await waitFor(() => expect(onClose).toHaveBeenCalledTimes(1));
  });

  it('does not close silently on a backdrop click over a dirty buffer', async () => {
    dialogs.confirm.mockResolvedValue(false);
    const onClose = vi.fn();
    render(<PapyrusPanel onClose={onClose} />);
    await openScript(SCRIPT_A);
    fireEvent.change(editor(), { target: { value: 'A edited' } });

    fireEvent.click(document.querySelector('.papyrus-overlay')!);
    await waitFor(() => expect(dialogs.confirm).toHaveBeenCalledTimes(1));
    expect(onClose).not.toHaveBeenCalled();
  });

  it('closes a clean panel without asking', async () => {
    const onClose = vi.fn();
    render(<PapyrusPanel onClose={onClose} />);
    await openScript(SCRIPT_A);

    fireEvent.click(document.querySelector('.papyrus-close')!);
    expect(onClose).toHaveBeenCalledTimes(1);
    expect(dialogs.confirm).not.toHaveBeenCalled();
  });
});

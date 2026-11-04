import { act, cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import type { ConfirmRequest, PromptRequest } from './useDialogs';
import PapyrusPanel from './PapyrusPanel';

const dialogs = vi.hoisted(() => ({
  confirm: vi.fn<(req: ConfirmRequest) => Promise<boolean>>(),
  prompt: vi.fn<(req: PromptRequest) => Promise<string | null>>(),
}));
vi.mock('./useDialogs', () => ({ useDialogs: () => ({ confirm: dialogs.confirm, prompt: dialogs.prompt }) }));

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
const references = vi.fn<(text: string, path: string, offset: number, imports: string) => Promise<string>>();
const rename = vi.fn<(text: string, path: string, offset: number, newName: string, imports: string) => Promise<string>>();
const workspaceSymbols = vi.fn<(query: string, limit: number, imports: string) => Promise<string>>();
const writeScript = vi.fn<(path: string, text: string) => Promise<string>>();

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
          WriteScript: writeScript,
          References: references,
          Rename: rename,
          WorkspaceSymbols: workspaceSymbols,
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
  dialogs.prompt.mockResolvedValue(null);
  references.mockResolvedValue(JSON.stringify({ resolved: false, reason: 'unresolved' }));
  rename.mockResolvedValue(JSON.stringify({ ok: false, reason: 'refused' }));
  workspaceSymbols.mockResolvedValue(JSON.stringify({ truncated: false, results: [] }));
  writeScript.mockResolvedValue('');
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

describe('Papyrus byte offsets and editor navigation (UTF-8 to UTF-16)', () => {
  it('F12 sends the caret as a UTF-8 byte offset and selects the range in UTF-16 units', async () => {
    symbolAt.mockResolvedValue(JSON.stringify({
      resolved: true, name: 'Target', signature: 'Target()', file: SCRIPT_A, sameFile: true, start: 3, length: 6,
    }));
    render(<PapyrusPanel onClose={() => {}} />);
    await openScript(SCRIPT_A);
    fireEvent.change(editor(), { target: { value: 'é Target' } });
    editor().setSelectionRange(2, 2);

    fireEvent.keyDown(editor(), { key: 'F12' });
    await waitFor(() => expect(symbolAt).toHaveBeenLastCalledWith('é Target', SCRIPT_A, 3, ''));
    await waitFor(() => expect(editor().selectionStart).toBe(2));
    expect(editor().selectionEnd).toBe(8);
  });

  it('a problem row for a range after non-ASCII text selects the same characters', async () => {
    checkText.mockImplementation(async (_text, _path, revision) => JSON.stringify({
      revision, complete: true, errorCount: 1,
      diagnostics: [{ code: 'PAP0030', severity: 'error', message: 'Bad target', line: 1, column: 3, start: 3, length: 6 }],
    }));
    render(<PapyrusPanel onClose={() => {}} />);
    await openScript(SCRIPT_A);
    fireEvent.change(editor(), { target: { value: 'é Target' } });

    fireEvent.click((await screen.findByText('Bad target')).closest('button')!);
    expect(editor().selectionStart).toBe(2);
    expect(editor().selectionEnd).toBe(8);
  });

  it('a CheckText host error without a revision replaces the checking line', async () => {
    checkText.mockResolvedValue(JSON.stringify({ error: 'bridge down' }));
    render(<PapyrusPanel onClose={() => {}} />);
    await openScript(SCRIPT_A);
    fireEvent.change(editor(), { target: { value: 'A edited' } });

    await screen.findByText('bridge down');
    expect(screen.queryByText(/checking/)).toBeNull();
  });

  it('typing ) asks for the signature again so the enclosing call keeps its signature', async () => {
    signatureHelp.mockResolvedValue(JSON.stringify({
      active: true, activeSignature: 0, activeParameter: 0,
      signatures: [{ label: 'Function Pick(Int first)', documentation: null, parameters: [{ label: 'Int first', documentation: null }] }],
    }));
    render(<PapyrusPanel onClose={() => {}} />);
    await openScript(SCRIPT_A);
    fireEvent.change(editor(), { target: { value: 'Pick(' } });
    await waitFor(() => expect(document.querySelector('.papyrus-signature')).not.toBeNull());

    fireEvent.change(editor(), { target: { value: 'Pick(1)' } });
    await waitFor(() => expect(signatureHelp).toHaveBeenLastCalledWith('Pick(1)', SCRIPT_A, 7, ''));
    await settle();
    expect(document.querySelector('.papyrus-signature')).not.toBeNull();
  });

  it('a hint is listed dimmed and does not count toward the error total', async () => {
    checkText.mockImplementation(async (_text, _path, revision) => JSON.stringify({
      revision, complete: true, errorCount: 0, diagnostics: [],
      hints: [{ code: 'PAPH002', severity: 'hint', unnecessary: true, message: "Local 'spare' is never used.", line: 3, column: 7, start: 30, length: 5 }],
    }));
    render(<PapyrusPanel onClose={() => {}} />);
    await openScript(SCRIPT_A);
    fireEvent.change(editor(), { target: { value: 'A edited' } });

    const hint = await screen.findByText("Local 'spare' is never used.");
    expect(hint.closest('button')!.className).toContain('hint');
    expect(screen.getByText('no errors')).toBeTruthy();
  });
});

describe('Papyrus find references, rename and workspace symbols', () => {
  const declaredAt = (file: string) => ({ name: 'n', kind: 'Local', signature: 'Int n', file, line: 1, column: 1, start: 0, length: 1 });

  it('a references row in another file opens that file and selects the range', async () => {
    references.mockResolvedValue(JSON.stringify({
      declaration: declaredAt(SCRIPT_A),
      references: [{ file: SCRIPT_B, line: 1, column: 3, start: 2, length: 4, isDeclaration: false }],
    }));
    render(<PapyrusPanel onClose={() => {}} />);
    await openScript(SCRIPT_A);

    fireEvent.keyDown(editor(), { key: 'F12', shiftKey: true });
    fireEvent.click((await screen.findByText('b.psc:1:3')).closest('button')!);
    await waitFor(() => expect(editor().value).toBe(texts[SCRIPT_B]));
    await waitFor(() => expect(editor().selectionStart).toBe(2));
    expect(editor().selectionEnd).toBe(6);
  });

  it('a rename applies its edits to the buffer when the target follows non-ASCII text', async () => {
    rename.mockResolvedValue(JSON.stringify({
      ok: true,
      edits: [
        { file: SCRIPT_A, start: 3, length: 6, newText: 'Goal' },
        { file: SCRIPT_A, start: 12, length: 6, newText: 'Goal' },
      ],
      fileRename: null,
    }));
    dialogs.prompt.mockResolvedValue('Goal');
    render(<PapyrusPanel onClose={() => {}} />);
    await openScript(SCRIPT_A);
    fireEvent.change(editor(), { target: { value: 'é Target = Target' } });
    editor().setSelectionRange(2, 2);

    fireEvent.keyDown(editor(), { key: 'F2' });
    await waitFor(() => expect(rename).toHaveBeenCalledTimes(1));
    expect(rename).toHaveBeenLastCalledWith('é Target = Target', SCRIPT_A, 3, 'Goal', '');
    await waitFor(() => expect(editor().value).toBe('é Goal = Goal'));
    expect(dialogs.confirm).not.toHaveBeenCalled();
  });

  it('a rename that changes another file asks first and writes that file through WriteScript', async () => {
    rename.mockResolvedValue(JSON.stringify({ ok: true, edits: [{ file: SCRIPT_B, start: 0, length: 3, newText: 'Zed' }], fileRename: null }));
    dialogs.prompt.mockResolvedValue('Zed');
    render(<PapyrusPanel onClose={() => {}} />);
    await openScript(SCRIPT_A);
    editor().setSelectionRange(0, 0);

    fireEvent.keyDown(editor(), { key: 'F2' });
    await waitFor(() => expect(writeScript).toHaveBeenCalledWith(SCRIPT_B, 'Zedbbbbbbb'));
    expect(dialogs.confirm).toHaveBeenCalledWith(expect.objectContaining({ message: expect.stringContaining('b.psc') }));
  });

  it('declining the cross-file rename writes nothing and leaves the buffer alone', async () => {
    dialogs.confirm.mockResolvedValue(false);
    rename.mockResolvedValue(JSON.stringify({ ok: true, edits: [{ file: SCRIPT_B, start: 0, length: 3, newText: 'Zed' }], fileRename: null }));
    dialogs.prompt.mockResolvedValue('Zed');
    render(<PapyrusPanel onClose={() => {}} />);
    await openScript(SCRIPT_A);
    editor().setSelectionRange(0, 0);

    fireEvent.keyDown(editor(), { key: 'F2' });
    await waitFor(() => expect(dialogs.confirm).toHaveBeenCalledTimes(1));
    await settle();
    expect(writeScript).not.toHaveBeenCalled();
    expect(editor().value).toBe('abcdefghijk');
  });

  it('workspace search waits 200 ms after the last keystroke, then jumps to the chosen symbol', async () => {
    workspaceSymbols.mockResolvedValue(JSON.stringify({
      truncated: false,
      results: [{ name: 'Target', kind: 'Function', signature: 'Function Target()', container: 'Greeter', file: SCRIPT_B, line: 2, column: 1, start: 4, length: 6 }],
    }));
    render(<PapyrusPanel onClose={() => {}} />);
    await openScript(SCRIPT_A);
    const search = screen.getByPlaceholderText(/Find a symbol/) as HTMLInputElement;

    fireEvent.keyDown(editor(), { key: 't', ctrlKey: true });
    expect(document.activeElement).toBe(search);
    fireEvent.change(search, { target: { value: 'Ta' } });
    fireEvent.change(search, { target: { value: 'Tar' } });
    fireEvent.change(search, { target: { value: 'Targ' } });
    expect(workspaceSymbols).not.toHaveBeenCalled();

    await waitFor(() => expect(workspaceSymbols).toHaveBeenCalledTimes(1));
    expect(workspaceSymbols).toHaveBeenCalledWith('Targ', 0, '');
    fireEvent.click((await screen.findByText('Target')).closest('button')!);
    await waitFor(() => expect(editor().value).toBe(texts[SCRIPT_B]));
    await waitFor(() => expect(editor().selectionStart).toBe(4));
  });

  it('a rename whose text changed while Rename was pending applies nothing and keeps the typed text', async () => {
    rename.mockImplementation(async () => {
      fireEvent.change(editor(), { target: { value: 'typed while renaming' } });
      return JSON.stringify({ ok: true, edits: [{ file: SCRIPT_A, start: 0, length: 3, newText: 'Zed' }], fileRename: null });
    });
    dialogs.prompt.mockResolvedValue('Zed');
    render(<PapyrusPanel onClose={() => {}} />);
    await openScript(SCRIPT_A);
    editor().setSelectionRange(0, 0);

    fireEvent.keyDown(editor(), { key: 'F2' });
    await waitFor(() => expect(screen.getByText(/Rename not applied/)).toBeTruthy());
    expect(editor().value).toBe('typed while renaming');
    expect(writeScript).not.toHaveBeenCalled();
  });

  it('a rename that lists the same edit twice applies it once', async () => {
    rename.mockResolvedValue(JSON.stringify({
      ok: true,
      edits: [
        { file: SCRIPT_A, start: 0, length: 3, newText: 'Zed' },
        { file: SCRIPT_A, start: 0, length: 3, newText: 'Zed' },
      ],
      fileRename: null,
    }));
    dialogs.prompt.mockResolvedValue('Zed');
    render(<PapyrusPanel onClose={() => {}} />);
    await openScript(SCRIPT_A);
    editor().setSelectionRange(0, 0);

    fireEvent.keyDown(editor(), { key: 'F2' });
    await waitFor(() => expect(editor().value).toBe('Zeddefghijk'));
  });

  it('typing clears the references list, because its offsets belong to the text that was searched', async () => {
    references.mockResolvedValue(JSON.stringify({
      declaration: declaredAt(SCRIPT_A),
      references: [{ file: SCRIPT_A, line: 1, column: 3, start: 2, length: 4, isDeclaration: false }],
    }));
    render(<PapyrusPanel onClose={() => {}} />);
    await openScript(SCRIPT_A);
    fireEvent.keyDown(editor(), { key: 'F12', shiftKey: true });
    await screen.findByText(/REFERENCES to/);

    fireEvent.change(editor(), { target: { value: 'abcdefghijkl' } });
    expect(screen.queryByText(/REFERENCES to/)).toBeNull();
  });

  it('a references reply that arrives after an edit is dropped instead of listing stale offsets', async () => {
    let answer: (json: string) => void = () => {};
    references.mockImplementationOnce(() => new Promise<string>(resolve => { answer = resolve; }));
    render(<PapyrusPanel onClose={() => {}} />);
    await openScript(SCRIPT_A);
    fireEvent.keyDown(editor(), { key: 'F12', shiftKey: true });
    await waitFor(() => expect(references).toHaveBeenCalledTimes(1));

    fireEvent.change(editor(), { target: { value: 'abcdefghijkl' } });
    answer(JSON.stringify({
      declaration: declaredAt(SCRIPT_A),
      references: [{ file: SCRIPT_A, line: 1, column: 3, start: 2, length: 4, isDeclaration: false }],
    }));
    await settle();
    expect(screen.queryByText(/REFERENCES to/)).toBeNull();
  });

  it('a workspace hit in the open file selects its line when the buffer has unsaved edits', async () => {
    workspaceSymbols.mockResolvedValue(JSON.stringify({
      truncated: false,
      results: [{ name: 'Target', kind: 'Function', signature: 'Function Target()', container: null, file: SCRIPT_A, line: 2, column: 1, start: 4, length: 6 }],
    }));
    render(<PapyrusPanel onClose={() => {}} />);
    await openScript(SCRIPT_A);
    fireEvent.change(editor(), { target: { value: 'abcdefghijk\nxyz' } });

    fireEvent.change(screen.getByPlaceholderText(/Find a symbol/), { target: { value: 'Targ' } });
    fireEvent.click((await screen.findByText('Target')).closest('button')!);
    await waitFor(() => expect(editor().selectionStart).toBe(12));
    expect(editor().selectionEnd).toBe(15);
  });

  it('a reference in an open CRLF file selects the same characters the search reported', async () => {
    const crlf = 'C:\\scripts\\d.psc';
    readScript.mockImplementation(async path => (path === crlf ? 'Alpha\r\nBeta Gamma' : texts[path] ?? 'ERR: missing'));
    references.mockResolvedValue(JSON.stringify({
      declaration: declaredAt(crlf),
      references: [{ file: crlf, line: 2, column: 6, start: 11, length: 5, isDeclaration: false }],
    }));
    render(<PapyrusPanel onClose={() => {}} />);
    fireEvent.change(sourceInput(), { target: { value: crlf } });
    fireEvent.click(screen.getByRole('button', { name: 'Open in editor' }));
    await waitFor(() => expect(editor().value).toBe('Alpha\nBeta Gamma'));

    editor().setSelectionRange(11, 11);
    fireEvent.keyDown(editor(), { key: 'F12', shiftKey: true });
    fireEvent.click((await screen.findByText('d.psc:2:6')).closest('button')!);
    await waitFor(() => expect(editor().selectionStart).toBe(11));
    expect(editor().selectionEnd).toBe(16);
  });
});

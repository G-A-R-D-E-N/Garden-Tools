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

const symbolIn = (file: string, start: number) => JSON.stringify({
  resolved: true, name: 'Target', signature: 'Target()', file, sameFile: false, start, length: 3,
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

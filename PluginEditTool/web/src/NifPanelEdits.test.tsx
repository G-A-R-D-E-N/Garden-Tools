import { cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import type { ConfirmRequest } from './useDialogs';
import NifPanel from './NifPanel';

const dialogs = vi.hoisted(() => ({ confirm: vi.fn<(req: ConfirmRequest) => Promise<boolean>>() }));
vi.mock('./useDialogs', () => ({ useDialogs: () => ({ confirm: dialogs.confirm }) }));
vi.mock('./NifViewport', () => ({ default: () => null }));

const A = 'C:\\mods\\a.nif';
const B = 'C:\\mods\\b.nif';
const C = 'C:\\mods\\c.nif';
const MA = 'C:\\mods\\a.bgsm';
const MB = 'C:\\mods\\b.bgsm';

const geo = vi.fn<(path: string) => Promise<string>>();
const tree = vi.fn<(path: string) => Promise<string>>();
const applyEdits = vi.fn<(path: string, editsJson: string, outNif: string) => Promise<string>>();
const inspect = vi.fn<(path: string) => Promise<string>>();
const setFields = vi.fn<(path: string, fieldsJson: string, outPath: string) => Promise<string>>();

const labelOf = (path: string) => (path.endsWith('a.nif') || path.endsWith('a.bgsm') ? 'Alpha' : 'Beta');

function treeFor(name: string) {
  return JSON.stringify({
    fo4: true, file: name,
    blocks: [{ id: 1, type: 'NiNode', name, group: 'Nodes', cat: '',
      fields: [{ key: 'name', label: 'Name', type: 'string', value: name }] }],
  });
}

function fieldsFor(value: string) {
  return JSON.stringify({
    fileName: 'skin.bgsm', version: 2,
    fields: [{ name: 'Texture', section: 'material', type: 'string', value }],
  });
}

function stubBridge() {
  vi.stubGlobal('chrome', {
    webview: {
      addEventListener: vi.fn(),
      removeEventListener: vi.fn(),
      hostObjects: {
        nif: { StageDroppedFile: vi.fn(), Geo: geo, Tree: tree, ApplyEdits: applyEdits },
        material: { Inspect: inspect, SetFields: setFields },
      },
    },
  });
}

function pathInput(pattern: RegExp) {
  return screen.getByPlaceholderText(pattern) as HTMLInputElement;
}

async function loadNif(path: string) {
  fireEvent.change(pathInput(/drag a \.nif here/), { target: { value: path } });
  fireEvent.click(document.querySelector('.nif-load-btn')!);
  await screen.findByDisplayValue(labelOf(path));
}

async function loadMaterial(path: string) {
  fireEvent.change(pathInput(/drag a \.bgsm/), { target: { value: path } });
  fireEvent.click(document.querySelector('.nif-load-btn')!);
  await screen.findByDisplayValue(labelOf(path));
}

function editField(current: string, value: string) {
  fireEvent.change(screen.getByDisplayValue(current), { target: { value } });
}

function save() {
  fireEvent.click(document.querySelector('.nif-save-btn')!);
}

function closeButton() {
  return document.querySelector('.papyrus-close')!;
}

function overlay() {
  return document.querySelector('.papyrus-overlay')!;
}

beforeEach(() => {
  vi.clearAllMocks();
  localStorage.clear();
  dialogs.confirm.mockResolvedValue(true);
  tree.mockImplementation(async p => treeFor(labelOf(p)));
  geo.mockResolvedValue(JSON.stringify({ shapes: [] }));
  applyEdits.mockResolvedValue('set OK');
  inspect.mockImplementation(async p => fieldsFor(labelOf(p)));
  setFields.mockResolvedValue('Set 1 field(s)');
  stubBridge();
});

afterEach(() => {
  cleanup();
  vi.unstubAllGlobals();
});

describe('NIF editor keeps edits with the file they belong to (GT-080)', () => {
  it('drops the previous file edits when another file is loaded', async () => {
    localStorage.setItem('nif.mode', 'edit');
    render(<NifPanel onClose={() => {}} />);
    await loadNif(A);
    editField('Alpha', 'Alpha edited');
    expect(screen.getByText('1 unsaved change')).toBeTruthy();

    await loadNif(B);
    expect(screen.queryByDisplayValue('Alpha edited')).toBeNull();
    expect(screen.getByText('No changes')).toBeTruthy();

    editField('Beta', 'Beta edited');
    save();
    await waitFor(() => expect(applyEdits).toHaveBeenCalled());
    expect(applyEdits.mock.calls[0][0]).toBe(B);
  });

  it('saves to the file loaded into the editor even when its geometry failed to load', async () => {
    localStorage.setItem('nif.mode', 'edit');
    render(<NifPanel onClose={() => {}} />);
    await loadNif(A);
    geo.mockRejectedValueOnce(new Error('no geometry'));
    await loadNif(B);
    editField('Beta', 'Beta edited');
    save();
    await waitFor(() => expect(applyEdits).toHaveBeenCalled());
    expect(applyEdits.mock.calls[0][0]).toBe(B);
  });

  it('keeps the save target when the path input is changed without loading', async () => {
    localStorage.setItem('nif.mode', 'edit');
    geo.mockRejectedValue(new Error('no geometry'));
    render(<NifPanel onClose={() => {}} />);
    await loadNif(A);
    editField('Alpha', 'Alpha edited');
    fireEvent.change(pathInput(/drag a \.nif here/), { target: { value: C } });
    save();
    await waitFor(() => expect(applyEdits).toHaveBeenCalled());
    expect(applyEdits.mock.calls[0][0]).toBe(A);
  });

  it('shows the loaded save target next to the save button', async () => {
    localStorage.setItem('nif.mode', 'edit');
    render(<NifPanel onClose={() => {}} />);
    await loadNif(A);
    expect(screen.getByText('Save target: a.nif')).toBeTruthy();
    expect(screen.getByTitle(A)).toBeTruthy();
  });

  it('keeps the edit and shows the ApplyEdits error when a save fails', async () => {
    localStorage.setItem('nif.mode', 'edit');
    render(<NifPanel onClose={() => {}} />);
    await loadNif(A);
    editField('Alpha', 'Alpha edited');
    applyEdits.mockResolvedValueOnce('Error: file is locked');
    save();
    await screen.findByText('Save failed -- Error: file is locked');
    expect(screen.getByText('1 unsaved change')).toBeTruthy();
    expect(screen.getByDisplayValue('Alpha edited')).toBeTruthy();
    expect(tree).toHaveBeenCalledTimes(1);
  });

  it('drops the previous material edits when another material is loaded', async () => {
    localStorage.setItem('nif.mode', 'materials');
    render(<NifPanel onClose={() => {}} />);
    await loadMaterial(MA);
    editField('Alpha', 'Alpha edited');
    expect(screen.getByText('1 unsaved change')).toBeTruthy();

    await loadMaterial(MB);
    expect(screen.queryByDisplayValue('Alpha edited')).toBeNull();

    editField('Beta', 'Beta edited');
    save();
    await waitFor(() => expect(setFields).toHaveBeenCalled());
    expect(setFields.mock.calls[0][0]).toBe(MB);
  });

  it('saves and reloads the loaded material when the path input is changed without loading', async () => {
    localStorage.setItem('nif.mode', 'materials');
    render(<NifPanel onClose={() => {}} />);
    await loadMaterial(MA);
    editField('Alpha', 'Alpha edited');

    fireEvent.change(pathInput(/drag a \.bgsm/), { target: { value: MB } });
    save();
    await waitFor(() => expect(setFields).toHaveBeenCalled());
    expect(setFields.mock.calls[0][0]).toBe(MA);
    await waitFor(() => expect(inspect).toHaveBeenCalledTimes(2));
    expect(inspect.mock.calls[1][0]).toBe(MA);
    expect(inspect).not.toHaveBeenCalledWith(MB);
  });
});

describe('NIF panel close with unsaved edits (GT-088)', () => {
  it('asks before closing a dirty editor and keeps the edit when cancelled', async () => {
    localStorage.setItem('nif.mode', 'edit');
    dialogs.confirm.mockResolvedValue(false);
    const onClose = vi.fn();
    render(<NifPanel onClose={onClose} />);
    await loadNif(A);
    editField('Alpha', 'Alpha edited');

    fireEvent.click(closeButton());
    await waitFor(() => expect(dialogs.confirm).toHaveBeenCalledTimes(1));
    expect(onClose).not.toHaveBeenCalled();
    expect(screen.getByDisplayValue('Alpha edited')).toBeTruthy();
  });

  it('closes after the user confirms discarding the dirty edit', async () => {
    localStorage.setItem('nif.mode', 'edit');
    const onClose = vi.fn();
    render(<NifPanel onClose={onClose} />);
    await loadNif(A);
    editField('Alpha', 'Alpha edited');

    fireEvent.click(closeButton());
    await waitFor(() => expect(onClose).toHaveBeenCalledTimes(1));
    expect(dialogs.confirm).toHaveBeenCalledTimes(1);
  });

  it('does not close silently on a backdrop click over a dirty editor', async () => {
    localStorage.setItem('nif.mode', 'edit');
    dialogs.confirm.mockResolvedValue(false);
    const onClose = vi.fn();
    render(<NifPanel onClose={onClose} />);
    await loadNif(A);
    editField('Alpha', 'Alpha edited');

    fireEvent.click(overlay());
    await waitFor(() => expect(dialogs.confirm).toHaveBeenCalledTimes(1));
    expect(onClose).not.toHaveBeenCalled();
  });

  it('asks before closing over unsaved material edits', async () => {
    localStorage.setItem('nif.mode', 'materials');
    dialogs.confirm.mockResolvedValue(false);
    const onClose = vi.fn();
    render(<NifPanel onClose={onClose} />);
    await loadMaterial(MA);
    editField('Alpha', 'Alpha edited');

    fireEvent.click(closeButton());
    await waitFor(() => expect(dialogs.confirm).toHaveBeenCalledTimes(1));
    expect(onClose).not.toHaveBeenCalled();
  });

  it('closes a clean panel without asking', async () => {
    localStorage.setItem('nif.mode', 'edit');
    const onClose = vi.fn();
    render(<NifPanel onClose={onClose} />);
    await loadNif(A);

    fireEvent.click(closeButton());
    await waitFor(() => expect(onClose).toHaveBeenCalledTimes(1));
    expect(dialogs.confirm).not.toHaveBeenCalled();
  });
});

describe('NIF load and mode switch over unsaved edits (GT-088)', () => {
  it('keeps the current file and edits when a tree load over a dirty editor is cancelled', async () => {
    localStorage.setItem('nif.mode', 'edit');
    dialogs.confirm.mockResolvedValue(false);
    render(<NifPanel onClose={() => {}} />);
    await loadNif(A);
    editField('Alpha', 'Alpha edited');

    fireEvent.change(pathInput(/drag a \.nif here/), { target: { value: B } });
    fireEvent.click(document.querySelector('.nif-load-btn')!);
    await waitFor(() => expect(dialogs.confirm).toHaveBeenCalledTimes(1));
    expect(tree).not.toHaveBeenCalledWith(B);
    expect(screen.getByDisplayValue('Alpha edited')).toBeTruthy();
  });

  it('loads the new file clean after the user confirms the discard', async () => {
    localStorage.setItem('nif.mode', 'edit');
    render(<NifPanel onClose={() => {}} />);
    await loadNif(A);
    editField('Alpha', 'Alpha edited');

    fireEvent.change(pathInput(/drag a \.nif here/), { target: { value: B } });
    fireEvent.click(document.querySelector('.nif-load-btn')!);
    await screen.findByDisplayValue('Beta');
    expect(dialogs.confirm).toHaveBeenCalledTimes(1);
    expect(screen.queryByDisplayValue('Alpha edited')).toBeNull();
    expect(screen.getByText('No changes')).toBeTruthy();
  });

  it('keeps the current material and edits when a material load over a dirty editor is cancelled', async () => {
    localStorage.setItem('nif.mode', 'materials');
    dialogs.confirm.mockResolvedValue(false);
    render(<NifPanel onClose={() => {}} />);
    await loadMaterial(MA);
    editField('Alpha', 'Alpha edited');

    fireEvent.change(pathInput(/drag a \.bgsm/), { target: { value: MB } });
    fireEvent.click(document.querySelector('.nif-load-btn')!);
    await waitFor(() => expect(dialogs.confirm).toHaveBeenCalledTimes(1));
    expect(inspect).not.toHaveBeenCalledWith(MB);
    expect(screen.getByDisplayValue('Alpha edited')).toBeTruthy();
  });

  it('loads the new material clean after the user confirms the discard', async () => {
    localStorage.setItem('nif.mode', 'materials');
    render(<NifPanel onClose={() => {}} />);
    await loadMaterial(MA);
    editField('Alpha', 'Alpha edited');

    fireEvent.change(pathInput(/drag a \.bgsm/), { target: { value: MB } });
    fireEvent.click(document.querySelector('.nif-load-btn')!);
    await screen.findByDisplayValue('Beta');
    expect(dialogs.confirm).toHaveBeenCalledTimes(1);
    expect(screen.queryByDisplayValue('Alpha edited')).toBeNull();
  });

  it('keeps the edit when a mode switch over a dirty editor is cancelled', async () => {
    localStorage.setItem('nif.mode', 'edit');
    dialogs.confirm.mockResolvedValue(false);
    render(<NifPanel onClose={() => {}} />);
    await loadNif(A);
    editField('Alpha', 'Alpha edited');

    fireEvent.click(screen.getByRole('button', { name: 'View' }));
    await waitFor(() => expect(dialogs.confirm).toHaveBeenCalledTimes(1));
    expect(screen.getByDisplayValue('Alpha edited')).toBeTruthy();
    expect(screen.getByText('1 unsaved change')).toBeTruthy();
  });

  it('switches mode and drops the edit after the user confirms the switch', async () => {
    localStorage.setItem('nif.mode', 'edit');
    render(<NifPanel onClose={() => {}} />);
    await loadNif(A);
    editField('Alpha', 'Alpha edited');

    fireEvent.click(screen.getByRole('button', { name: 'Materials' }));
    await screen.findByPlaceholderText(/drag a \.bgsm/);
    expect(dialogs.confirm).toHaveBeenCalledTimes(1);
    expect(screen.queryByDisplayValue('Alpha edited')).toBeNull();
  });

  it('keeps the open tree and its edits when a same-path reload fails', async () => {
    localStorage.setItem('nif.mode', 'edit');
    render(<NifPanel onClose={() => {}} />);
    await loadNif(A);
    editField('Alpha', 'Alpha edited');

    tree.mockRejectedValueOnce(new Error('file is locked'));
    fireEvent.click(document.querySelector('.nif-load-btn')!);
    await screen.findByText('Could not load tree: file is locked');
    expect(dialogs.confirm).not.toHaveBeenCalled();
    expect(screen.getByDisplayValue('Alpha edited')).toBeTruthy();
    expect(screen.getByText('1 unsaved change')).toBeTruthy();
  });
});

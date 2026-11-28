import { act, cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import type { ConfirmRequest } from '../useDialogs';
import BlueprintPanel from './BlueprintPanel';

const dialogs = vi.hoisted(() => ({ confirm: vi.fn<(req: ConfirmRequest) => Promise<boolean>>() }));
vi.mock('../useDialogs', () => ({ useDialogs: () => ({ confirm: dialogs.confirm }) }));
vi.mock('./BlueprintCanvas', () => ({ default: () => null }));

const GRAPH_PATH = 'C:\\graphs\\loaded.bpgraph';
const loadedDoc = {
  schema: 1, id: 'doc-loaded', kind: 'papyrusScript',
  header: { scriptName: 'Loaded', extends: null, flags: [], imports: [], docComment: null, autoState: null },
  variables: [], structs: [], nodes: [], wires: [],
};

const graph = {
  SearchPalette: vi.fn(async () => JSON.stringify({ entries: [], total: 0 })),
  ValidateGraph: vi.fn(async () => JSON.stringify({ diagnostics: [] })),
  BrowseForScript: vi.fn(async () => GRAPH_PATH),
  LoadGraph: vi.fn(async () => JSON.stringify(loadedDoc)),
};

function stubBridge() {
  vi.stubGlobal('chrome', {
    webview: {
      addEventListener: vi.fn(),
      removeEventListener: vi.fn(),
      hostObjects: { graph },
    },
  });
}

async function settle() {
  await act(async () => { await new Promise(resolve => setTimeout(resolve, 0)); });
}

function editScriptName(value: string) {
  fireEvent.change(screen.getByDisplayValue('MyScript'), { target: { value } });
}

function clickOpen() {
  fireEvent.click(screen.getByRole('button', { name: 'Open' }));
}

beforeEach(() => {
  vi.clearAllMocks();
  localStorage.clear();
  dialogs.confirm.mockResolvedValue(true);
  stubBridge();
});

afterEach(() => {
  cleanup();
  vi.unstubAllGlobals();
});

describe('Blueprint LOAD over unsaved changes (GT-088)', () => {
  it('asks before loading a graph over unsaved changes and keeps them when cancelled', async () => {
    dialogs.confirm.mockResolvedValue(false);
    render(<BlueprintPanel onClose={() => {}} />);
    editScriptName('Edited');

    clickOpen();
    await waitFor(() => expect(dialogs.confirm).toHaveBeenCalledTimes(1));
    await settle();
    expect(graph.LoadGraph).not.toHaveBeenCalled();
    expect(screen.getByDisplayValue('Edited')).toBeTruthy();
  });

  it('loads the graph over unsaved changes after the user confirms', async () => {
    render(<BlueprintPanel onClose={() => {}} />);
    editScriptName('Edited');

    clickOpen();
    await waitFor(() => expect(screen.getByDisplayValue('Loaded')).toBeTruthy());
    expect(dialogs.confirm).toHaveBeenCalledTimes(1);
    expect(graph.LoadGraph).toHaveBeenCalledWith(GRAPH_PATH);
  });

  it('loads a clean graph without asking', async () => {
    render(<BlueprintPanel onClose={() => {}} />);

    clickOpen();
    await waitFor(() => expect(screen.getByDisplayValue('Loaded')).toBeTruthy());
    expect(dialogs.confirm).not.toHaveBeenCalled();
  });
});

describe('Blueprint close over unsaved changes (GT-088)', () => {
  it('keeps the graph when the × is cancelled over unsaved changes', async () => {
    dialogs.confirm.mockResolvedValue(false);
    const onClose = vi.fn();
    render(<BlueprintPanel onClose={onClose} />);
    editScriptName('Edited');

    fireEvent.click(document.querySelector('.papyrus-close')!);
    await waitFor(() => expect(dialogs.confirm).toHaveBeenCalledTimes(1));
    await settle();
    expect(onClose).not.toHaveBeenCalled();
    expect(screen.getByDisplayValue('Edited')).toBeTruthy();
  });

  it('closes after the user confirms discarding the graph', async () => {
    const onClose = vi.fn();
    render(<BlueprintPanel onClose={onClose} />);
    editScriptName('Edited');

    fireEvent.click(document.querySelector('.papyrus-close')!);
    await waitFor(() => expect(onClose).toHaveBeenCalledTimes(1));
    expect(dialogs.confirm).toHaveBeenCalledTimes(1);
  });
});

describe('Blueprint background calls report failures (#41)', () => {
  it('shows a failed palette search', async () => {
    graph.SearchPalette.mockRejectedValueOnce(new Error('palette offline'));
    render(<BlueprintPanel onClose={() => {}} />);
    expect(await screen.findByText('Error: palette offline')).toBeTruthy();
  });

  it('shows a failed validation', async () => {
    graph.ValidateGraph.mockRejectedValueOnce(new Error('validator offline'));
    render(<BlueprintPanel onClose={() => {}} />);
    expect(await screen.findByText('Error: validator offline', {}, { timeout: 3000 })).toBeTruthy();
  });
});

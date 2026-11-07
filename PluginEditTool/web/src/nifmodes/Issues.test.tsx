import { act, cleanup, fireEvent, render, screen, waitFor, within } from '@testing-library/react';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import type { NifHost } from '../backend';
import NifPanel from '../NifPanel';
import type { ConfirmRequest } from '../useDialogs';
import Issues, { type VerifyCheck } from './Issues';

const dialogs = vi.hoisted(() => ({ confirm: vi.fn<(req: ConfirmRequest) => Promise<boolean>>() }));
vi.mock('../useDialogs', () => ({ useDialogs: () => ({ confirm: dialogs.confirm }) }));
vi.mock('../NifViewport', () => ({ default: () => null }));
vi.mock('../NifEditor', () => ({ default: () => null }));

const PATH = 'C:\\mods\\Body.nif';
const SAVED = 'C:\\mods\\Body_fixed.nif';

const fix = vi.fn<(path: string, outNif: string, check?: string) => Promise<string>>();
const browseForSave = vi.fn<(title: string, filter: string) => Promise<string>>();
const verify = vi.fn<(path: string) => Promise<string>>();

const passing = (name: string): VerifyCheck => ({ name, failure: '', fixable: false });
const failing = (name: string, failure: string, fixable: boolean): VerifyCheck => ({ name, failure, fixable });

const checks: VerifyCheck[] = [
  passing('header parses'),
  failing('no editor markers', 'block 5 sets the BSX editor-marker bit', true),
  failing('texture paths are relative', "texture 'C:\\a.dds' is a drive-letter path", false),
  passing('UVs match the vertex count'),
];

function renderIssues(list: VerifyCheck[] = checks) {
  const onWritten = vi.fn<(outPath: string) => void>();
  const appendLog = vi.fn<(line: string) => void>();
  const nif = { Fix: fix, BrowseForSave: browseForSave } as unknown as NifHost;
  render(<Issues nif={nif} path={PATH} checks={list} onWritten={onWritten} appendLog={appendLog} />);
  return { onWritten, appendLog };
}

function groupOf(name: string) {
  return screen.getByText(name).closest('section') as HTMLElement;
}

beforeEach(() => {
  vi.clearAllMocks();
  localStorage.clear();
  dialogs.confirm.mockResolvedValue(true);
  fix.mockResolvedValue('RESULT: fix OK (in-process)\nSaved: ' + SAVED);
  browseForSave.mockResolvedValue(SAVED);
});

afterEach(() => {
  cleanup();
  vi.unstubAllGlobals();
});

function stubPanelBridge() {
  vi.stubGlobal('chrome', {
    webview: {
      addEventListener: vi.fn(),
      removeEventListener: vi.fn(),
      hostObjects: {
        nif: {
          Verify: verify, Fix: fix, BrowseForSave: browseForSave, StageDroppedFile: vi.fn(),
          Geo: vi.fn(async () => JSON.stringify({ shapes: [] })), Tree: vi.fn(async () => JSON.stringify({ blocks: [] })),
        },
      },
    },
  });
}

async function runVerifyOn(path: string) {
  render(<NifPanel onClose={() => undefined} />);
  fireEvent.change(screen.getByPlaceholderText(/drag a \.nif here/), { target: { value: path } });
  fireEvent.click(document.querySelector('.papyrus-run')!);
}

describe('Issues', () => {
  // Detects: a passing check listed as a finding, so a clean check would look like a failure
  it('lists only the failing checks, each under its own name', () => {
    renderIssues();
    expect(screen.getByText('2 of 4 checks failed')).toBeDefined();
    expect(screen.queryByText('header parses')).toBeNull();
    expect(screen.getByText('block 5 sets the BSX editor-marker bit')).toBeDefined();
    expect(screen.getByText("texture 'C:\\a.dds' is a drive-letter path")).toBeDefined();
  });

  // Detects: a fix button shown on a check that no repair clears, which would change nothing and leave the finding
  it('offers a fix only on a finding that the fix route repairs', () => {
    renderIssues();
    expect(within(groupOf('no editor markers')).getByRole('button', { name: 'Fix' })).toBeDefined();
    expect(within(groupOf('texture paths are relative')).queryByRole('button', { name: 'Fix' })).toBeNull();
  });

  // Detects: a row repair must pass its selected check and confirm before overwriting.
  it('confirms before a fix overwrites the open file, then writes back to the same path', async () => {
    const { onWritten } = renderIssues();
    fireEvent.click(within(groupOf('no editor markers')).getByRole('button', { name: 'Fix' }));
    await waitFor(() => expect(fix).toHaveBeenCalledWith(PATH, PATH, 'no editor markers'));
    expect(dialogs.confirm).toHaveBeenCalledWith(expect.objectContaining({ danger: true }));
    expect(dialogs.confirm.mock.invocationCallOrder[0]).toBeLessThan(fix.mock.invocationCallOrder[0]);
    expect(onWritten).toHaveBeenCalledWith(PATH);
  });

  // Detects: a declined overwrite still running the fix, so the file changes without consent
  it('writes nothing when the overwrite is declined', async () => {
    dialogs.confirm.mockResolvedValue(false);
    renderIssues();
    fireEvent.click(screen.getByRole('button', { name: /Fix all/ }));
    await waitFor(() => expect(dialogs.confirm).toHaveBeenCalledTimes(1));
    await act(async () => {
      await new Promise(resolve => setTimeout(resolve, 0));
    });
    expect(fix).not.toHaveBeenCalled();
  });

  // Detects: Save As writing the fixed copy back over the source NIF instead of the path the user picked
  it('saves the fixed copy to the chosen path and leaves the source alone', async () => {
    const { onWritten } = renderIssues();
    fireEvent.click(screen.getByRole('button', { name: /Save As/ }));
    await waitFor(() => expect(fix).toHaveBeenCalledWith(PATH, SAVED));
    expect(dialogs.confirm).not.toHaveBeenCalled();
    expect(onWritten).toHaveBeenCalledWith(SAVED);
  });

  // Detects: a cancelled Save As running a fix into an empty output path
  it('does nothing when Save As is cancelled', async () => {
    browseForSave.mockResolvedValue('');
    renderIssues();
    fireEvent.click(screen.getByRole('button', { name: /Save As/ }));
    await waitFor(() => expect(browseForSave).toHaveBeenCalledTimes(1));
    await act(async () => {
      await new Promise(resolve => setTimeout(resolve, 0));
    });
    expect(fix).not.toHaveBeenCalled();
  });

  // Detects: Fix all running the repair more than once for a single click
  it('runs Fix all as one repair of the file', async () => {
    renderIssues();
    fireEvent.click(screen.getByRole('button', { name: /Fix all \(1\)/ }));
    await waitFor(() => expect(fix).toHaveBeenCalledTimes(1));
    await act(async () => {
      await new Promise(resolve => setTimeout(resolve, 0));
    });
    expect(fix).toHaveBeenCalledTimes(1);
  });

  // Detects: a failed repair shown as a finished one, so the file looks fixed when nothing was written
  it('shows a failed repair and does not report the file as written', async () => {
    fix.mockResolvedValue('Error: NIF not found');
    const { onWritten, appendLog } = renderIssues();
    fireEvent.click(screen.getByRole('button', { name: /Fix all/ }));
    expect(await screen.findByText('Error: NIF not found')).toBeDefined();
    expect(onWritten).not.toHaveBeenCalled();
    expect(appendLog).toHaveBeenCalledWith(expect.stringContaining('✗'));
  });

  // Detects: a report whose checks all pass still offering a repair or listing findings
  it('offers no repair and lists nothing when every check passes', () => {
    renderIssues([passing('header parses'), passing('UVs match the vertex count')]);
    expect(screen.getByText('All 2 checks pass')).toBeDefined();
    expect(screen.queryByRole('button', { name: /Fix/ })).toBeNull();
    expect(screen.queryByRole('button', { name: /Save As/ })).toBeNull();
  });
});

describe('NifPanel verify output', () => {
  // Detects: Verify falling back to the plain-text report instead of the issues list built from its checks
  it('shows the verify result as issues with their fix button', async () => {
    localStorage.setItem('nif.mode', 'verify');
    verify.mockResolvedValue(JSON.stringify({
      text: 'RESULT: 2 checks, 1 failed\n[ok] header parses\n[FAIL] no editor markers: block 5 sets the BSX editor-marker bit',
      checks: [passing('header parses'), failing('no editor markers', 'block 5 sets the BSX editor-marker bit', true)],
    }));
    stubPanelBridge();
    await runVerifyOn(PATH);
    expect(await screen.findByText('1 of 2 checks failed')).toBeDefined();
    expect(document.querySelector('.nif-output')).toBeNull();
    expect(within(groupOf('no editor markers')).getByRole('button', { name: 'Fix' })).toBeDefined();
  });

  // Detects: a failed Verify whose error is swallowed by the issues view, so the user sees nothing happen
  it('keeps a failed verify as its error text', async () => {
    localStorage.setItem('nif.mode', 'verify');
    verify.mockResolvedValue('Error: NIF not found: C:\\mods\\Body.nif');
    stubPanelBridge();
    await runVerifyOn(PATH);
    expect(await screen.findByText('Error: NIF not found: C:\\mods\\Body.nif')).toBeDefined();
  });
});

import { cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import PrevisPanel from './PrevisPanel';

const bridge = vi.hoisted(() => ({
  enabled: true,
  scan: vi.fn<(...args: unknown[]) => Promise<unknown>>(),
  folder: vi.fn<(...args: unknown[]) => Promise<unknown>>(),
}));

vi.mock('./backend', () => ({
  getPrevis: () => (bridge.enabled ? { Scan: bridge.scan, GetDataFolder: bridge.folder } : undefined),
}));

const report = {
  dataDir: 'D:/Fallout 4/Data',
  summary: {
    cells: 62393,
    broken: 1,
    outdated: 1,
    skipped: 0,
    cdxFiles: 6,
    cdxRows: 2413992,
    csgFiles: 6,
    csgRows: 64869,
    csgChunks: 13851,
    tomeFiles: 1422,
    ocNifFiles: 176569,
    problems: 0,
  },
  pendingPlugins: [],
  problems: [],
  broken: [{
    cell: '00DC49:Fallout4.esm',
    plugin: 'Fallout4.esm',
    issues: [{ code: 'tome_missing', detail: 'VISI is set but the cluster .uvd is missing' }],
    outdated: [],
    skips: [],
  }],
  outdated: [{
    cell: '003C0F0:Fallout4.esm',
    plugin: 'Fallout4.esm',
    issues: [],
    outdated: [{ code: 'pcmb_stale', detail: 'PCMB stamp is older than a child record version date' }],
    skips: [],
  }],
  skipped: [{
    cell: '0012AB:Fallout4.esm',
    plugin: 'Fallout4.esm',
    issues: [],
    outdated: [],
    skips: ['override with no previs of its own: engine behaviour unverified'],
  }],
  truncated: false,
};

beforeEach(() => {
  vi.resetAllMocks();
  bridge.enabled = true;
  localStorage.clear();
});

afterEach(() => {
  cleanup();
  localStorage.clear();
});

describe('previs report panel', () => {
  it('prefills the Data folder and lists broken and outdated cells after a scan', async () => {
    bridge.folder.mockResolvedValue({ dataDir: 'D:/Fallout 4/Data', source: 'settings' });
    bridge.scan.mockResolvedValue(report);
    render(<PrevisPanel onClose={() => {}} />);
    await waitFor(() => expect(screen.getByDisplayValue('D:/Fallout 4/Data')).toBeTruthy());

    fireEvent.click(screen.getByRole('button', { name: /scan/i }));

    expect(await screen.findByText('00DC49:Fallout4.esm')).toBeTruthy();
    expect(screen.getByText('tome_missing')).toBeTruthy();
    expect(screen.getByText('003C0F0:Fallout4.esm')).toBeTruthy();
    expect(screen.getByText('pcmb_stale')).toBeTruthy();
    expect(bridge.scan).toHaveBeenCalledWith('D:/Fallout 4/Data');
  });

  it('filters the reported cells by findings', async () => {
    bridge.folder.mockResolvedValue({ dataDir: '', source: 'unset' });
    bridge.scan.mockResolvedValue(report);
    render(<PrevisPanel onClose={() => {}} />);
    fireEvent.click(screen.getByRole('button', { name: /scan/i }));
    expect(await screen.findByText('003C0F0:Fallout4.esm')).toBeTruthy();

    fireEvent.change(screen.getByPlaceholderText(/filter cells/i), { target: { value: 'pcmb' } });

    expect(screen.queryByText('00DC49:Fallout4.esm')).toBeNull();
    expect(screen.getByText('003C0F0:Fallout4.esm')).toBeTruthy();
  });

  it('reports a failed scan instead of showing a partial report', async () => {
    bridge.folder.mockResolvedValue({ dataDir: 'D:/missing', source: 'settings' });
    bridge.scan.mockRejectedValue(new Error('not a readable Data folder'));
    render(<PrevisPanel onClose={() => {}} />);
    fireEvent.click(screen.getByRole('button', { name: /scan/i }));
    expect(await screen.findByText(/Scan failed: not a readable Data folder/)).toBeTruthy();
    expect(screen.queryByText(/Broken cells/)).toBeNull();
  });

  it('lists skipped cells and never shows the green state while anything is unmeasured', async () => {
    bridge.folder.mockResolvedValue({ dataDir: '', source: 'unset' });
    bridge.scan.mockResolvedValue({
      ...report,
      summary: { ...report.summary, broken: 0, outdated: 0, skipped: 1 },
      broken: [],
      outdated: [],
    });
    render(<PrevisPanel onClose={() => {}} />);
    fireEvent.click(screen.getByRole('button', { name: /scan/i }));

    expect(await screen.findByText('0012AB:Fallout4.esm')).toBeTruthy();
    expect(screen.getByText(/Skipped cells/)).toBeTruthy();
    expect(document.querySelector('.previs-stat-ok')).toBeNull();
  });

  // Detects: a scan that hides plugins with unsaved edits, instead of naming them as not in the scan, must fail.
  it('names the plugins with unsaved edits that the scan does not read', async () => {
    bridge.folder.mockResolvedValue({ dataDir: '', source: 'unset' });
    bridge.scan.mockResolvedValue({ ...report, pendingPlugins: ['Mod.esp'] });
    render(<PrevisPanel onClose={() => {}} />);
    fireEvent.click(screen.getByRole('button', { name: /scan/i }));

    expect(await screen.findByText(/Unsaved edits in Mod\.esp are not in this scan/)).toBeTruthy();
  });

  it('says so when the desktop bridge is missing', () => {
    bridge.enabled = false;
    render(<PrevisPanel onClose={() => {}} />);
    expect(screen.getByText(/Previs bridge not available/)).toBeTruthy();
  });
});

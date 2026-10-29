import { cleanup, fireEvent, render, screen } from '@testing-library/react';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import type { StringsHost, StringsPage } from './backend';
import LocalizationPanel from './LocalizationPanel';
import type { ConfirmRequest } from './useDialogs';

const confirm = vi.hoisted(() => vi.fn<(req: ConfirmRequest) => Promise<boolean>>());
vi.mock('./useDialogs', () => ({ useDialogs: () => ({ confirm }) }));

const pageJson = (page: Partial<StringsPage> & Pick<StringsPage, 'rows'>) =>
  JSON.stringify({ total: page.rows.length, offset: 0, limit: 200, ...page });

const tablesJson = (skippedArchives: { path: string; reason: string }[] = []) => JSON.stringify({
  tables: [
    { kind: 'STRINGS', path: 'Data/Strings/Mod_en.STRINGS', count: 3, source: 'loose' },
    { kind: 'DLSTRINGS', path: 'Data/Strings/Mod_en.DLSTRINGS', count: 1, source: 'loose' },
  ],
  skippedArchives,
});

const EMPTY_TABLES = JSON.stringify({ tables: [], skippedArchives: [] });

function installHost(): StringsHost {
  const host: StringsHost = {
    ListStringTables: vi.fn(async () => tablesJson()),
    GetStrings: vi.fn(async () => pageJson({
      total: 2,
      rows: [
        { id: 10, text: 'Apple pie' },
        { id: 20, text: 'Banana' },
      ],
    })),
    LookupString: vi.fn(async () => '[]'),
    SetString: vi.fn(async () => '{"id":10,"pending":true}'),
    AddString: vi.fn(async () => '{"id":30,"pending":true}'),
    RemoveString: vi.fn(async () => '{"id":10,"pending":true}'),
    ImportStrings: vi.fn(async () => '{"rows":1,"mode":"merge","pending":true}'),
    ExportStrings: vi.fn(async () => '{"kind":"STRINGS","language":"en","rows":[]}'),
    SaveStringTables: vi.fn(async () => '{"saved":[]}'),
    DiscardStringEdits: vi.fn(async () => '{"discarded":0}'),
  };
  Object.assign(window, {
    chrome: {
      webview: {
        hostObjects: {
          strings: host,
          appInterop: {
            GetPlugins: vi.fn(async () => JSON.stringify([{ Key: 'Mod.esp' }, { Key: 'Other.esp' }])),
          },
        },
      },
    },
  });
  return host;
}

async function pickPlugin(firstRow = 'Apple pie') {
  await screen.findByRole('option', { name: 'Mod.esp' });
  fireEvent.change(screen.getByRole('combobox', { name: 'Plugin' }), { target: { value: 'Mod.esp' } });
  await screen.findByText(firstRow);
}

beforeEach(() => { confirm.mockReset(); confirm.mockResolvedValue(true); });

afterEach(() => {
  cleanup();
  localStorage.clear();
  Reflect.deleteProperty(window, 'chrome');
});

describe('LocalizationPanel', () => {
  it('says the bridge is unavailable outside the desktop host', () => {
    render(<LocalizationPanel onClose={() => {}} />);
    expect(screen.getByText(/Strings bridge not available/)).toBeTruthy();
  });

  it('lists tables for the chosen plugin and shows their rows', async () => {
    const host = installHost();
    render(<LocalizationPanel onClose={() => {}} />);
    await pickPlugin();

    expect(host.ListStringTables).toHaveBeenCalledWith('Mod.esp', 'en');
    expect(host.GetStrings).toHaveBeenCalledWith('Mod.esp', 'en', 'STRINGS', '', 0, 200);
    expect(screen.getByText('Banana')).toBeTruthy();
  });

  it('sends the trimmed search text and resets to the first page', async () => {
    const host = installHost();
    render(<LocalizationPanel onClose={() => {}} />);
    await pickPlugin();

    const input = screen.getByRole('textbox', { name: 'Search text or ID' });
    fireEvent.change(input, { target: { value: '  apple ' } });
    fireEvent.submit(input.closest('form')!);

    await vi.waitFor(() =>
      expect(host.GetStrings).toHaveBeenLastCalledWith('Mod.esp', 'en', 'STRINGS', 'apple', 0, 200));
  });

  it('lists every table that holds a numeric ID', async () => {
    const host = installHost();
    vi.mocked(host.LookupString).mockResolvedValue(JSON.stringify([
      { kind: 'STRINGS', id: 10, text: 'Apple pie' },
      { kind: 'DLSTRINGS', id: 10, text: 'Apple dessert' },
    ]));
    render(<LocalizationPanel onClose={() => {}} />);
    await pickPlugin();

    const input = screen.getByRole('textbox', { name: 'Search text or ID' });
    fireEvent.change(input, { target: { value: '10' } });
    fireEvent.submit(input.closest('form')!);

    expect(await screen.findByText('Apple dessert')).toBeTruthy();
    expect(screen.getByText('DLSTRINGS')).toBeTruthy();
    expect(host.LookupString).toHaveBeenCalledWith('Mod.esp', 'en', 10);
  });

  it('pages forward by the page size', async () => {
    const host = installHost();
    vi.mocked(host.GetStrings).mockImplementation(async (_p, _l, _t, _f, offset) => pageJson({
      total: 450,
      offset,
      rows: [{ id: 10 + offset, text: `row ${offset}` }],
    }));
    render(<LocalizationPanel onClose={() => {}} />);
    await pickPlugin('row 0');

    fireEvent.click(screen.getByText('Next', { exact: false }));
    await vi.waitFor(() =>
      expect(host.GetStrings).toHaveBeenLastCalledWith('Mod.esp', 'en', 'STRINGS', '', 200, 200));
  });

  it('shows read failures through errorText', async () => {
    const host = installHost();
    vi.mocked(host.GetStrings).mockRejectedValue(new Error('boom'));
    render(<LocalizationPanel onClose={() => {}} />);
    await screen.findByRole('option', { name: 'Mod.esp' });
    fireEvent.change(screen.getByRole('combobox', { name: 'Plugin' }), { target: { value: 'Mod.esp' } });

    expect(await screen.findByText('Could not read strings: boom')).toBeTruthy();
  });

  it('does not stay on Loading when the language has no tables', async () => {
    const host = installHost();
    vi.mocked(host.ListStringTables).mockImplementation(async (_p, lang) => lang === 'de' ? EMPTY_TABLES : tablesJson());
    vi.mocked(host.GetStrings).mockImplementation(async (_p, _l, t) =>
      t === 'DLSTRINGS' ? new Promise<string>(() => {}) : pageJson({ total: 2, rows: [{ id: 10, text: 'Apple pie' }] }));
    render(<LocalizationPanel onClose={() => {}} />);
    await pickPlugin();

    fireEvent.change(screen.getByRole('combobox', { name: 'String table' }), { target: { value: 'DLSTRINGS' } });
    fireEvent.change(screen.getByRole('combobox', { name: 'Language' }), { target: { value: 'de' } });
    expect(await screen.findByText('No string tables for this plugin and language.')).toBeTruthy();
    expect(screen.queryByText('Loading…')).toBeNull();
  });

  it('keeps the chosen table and stores the language when the language changes', async () => {
    const host = installHost();
    render(<LocalizationPanel onClose={() => {}} />);
    await pickPlugin();

    fireEvent.change(screen.getByRole('combobox', { name: 'String table' }), { target: { value: 'DLSTRINGS' } });
    fireEvent.change(screen.getByRole('combobox', { name: 'Language' }), { target: { value: 'fr' } });

    await vi.waitFor(() =>
      expect(host.GetStrings).toHaveBeenLastCalledWith('Mod.esp', 'fr', 'DLSTRINGS', '', 0, 200));
    expect(localStorage.getItem('localization.language')).toBe('fr');
  });

  it('disables Prev on the first page and Next on the last, and Prev steps back one page', async () => {
    const host = installHost();
    vi.mocked(host.GetStrings).mockImplementation(async (_p, _l, _t, _f, offset) => pageJson({
      total: 450,
      offset,
      rows: [{ id: 10 + offset, text: `row ${offset}` }],
    }));
    render(<LocalizationPanel onClose={() => {}} />);
    await pickPlugin('row 0');
    const prev = () => screen.getByRole('button', { name: /Prev/ }) as HTMLButtonElement;
    const next = () => screen.getByRole('button', { name: /Next/ }) as HTMLButtonElement;

    await vi.waitFor(() => expect(next().disabled).toBe(false));
    expect(prev().disabled).toBe(true);

    fireEvent.click(next());
    await screen.findByText('row 200');
    await vi.waitFor(() => expect(next().disabled).toBe(false));
    fireEvent.click(next());
    await screen.findByText('row 400');
    await vi.waitFor(() => expect(prev().disabled).toBe(false));
    expect(next().disabled).toBe(true);

    fireEvent.click(prev());
    await screen.findByText('201-201 of 450');
    await vi.waitFor(() =>
      expect(host.GetStrings).toHaveBeenLastCalledWith('Mod.esp', 'en', 'STRINGS', '', 200, 200));
  });

  it('stages an edit and saves it: SetString runs before SaveStringTables', async () => {
    const host = installHost();
    render(<LocalizationPanel onClose={() => {}} />);
    await pickPlugin();

    fireEvent.click(screen.getByRole('button', { name: 'Edit string 10' }));
    fireEvent.change(screen.getByRole('textbox', { name: 'Text for string 10' }), { target: { value: 'Apple tart' } });
    fireEvent.click(screen.getByRole('button', { name: 'Save edit' }));
    await vi.waitFor(() => expect(host.ListStringTables).toHaveBeenCalledTimes(2));
    expect(host.SetString).toHaveBeenCalledWith('Mod.esp', 'en', 'STRINGS', 10, 'Apple tart');

    const save = () => screen.getByRole('button', { name: 'Save string tables' }) as HTMLButtonElement;
    await vi.waitFor(() => expect(save().disabled).toBe(false));
    fireEvent.click(save());
    await vi.waitFor(() => expect(host.SaveStringTables).toHaveBeenCalledWith('Mod.esp', 'en'));
    expect(vi.mocked(host.SetString).mock.invocationCallOrder[0])
      .toBeLessThan(vi.mocked(host.SaveStringTables).mock.invocationCallOrder[0]);
  });

  it('imports the chosen file with the chosen mode', async () => {
    const host = installHost();
    render(<LocalizationPanel onClose={() => {}} />);
    await pickPlugin();

    const json = JSON.stringify({ kind: 'STRINGS', language: 'en', rows: [{ id: 10, text: 'Pear' }] });
    const file = { text: async () => json } as unknown as File;
    fireEvent.change(screen.getByRole('combobox', { name: 'Import mode' }), { target: { value: 'replace' } });
    fireEvent.change(screen.getByLabelText('Import file'), { target: { files: [file] } });

    await vi.waitFor(() =>
      expect(host.ImportStrings).toHaveBeenCalledWith('Mod.esp', 'en', 'STRINGS', json, 'replace'));
  });

  it('asks before removing a string and sends nothing when the user declines', async () => {
    const host = installHost();
    confirm.mockResolvedValue(false);
    render(<LocalizationPanel onClose={() => {}} />);
    await pickPlugin();

    fireEvent.click(screen.getByRole('button', { name: 'Remove string 10' }));
    await vi.waitFor(() => expect(confirm).toHaveBeenCalledTimes(1));
    expect(host.RemoveString).not.toHaveBeenCalled();
  });

  it('removes a string once the user confirms', async () => {
    const host = installHost();
    render(<LocalizationPanel onClose={() => {}} />);
    await pickPlugin();

    fireEvent.click(screen.getByRole('button', { name: 'Remove string 10' }));
    await vi.waitFor(() => expect(host.RemoveString).toHaveBeenCalledWith('Mod.esp', 'en', 'STRINGS', 10));
  });

  it('asks before a replace import and sends nothing when the user declines', async () => {
    const host = installHost();
    confirm.mockResolvedValue(false);
    render(<LocalizationPanel onClose={() => {}} />);
    await pickPlugin();

    const file = { text: async () => '{"rows":[]}' } as unknown as File;
    fireEvent.change(screen.getByRole('combobox', { name: 'Import mode' }), { target: { value: 'replace' } });
    fireEvent.change(screen.getByLabelText('Import file'), { target: { files: [file] } });
    await vi.waitFor(() => expect(confirm).toHaveBeenCalledTimes(1));
    expect(confirm.mock.calls[0][0].confirmLabel).toBe('Replace');
    expect(host.ImportStrings).not.toHaveBeenCalled();
  });

  it('does not ask before a merge import', async () => {
    const host = installHost();
    render(<LocalizationPanel onClose={() => {}} />);
    await pickPlugin();

    const json = '{"rows":[]}';
    const file = { text: async () => json } as unknown as File;
    fireEvent.change(screen.getByRole('combobox', { name: 'Import mode' }), { target: { value: 'merge' } });
    fireEvent.change(screen.getByLabelText('Import file'), { target: { files: [file] } });
    await vi.waitFor(() =>
      expect(host.ImportStrings).toHaveBeenCalledWith('Mod.esp', 'en', 'STRINGS', json, 'merge'));
    expect(confirm).not.toHaveBeenCalled();
  });

  it('shows skipped archives as a warning', async () => {
    const host = installHost();
    vi.mocked(host.ListStringTables).mockResolvedValue(
      tablesJson([{ path: 'Data/Patch - Junk.ba2', reason: 'not a BA2 archive' }]));
    render(<LocalizationPanel onClose={() => {}} />);
    await pickPlugin();

    expect(screen.getByText(/Patch - Junk\.ba2/)).toBeTruthy();
  });
});

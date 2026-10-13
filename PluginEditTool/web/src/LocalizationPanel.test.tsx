import { cleanup, fireEvent, render, screen } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';
import type { StringsHost, StringsPage } from './backend';
import LocalizationPanel from './LocalizationPanel';

const pageJson = (page: Partial<StringsPage> & Pick<StringsPage, 'rows'>) =>
  JSON.stringify({ total: page.rows.length, offset: 0, limit: 200, ...page });

const TABLES_JSON = JSON.stringify([
  { kind: 'STRINGS', path: 'Data/Strings/Mod_en.STRINGS', count: 3 },
  { kind: 'DLSTRINGS', path: 'Data/Strings/Mod_en.DLSTRINGS', count: 1 },
]);

function installHost(): StringsHost {
  const host: StringsHost = {
    ListStringTables: vi.fn(async () => TABLES_JSON),
    GetStrings: vi.fn(async () => pageJson({
      total: 2,
      rows: [
        { id: 10, text: 'Apple pie' },
        { id: 20, text: 'Banana' },
      ],
    })),
    LookupString: vi.fn(async () => '[]'),
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

async function pickPlugin() {
  await screen.findByRole('option', { name: 'Mod.esp' });
  fireEvent.change(screen.getByRole('combobox', { name: 'Plugin' }), { target: { value: 'Mod.esp' } });
  await screen.findByText('Apple pie');
}

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

  it('pages forward by the page size', async () => {
    const host = installHost();
    vi.mocked(host.GetStrings).mockImplementation(async (_p, _l, _t, _f, offset) => pageJson({
      total: 450,
      offset,
      rows: [{ id: 10 + offset, text: `row ${offset}` }],
    }));
    render(<LocalizationPanel onClose={() => {}} />);
    await pickPlugin();

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
    vi.mocked(host.ListStringTables).mockImplementation(async (_p, lang) => lang === 'de' ? '[]' : TABLES_JSON);
    vi.mocked(host.GetStrings).mockImplementation(async (_p, _l, t) =>
      t === 'DLSTRINGS' ? new Promise<string>(() => {}) : pageJson({ total: 2, rows: [{ id: 10, text: 'Apple pie' }] }));
    render(<LocalizationPanel onClose={() => {}} />);
    await pickPlugin();

    fireEvent.change(screen.getByRole('combobox', { name: 'String table' }), { target: { value: 'DLSTRINGS' } });
    fireEvent.change(screen.getByRole('combobox', { name: 'Language' }), { target: { value: 'de' } });
    expect(await screen.findByText('No string tables for this plugin and language.')).toBeTruthy();
    expect(screen.queryByText('Loading…')).toBeNull();
  });
});

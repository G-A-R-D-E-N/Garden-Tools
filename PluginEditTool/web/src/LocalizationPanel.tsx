import { useState, useRef, useEffect, type ChangeEvent } from 'react';
import { Languages, X, Search, ChevronLeft, ChevronRight, AlertTriangle } from 'lucide-react';
import {
  getAppInterop, getStrings,
  type SkippedStringArchive, type StringLookupRow, type StringRow, type StringsHost, type StringsPage,
  type StringTableInfo, type StringTablesResult,
} from './backend';
import { errorText } from './errors';
import { useDialogs } from './useDialogs';
import './PapyrusPanel.css';
import './LocalizationPanel.css';

const LS = (k: string, d: string) => localStorage.getItem('localization.' + k) ?? d;
const setLS = (k: string, v: string) => localStorage.setItem('localization.' + k, v);

const LANGUAGES = ['en', 'de', 'it', 'es', 'esmx', 'fr', 'pl', 'ptbr', 'cn', 'ru', 'ja'];
const PAGE_SIZE = 200;

interface Editing { id: number; index: number; }


export default function LocalizationPanel({ onClose }: { onClose: () => void }) {
  const { confirm: askConfirm } = useDialogs();
  const [plugins, setPlugins] = useState<string[]>([]);
  const [plugin, setPlugin] = useState('');
  const [language, setLanguage] = useState(() => LS('language', 'en'));
  const [tables, setTables] = useState<StringTableInfo[]>([]);
  const [skipped, setSkipped] = useState<SkippedStringArchive[]>([]);
  const [table, setTable] = useState('');
  const [query, setQuery] = useState('');
  const [applied, setApplied] = useState('');
  const [page, setPage] = useState<StringsPage | null>(null);
  const [lookup, setLookup] = useState<StringLookupRow[] | null>(null);
  const [editing, setEditing] = useState<Editing | null>(null);
  const [draft, setDraft] = useState('');
  const [newText, setNewText] = useState('');
  const [importMode, setImportMode] = useState<'merge' | 'replace'>('merge');
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState('');
  const tableSeq = useRef(0);
  const pageSeq = useRef(0);
  const importRef = useRef<HTMLInputElement>(null);

  const strings = getStrings();
  const appInterop = getAppInterop();
  const unavailable = !strings;
  const pending = tables.some(t => t.pending);

  useEffect(() => {
    if (!appInterop) return;
    appInterop.GetPlugins()
      .then(raw => setPlugins((JSON.parse(raw) as { Key: string }[]).map(row => row.Key)))
      .catch(e => { setPlugins([]); setError('Could not list plugins: ' + errorText(e)); });
  }, [appInterop]);

  const readPage = (p: string, lang: string, t: string, filter: string, start: number) => {
    setLookup(null);
    setEditing(null);
    if (!strings || !p || !t) return;
    const mySeq = ++pageSeq.current;
    setBusy(true);
    strings.GetStrings(p, lang, t, filter, start, PAGE_SIZE)
      .then(raw => {
        if (pageSeq.current !== mySeq) return;
        setPage(JSON.parse(raw) as StringsPage);
        setError('');
      })
      .catch(e => {
        if (pageSeq.current !== mySeq) return;
        setPage(null);
        setError('Could not read strings: ' + errorText(e));
      })
      .finally(() => { if (pageSeq.current === mySeq) setBusy(false); });
  };

  const loadTables = (p: string, lang: string, preferred: string, filter: string, start = 0) => {
    const mySeq = ++tableSeq.current;
    ++pageSeq.current;
    setPage(null);
    setLookup(null);
    setTables([]);
    setSkipped([]);
    setTable('');
    setError('');
    setBusy(false);
    if (!strings || !p) return;
    strings.ListStringTables(p, lang)
      .then(raw => {
        if (tableSeq.current !== mySeq) return;
        const result = JSON.parse(raw) as StringTablesResult;
        const chosen = result.tables.find(t => t.kind === preferred)?.kind ?? result.tables[0]?.kind ?? '';
        setTables(result.tables);
        setSkipped(result.skippedArchives);
        setTable(chosen);
        readPage(p, lang, chosen, filter, start);
      })
      .catch(e => {
        if (tableSeq.current !== mySeq) return;
        setError('Could not list string tables: ' + errorText(e));
      });
  };

  const lookupId = (id: number) => {
    if (!strings) return;
    const mySeq = ++pageSeq.current;
    setBusy(true);
    strings.LookupString(plugin, language, id)
      .then(raw => {
        if (pageSeq.current !== mySeq) return;
        setPage(null);
        setLookup(JSON.parse(raw) as StringLookupRow[]);
        setError('');
      })
      .catch(e => {
        if (pageSeq.current !== mySeq) return;
        setLookup(null);
        setError('Could not look up string: ' + errorText(e));
      })
      .finally(() => { if (pageSeq.current === mySeq) setBusy(false); });
  };

  const reload = () => loadTables(plugin, language, table, applied, page?.offset ?? 0);

  const write = (label: string, call: (host: StringsHost) => Promise<unknown>, after: () => void = reload) => {
    if (!strings) return;
    setBusy(true);
    call(strings)
      .then(() => { setError(''); after(); })
      .catch(e => { setBusy(false); setError(`${label}: ${errorText(e)}`); });
  };

  const selectPlugin = (name: string) => {
    setPlugin(name);
    setQuery('');
    setApplied('');
    loadTables(name, language, table, '');
  };

  const selectLanguage = (lang: string) => {
    setLanguage(lang);
    setLS('language', lang);
    loadTables(plugin, lang, table, applied);
  };

  const selectTable = (t: string) => {
    setTable(t);
    readPage(plugin, language, t, applied, 0);
  };

  const submitSearch = () => {
    const filter = query.trim();
    setApplied(filter);
    if (plugin && /^\d+$/.test(filter)) lookupId(Number(filter));
    else readPage(plugin, language, table, filter, 0);
  };

  const goTo = (start: number) => readPage(plugin, language, table, applied, start);

  const startEdit = (index: number, row: StringRow) => {
    setEditing({ id: row.id, index });
    setDraft(row.text);
  };

  const saveEdit = () => {
    if (!editing) return;
    const { id } = editing;
    write('Could not save edit', h => h.SetString(plugin, language, table, id, draft), () => {
      setEditing(null);
      reload();
    });
  };

  const removeRow = async (id: number) => {
    if (!await askConfirm({
      title: 'Remove string?', danger: true, confirmLabel: 'Remove',
      message: `Remove string ${id} from ${table} (${language})? Saving the string tables writes the removal.`,
    })) return;
    write('Could not remove string', h => h.RemoveString(plugin, language, table, id));
  };

  const addRow = () => write('Could not add string', h => h.AddString(plugin, language, table, newText), () => {
    setNewText('');
    reload();
  });

  const importChosen = async (e: ChangeEvent<HTMLInputElement>) => {
    const file = e.target.files?.[0];
    e.target.value = '';
    if (!file) return;
    if (importMode === 'replace' && !await askConfirm({
      title: 'Replace every string?', danger: true, confirmLabel: 'Replace',
      message: `Replace all strings in ${table} (${language}) with the rows in this file? Strings the file does not list are removed.`,
    })) return;
    write('Could not import strings', async h => h.ImportStrings(plugin, language, table, await file.text(), importMode));
  };

  const exportTable = () => {
    if (!strings) return;
    strings.ExportStrings(plugin, language, table)
      .then(raw => {
        const url = URL.createObjectURL(new Blob([raw], { type: 'application/json' }));
        const link = document.createElement('a');
        link.href = url;
        link.download = `${plugin}-${table}-${language}.json`;
        link.click();
        URL.revokeObjectURL(url);
      })
      .catch(e => setError(`Could not export strings: ${errorText(e)}`));
  };

  const rangeText = page && page.total > 0
    ? `${page.offset + 1}-${page.offset + page.rows.length} of ${page.total.toLocaleString()}`
    : '0 of 0';

  return (
    <div className="papyrus-overlay" onClick={onClose}>
      <div className="papyrus-modal glass-panel localization-modal" onClick={e => e.stopPropagation()}>
        <div className="papyrus-header">
          <span className="papyrus-title"><Languages size={16} /> Localization</span>
          <button className="papyrus-close" onClick={onClose} title="Close"><X size={16} /></button>
        </div>

        {unavailable && <div className="papyrus-warn">Strings bridge not available -- run the desktop app (not the browser dev server).</div>}

        <div className="localization-toolbar">
          <select aria-label="Plugin" value={plugin} onChange={e => selectPlugin(e.target.value)} disabled={unavailable}>
            <option value="">Choose a plugin…</option>
            {plugins.map(p => <option key={p} value={p}>{p}</option>)}
          </select>
          <select aria-label="Language" value={language} onChange={e => selectLanguage(e.target.value)} disabled={unavailable}>
            {LANGUAGES.map(l => <option key={l} value={l}>{l}</option>)}
          </select>
          <select aria-label="String table" value={table} onChange={e => selectTable(e.target.value)} disabled={unavailable || tables.length === 0}>
            {tables.length === 0 && <option value="">No tables</option>}
            {tables.map(t => <option key={t.kind} value={t.kind}>{`${t.pending ? '* ' : ''}${t.kind} (${t.count}, ${t.source})`}</option>)}
          </select>
          <form className="localization-search" onSubmit={e => { e.preventDefault(); submitSearch(); }}>
            <input aria-label="Search text or ID" value={query} onChange={e => setQuery(e.target.value)} placeholder="Text or ID"
                   disabled={unavailable || !table} />
            <button type="submit" disabled={unavailable || busy || !table}>
              <Search size={13} /> Search
            </button>
          </form>
        </div>

        <div className="localization-actions">
          <input aria-label="New string text" value={newText} onChange={e => setNewText(e.target.value)} placeholder="New string text"
                 disabled={unavailable || !table} />
          <button onClick={addRow} disabled={busy || unavailable || !table || newText === ''}>Add string</button>
          <button onClick={exportTable} disabled={unavailable || !table}>Export</button>
          <button onClick={() => importRef.current?.click()} disabled={unavailable || !table}>Import</button>
          <select aria-label="Import mode" value={importMode} onChange={e => setImportMode(e.target.value as 'merge' | 'replace')} disabled={unavailable}>
            <option value="merge">Merge</option>
            <option value="replace">Replace</option>
          </select>
          <input ref={importRef} type="file" accept=".json,application/json" aria-label="Import file" hidden onChange={importChosen} />
          <button onClick={() => write('Could not save string tables', h => h.SaveStringTables(plugin, language))}
                  disabled={busy || unavailable || !plugin}>Save string tables</button>
          <button onClick={() => write('Could not discard string edits', h => h.DiscardStringEdits(plugin, language))}
                  disabled={busy || unavailable || !plugin}>Discard edits</button>
          {pending && <span className="localization-pending">Unsaved edits</span>}
        </div>

        <div className="localization-body">
          {skipped.length > 0 && (
            <div className="papyrus-banner warn">
              <AlertTriangle size={15} />
              <span className="papyrus-banner-text">
                {`Skipped unreadable archives: ${skipped.map(a => `${a.path} (${a.reason})`).join('; ')}`}
              </span>
            </div>
          )}
          {error && (
            <div className="papyrus-banner error">
              <AlertTriangle size={15} />
              <span className="papyrus-banner-text">{error}</span>
            </div>
          )}

          {lookup ? (
            lookup.length === 0 ? (
              <div className="nif-view-empty">{`No string table has ID ${applied}.`}</div>
            ) : (
              <table className="localization-table">
                <thead>
                  <tr><th>Table</th><th>ID</th><th>Text</th></tr>
                </thead>
                <tbody>
                  {lookup.map((row, i) => (
                    <tr key={`${row.kind}:${row.id}:${i}`}>
                      <td>{row.kind}{row.pending ? ' (unsaved)' : ''}</td>
                      <td>{row.id}</td>
                      <td className="localization-text">{row.text}</td>
                    </tr>
                  ))}
                </tbody>
              </table>
            )
          ) : !page ? (
            <div className="nif-view-empty">
              {unavailable ? 'Strings bridge unavailable.'
                : !plugin ? 'Pick a plugin to browse its string tables.'
                : busy ? 'Loading…'
                : tables.length === 0 ? 'No string tables for this plugin and language.' : ''}
            </div>
          ) : (
            <>
              <div className="localization-pager">
                <button onClick={() => goTo(Math.max(0, page.offset - PAGE_SIZE))} disabled={busy || page.offset === 0}>
                  <ChevronLeft size={13} /> Prev
                </button>
                <span>{rangeText}</span>
                <button onClick={() => goTo(page.offset + PAGE_SIZE)} disabled={busy || page.offset + PAGE_SIZE >= page.total}>
                  Next <ChevronRight size={13} />
                </button>
              </div>
              <table className="localization-table">
                <thead>
                  <tr><th>ID</th><th>Text</th><th></th></tr>
                </thead>
                <tbody>
                  {page.rows.map((row, i) => editing && editing.index === i ? (
                    <tr key={`${row.id}:${i}`}>
                      <td>{row.id}</td>
                      <td>
                        <textarea aria-label={`Text for string ${row.id}`} value={draft} rows={3}
                                  onChange={e => setDraft(e.target.value)} />
                      </td>
                      <td className="localization-actions-cell">
                        <button onClick={saveEdit} disabled={busy}>Save edit</button>
                        <button onClick={() => setEditing(null)} disabled={busy}>Cancel</button>
                      </td>
                    </tr>
                  ) : (
                    <tr key={`${row.id}:${i}`}>
                      <td>{row.id}</td>
                      <td className="localization-text">{row.text}</td>
                      <td className="localization-actions-cell">
                        <button aria-label={`Edit string ${row.id}`} onClick={() => startEdit(i, row)} disabled={busy || unavailable}>Edit</button>
                        <button aria-label={`Remove string ${row.id}`} onClick={() => removeRow(row.id)} disabled={busy || unavailable}>Remove</button>
                      </td>
                    </tr>
                  ))}
                </tbody>
              </table>
            </>
          )}
        </div>
      </div>
    </div>
  );
}

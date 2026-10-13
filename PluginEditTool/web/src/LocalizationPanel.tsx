import { useState, useRef, useEffect } from 'react';
import { Languages, X, Search, ChevronLeft, ChevronRight, AlertTriangle } from 'lucide-react';
import { getAppInterop, getStrings, type StringTableInfo, type StringsPage } from './backend';
import { errorText } from './errors';
import './PapyrusPanel.css';
import './LocalizationPanel.css';

const LS = (k: string, d: string) => localStorage.getItem('localization.' + k) ?? d;
const setLS = (k: string, v: string) => localStorage.setItem('localization.' + k, v);

const LANGUAGES = ['en', 'de', 'it', 'es', 'esmx', 'fr', 'pl', 'ptbr', 'cn', 'ru', 'ja'];
const PAGE_SIZE = 200;


export default function LocalizationPanel({ onClose }: { onClose: () => void }) {
  const [plugins, setPlugins] = useState<string[]>([]);
  const [plugin, setPlugin] = useState('');
  const [language, setLanguage] = useState(() => LS('language', 'en'));
  const [tables, setTables] = useState<StringTableInfo[]>([]);
  const [table, setTable] = useState('');
  const [query, setQuery] = useState('');
  const [applied, setApplied] = useState('');
  const [page, setPage] = useState<StringsPage | null>(null);
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState('');
  const tableSeq = useRef(0);
  const pageSeq = useRef(0);

  const strings = getStrings();
  const appInterop = getAppInterop();
  const unavailable = !strings;

  useEffect(() => {
    if (!appInterop) return;
    appInterop.GetPlugins()
      .then(raw => setPlugins((JSON.parse(raw) as { Key: string }[]).map(row => row.Key)))
      .catch(e => { setPlugins([]); setError('Could not list plugins: ' + errorText(e)); });
  }, [appInterop]);

  const readPage = (p: string, lang: string, t: string, filter: string, start: number) => {
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

  const loadTables = (p: string, lang: string, preferred: string, filter: string) => {
    const mySeq = ++tableSeq.current;
    ++pageSeq.current;
    setPage(null);
    setTables([]);
    setTable('');
    setError('');
    setBusy(false);
    if (!strings || !p) return;
    strings.ListStringTables(p, lang)
      .then(raw => {
        if (tableSeq.current !== mySeq) return;
        const list = JSON.parse(raw) as StringTableInfo[];
        const chosen = list.find(t => t.kind === preferred)?.kind ?? list[0]?.kind ?? '';
        setTables(list);
        setTable(chosen);
        readPage(p, lang, chosen, filter, 0);
      })
      .catch(e => {
        if (tableSeq.current !== mySeq) return;
        setError('Could not list string tables: ' + errorText(e));
      });
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
    readPage(plugin, language, table, filter, 0);
  };

  const goTo = (start: number) => readPage(plugin, language, table, applied, start);

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
            {tables.map(t => <option key={t.kind} value={t.kind}>{`${t.kind} (${t.count})`}</option>)}
          </select>
          <form className="localization-search" onSubmit={e => { e.preventDefault(); submitSearch(); }}>
            <input aria-label="Search text or ID" value={query} onChange={e => setQuery(e.target.value)} placeholder="Text or ID"
                   disabled={unavailable || !table} />
            <button type="submit" disabled={unavailable || busy || !table}>
              <Search size={13} /> Search
            </button>
          </form>
        </div>

        <div className="localization-body">
          {error && (
            <div className="papyrus-banner error">
              <AlertTriangle size={15} />
              <span className="papyrus-banner-text">{error}</span>
            </div>
          )}

          {!page ? (
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
                  <tr><th>ID</th><th>Text</th></tr>
                </thead>
                <tbody>
                  {page.rows.map((row, i) => (
                    <tr key={`${row.id}:${i}`}>
                      <td>
                        {row.id}
                      </td>
                      <td className="localization-text">{row.text}</td>
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

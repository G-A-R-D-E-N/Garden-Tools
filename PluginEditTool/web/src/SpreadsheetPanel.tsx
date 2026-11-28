import { useState, useEffect, useRef } from 'react';
import { Table2, X, RefreshCw, Save } from 'lucide-react';
import { getBackend, type RecordTypeEntry, type ScalarPresentation } from './backend';
import { ScalarCell } from './ScalarEditor';
import { errorText } from './errors';
import { useDialogs } from './useDialogs';
import './PapyrusPanel.css';
import './SpreadsheetPanel.css';

const LS = (k: string, d: string) => localStorage.getItem('spreadsheet.' + k) ?? d;
const setLS = (k: string, v: string) => localStorage.setItem('spreadsheet.' + k, v);

interface GridRow { formKey: string; editorId: string; cells: string[]; editable?: boolean[]; }
interface GridData {
  columns: string[]; rows: GridRow[]; total: number; offset: number; error?: string;
  presentations?: Record<string, ScalarPresentation>;
}

export default function SpreadsheetPanel({ onClose }: { onClose: () => void }) {
  const backend = getBackend();
  const unavailable = !backend;
  const { confirm: askConfirm } = useDialogs();

  const [plugins, setPlugins] = useState<string[]>([]);
  const [types, setTypes] = useState<RecordTypeEntry[]>([]);
  const [plugin, setPlugin] = useState(() => LS('plugin', ''));
  const [type, setType] = useState(() => LS('type', ''));
  const [grid, setGrid] = useState<GridData | null>(null);
  const [edits, setEdits] = useState<Map<string, string>>(new Map());
  const [busy, setBusy] = useState(false);
  const [saving, setSaving] = useState(false);
  const [error, setError] = useState('');
  const [result, setResult] = useState('');
  const loadSeq = useRef(0);

  useEffect(() => setLS('plugin', plugin), [plugin]);
  useEffect(() => setLS('type', type), [type]);

  useEffect(() => {
    if (!backend) return;
    let cancelled = false;
    void (async () => {
      try {
        // Show every loaded plugin, not only editable ones: reads work for
        // any loaded plugin and the backend rejects protected writes with a
        // clear error. On a transient fetch failure keep the stale list
        // instead of wiping the user's selection.
        const activeRaw = JSON.parse(await backend.GetActivePlugins()) as Array<{ Name?: unknown }>;
        const names = activeRaw
          .map(entry => entry?.Name)
          .filter((name): name is string => typeof name === 'string' && name.length > 0);
        if (cancelled) return;
        setPlugins(names);
      } catch (e) {
        if (!cancelled && plugins.length === 0) {
          setPlugins([]);
        }
        if (!cancelled) setError('Could not list plugins: ' + errorText(e));
      }
    })();
    return () => { cancelled = true; };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [backend]);

  useEffect(() => {
    if (!backend) return;
    if (!plugin.trim()) return;
    let cancelled = false;
    void (async () => {
      try {
        const raw = JSON.parse(await backend.GetPluginRecordTypes(plugin.trim())) as string[];
        const parsed = raw.map(s => {
          const m = /^(.*?)\s*\((\d+)\)\s*$/.exec(s);
          const name = m ? m[1] : s;
          return { Type: name, FriendlyName: name, Count: m ? Number(m[2]) : 0 } as RecordTypeEntry;
        });
        if (!cancelled) setTypes(parsed);
      } catch (e) {
        if (!cancelled) { setTypes([]); setError('Could not list record types for ' + plugin + ': ' + (e instanceof Error ? e.message : String(e))); }
      }
    })();
    return () => { cancelled = true; };
  }, [backend, plugin]);

  const cellKey = (formKey: string, col: string) => `${formKey}|${col}`;

  const confirmDiscard = (message: string) => edits.size === 0 || askConfirm({
    title: 'Discard unsaved edits?', danger: true, confirmLabel: 'Discard', message,
  });

  const requestClose = async () => {
    if (await confirmDiscard('Closing the Spreadsheet discards the unsaved cell edits.')) onClose();
  };

  const selectPlugin = async (name: string) => {
    if (!await confirmDiscard('Switching plugins discards the unsaved cell edits.')) return;
    loadSeq.current++;
    setPlugin(name);
    setType('');
    setTypes([]);
    setGrid(null);
    setEdits(new Map());
    setError('');
    setResult('');
    setBusy(false);
  };

  const load = async (message = '') => {
    if (!backend || !plugin.trim() || !type.trim()) return;
    const mySeq = ++loadSeq.current;
    setBusy(true); setError(''); setResult(''); setEdits(new Map());
    try {
      const raw = await backend.GetRecordsGrid(plugin.trim(), type.trim(), 200, 0);
      if (loadSeq.current !== mySeq) return;
      const parsed = JSON.parse(raw) as GridData;
      if (parsed.error) throw new Error(parsed.error);
      setGrid(parsed);
      setResult(message);
    } catch (e) {
      if (loadSeq.current !== mySeq) return;
      setGrid(null);
      setError('Could not load: ' + (e instanceof Error ? e.message : String(e)));
    } finally {
      if (loadSeq.current === mySeq) setBusy(false);
    }
  };

  const requestLoad = async () => {
    if (await confirmDiscard('Loading the grid discards the unsaved cell edits.')) await load();
  };

  const onEdit = (formKey: string, col: string, value: string) => {
    setEdits(prev => {
      const next = new Map(prev);
      next.set(cellKey(formKey, col), value);
      return next;
    });
  };

  const save = async () => {
    if (!backend || !grid || edits.size === 0) return;
    const mySeq = loadSeq.current;
    setSaving(true); setError(''); setResult('');
    let ok = 0;
    const failures: string[] = [];
    try {
      for (const [key, value] of edits) {
        const sep = key.indexOf('|');
        const formKey = key.slice(0, sep);
        const col = key.slice(sep + 1);
        try {
          await backend.SetField(plugin.trim(), formKey, col, value);
          ok++;
        } catch (e) {
          failures.push(`${formKey}.${col}: ${e instanceof Error ? e.message : String(e)}`);
        }
      }

      let saveMsg = '';
      if (ok > 0) {
        try {
          const r = await backend.SavePlugin(plugin.trim(), '');
          saveMsg = ` Saved to disk: ${r}`;
        } catch (e) {
          saveMsg = ` NOT saved to disk: ${e instanceof Error ? e.message : String(e)}`;
        }
      }
      if (loadSeq.current !== mySeq) return;
      await load(`Applied ${ok} of ${edits.size} edit(s).`
        + (failures.length ? ` ${failures.length} failed: ${failures.slice(0, 5).join('; ')}` : '')
        + saveMsg);
    } finally {
      setSaving(false);
    }
  };

  return (
    <div className="papyrus-overlay" onClick={requestClose}>
      <div className="papyrus-modal glass-panel spreadsheet-modal" onClick={e => e.stopPropagation()}>
        <div className="papyrus-header">
          <span className="papyrus-title"><Table2 size={16} /> Spreadsheet</span>
          <button className="papyrus-close" onClick={requestClose} title="Close"><X size={16} /></button>
        </div>

        {unavailable && <div className="papyrus-warn">Backend bridge not available -- run the desktop app (not the browser dev server).</div>}

        <div className="spreadsheet-toolbar">
          <select className="spreadsheet-select" value={plugin} onChange={e => selectPlugin(e.target.value)} disabled={unavailable}>
            <option value="">Choose a plugin…</option>
            {plugins.map(p => <option key={p} value={p}>{p}</option>)}
          </select>
          <select className="spreadsheet-select" value={type} onChange={e => setType(e.target.value)} disabled={unavailable}>
            <option value="">Choose a record type…</option>
            {(plugin.trim() ? types : []).map(t => <option key={t.Type} value={t.Type}>{t.FriendlyName} ({t.Count})</option>)}
          </select>
          <button className="papyrus-run" onClick={() => void requestLoad()} disabled={busy || unavailable || !plugin.trim() || !type.trim()}>
            <RefreshCw size={13} /> {busy ? 'Loading…' : 'Load'}
          </button>
          <button className="papyrus-run spreadsheet-save-btn" onClick={save} disabled={saving || busy || unavailable || edits.size === 0}>
            <Save size={13} /> {saving ? 'Saving…' : `Save Changes (${edits.size})`}
          </button>
        </div>

        {(error || result) && <div className={error ? 'papyrus-warn' : 'spreadsheet-result'}>{error || result}</div>}

        <div className="spreadsheet-body">
          {!grid ? (
            <div className="nif-view-empty">{unavailable ? 'Backend bridge unavailable.' : 'Pick a plugin and a record type, then Load.'}</div>
          ) : (
            <table className="spreadsheet-table">
              <thead>
                <tr>
                  <th>EditorID</th>
                  {grid.columns.map(c => <th key={c}>{c}</th>)}
                </tr>
              </thead>
              <tbody>
                {grid.rows.map(row => (
                  <tr key={row.formKey}>
                    <td className="spreadsheet-edid" title={row.formKey}>{row.editorId || '(no EditorID)'}</td>
                    {grid.columns.map((col, i) => {
                      const key = cellKey(row.formKey, col);
                      const dirty = edits.has(key);
                      const canEdit = row.editable?.[i] ?? true;
                      const presentation = grid.presentations?.[col];
                      const value = dirty ? edits.get(key)! : row.cells[i];
                      return (
                        <td key={col} className={dirty ? 'spreadsheet-dirty' : ''}>
                          {presentation ? (
                            <ScalarCell presentation={presentation} value={value} disabled={!canEdit}
                              onValue={next => onEdit(row.formKey, col, next)} />
                          ) : (
                            <input
                              className="spreadsheet-cell-input"
                              value={value}
                              disabled={!canEdit}
                              title={canEdit ? undefined : 'Read-only'}
                              onChange={e => onEdit(row.formKey, col, e.target.value)}
                            />
                          )}
                        </td>
                      );
                    })}
                  </tr>
                ))}
              </tbody>
            </table>
          )}
          {grid && grid.total > grid.rows.length && (
            <div className="spreadsheet-truncated">Showing {grid.rows.length} of {grid.total}. Narrow further isn't supported yet -- raise the row cap in a later pass if needed.</div>
          )}
        </div>
      </div>
    </div>
  );
}
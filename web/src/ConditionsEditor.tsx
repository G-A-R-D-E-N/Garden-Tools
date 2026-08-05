import { useEffect, useState } from 'react';
import { X, Plus, Trash2, ArrowUp, ArrowDown } from 'lucide-react';
import type { ConditionDto } from './backend';
import RecordPicker from './RecordPicker';
import './ConditionsEditor.css';

const back = () => window.chrome?.webview?.hostObjects?.backend;

const OPERATORS = ['==', '!=', '>', '>=', '<', '<='];

const BLANK: ConditionDto = { function: 'GetItemCount', operator: '==', value: 1, runOn: 'Subject' };

/** Which field a record picker is currently filling in. */
type PickTarget = { index: number; key: 'param1' | 'param2' | 'reference' | 'compareGlobal' };

/**
 * xEdit's condition editor. SetConditions replaces the whole list, so this reads the current one
 * back with GetConditions, edits it locally, and writes it in one call -- which also means Cancel
 * genuinely cancels, since nothing is sent until Save.
 */
export default function ConditionsEditor(
  { plugin, record, path, label, onClose, onSaved }:
  { plugin: string; record: string; path: string; label: string;
    onClose: () => void; onSaved: (msg: string) => void }
) {
  const [rows, setRows] = useState<ConditionDto[] | null>(null);
  const [functions, setFunctions] = useState<string[]>([]);
  const [runOns, setRunOns] = useState<string[]>([]);
  const [error, setError] = useState('');
  const [saving, setSaving] = useState(false);
  const [pick, setPick] = useState<PickTarget | null>(null);

  useEffect(() => {
    const b = back();
    if (!b) { setError('No backend bridge.'); setRows([]); return; }
    (async () => {
      try {
        const raw = await b.GetConditionsAt(plugin, record, path);
        const parsed = JSON.parse(raw);
        if (Array.isArray(parsed)) setRows(parsed);
        else { setError(String(raw)); setRows([]); }
      } catch (e: any) { setError('Could not read conditions: ' + (e?.message || e)); setRows([]); }
      try { setFunctions(JSON.parse(await b.GetConditionFunctions())); } catch { setFunctions([]); }
      try { setRunOns(JSON.parse(await b.GetConditionRunOnTypes())); } catch { setRunOns(['Subject']); }
    })();
  }, [plugin, record, path]);

  const patch = (i: number, change: Partial<ConditionDto>) =>
    setRows(rs => (rs ?? []).map((r, ri) => (ri === i ? { ...r, ...change } : r)));

  const move = (i: number, delta: number) =>
    setRows(rs => {
      if (!rs) return rs;
      const j = i + delta;
      if (j < 0 || j >= rs.length) return rs;
      const next = [...rs];
      [next[i], next[j]] = [next[j], next[i]];
      return next;
    });

  const save = async () => {
    const b = back();
    if (!b || !rows) return;
    setSaving(true);
    try {
      const msg = await b.SetConditionsAt(plugin, record, path, JSON.stringify(rows));
      onSaved(msg);
      onClose();
    } catch (e: any) {
      setError('Save failed: ' + (e?.message || e));
      setSaving(false);
    }
  };

  return (
    <div className="ce-overlay" onClick={onClose}>
      <div className="ce-modal" onClick={e => e.stopPropagation()}>
        <div className="ce-header">
          <span>Conditions -- {label} in {plugin}{path === 'Conditions' ? '' : ` (${path})`}</span>
          <button onClick={onClose} title="Close"><X size={16} /></button>
        </div>

        {error && <div className="ce-error">{error}</div>}

        <div className="ce-rows">
          {rows === null ? (
            <div className="ce-note">Reading…</div>
          ) : rows.length === 0 ? (
            <div className="ce-note">No conditions. Add one below -- an empty list means the record always applies.</div>
          ) : rows.map((r, i) => (
            <div className="ce-row" key={i}>
              <div className="ce-row-head">
                <span className="ce-index">{i + 1}</span>
                <select value={r.function} onChange={e => patch(i, { function: e.target.value })}>
                  {(functions.includes(r.function) ? functions : [r.function, ...functions]).map(f => (
                    <option key={f} value={f}>{f}</option>
                  ))}
                </select>
                <select className="ce-op" value={r.operator} onChange={e => patch(i, { operator: e.target.value })}>
                  {OPERATORS.map(o => <option key={o} value={o}>{o}</option>)}
                </select>
                {r.compareGlobal !== undefined ? (
                  <button className="ce-link" onClick={() => setPick({ index: i, key: 'compareGlobal' })}>
                    {r.compareGlobal || 'pick global…'}
                  </button>
                ) : (
                  <input
                    className="ce-value"
                    type="number"
                    step="any"
                    value={r.value ?? 0}
                    onChange={e => patch(i, { value: Number(e.target.value) })}
                  />
                )}
                <button
                  className="ce-toggle"
                  title={r.compareGlobal !== undefined ? 'Compare against a constant instead' : 'Compare against a global variable instead'}
                  onClick={() => patch(i, r.compareGlobal !== undefined
                    ? { compareGlobal: undefined, value: r.value ?? 1 }
                    : { compareGlobal: '', value: undefined })}
                >
                  {r.compareGlobal !== undefined ? 'vs global' : 'vs value'}
                </button>
                <span className="ce-spacer" />
                <button title="Move up" onClick={() => move(i, -1)}><ArrowUp size={13} /></button>
                <button title="Move down" onClick={() => move(i, 1)}><ArrowDown size={13} /></button>
                <button className="ce-danger" title="Remove"
                  onClick={() => setRows(rs => (rs ?? []).filter((_, ri) => ri !== i))}>
                  <Trash2 size={13} />
                </button>
              </div>
              <div className="ce-row-args">
                <label>Param 1</label>
                <input value={String(r.param1 ?? '')} placeholder="FormKey, EditorID or number"
                  onChange={e => patch(i, { param1: e.target.value })} />
                <button className="ce-link" onClick={() => setPick({ index: i, key: 'param1' })}>pick…</button>

                <label>Param 2</label>
                <input value={String(r.param2 ?? '')} placeholder="FormKey, EditorID or number"
                  onChange={e => patch(i, { param2: e.target.value })} />
                <button className="ce-link" onClick={() => setPick({ index: i, key: 'param2' })}>pick…</button>

                <label>Run on</label>
                <select value={r.runOn ?? 'Subject'} onChange={e => patch(i, { runOn: e.target.value })}>
                  {(runOns.length ? runOns : ['Subject']).map(o => <option key={o} value={o}>{o}</option>)}
                </select>

                {r.runOn === 'Reference' && (
                  <>
                    <label>Reference</label>
                    <input value={r.reference ?? ''} placeholder="e.g. PlayerRef"
                      onChange={e => patch(i, { reference: e.target.value })} />
                    <button className="ce-link" onClick={() => setPick({ index: i, key: 'reference' })}>pick…</button>
                  </>
                )}
              </div>
            </div>
          ))}
        </div>

        <div className="ce-footer">
          <button onClick={() => setRows(rs => [...(rs ?? []), { ...BLANK }])}>
            <Plus size={13} /> Add condition
          </button>
          <span className="ce-spacer" />
          <button onClick={onClose}>Cancel</button>
          <button className="ce-primary" disabled={rows === null || saving} onClick={save}>
            {saving ? 'Saving…' : 'Save conditions'}
          </button>
        </div>
      </div>

      {pick && (
        <RecordPicker
          title={pick.key}
          refType={null}
          refTypes={null}
          onPick={fk => { patch(pick.index, { [pick.key]: fk } as Partial<ConditionDto>); setPick(null); }}
          onClose={() => setPick(null)}
        />
      )}
    </div>
  );
}

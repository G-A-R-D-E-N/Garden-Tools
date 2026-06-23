import { useEffect, useMemo, useState } from 'react';
import {
  Folder, ChevronRight, ChevronDown, Filter, EyeOff, Pencil, Copy, Save, Trash2,
  Link2, AlertTriangle, Hash, Minimize2, Eraser,
} from 'lucide-react';
import type { ConflictMatrix, ConflictFieldRow, RefByEntry, RecordProblem } from './backend';
import RecordPicker from './RecordPicker';
import './RecordView.css';

const back = () => window.chrome?.webview?.hostObjects?.backend;

// An array element ("[0]") or a summarized condition/component collapses its sub-fields by default;
// named container fields (Conditions, Components, Model) stay expanded so their entries show.
const defaultExpanded = (r: ConflictFieldRow) => !(r.IsSummary || r.DisplayLabel.startsWith('['));

// xEdit-style per-cell conflict status -> drives the TEXT color (background stays flat). The common
// "identical to master" case is dimmed so the eye is drawn to the few real conflicts.
type Status = 'none' | 'identical' | 'master' | 'override' | 'win' | 'lose';
function cellStatus(vals: string[], idx: number): Status {
  const me = vals[idx] ?? '';
  if (me === '') return 'none';
  const present = vals.filter(v => v !== '');
  let winner = -1;
  for (let i = vals.length - 1; i >= 0; i--) { if (vals[i] !== '') { winner = i; break; } }
  if (present.length <= 1) return 'override';            // only this plugin defines it
  const allSame = present.every(v => v === present[0]);
  if (allSame) return idx === 0 ? 'master' : 'identical';
  if (idx === winner) return 'win';                      // the value the game uses
  if (idx === 0) return 'master';
  if (me === vals[winner]) return 'override';            // matches the winner, just not last
  return 'lose';                                         // overridden / hidden
}

interface Menu { x: number; y: number; row: ConflictFieldRow; col: number; }
interface Edit { field: string; col: number; }

export default function RecordView(
  { matrix, plugin, onReload }: { matrix: ConflictMatrix; plugin: string; onReload: () => Promise<void> }
) {
  const [filter, setFilter] = useState('');
  const [onlyDiffing, setOnlyDiffing] = useState(false);
  // Conflict-status filter: 'all' or one of the per-cell statuses. When set, keep only rows that
  // have at least one cell of that status across the plugin columns.
  const [statusFilter, setStatusFilter] = useState<'all' | 'win' | 'lose' | 'override' | 'identical'>('all');
  const [ov, setOv] = useState<Record<string, boolean>>({});
  const [menu, setMenu] = useState<Menu | null>(null);
  const [edit, setEdit] = useState<Edit | null>(null);
  const [editVal, setEditVal] = useState('');
  const [picker, setPicker] = useState<{ row: ConflictFieldRow; col: number } | null>(null);
  const [status, setStatus] = useState('');
  const [editablePlugins, setEditablePlugins] = useState<string[]>([]);

  // Bottom drawer
  const [drawerOpen, setDrawerOpen] = useState(true);
  const [drawerTab, setDrawerTab] = useState<'referenced' | 'problems'>('problems');
  const [refBy, setRefBy] = useState<RefByEntry[] | null>(null);
  const [problems, setProblems] = useState<RecordProblem[] | null>(null);

  useEffect(() => {
    const close = () => setMenu(null);
    window.addEventListener('click', close);
    window.addEventListener('scroll', close, true);
    return () => { window.removeEventListener('click', close); window.removeEventListener('scroll', close, true); };
  }, []);

  // Load Referenced By + Problems when the record changes.
  useEffect(() => {
    const b = back();
    if (!b) return;
    setRefBy(null); setProblems(null);
    (async () => {
      try { setProblems(JSON.parse(await b.GetProblems(matrix.FormKey))); } catch { setProblems([]); }
      try { setRefBy(JSON.parse(await b.GetReferencedBy(matrix.FormKey))); } catch { setRefBy([]); }
    })();
  }, [matrix.FormKey]);

  const winnerIdx = useMemo(() => {
    const i = matrix.Plugins.lastIndexOf(matrix.Winner);
    return i >= 0 ? i : matrix.Plugins.length - 1;
  }, [matrix]);

  const isCollapsed = (r: ConflictFieldRow) => !(r.Field in ov ? ov[r.Field] : defaultExpanded(r));
  const toggle = (r: ConflictFieldRow) =>
    setOv(prev => ({ ...prev, [r.Field]: r.Field in prev ? !prev[r.Field] : !defaultExpanded(r) }));

  const f = filter.trim().toLowerCase();
  const filtering = f.length > 0;

  const matched = useMemo(() => matrix.Rows.filter(r => {
    if (onlyDiffing && !r.Differs) return false;
    if (f && !(r.DisplayLabel || r.Field).toLowerCase().includes(f)) return false;
    if (statusFilter !== 'all') {
      const hit = r.Values.some((_, ci) => cellStatus(r.Values, ci) === statusFilter);
      if (!hit) return false;
    }
    return true;
  }), [matrix, f, onlyDiffing, statusFilter]);

  const rows = useMemo(() => {
    if (filtering) return matched;
    const out: ConflictFieldRow[] = [];
    const collapsedLevels: number[] = [];
    for (const r of matched) {
      while (collapsedLevels.length && collapsedLevels[collapsedLevels.length - 1] >= r.Level) collapsedLevels.pop();
      if (collapsedLevels.length > 0) continue;
      out.push(r);
      if (r.HasChildren && isCollapsed(r)) collapsedLevels.push(r.Level);
    }
    return out;
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [matched, ov, filtering]);

  // ---- editing ----
  const startEdit = (r: ConflictFieldRow, col: number) => {
    setMenu(null);
    if (r.HasChildren) return;       // containers aren't editable
    if (r.EditKind === 'Ref') { setPicker({ row: r, col }); return; }   // FormLink -> record picker
    setEdit({ field: r.Field, col });
    setEditVal(r.Values[col] ?? '');
  };

  const commitEdit = async (r: ConflictFieldRow, col: number, value = editVal) => {
    setEdit(null);
    const b = back();
    if (!b) return;
    if (value === (r.Values[col] ?? '')) return;   // no change
    setStatus('Saving…');
    try {
      setStatus(await b.SetField(matrix.Plugins[col], matrix.FormKey, r.Field, value));
      await onReload();
    } catch (e: any) { setStatus('Edit failed: ' + (e?.message || e)); }
  };

  // Fetch the open editable plugins when the context menu opens, to offer as copy targets (no prompt).
  useEffect(() => {
    if (!menu) return;
    const b = back();
    if (!b) return;
    (async () => { try { setEditablePlugins(JSON.parse(await b.GetEditablePlugins())); } catch { setEditablePlugins([]); } })();
  }, [menu]);

  const copyInto = async (col: number, target: string) => {
    setMenu(null);
    const b = back();
    if (!b) return;
    setStatus('Copying…');
    try { setStatus(await b.CopyAsOverride(matrix.Plugins[col], matrix.FormKey, target)); await onReload(); }
    catch (e: any) { setStatus('Copy failed: ' + (e?.message || e)); }
  };

  const savePlugin = async (col: number) => {
    setMenu(null);
    const b = back();
    if (!b) return;
    setStatus('Saving plugin…');
    try { setStatus(await b.SavePlugin(matrix.Plugins[col], '')); }
    catch (e: any) { setStatus('Save failed: ' + (e?.message || e)); }
  };

  const deleteRecord = async (col: number) => {
    setMenu(null);
    const b = back();
    if (!b) return;
    const plug = matrix.Plugins[col];
    if (!window.confirm(`Delete ${matrix.EditorID || matrix.FormKey} from ${plug}?`)) return;
    setStatus('Deleting…');
    try { setStatus(await b.DeleteRecord(plug, matrix.FormKey)); await onReload(); }
    catch (e: any) { setStatus('Delete failed: ' + (e?.message || e)); }
  };

  const renumberFormId = async (col: number) => {
    setMenu(null);
    const b = back();
    if (!b) return;
    const plug = matrix.Plugins[col];
    const cur = matrix.FormKey.split(':')[0];
    const next = window.prompt(`New FormID for ${matrix.EditorID || matrix.FormKey} in ${plug} (6-digit hex):`, cur);
    if (!next) return;
    setStatus('Renumbering…');
    try { setStatus(await b.RenumberFormId(plug, matrix.FormKey, next)); await onReload(); }
    catch (e: any) { setStatus('Renumber failed: ' + (e?.message || e)); }
  };

  const compactEsl = async (col: number) => {
    setMenu(null);
    const b = back();
    if (!b) return;
    setStatus('Compacting…');
    try { setStatus(await b.CompactToEsl(matrix.Plugins[col])); await onReload(); }
    catch (e: any) { setStatus('Compact failed: ' + (e?.message || e)); }
  };

  const cleanPlugin = async (col: number) => {
    setMenu(null);
    const b = back();
    if (!b) return;
    setStatus('Cleaning…');
    try { setStatus(await b.CleanPlugin(matrix.Plugins[col])); await onReload(); }
    catch (e: any) { setStatus('Clean failed: ' + (e?.message || e)); }
  };

  const renderEditor = (r: ConflictFieldRow, ci: number) => {
    if (r.EditKind === 'Bool') {
      return (
        <select className="rv-edit-input" autoFocus value={editVal}
          onChange={e => commitEdit(r, ci, e.target.value)} onBlur={() => setEdit(null)}>
          <option value="True">True</option>
          <option value="False">False</option>
        </select>
      );
    }
    if (r.EditKind === 'Enum' && r.EnumOptions && r.EnumOptions.length > 0) {
      return (
        <select className="rv-edit-input" autoFocus value={editVal}
          onChange={e => commitEdit(r, ci, e.target.value)} onBlur={() => setEdit(null)}>
          {r.EnumOptions.map(o => <option key={o} value={o}>{o}</option>)}
        </select>
      );
    }
    return (
      <input className="rv-edit-input" autoFocus value={editVal}
        onChange={e => setEditVal(e.target.value)}
        onBlur={() => commitEdit(r, ci)}
        onKeyDown={e => {
          if (e.key === 'Enter') { e.preventDefault(); commitEdit(r, ci); }
          if (e.key === 'Escape') { e.preventDefault(); setEdit(null); }
        }} />
    );
  };

  const problemCount = problems?.length ?? 0;

  return (
    <div className="rv-container">
      {/* Toolbar */}
      <div className="rv-toolbar">
        <div className="rv-filter">
          <Filter size={12} />
          <input value={filter} onChange={e => setFilter(e.target.value)} placeholder="Filter fields…" />
        </div>
        <button className={`rv-toggle-btn ${onlyDiffing ? 'on' : ''}`} onClick={() => setOnlyDiffing(v => !v)}>
          <EyeOff size={12} /> Only conflicts
        </button>
        <select
          className="rv-status-filter"
          value={statusFilter}
          onChange={e => setStatusFilter(e.target.value as typeof statusFilter)}
          title="Show only rows that have a cell of this conflict status"
        >
          <option value="all">All statuses</option>
          <option value="win">Winning</option>
          <option value="lose">Overridden (losing)</option>
          <option value="override">Override (sole/last)</option>
          <option value="identical">Identical to master</option>
        </select>
        {status && <span className="rv-status">{status}</span>}
      </div>

      {/* Breadcrumb */}
      <div className="rv-breadcrumb">
        <Folder size={13} />
        <span>{plugin}</span>
        <ChevronRight size={11} />
        <span className="rv-bc-cat">{matrix.Type}</span>
        <ChevronRight size={11} />
        <span className="rv-bc-rec">{matrix.EditorID || matrix.FormKey}</span>
      </div>

      {/* Comparison grid */}
      <div className="rv-grid-scroll">
        <table className="rv-grid" style={{ minWidth: 320 + matrix.Plugins.length * 240 }}>
          <thead>
            <tr>
              <th className="rv-prop-col rv-sticky">Record Property</th>
              {matrix.Plugins.map((p, i) => (
                <th key={p + i} className={i === winnerIdx ? 'rv-winner-col' : ''}>
                  <span className="rv-col-name">{p}</span>
                  {i === winnerIdx && matrix.Plugins.length > 1 && <span className="rv-winner-badge">WINNER</span>}
                </th>
              ))}
            </tr>
          </thead>
          <tbody>
            {rows.length === 0 ? (
              <tr><td className="rv-empty" colSpan={matrix.Plugins.length + 1}>No fields match.</td></tr>
            ) : rows.map((r, ri) => (
              <tr key={ri} className={r.Differs ? 'rv-diff' : ''}>
                <td
                  className={`rv-prop-col rv-sticky ${r.HasChildren ? 'rv-expandable' : ''}`}
                  onDoubleClick={() => r.HasChildren && toggle(r)}
                >
                  <span className="rv-prop-inner" style={{ paddingLeft: r.Level * 14 }}>
                    {r.HasChildren ? (
                      <span className="rv-expand" onClick={e => { e.stopPropagation(); toggle(r); }}>
                        {isCollapsed(r) ? <ChevronRight size={12} /> : <ChevronDown size={12} />}
                      </span>
                    ) : <span className="rv-expand-spacer" />}
                    {r.DisplayLabel || r.Field}
                  </span>
                </td>
                {matrix.Plugins.map((_, ci) => {
                  const editing = edit && edit.field === r.Field && edit.col === ci;
                  const st = cellStatus(r.Values, ci);
                  return (
                    <td
                      key={ci}
                      className={`rv-cell rv-c-${st}`}
                      onDoubleClick={() => !r.HasChildren && startEdit(r, ci)}
                      onContextMenu={e => { if (!r.HasChildren) { e.preventDefault(); e.stopPropagation(); setMenu({ x: e.clientX, y: e.clientY, row: r, col: ci }); } }}
                    >
                      {editing ? renderEditor(r, ci) : r.Values[ci]}
                    </td>
                  );
                })}
              </tr>
            ))}
          </tbody>
        </table>
      </div>

      {/* Bottom drawer: Referenced By / Problems */}
      <div className={`rv-drawer ${drawerOpen ? 'open' : 'closed'}`}>
        <div className="rv-drawer-tabs">
          <button className={drawerTab === 'referenced' ? 'on' : ''} onClick={() => { setDrawerTab('referenced'); setDrawerOpen(true); }}>
            <Link2 size={12} /> Referenced By {refBy === null ? '…' : `(${refBy.length})`}
          </button>
          <button className={drawerTab === 'problems' ? 'on' : ''} onClick={() => { setDrawerTab('problems'); setDrawerOpen(true); }}>
            <AlertTriangle size={12} /> Problems
            {problemCount > 0 && <span className="rv-prob-badge">{problemCount}</span>}
          </button>
          <button className="rv-drawer-collapse" onClick={() => setDrawerOpen(o => !o)} title={drawerOpen ? 'Collapse' : 'Expand'}>
            {drawerOpen ? <ChevronDown size={14} /> : <ChevronRight size={14} />}
          </button>
        </div>
        {drawerOpen && (
          <div className="rv-drawer-body">
            {drawerTab === 'referenced' ? (
              refBy === null ? <div className="rv-drawer-note">Scanning load order…</div>
              : refBy.length === 0 ? <div className="rv-drawer-note">Nothing references this record.</div>
              : refBy.map((e, i) => (
                <div key={i} className="rv-ref-row">
                  <span className="rv-ref-type">{e.Type}</span>
                  <span className="rv-ref-id">{e.EditorID || e.FormKey}</span>
                  <span className="rv-ref-plugin">{e.Plugin}</span>
                </div>
              ))
            ) : (
              problems === null ? <div className="rv-drawer-note">Checking…</div>
              : problems.length === 0 ? <div className="rv-drawer-note">No problems found.</div>
              : problems.map((p, i) => (
                <div key={i} className="rv-prob-row">
                  <AlertTriangle size={12} className="rv-prob-icon" />
                  <span>{p.Description}</span>
                </div>
              ))
            )}
          </div>
        )}
      </div>

      {/* Right-click context menu */}
      {menu && (
        <div className="rv-context" style={{ left: menu.x, top: menu.y }} onClick={e => e.stopPropagation()}>
          <button onClick={() => startEdit(menu.row, menu.col)}><Pencil size={13} /> Edit value <span className="rv-key">F2</span></button>
          <div className="rv-context-sep" />
          <div className="rv-context-label">Copy as Override into</div>
          {(editablePlugins.includes('ConflictPatch.esp') ? editablePlugins : [...editablePlugins, 'ConflictPatch.esp']).map(p => (
            <button key={p} onClick={() => copyInto(menu.col, p)}><Copy size={13} /> {p}</button>
          ))}
          <div className="rv-context-sep" />
          <button onClick={() => savePlugin(menu.col)}><Save size={13} /> Save “{matrix.Plugins[menu.col]}”</button>
          <button className="rv-danger" onClick={() => deleteRecord(menu.col)}><Trash2 size={13} /> Delete from “{matrix.Plugins[menu.col]}”</button>
          <div className="rv-context-sep" />
          <div className="rv-context-label">Plugin: {matrix.Plugins[menu.col]}</div>
          <button onClick={() => renumberFormId(menu.col)}><Hash size={13} /> Change FormID…</button>
          <button onClick={() => compactEsl(menu.col)}><Minimize2 size={13} /> Compact to ESL</button>
          <button onClick={() => cleanPlugin(menu.col)}><Eraser size={13} /> Clean (UDR)</button>
        </div>
      )}

      {/* FormLink record picker */}
      {picker && (
        <RecordPicker
          title={picker.row.DisplayLabel}
          refType={picker.row.RefType}
          refTypes={picker.row.RefTypes}
          onPick={fk => { const p = picker; setPicker(null); commitEdit(p.row, p.col, fk); }}
          onClose={() => setPicker(null)}
        />
      )}
    </div>
  );
}

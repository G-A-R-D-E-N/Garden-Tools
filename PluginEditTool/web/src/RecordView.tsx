import { Fragment, useEffect, useMemo, useRef, useState } from 'react';
import {
  ChevronRight, ChevronDown, EyeOff, Link2, AlertTriangle, Crown, Clock, Network, HelpCircle,
} from 'lucide-react';
import { TYPED_RECORD_TYPES, type ConflictMatrix, type ConflictFieldRow, type RefByEntry, type RecordProblem, type Dependency, type HistoryEntry, type ElementActions, type BulkTarget } from './backend';
import { ScalarEditor, ScalarText } from './ScalarEditor';
import { rawEditable } from './scalarPresentation';
import RecordPicker from './RecordPicker';
import TypedRecordPanel from './TypedRecordPanel';
import ProblemsList from './ProblemsList';
import ConditionsEditor from './ConditionsEditor';
import { useDialogs } from './useDialogs';
import WorkspaceHeader from './Workspace/WorkspaceHeader';
import { buildActions } from './Workspace/actions';
import ConflictsView, { type ConflictLayout } from './Workspace/ConflictsView';
import { back, f2Edit, useRecordActions } from './recordActions';
import RecordContextMenu from './RecordContextMenu';
import {
  defaultExpanded, resolveStatus, type SeverityFilter, SEVERITY_SETS, LEVEL_BADGES, LEGEND_ROWS, LEGEND_CELLS,
  SEV_ORDER,
} from './recordStatus';
import './RecordView.css';
import { errorText } from './errors';

export type RecordTab = 'grid' | 'fields' | 'conflicts' | 'references' | 'history' | 'dependencies' | 'typed';

interface Menu { x: number; y: number; row: ConflictFieldRow; col: number; }
interface Edit { field: string; col: number; }

function TabBtn({ id, current, set, children, count }: {
  id: RecordTab; current: RecordTab; set: (t: RecordTab) => void;
  children: React.ReactNode; count?: number;
}) {
  return (
    <button className={`rv-tab ${current === id ? 'active' : ''}`} onClick={() => set(id)}>
      {children}
      {count !== undefined && count > 0 && <span className="rv-tab-count">{count}</span>}
    </button>
  );
}

export default function RecordView(
  { matrix, plugin, onReload, onOpenRecord, activeTab: externalTab, onTabChange, highlightField,
    highlightSeq, onPluginsChanged, selectedRecords, setErrorStr }: {
    matrix: ConflictMatrix; plugin: string;
    onReload: () => Promise<void>;
    onOpenRecord?: (formKey: string, plugin: string) => void;
    activeTab?: RecordTab;
    onTabChange?: (t: RecordTab) => void;
    highlightField?: string;
    highlightSeq?: number;
    onPluginsChanged?: () => void;
    selectedRecords?: BulkTarget[]; setErrorStr?: (message: string) => void;
  }
) {
  const [internalTab, setInternalTab] = useState<RecordTab>('grid');
  const recordTab = externalTab ?? internalTab;
  const setRecordTab = (t: RecordTab) => { setInternalTab(t); onTabChange?.(t); };

  const [filter, setFilter] = useState('');
  const [valueFilter, setValueFilter] = useState('');
  const [legendOpen, setLegendOpen] = useState(false);
  const [onlyDiffing, setOnlyDiffing] = useState(false);
  const [rawView, setRawView] = useState(false);
  const [severityFilter, setSeverityFilter] = useState<SeverityFilter>('all');
  const [ov, setOv] = useState<Record<string, boolean>>({});
  const [menu, setMenu] = useState<Menu | null>(null);
  const openMenu = (m: Menu) => { setMenu(m); setElementActions(null); };
  const [edit, setEdit] = useState<Edit | null>(null);
  const [editVal, setEditVal] = useState('');
  const [picker, setPicker] = useState<{ row: ConflictFieldRow; col: number } | null>(null);

  const { pickPlugin: askForTarget } = useDialogs();
  const [conditions, setConditions] = useState<{ plugin: string; path: string; label: string } | null>(null);
  const [status, setStatus] = useState('');
  const [editablePlugins, setEditablePlugins] = useState<string[]>([]);
  const [elementActions, setElementActions] = useState<ElementActions | null>(null);

  const [collapsedGroups, setCollapsedGroups] = useState<Record<number, boolean>>({});

  type ColumnWidthMode = 'standard' | 'fitAll' | 'fitText' | 'fitSmart';
  const [columnWidthMode, setColumnWidthMode] = useState<ColumnWidthMode>(
    () => (localStorage.getItem('rvColumnWidthMode') as ColumnWidthMode) || 'fitAll');
  const changeColumnWidthMode = (m: ColumnWidthMode) => {
    setColumnWidthMode(m);
    localStorage.setItem('rvColumnWidthMode', m);
  };

  const flashing = (r: ConflictFieldRow) => !!highlightField && r.Field === highlightField;
  const [flashKey, setFlashKey] = useState({ field: highlightField, seq: highlightSeq });
  if (highlightField && highlightSeq !== flashKey.seq) setFlashKey({ field: highlightField, seq: highlightSeq });
  const rowKey = (r: ConflictFieldRow, ri: number) => r.Field === flashKey.field ? `${ri}:${flashKey.seq}` : ri;
  const [prevFormKey, setPrevFormKey] = useState(matrix.FormKey);

  const [drawerOpen, setDrawerOpen] = useState(true);
  const [refBy, setRefBy] = useState<RefByEntry[] | null>(null);
  const [problems, setProblems] = useState<RecordProblem[] | null>(null);
  const [deps, setDeps] = useState<Dependency[] | null>(null);
  const [history, setHistory] = useState<HistoryEntry[] | null>(null);

  const [scanErrors, setScanErrors] = useState<Record<string, string>>({});
  const loadSeq = useRef(0);
  const menuSeq = useRef(0);

  const [anchors, setAnchors] = useState<[string, string] | null>(null);
  const [compareOnly, setCompareOnly] = useState(false);

  const visibleCols = useMemo(() => {
    const all = matrix.Plugins.map((_, i) => i);
    if (!compareOnly || !anchors) return all;
    const picked = [matrix.Plugins.indexOf(anchors[0]), matrix.Plugins.indexOf(anchors[1])]
      .filter(i => i >= 0);
    return picked.length ? [...new Set(picked)].sort((a, b) => a - b) : all;
  }, [matrix.Plugins, compareOnly, anchors]);
  const visiblePlugins = useMemo(
    () => visibleCols.map(i => [matrix.Plugins[i], i] as const), [visibleCols, matrix.Plugins]);
  const [conflictLayout, setConflictLayout] = useState<ConflictLayout>(
    () => (localStorage.getItem('conflictLayout') as ConflictLayout) || 'cards');
  const changeLayout = (next: ConflictLayout) => {
    setConflictLayout(next);
    localStorage.setItem('conflictLayout', next);
  };

  useEffect(() => {
    const close = () => setMenu(null);
    window.addEventListener('click', close);
    window.addEventListener('scroll', close, true);
    return () => { window.removeEventListener('click', close); window.removeEventListener('scroll', close, true); };
  }, []);

  if (prevFormKey !== matrix.FormKey) {
    setPrevFormKey(matrix.FormKey);
    setRefBy(null); setProblems(null); setCollapsedGroups({});
    setDeps(null); setHistory(null); setAnchors(null); setEdit(null);
    setScanErrors({});
  }

  useEffect(() => {
    const b = back();
    if (!b) return;
    const mine = ++loadSeq.current;
    const live = () => mine === loadSeq.current;
    (async () => {
      const fail = (k: string, e: unknown) => {
        if (live()) setScanErrors(prev => ({ ...prev, [k]: e instanceof Error ? e.message : String(e) }));
      };
      try { const raw = await b.GetProblems(matrix.FormKey); if (live()) setProblems(JSON.parse(raw)); }
      catch (e) { if (live()) setProblems([]); fail('problems', e); }
      try { const raw = await b.GetReferencedBy(matrix.FormKey); if (live()) setRefBy(JSON.parse(raw)); }
      catch (e) { if (live()) setRefBy([]); fail('refBy', e); }
      try { const raw = await b.GetDependencies(matrix.FormKey); if (live()) setDeps(JSON.parse(raw)); }
      catch (e) { if (live()) setDeps([]); fail('deps', e); }
      try { const raw = await b.GetHistory(matrix.FormKey); if (live()) setHistory(JSON.parse(raw)); }
      catch (e) { if (live()) setHistory([]); fail('history', e); }
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
  const vf = valueFilter.trim().toLowerCase();

  const filtering = f.length > 0 || vf.length > 0;

  const viewRows = useMemo(() => matrix.Rows.filter(r => rawView ? r.Group === 'Raw' : r.Group !== 'Raw'), [matrix.Rows, rawView]);

  const matched = useMemo(() => viewRows.filter(r => {
    if (onlyDiffing && !r.Differs) return false;
    if (f && !(r.DisplayLabel || r.Field).toLowerCase().includes(f)) return false;
    if (vf && !r.Values.some(v => v.toLowerCase().includes(vf))) return false;
    if (severityFilter !== 'all' && !SEVERITY_SETS[severityFilter].has(r.Severity ?? 'none')) return false;
    return true;
  }), [viewRows, f, vf, onlyDiffing, severityFilter]);

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
  }, [matched, ov, filtering]);

  const isGenuineConflict = (r: ConflictFieldRow) => {
    if (!r.Differs || r.HasChildren) return false;
    const nonEmpty = r.Values.filter(v => v !== '' && v != null);
    if (nonEmpty.length < 2) return false;
    return !nonEmpty.every(v => v === nonEmpty[0]);
  };

  const conflictLeafRows = useMemo(
    () => viewRows.filter(isGenuineConflict),
    [viewRows]
  );

  const conflictGroups = useMemo(() => {
    type CGroup = { label: string; rows: ConflictFieldRow[]; severity: string };
    const groups: CGroup[] = [];
    let cur: CGroup | null = null;

    for (const r of viewRows) {
      if (r.HasChildren && r.Level <= 1) {
        if (cur && cur.rows.length > 0) groups.push(cur);
        cur = { label: r.DisplayLabel || r.Field, rows: [], severity: 'none' };
      } else if (isGenuineConflict(r)) {
        if (!cur) cur = { label: 'General', rows: [], severity: 'none' };
        cur.rows.push(r);
        const sev = r.Severity ?? 'none';
        if ((SEV_ORDER[sev] ?? 0) > (SEV_ORDER[cur.severity] ?? 0)) cur.severity = sev;
      }
    }
    if (cur && cur.rows.length > 0) groups.push(cur);
    return groups;
  }, [viewRows]);

  const startEdit = (r: ConflictFieldRow, col: number) => {
    setMenu(null);
    if (r.HasChildren || r.Editable?.[col] === false) return;
    if (r.EditKind === 'Ref') { setPicker({ row: r, col }); return; }
    if (r.Presentation && !rawEditable(r.Values[col] ?? '', r.Presentation)) return;
    setEdit({ field: r.Field, col });
    setEditVal(r.Values[col] ?? '');
  };

  const commitEdit = async (r: ConflictFieldRow, col: number, value = editVal) => {
    setEdit(null);
    if (r.Editable?.[col] === false) return;
    const b = back();
    if (!b) return;
    if (value === (r.Values[col] ?? '')) return;
    setStatus('Saving…');
    try {
      setStatus(await b.SetField(matrix.Plugins[col], matrix.FormKey, r.Field, value));
      await onReload();
    } catch (e) { setStatus('Edit failed: ' + errorText(e)); }
  };

  useEffect(() => {
    if (!menu) return;
    const b = back();
    if (!b) return;
    const mine = ++menuSeq.current;
    const live = () => mine === menuSeq.current;
    (async () => {
      try { const raw = await b.GetEditablePlugins(); if (live()) setEditablePlugins(JSON.parse(raw)); }
      catch (e) { if (live()) { setEditablePlugins([]); setStatus('Could not list editable plugins: ' + errorText(e)); } }
      try {
        const raw = await b.DescribeElement(matrix.Plugins[menu.col], matrix.FormKey, menu.row.Field);
        if (live()) setElementActions(JSON.parse(raw));
      } catch (e) { if (live()) { setElementActions(null); setStatus('Could not read element actions: ' + errorText(e)); } }
    })();
  }, [menu]);

  const actions = useRecordActions({
    matrix, editablePlugins, closeMenu: () => setMenu(null), setStatus, onReload, onPluginsChanged, selected: selectedRecords, setErrorStr,
  });
  const { copyInto, renumberFormId, compactEsl, cleanPlugin, deleteRecord } = actions;

  const winnerCol = Math.max(0, matrix.Plugins.lastIndexOf(matrix.Winner));
  const headerActions = useMemo(() => buildActions({
    copyAsOverride: () => {
      void (async () => {
        const picked = await askForTarget({
          title: 'Copy as override',
          description: `Copies ${matrix.EditorID || matrix.FormKey} from ${matrix.Plugins[winnerCol]} into the plugin you choose, as an override.`,
          confirmLabel: 'Copy',
          defaultTarget: editablePlugins[0] ?? '',
        });
        if (picked) await copyInto(winnerCol, picked.target);
      })();
    },
    changeFormId: () => void renumberFormId(winnerCol),
    compactToEsl: () => void compactEsl(winnerCol),
    cleanUdr: () => void cleanPlugin(winnerCol),
    deleteRecord: () => void deleteRecord(winnerCol),
  }), [matrix.FormKey, matrix.Winner, editablePlugins]);

  const renderEditor = (r: ConflictFieldRow, ci: number) => {
    if (r.Presentation) {
      return <ScalarEditor presentation={r.Presentation} value={editVal} onValue={setEditVal}
        onCommit={v => commitEdit(r, ci, v)} onCancel={() => setEdit(null)} />;
    }
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

  const renderGrid = (gridRows: ConflictFieldRow[]) => (
    <table
      className={`rv-grid rv-cw-${columnWidthMode}`}
      style={columnWidthMode === 'standard' ? { minWidth: 320 + visibleCols.length * 240 } : undefined}
    >
      <thead>
        <tr>
          <th className="rv-prop-col rv-sticky">Record Property</th>
          {visiblePlugins.map(([p, i]) => (
            <th key={p + i} className={i === winnerIdx ? 'rv-winner-col' : ''}>
              <span className="rv-col-name">{p}</span>
              {i === winnerIdx && matrix.Plugins.length > 1 && <span className="rv-winner-badge">WINNER</span>}
            </th>
          ))}
        </tr>
      </thead>
      <tbody>
        {gridRows.length === 0 ? (
          <tr><td className="rv-empty" colSpan={visibleCols.length + 1}>No fields match.</td></tr>
        ) : gridRows.map((r, ri) => (
          <tr key={rowKey(r, ri)} data-flash-seq={flashing(r) ? highlightSeq : undefined} className={[`rv-s-${r.Severity ?? 'none'}`, r.Differs ? 'rv-diff' : '', flashing(r) ? 'rv-row-flash' : ''].filter(Boolean).join(' ')}>
            <td
              className={`rv-prop-col rv-sticky ${r.HasChildren ? 'rv-expandable' : ''}`}
              onDoubleClick={() => r.HasChildren && toggle(r)} {...f2Edit(() => startEdit(r, winnerCol))}
              onContextMenu={e => { e.preventDefault(); e.stopPropagation(); openMenu({ x: e.clientX, y: e.clientY, row: r, col: winnerCol }); }}
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
            {visibleCols.map(ci => {
              const editing = edit && edit.field === r.Field && edit.col === ci;
              const st = resolveStatus(r, ci);
              return (
                <td
                  key={ci}
                  className={`rv-cell rv-c-${st}`}
                  onDoubleClick={() => !r.HasChildren && startEdit(r, ci)} {...f2Edit(() => startEdit(r, ci))}
                  onContextMenu={e => { e.preventDefault(); e.stopPropagation(); openMenu({ x: e.clientX, y: e.clientY, row: r, col: ci }); }}
                >
                  {st === 'win' && <Crown size={10} className="rv-win-icon" />}
                  {editing ? renderEditor(r, ci) : <ScalarText raw={r.Values[ci]} presentation={r.Presentation} />}
                </td>
              );
            })}
          </tr>
        ))}
      </tbody>
    </table>
  );

  const problemCount = problems?.length ?? 0;
  const refByCount = refBy?.length ?? 0;
  const conflictCount = conflictLeafRows.length;

  return (
    <div className="rv-container">
      <div className="rv-tab-bar">
        <TabBtn id="grid" current={recordTab} set={setRecordTab}>Record View</TabBtn>
        <TabBtn id="fields" current={recordTab} set={setRecordTab} count={matrix.Rows.length}>Field View</TabBtn>
        <TabBtn id="conflicts" current={recordTab} set={setRecordTab} count={conflictCount}>Conflicts</TabBtn>
        <TabBtn id="references" current={recordTab} set={setRecordTab} count={refByCount}>
          <Link2 size={12} /> References
        </TabBtn>
        <TabBtn id="history" current={recordTab} set={setRecordTab}>
          <Clock size={12} /> History
        </TabBtn>
        <TabBtn id="dependencies" current={recordTab} set={setRecordTab}>
          <Network size={12} /> Dependencies
        </TabBtn>
        {TYPED_RECORD_TYPES.includes(matrix.Type) && <TabBtn id="typed" current={recordTab} set={setRecordTab}>Typed Fields</TabBtn>}
      </div>

      <WorkspaceHeader
        matrix={matrix}
        anchors={anchors}
        onAnchorsChange={setAnchors}
        compareOnly={compareOnly}
        onCompareOnlyChange={setCompareOnly}
        showCompare={recordTab === 'conflicts'}
        actions={headerActions}
        onOpenRecord={(fk, pl) => onOpenRecord?.(fk, pl)}
      />

      {recordTab === 'grid' && (
        <>
          <div className="rv-toolbar">
            <button className={`rv-toggle-btn ${rawView ? 'on' : ''}`} aria-pressed={rawView} onClick={() => setRawView(v => !v)}>
              Raw / Advanced
            </button>
            <span className="rv-tb-label">Filter by name</span>
            <div className="rv-filter">
              <input value={filter} onChange={e => setFilter(e.target.value)} placeholder="field name…" />
            </div>
            <span className="rv-tb-label">by value</span>
            <div className="rv-filter">
              <input value={valueFilter} onChange={e => setValueFilter(e.target.value)} placeholder="any value…" />
            </div>
            <button className={`rv-toggle-btn ${onlyDiffing ? 'on' : ''}`} onClick={() => setOnlyDiffing(v => !v)}>
              <EyeOff size={12} /> Only conflicts
            </button>
            <select
              className="rv-status-filter"
              value={severityFilter}
              onChange={e => setSeverityFilter(e.target.value as SeverityFilter)}
              title="Filter rows by conflict severity"
            >
              <option value="all">All rows</option>
              <option value="override">Conflicts (override+)</option>
              <option value="conflict">Real conflicts (conflict+)</option>
              <option value="critical">Critical only</option>
            </select>
            <select
              className="rv-status-filter"
              value={columnWidthMode}
              onChange={e => changeColumnWidthMode(e.target.value as ColumnWidthMode)}
              title="How plugin columns share the available width"
            >
              <option value="standard">Column width: Standard</option>
              <option value="fitAll">Column width: Fit All</option>
              <option value="fitText">Column width: Fit Text</option>
              <option value="fitSmart">Column width: Fit Smart</option>
            </select>
            {(() => {
              const badge = LEVEL_BADGES[matrix.Level];
              return badge ? <span className={`rv-lvl-badge ${badge.cls}`}>{badge.label}</span> : null;
            })()}
            <span className="rv-tb-src" title={`Opened from ${plugin}`}>{plugin}</span>
            {status && <span className="rv-status">{status}</span>}
            <button
              className="rv-legend-btn"
              onClick={() => setLegendOpen(o => !o)}
              title="What the colours mean"
            >
              <HelpCircle size={12} /> Legend
            </button>
            {legendOpen && (
              <div className="rv-legend">
                <h4>Row: record state</h4>
                {LEGEND_ROWS.map(l => (
                  <div key={l.cls} className="rv-legend-row">
                    <span className={`rv-legend-swatch ${l.cls}`} />
                    <span>{l.label}</span>
                  </div>
                ))}
                <h4>Value: per plugin</h4>
                {LEGEND_CELLS.map(l => (
                  <div key={l.cls} className="rv-legend-row">
                    <span className="rv-legend-swatch" />
                    <span className={l.cls}>{l.label}</span>
                  </div>
                ))}
              </div>
            )}
          </div>
          <div className="rv-grid-scroll">
            {renderGrid(rows)}
          </div>
        </>
      )}

      {recordTab === 'conflicts' && (
        <ConflictsView
          matrix={matrix}
          rows={conflictLeafRows}
          layout={conflictLayout}
          onLayoutChange={changeLayout}
          anchors={anchors}
          matrixView={
            <div className="rv-conflicts-tab">
              <div className="rv-conflict-groups">
                <table className="rv-grid rv-cg-table" style={{ minWidth: 280 + visibleCols.length * 200 }}>
                  <thead>
                    <tr>
                      <th className="rv-prop-col rv-sticky">Field</th>
                      {visiblePlugins.map(([p, i]) => (
                        <th key={p + i} className={i === winnerIdx ? 'rv-winner-col' : ''}>
                          <span className="rv-col-name" title={p}>{p}</span>
                          {i === winnerIdx && matrix.Plugins.length > 1 && <span className="rv-winner-badge">WINNER</span>}
                        </th>
                      ))}
                    </tr>
                  </thead>
                  <tbody>
                    {conflictGroups.map((g, gi) => {
                      const collapsed = !!collapsedGroups[gi];
                      const colCount = visibleCols.length + 1;
                      return (
                        <Fragment key={gi}>
                          <tr
                            className={`rv-cg-sep rv-cg-sep-sev-${g.severity}`}
                            onClick={() => setCollapsedGroups(prev => ({ ...prev, [gi]: !prev[gi] }))}
                          >
                            <td colSpan={colCount}>
                              <div className="rv-sep-inner">
                                <span className="rv-sep-name">{g.label}</span>
                                <span className={`rv-sep-badge rv-cg-sev-badge-${g.severity}`}>
                                  {g.rows.length} conflict{g.rows.length !== 1 ? 's' : ''}
                                </span>
                                <ChevronRight size={12} className={`rv-sep-chevron${collapsed ? '' : ' rv-sep-chevron-open'}`} />
                              </div>
                            </td>
                          </tr>
                          {!collapsed && g.rows.map((r, ri) => (
                            <tr key={rowKey(r, ri)} data-flash-seq={flashing(r) ? highlightSeq : undefined} className={[`rv-s-${r.Severity ?? 'none'}`, 'rv-diff', flashing(r) ? 'rv-row-flash' : ''].filter(Boolean).join(' ')}>
                              <td
                                className="rv-prop-col rv-sticky" {...f2Edit(() => startEdit(r, winnerCol))}
                                onContextMenu={e => { e.preventDefault(); e.stopPropagation(); openMenu({ x: e.clientX, y: e.clientY, row: r, col: winnerCol }); }}
                              >
                                <span className="rv-prop-inner">{r.DisplayLabel || r.Field}</span>
                              </td>
                              {visibleCols.map(ci => {
                                const editing = edit && edit.field === r.Field && edit.col === ci;
                                const st = resolveStatus(r, ci);
                                return (
                                  <td
                                    key={ci}
                                    className={`rv-cell rv-c-${st}`}
                                    onDoubleClick={() => startEdit(r, ci)} {...f2Edit(() => startEdit(r, ci))}
                                    onContextMenu={e => { e.preventDefault(); e.stopPropagation(); openMenu({ x: e.clientX, y: e.clientY, row: r, col: ci }); }}
                                  >
                                    {st === 'win' && <Crown size={10} className="rv-win-icon" />}
                                    {editing ? renderEditor(r, ci) : <ScalarText raw={r.Values[ci]} presentation={r.Presentation} />}
                                  </td>
                                );
                              })}
                            </tr>
                          ))}
                        </Fragment>
                      );
                    })}
                  </tbody>
                </table>
              </div>
            </div>
          }
        />
      )}

      {recordTab === 'references' && (
        <div className="rv-references-tab">
          {refBy === null ? (
            <div className="rv-tab-loading">Scanning load order for references…</div>
          ) : scanErrors.refBy ? (
            <div className="rv-tab-empty" style={{ color: '#f44747' }}>
              The reference scan failed, so this is NOT a list of nothing -- it is no answer at all.
              Do not treat this record as unreferenced. <br />{scanErrors.refBy}
            </div>
          ) : refBy.length === 0 ? (
            <div className="rv-tab-empty">Nothing in the load order references this record.</div>
          ) : (
            <>
              <div className="rv-ref-header">{refBy.length} record{refBy.length !== 1 ? 's' : ''} reference this</div>
              <div className="rv-ref-list">
                {refBy.map((e, i) => (
                  <div
                    key={i}
                    className={`rv-ref-row ${onOpenRecord ? 'rv-ref-clickable' : ''}`}
                    onClick={() => onOpenRecord?.(e.FormKey, e.Plugin)}
                    title={onOpenRecord ? `Open ${e.EditorID || e.FormKey}` : undefined}
                  >
                    <span className="rv-ref-type">{e.Type}</span>
                    <span className="rv-ref-id">{e.EditorID || e.FormKey}</span>
                    <span className="rv-ref-plugin">{e.Plugin}</span>
                  </div>
                ))}
              </div>
            </>
          )}
        </div>
      )}

      {recordTab === 'fields' && (
        <div className="rv-list-tab">
          <div className="rv-tab-note">
            Every field of the winning version as a flat list, without the tree. Useful for reading
            or copying a whole record at once; edit from Record View.
          </div>
          <table className="rv-flat-table">
            <thead>
              <tr><th>Field</th><th>Value</th><th>Kind</th><th>Group</th></tr>
            </thead>
            <tbody>
              {viewRows.map(r => (
                <tr key={r.Field} className={r.Differs ? 'rv-row-diff' : ''}>
                  <td className="rv-mono" title={r.Field}>{r.DisplayLabel || r.Field}</td>
                  <td>{r.Values[winnerIdx] ?? <em>not set</em>}</td>
                  <td className="rv-dim">{r.Kind ?? 'Value'}</td>
                  <td className="rv-dim">{r.GroupLabel || '-'}</td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
      )}

      {recordTab === 'history' && (
        <div className="rv-list-tab">
          {history === null ? (
            <div className="rv-tab-loading">Reading the override chain…</div>
          ) : history.length === 0 ? (
            <div className="rv-tab-empty">No plugin carries this record.</div>
          ) : (
            <>
              <div className="rv-tab-note">
                Override chain in load order. Plugin files carry no edit history, so this is who
                touched the record, not when it was changed.
              </div>
              <table className="rv-flat-table">
                <thead>
                  <tr><th>#</th><th>Plugin</th><th>Action</th><th>Changed fields</th><th>File modified</th></tr>
                </thead>
                <tbody>
                  {history.map(h => (
                    <tr key={`${h.LoadOrder}:${h.Plugin}`}>
                      <td className="rv-num">{h.LoadOrder}</td>
                      <td>{h.Plugin}</td>
                      <td><span className={`rv-tag rv-tag-${h.Action.split(' ')[0]}`}>{h.Action}</span></td>
                      <td className="rv-num">{h.ChangedFields}</td>
                      <td className="rv-dim">{h.LastModified || '-'}</td>
                    </tr>
                  ))}
                </tbody>
              </table>
            </>
          )}
        </div>
      )}

      {recordTab === 'dependencies' && (
        <div className="rv-list-tab">
          {deps === null ? (
            <div className="rv-tab-loading">Resolving outgoing references…</div>
          ) : scanErrors.deps ? (
            <div className="rv-tab-empty" style={{ color: '#f44747' }}>
              The dependency scan failed, so this is no answer rather than an empty one.<br />{scanErrors.deps}
            </div>
          ) : deps.length === 0 ? (
            <div className="rv-tab-empty">This record does not reference anything.</div>
          ) : (
            <>
              <div className="rv-tab-note">
                What the winning version needs in order to load. A row marked missing is a dangling
                FormLink, which is a crash risk rather than a cosmetic problem.
              </div>
              <table className="rv-flat-table">
                <thead>
                  <tr><th>FormID</th><th>Editor ID</th><th>Type</th><th>Provided by</th></tr>
                </thead>
                <tbody>
                  {deps.map(d => (
                    <tr
                      key={d.FormKey}
                      className={d.Kind === 'missing' ? 'rv-row-bad' : 'rv-row-link'}
                      onClick={() => d.Kind === 'link' && onOpenRecord?.(d.FormKey, d.Plugin)}
                    >
                      <td className="rv-mono">{d.FormKey}</td>
                      <td>{d.EditorId || <em>unnamed</em>}</td>
                      <td>{d.Type || '-'}</td>
                      <td>
                        {d.Kind === 'missing'
                          ? <span className="rv-tag rv-tag-missing">unresolved</span>
                          : d.Plugin}
                      </td>
                    </tr>
                  ))}
                </tbody>
              </table>
            </>
          )}
        </div>
      )}
      {recordTab === 'typed' && <TypedRecordPanel matrix={matrix} plugin={plugin} onChanged={onReload} />}

      <div className={`rv-drawer ${drawerOpen ? 'open' : 'closed'}`}>
        <div className="rv-drawer-tabs">
          <button className="on">
            <AlertTriangle size={12} /> Problems
            {problemCount > 0 && <span className="rv-prob-badge">{problemCount}</span>}
          </button>
          <button className="rv-drawer-collapse" onClick={() => setDrawerOpen(o => !o)} title={drawerOpen ? 'Collapse' : 'Expand'}>
            {drawerOpen ? <ChevronDown size={14} /> : <ChevronRight size={14} />}
          </button>
        </div>
        {drawerOpen && (
          <div className="rv-drawer-body">
            <ProblemsList key={matrix.FormKey} problems={problems} error={scanErrors.problems} />
          </div>
        )}
      </div>

      {menu && (
        <RecordContextMenu menu={menu} matrix={matrix} elementActions={elementActions} actions={actions}
          onEdit={startEdit}
          onEditConditions={(plugin, path) => {
            setMenu(null);
            setConditions({ plugin, path, label: matrix.EditorID || matrix.FormKey });
          }}
          onShowTab={tab => { setMenu(null); setRecordTab(tab); }} />
      )}

      {picker && (
        <RecordPicker
          title={picker.row.DisplayLabel}
          refType={picker.row.RefType}
          refTypes={picker.row.RefTypes}
          onPick={fk => { const p = picker; setPicker(null); commitEdit(p.row, p.col, fk); }}
          onClose={() => setPicker(null)}
        />
      )}

      {conditions && (
        <ConditionsEditor
          plugin={conditions.plugin}
          record={matrix.FormKey}
          path={conditions.path}
          label={conditions.label}
          onClose={() => setConditions(null)}
          onSaved={async msg => { setStatus(msg); await onReload(); }}
        />
      )}
    </div>
  );
}

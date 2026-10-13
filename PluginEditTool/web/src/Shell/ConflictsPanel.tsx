import type { ConflictEntry } from '../backend';
import type { ConflictSelection, SelectMode } from './conflictSelection';

const modeOf = (e: React.MouseEvent): SelectMode => {
  if (e.shiftKey) return 'range';
  return e.ctrlKey || e.metaKey ? 'toggle' : 'plain';
};

export default function ConflictsPanel({ conflicts, conflictSel, conflictBusy, onRescan, onBatchCopy, onSelect, onClear, onOpen }: {
  conflicts: ConflictEntry[] | null;
  conflictSel: ConflictSelection;
  conflictBusy: boolean;
  onRescan: () => void;
  onBatchCopy: () => void;
  onSelect: (formKey: string, mode: SelectMode) => void;
  onClear: () => void;
  onOpen: (formKey: string, winner: string) => Promise<void>;
}) {
  const picked = (conflicts ?? []).filter(c => conflictSel[c.FormKey]);
  const compare = async () => {
    for (const c of picked) await onOpen(c.FormKey, c.Winner);
  };

  return (
    <div className="conflicts-panel" onKeyDown={e => { if (e.key === 'Escape') onClear(); }}>
      <div className="conflicts-actions">
        <button className="sidebar-action-btn" onClick={onRescan}>Rescan</button>
        <button
          className="sidebar-action-btn"
          disabled={conflictBusy || picked.length === 0}
          title={picked.length === 0 ? 'Select at least one record to copy' : undefined}
          onClick={onBatchCopy}
        >
          Copy selected → patch
        </button>
        <button
          className="sidebar-action-btn"
          disabled={conflictBusy || picked.length !== 2}
          title={picked.length === 2 ? 'Open the two selected records in the record view' : 'Select exactly two records to compare'}
          onClick={compare}
        >
          Compare Selected
        </button>
        <span className="conflicts-selected">{picked.length} selected</span>
      </div>
      {conflicts === null ? (
        <div className="sidebar-empty">Scanning conflicts…</div>
      ) : conflicts.length === 0 ? (
        <div className="sidebar-empty">No conflicts. Load a modlist and Scan Conflicts.</div>
      ) : (
        <div className="conflicts-list" role="listbox" aria-multiselectable="true" aria-label="Conflicts">
          {conflicts.map((c, i) => (
            <div
              key={`${c.FormKey}:${i}`}
              className="conflict-row"
              role="option"
              aria-selected={!!conflictSel[c.FormKey]}
              onClick={e => onSelect(c.FormKey, modeOf(e))}
            >
              <input
                type="checkbox"
                checked={!!conflictSel[c.FormKey]}
                onClick={e => e.stopPropagation()}
                onChange={() => onSelect(c.FormKey, 'toggle')}
              />
              <span
                className="conflict-id"
                onClick={e => { if (!e.ctrlKey && !e.metaKey && !e.shiftKey) void onOpen(c.FormKey, c.Winner); }}
                title={`${c.FormKey} · ${c.Plugins.length} plugins`}
              >
                {c.EditorID || c.FormKey}
              </span>
              <span className="conflict-type">{c.Type}</span>
              <span className="conflict-count">{c.Plugins.length}</span>
            </div>
          ))}
        </div>
      )}
    </div>
  );
}

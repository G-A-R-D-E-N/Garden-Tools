import type { ConflictEntry } from '../backend';
import { modeOf, type ConflictSelection, type SelectMode } from './conflictSelection';
import { useOpenSelectedInTabs } from './useConflictBatch';

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
  const { comparing, compare } = useOpenSelectedInTabs();
  const picked = (conflicts ?? []).filter(c => conflictSel[c.FormKey]);

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
          disabled={conflictBusy || comparing || picked.length !== 2}
          title={picked.length === 2 ? 'Open the two selected records in the record view' : 'Select exactly two records to compare'}
          onClick={() => compare(picked.map(c => ({ formKey: c.FormKey, plugin: c.Winner })), onOpen)}
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
        <div className="conflicts-list" tabIndex={0} aria-label="Conflicts">
          {conflicts.map((c, i) => (
            <div
              key={`${c.FormKey}:${i}`}
              className={`conflict-row${conflictSel[c.FormKey] ? ' selected' : ''}`}
              onClick={e => onSelect(c.FormKey, modeOf(e))}
            >
              <input
                type="checkbox"
                aria-label={c.EditorID || c.FormKey}
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

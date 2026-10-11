import type { ConflictEntry } from '../backend';

export default function ConflictsPanel({ conflicts, conflictSel, conflictBusy, onRescan, onBatchCopy, onToggle, onOpen }: {
  conflicts: ConflictEntry[] | null;
  conflictSel: Record<string, boolean>;
  conflictBusy: boolean;
  onRescan: () => void;
  onBatchCopy: () => void;
  onToggle: (formKey: string, checked: boolean) => void;
  onOpen: (formKey: string, winner: string) => void;
}) {
  return (
    <div className="conflicts-panel">
      <div className="conflicts-actions">
        <button className="sidebar-action-btn" onClick={onRescan}>Rescan</button>
        <button className="sidebar-action-btn" disabled={conflictBusy} onClick={onBatchCopy}>
          Copy selected → patch
        </button>
      </div>
      {conflicts === null ? (
        <div className="sidebar-empty">Scanning conflicts…</div>
      ) : conflicts.length === 0 ? (
        <div className="sidebar-empty">No conflicts. Load a modlist and Scan Conflicts.</div>
      ) : (
        <div className="conflicts-list">
          {conflicts.map((c, i) => (
            <div key={`${c.FormKey}:${i}`} className="conflict-row">
              <input
                type="checkbox"
                checked={!!conflictSel[c.FormKey]}
                onChange={e => onToggle(c.FormKey, e.target.checked)}
              />
              <span className="conflict-id" onClick={() => onOpen(c.FormKey, c.Winner)} title={`${c.FormKey} · ${c.Plugins.length} plugins`}>
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

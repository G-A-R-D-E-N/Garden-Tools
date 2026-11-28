import { logSeverity, type LogSeverity } from './cellLogSeverity';

const ROW_CLASS: Record<LogSeverity, string> = { error: 'err', warn: 'warn', ok: 'ok' };

export default function CellLogPane({ log, onClear }: { log: string[]; onClear: () => void }) {
  return (
    <div className="cell-sidebar-pane cell-log-pane">
      <div className="papyrus-log-head">
        <span>LOG ({log.length})</span>
        {log.length > 0 && <button className="papyrus-copy" onClick={onClear}>Clear</button>}
      </div>
      <div className="papyrus-log-body cell-log-body">
        {log.length === 0 ? <div className="papyrus-log-empty">No runs yet.</div>
          : log.map((l, i) => <div key={i} className={`papyrus-log-row ${ROW_CLASS[logSeverity(l)]}`}>{l}</div>)}
      </div>
    </div>
  );
}

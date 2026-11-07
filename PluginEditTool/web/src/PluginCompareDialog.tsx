import { useRef, useState } from 'react';
import './PluginCompareDialog.css';

export interface CompareRow {
  FormKey: string;
  Signature: string;
  Change: string;
  HeaderChanged: boolean;
  PlacementChanged: boolean;
  Subrecords: string[];
}
export interface SubrecordSide { Signature: string; Size: number; Hex: string; Text: string }
export interface RecordSides { FormKey: string; Before: SubrecordSide[] | null; After: SubrecordSide[] | null; Differs: boolean[] }

export interface CompareRequest {
  plugin: string;
  otherPath: string;
  rows: CompareRow[];
  loadRecord: (formKey: string) => Promise<RecordSides>;
}

const cell = (side: SubrecordSide | undefined) =>
  side ? (side.Text ? `"${side.Text}"` : side.Hex || '(empty)') : '';

export default function PluginCompareDialog({ req, onClose }: { req: CompareRequest; onClose: () => void }) {
  const [selected, setSelected] = useState<string | null>(null);
  const [sides, setSides] = useState<RecordSides | null>(null);
  const [error, setError] = useState('');
  const latest = useRef(0);

  const open = async (formKey: string) => {
    const request = ++latest.current;
    setSelected(formKey); setSides(null); setError('');
    try {
      const loaded = await req.loadRecord(formKey);
      if (request === latest.current) setSides(loaded);
    } catch (e) {
      if (request === latest.current) setError(e instanceof Error ? e.message : String(e));
    }
  };

  const before = sides?.Before ?? [];
  const after = sides?.After ?? [];
  const length = Math.max(before.length, after.length);
  const picked = req.rows.find(row => row.FormKey === selected);

  return (
    <div className="ptd-overlay" onClick={onClose}>
      <div className="ptd-modal glass-panel pcd-modal" role="dialog" aria-label="Compare plugins"
           onClick={e => e.stopPropagation()}
           onKeyDown={e => { if (e.key === 'Escape') { e.stopPropagation(); onClose(); } }}>
        <div className="ptd-header">
          <span className="ptd-title">Compare {req.plugin} to {req.otherPath}</span>
          <button className="ptd-close" onClick={onClose} title="Close">×</button>
        </div>
        {req.rows.length === 0 ? <div className="ptd-desc">No differences.</div> : (
          <div className="pcd-body">
            <ul className="pcd-list">
              {req.rows.map(row => (
                <li key={row.FormKey}>
                  <button className={row.FormKey === selected ? 'pcd-row active' : 'pcd-row'} onClick={() => open(row.FormKey)}>
                    <span className={`pcd-change pcd-${row.Change}`}>{row.Change}</span> {row.Signature} {row.FormKey}
                  </button>
                </li>
              ))}
            </ul>
            <div className="pcd-detail">
              {error && <div className="ptd-note ptd-note-warn">{error}</div>}
              {!selected && <div className="ptd-desc">Pick a record to see both versions.</div>}
              {picked?.HeaderChanged && <div className="ptd-note">Record header flags differ.</div>}
              {picked?.PlacementChanged && <div className="ptd-note">Record is in a different parent group.</div>}
              {sides && (
                <table className="pcd-table">
                  <thead><tr><th>#</th><th>{req.otherPath.split(/[\\/]/).pop()}</th><th>{req.plugin}</th></tr></thead>
                  <tbody>
                    {Array.from({ length }, (_, i) => {
                      const a = before[i];
                      const b = after[i];
                      const differs = sides.Differs[i];
                      return (
                        <tr key={i} className={differs ? 'pcd-diff' : ''}>
                          <td>{a?.Signature ?? b?.Signature}</td>
                          <td>{sides.Before ? cell(a) : '(not in this file)'}</td>
                          <td>{sides.After ? cell(b) : '(not in this file)'}</td>
                        </tr>
                      );
                    })}
                  </tbody>
                </table>
              )}
            </div>
          </div>
        )}
      </div>
    </div>
  );
}

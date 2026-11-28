import { AlertTriangle, CheckCircle2, XCircle } from 'lucide-react';
import type { GeoProgress } from './geoProgress';
import { statusBanner } from './util/cellStatus';

export default function CellProgressBanner({ progress, status, busy }: { progress: GeoProgress | null; status: string; busy: boolean }) {
  if (progress) {
    const indexing = progress.phase === 'indexing';
    const indexTotal = progress.indexTotal ?? 0;
    const done = indexing ? (progress.indexDone ?? 0) : progress.done;
    const total = indexing ? indexTotal : progress.total;
    return (
      <div className="cell-progress">
        <div className="cell-progress-label">
          <span>{indexing ? 'Indexing archives…' : 'Converting meshes…'}</span>
          {(!indexing || indexTotal > 0) && <span>{done}/{total}</span>}
        </div>
        <div className="cell-progress-track">
          <div className="cell-progress-fill" style={{ width: `${total > 0 ? (done / total) * 100 : 0}%` }} />
        </div>
      </div>
    );
  }
  const banner = statusBanner(status, busy);
  if (!banner) return null;
  return (
    <div className={`papyrus-banner ${banner.kind}`}>
      {banner.kind === 'ok' ? <CheckCircle2 size={15} /> : banner.kind === 'warn' ? <AlertTriangle size={15} /> : <XCircle size={15} />}
      <span className="papyrus-banner-text">{status}</span>
    </div>
  );
}

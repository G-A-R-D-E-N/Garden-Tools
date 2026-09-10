import './StatusBar.css';

interface StatusBarProps {
  status: string;
  progress: number | null;
  recordCount: number;
  visibleCount: number;
  selectedCount: number;
  loadOrder: string[];
  mo2Loaded?: boolean;
  mo2Stale?: boolean;
  mo2StaleConfig?: boolean;
  mo2StaleLoadOrder?: boolean;
  mo2Profile?: string;
}

export function mo2FreshnessLabel(args: {
  loaded?: boolean;
  stale?: boolean;
  staleConfig?: boolean;
  staleLoadOrder?: boolean;
  profile?: string;
}): string | null {
  if (!args.loaded) return null;
  const name = args.profile ? `MO2 ${args.profile}` : 'MO2 profile';
  if (!args.stale) return `${name} current`;
  if (args.staleConfig && !args.staleLoadOrder) return `${name} config changed`;
  if (args.staleLoadOrder && !args.staleConfig) return `${name} load order changed`;
  return `${name} changed`;
}

export default function StatusBar({
  status, progress, recordCount, visibleCount, selectedCount, loadOrder,
  mo2Loaded, mo2Stale, mo2StaleConfig, mo2StaleLoadOrder, mo2Profile,
}: StatusBarProps) {
  const loadOrderText = loadOrder.length ? loadOrder.join(', ') : 'none';
  const freshness = mo2FreshnessLabel({
    loaded: mo2Loaded,
    stale: mo2Stale,
    staleConfig: mo2StaleConfig,
    staleLoadOrder: mo2StaleLoadOrder,
    profile: mo2Profile,
  });
  return (
    <div className="status-bar">
      {progress !== null && (
        <div className="status-progress">
          <div
            className={`status-progress-fill ${progress === 0 ? 'indeterminate' : ''}`}
            style={{ width: `${Math.max(0, Math.min(100, progress))}%` }}
          />
        </div>
      )}
      <span className="status-text">{status || (progress !== null ? 'Working...' : 'Ready')}</span>
      {freshness && (
        <>
          <span className="status-sep" />
          <span className={`status-stat ${mo2Stale ? 'status-mo2-stale' : 'status-mo2-current'}`}>
            {freshness}
          </span>
        </>
      )}
      <span className="status-sep" />
      <span className="status-stat">Records: {recordCount}</span>
      <span className="status-stat">Visible: {visibleCount}</span>
      <span className="status-stat">Selected: {selectedCount}</span>
      <span className="status-stat status-loadorder" title={loadOrderText}>
        Load Order: {loadOrderText}
      </span>
    </div>
  );
}

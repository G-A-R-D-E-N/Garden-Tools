export default function EnvLoadingOverlay({ status, progress }: { status: string; progress: number | null }) {
  return (
    <div className="env-loading-overlay" role="alertdialog" aria-busy="true" aria-live="polite">
      <div className="env-loading-box">
        <div className="env-loading-title">Loading environment…</div>
        <div className="env-loading-msg">{status || 'Reading the load order. This can take a few seconds on a large modlist.'}</div>
        <div className="env-loading-track">
          <div
            className={`env-loading-fill ${progress === null ? 'indeterminate' : ''}`}
            style={progress === null ? undefined : { width: `${Math.max(2, Math.min(100, progress))}%` }}
          />
        </div>
        <div className="env-loading-hint">The tool is locked until this finishes, so nothing is asked of a half-built environment.</div>
      </div>
    </div>
  );
}

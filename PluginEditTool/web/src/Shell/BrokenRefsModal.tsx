export default function BrokenRefsModal({ report, onClose }: { report: string; onClose: () => void }) {
  return (
    <div className="broken-refs-overlay" onClick={onClose}>
      <div className="broken-refs-modal" onClick={e => e.stopPropagation()}>
        <div className="broken-refs-header">
          <span>Crash Risk Report</span>
          <div className="broken-refs-header-actions">
            <button className="sidebar-action-btn" onClick={() => navigator.clipboard.writeText(report)}>Copy</button>
            <button className="sidebar-action-btn" onClick={() => {
              const blob = new Blob([report], { type: 'text/plain' });
              const url = URL.createObjectURL(blob);
              const a = document.createElement('a');
              a.href = url;
              a.download = 'crash_risk_report.txt';
              a.click();
              URL.revokeObjectURL(url);
            }}>Export</button>
            <button className="broken-refs-close" onClick={onClose}>✕</button>
          </div>
        </div>
        <pre className="broken-refs-body">{report}</pre>
      </div>
    </div>
  );
}

import type { InventoryRow } from './debugToolsClient';
import {
  BANNER, STATUS_ORDER, STRUCTURAL_LABEL, VERDICT_LABELS, diagReportJson, diagReportText, isStructuralOnly, redactSetting, redactText, summarise,
  type DiagContext, type DiagReportInput, type DiagResult, type DiagSide, type DiagStatus, type RunVerdict,
} from './diagnosticModel';
import { errorText } from './errors';

const SECTION_TITLES: Record<DiagStatus, string> = {
  fail: 'Failed', error: 'Errors', unverified: 'Unverified', pass: 'Passed',
  skip: 'Skipped (not passed)', unsupported: 'Unsupported (not passed)',
};

function download(name: string, content: string, type: string) {
  const url = URL.createObjectURL(new Blob([content], { type }));
  const a = document.createElement('a');
  a.href = url;
  a.download = name;
  a.click();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
}

export function RunDot({ verdict }: { verdict: RunVerdict | null }) {
  const label = verdict ? `Last run: ${VERDICT_LABELS[verdict].toLowerCase()}` : 'Not run yet';
  return <span role="img" aria-label={label} title={label} className={`dt-dot dt-v-${verdict ?? 'none'}`} />;
}

export const VerdictBadge = ({ verdict }: { verdict: RunVerdict }) => (
  <span className={`dbg-badge dt-vb dt-v-${verdict}`}>{VERDICT_LABELS[verdict]}</span>
);

export function ReportActions({ scope, base, build, disabled, onError }: {
  scope: string; base: string; build: () => DiagReportInput; disabled?: boolean; onError: (message: string) => void;
}) {
  const copy = async () => {
    try {
      await navigator.clipboard.writeText(diagReportText(build()));
    } catch (e) {
      onError(errorText(e));
    }
  };
  const file = base.replace(/[^\w.-]+/g, '_');
  return (
    <span className="dt-report-actions">
      <button className="sidebar-action-btn dt-run-btn" disabled={disabled} onClick={() => void copy()}>{`Copy ${scope}`}</button>
      <button className="sidebar-action-btn dt-run-btn" disabled={disabled} aria-label={`Export ${scope} .txt`}
              onClick={() => download(`${file}.txt`, diagReportText(build()), 'text/plain')}>Export .txt</button>
      <button className="sidebar-action-btn dt-run-btn" disabled={disabled} aria-label={`Export ${scope} .json`}
              onClick={() => download(`${file}.json`, diagReportJson(build()), 'application/json')}>Export .json</button>
    </span>
  );
}

const cell = (side: DiagSide | null, field: keyof DiagSide) => (side ? redactText(side[field]) || '-' : '-');

function ResultCard({ result }: { result: DiagResult }) {
  const { left, right } = result;
  return (
    <div className="dt-result">
      <div className="dbg-box-row">
        <span className={`dbg-badge dbg-${result.status}`}>{result.status.toUpperCase()}</span>
        <span className="dbg-title">{redactText(result.title)}</span>
        <span className="dbg-meta">{result.durationMs > 0 ? `${result.durationMs} ms` : ''}</span>
      </div>
      <div className="dt-chips">
        {result.evidence.map(kind => <span key={kind} className="dt-chip">{kind}</span>)}
        {isStructuralOnly(result) && <span className="dt-chip dt-chip-warn">{STRUCTURAL_LABEL}</span>}
      </div>
      {(left || right) && (
        <table className="dt-table dt-compare">
          <thead><tr><th /><th>Left</th><th>Right</th></tr></thead>
          <tbody>
            <tr><th>Label</th><td>{cell(left, 'label')}</td><td>{cell(right, 'label')}</td></tr>
            <tr><th>Source</th><td>{cell(left, 'source')}</td><td>{cell(right, 'source')}</td></tr>
            <tr><th>Value</th><td>{cell(left, 'value')}</td><td>{cell(right, 'value')}</td></tr>
          </tbody>
        </table>
      )}
      {result.diffs.length > 0 && (
        <ul className="dt-diffs">{result.diffs.map((diff, i) => <li key={`${i}|${diff}`}>{redactText(diff)}</li>)}</ul>
      )}
      {result.detail && <pre className="dbg-excerpt">{redactText(result.detail)}</pre>}
      {result.outputs.map((path, i) => <div key={`${i}|${path}`} className="dbg-meta">{`Output: ${redactText(path)}`}</div>)}
    </div>
  );
}

export function SummaryLine({ results }: { results: DiagResult[] }) {
  const sum = summarise(results);
  return (
    <div className="dbg-meta">
      {`Passed: ${sum.pass} (structural only: ${sum.structuralOnly}) | Failed: ${sum.fail} | Errors: ${sum.error} | `
        + `Unverified: ${sum.unverified} | Skipped: ${sum.skip} | Unsupported: ${sum.unsupported}`}
    </div>
  );
}

export function ResultList({ results }: { results: DiagResult[] }) {
  return (
    <div className="dt-results">
      <div className="dt-banner" role="note">{BANNER}</div>
      <SummaryLine results={results} />
      {STATUS_ORDER.map(status => {
        const rows = results.filter(result => result.status === status);
        if (rows.length === 0) return null;
        return (
          <div key={status} className={`dt-block dt-block-${status}`}>
            <div className="dbg-group-title">{`${SECTION_TITLES[status]} (${rows.length})`}</div>
            {rows.map((row, i) => <ResultCard key={`${row.id}|${i}`} result={row} />)}
          </div>
        );
      })}
    </div>
  );
}

export function ContextCard({ context }: { context: DiagContext }) {
  const settings = Object.keys(context.settings).sort();
  const row = (label: string, value: string) => (
    <div className="dt-ctx-row"><span className="dbg-meta">{label}</span><span>{redactText(value) || '-'}</span></div>
  );
  const line = `${context.versions.fo4ide || 'FO4IDE'} | profile ${context.mo2Profile || 'none'} | `
    + `${context.loadOrder.length} plugins | ${context.assetRootsLowToHigh.length} asset roots`;
  return (
    <details className="dt-context" aria-label="Context" open>
      <summary className="dt-context-summary">
        <span className="dbg-group-title">Environment</span> <span className="dbg-meta">{redactText(line)}</span>
      </summary>
      {row('FO4IDE', context.versions.fo4ide)}
      {row('Build commit', context.versions.commit)}
      {row('Radicle ABI', context.versions.radicleAbi)}
      {row('Game build', context.gameBuild)}
      {row('Fallout 4 data dir', context.dataDir)}
      {row('MO2 profile', context.mo2Profile)}
      <details>
        <summary>{`Load order (${context.loadOrder.length})`}</summary>
        {context.loadOrder.map((name, i) => <div key={`${i}|${name}`} className="dbg-meta">{redactText(`${i + 1}. ${name}`)}</div>)}
      </details>
      <details>
        <summary>{`Asset roots, low to high priority (${context.assetRootsLowToHigh.length})`}</summary>
        {context.assetRootsLowToHigh.map((root, i) => <div key={`${i}|${root}`} className="dbg-meta">{redactText(`${i + 1}. ${root}`)}</div>)}
      </details>
      <details>
        <summary>{`Settings (${settings.length})`}</summary>
        {settings.map(key => <div key={key} className="dbg-meta">{`${key}: ${redactSetting(key, context.settings[key])}`}</div>)}
      </details>
    </details>
  );
}

export function Inventory({ rows }: { rows: InventoryRow[] }) {
  const count = (status: InventoryRow['status']) => rows.filter(row => row.status === status).length;
  return (
    <div>
      <div className="dbg-hint">
        Coverage is partial: this lists what FO4IDE checks today, not everything Fallout 4 needs.
      </div>
      <div className="dbg-meta">
        {`Integrated: ${count('integrated')} | Available: ${count('available')} | Gap: ${count('gap')}`}
      </div>
      <table className="dt-table dt-inventory">
        <thead><tr><th>Area</th><th>Capability</th><th>Status</th><th>Tool</th><th>Note</th></tr></thead>
        <tbody>
          {rows.map((row, i) => (
            <tr key={`${i}|${row.area}|${row.capability}`}>
              <td>{row.area}</td>
              <td>{row.capability}</td>
              <td><span className={`dbg-badge dt-inv-${row.status}`}>{row.status.toUpperCase()}</span></td>
              <td>{row.toolId || '-'}</td>
              <td>{row.note}</td>
            </tr>
          ))}
        </tbody>
      </table>
    </div>
  );
}

import { AlertTriangle, CheckCircle2, Info, XCircle } from 'lucide-react';
import type { FriendlyMessage } from '../friendlyErrors';
import { PANE_AREAS, type CheckFinding, type CheckSeverity } from './questChecks';
import { ErrorNote } from './questControls';
import type { CheckResult } from './useQuestCheck';

interface Props {
  result: CheckResult | null;
  running: boolean;
  error: FriendlyMessage | null;
  outOfDate: boolean;
  onOpen: (finding: CheckFinding) => void;
}

const ICONS = { error: XCircle, warning: AlertTriangle, info: Info } as const;
const LABELS: Record<CheckSeverity, string> = { error: 'Error', warning: 'Warning', info: 'Info' };

const plural = (count: number, word: string) => `${count} ${word}${count === 1 ? '' : 's'}`;

export default function CheckPanel({ result, running, error, outOfDate, onOpen }: Props) {
  if (!result && !running && !error) return null;
  const errors = result?.findings.filter(finding => finding.severity === 'error').length ?? 0;
  const warnings = result?.findings.filter(finding => finding.severity === 'warning').length ?? 0;
  return (
    <section className="quest-check-panel" aria-labelledby="quest-check-title">
      <h4 id="quest-check-title">Check results</h4>
      {running && <p className="quest-note" role="status">Checking this quest...</p>}
      {error && <ErrorNote error={error} />}
      {result && !running && (
        <>
          <p className="quest-check-counts" role="status">
            <span>{plural(errors, 'error')}</span>
            <span>{plural(warnings, 'warning')}</span>
          </p>
          <p className="quest-note">Checked at {result.checkedAt.toLocaleTimeString()}.</p>
          {outOfDate && <p className="quest-note" role="status">This list is out of date. Check this quest again.</p>}
          {result.findings.length === 0 ? (
            <p className="quest-check-empty"><CheckCircle2 aria-hidden="true" /> No problems found.</p>
          ) : (
            <ul className="quest-check-list" aria-label="Problems found">
              {result.findings.map((finding, at) => {
                const Icon = ICONS[finding.severity];
                return (
                  <li key={at} className={`quest-check-item quest-check-${finding.severity}`}>
                    <p className="quest-check-severity"><Icon aria-hidden="true" /> {LABELS[finding.severity]}</p>
                    <p className="quest-check-text">{finding.message}</p>
                    <p className="quest-check-area">{finding.area}</p>
                    {finding.record && <p className="quest-check-record"><code>{finding.record}</code></p>}
                    {finding.detail && (
                      <details className="quest-check-details">
                        <summary>Technical detail</summary>
                        <pre>{finding.detail}</pre>
                      </details>
                    )}
                    {finding.pane && (
                      <button type="button" className="sidebar-action-btn" onClick={() => onOpen(finding)}>
                        {finding.pane === 'stages' && finding.stage !== undefined
                          ? `Show stage ${finding.stage}`
                          : `Show the ${PANE_AREAS[finding.pane].toLowerCase()} pane`}
                      </button>
                    )}
                  </li>
                );
              })}
            </ul>
          )}
        </>
      )}
    </section>
  );
}

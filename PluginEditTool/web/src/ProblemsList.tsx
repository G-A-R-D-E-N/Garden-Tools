import { useState } from 'react';
import { AlertTriangle } from 'lucide-react';
import type { RecordProblem } from './backend';

export default function ProblemsList({ problems, error }: { problems: RecordProblem[] | null; error?: string }) {
  const [code, setCode] = useState('all');
  if (problems === null) return <div className="rv-drawer-note">Checking…</div>;
  if (error) return <div className="rv-drawer-note" style={{ color: '#f44747' }}>
    Problem check failed (not "no problems"): {error}</div>;
  if (problems.length === 0) return <div className="rv-drawer-note">No problems found.</div>;
  const codes = [...new Set<string>(problems.map(p => p.Code ?? ''))].sort();
  const active = codes.includes(code) ? code : 'all';
  return (
    <>
      <div className="rv-drawer-note">
        <label>Code{' '}
          <select value={active} onChange={e => setCode(e.target.value)}>
            <option value="all">All codes</option>
            {codes.map(c => <option key={c} value={c}>{c}</option>)}
          </select>
        </label>
      </div>
      {problems.filter(p => active === 'all' || (p.Code ?? '') === active).map((p, i) => (
        <div key={i} className={`rv-prob-row rv-prob-${p.Severity}`}>
          <AlertTriangle size={12} className="rv-prob-icon" />
          <span className="rv-mono">{p.Code ?? ''}</span>
          {p.Detail && <span className="rv-mono">{p.Detail}</span>}
          <span>{p.Description}</span>
        </div>
      ))}
    </>
  );
}

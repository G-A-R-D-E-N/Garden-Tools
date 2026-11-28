import { useCallback, useEffect, useRef, useState } from 'react';
import { ListChecks, X, Play, Square, RefreshCw, ChevronDown, ChevronRight, List } from 'lucide-react';
import {
  ageText, cancelRun, debugChecksEnabled, defaultTier, groupBoxes, loadList, loadStatus, loadTests, startRun,
  type CheckBox, type CheckList, type DebugStatus, type RunKind, type RunResult, type RunTest, type TestList,
} from './debugChecksClient';
import { errorText } from './errors';
import './DebugChecksPanel.css';

export default function DebugChecksPanel({ onClose, pollMs = 1000 }: { onClose: () => void; pollMs?: number }) {
  const [enabled, setEnabled] = useState<boolean | null>(null);
  const [list, setList] = useState<CheckList | null>(null);
  const [status, setStatus] = useState<DebugStatus | null>(null);
  const [tiers, setTiers] = useState<Record<string, string>>({});
  const [open, setOpen] = useState<Record<string, boolean>>({});
  const [error, setError] = useState('');
  const [listError, setListError] = useState('');
  const [polling, setPolling] = useState(false);
  const [baseRef, setBaseRef] = useState('');
  const [testLists, setTestLists] = useState<Record<string, TestList>>({});
  const [now, setNow] = useState(() => Date.now());
  // Invariant: only the newest poll may write status, so a slow older answer never replaces a newer one.
  const pollSeq = useRef(0);

  const run = status?.run ?? null;
  const active = run?.active === true;
  const off = enabled === false;

  const refreshList = useCallback(async () => {
    try {
      const next = await loadList();
      setList(next);
      setTiers(prev => Object.fromEntries(next.boxes.map(box => [box.id, prev[box.id] ?? defaultTier(box)])));
      setListError('');
    } catch (e) {
      setListError(errorText(e));
    }
  }, []);

  const poll = useCallback(async () => {
    const seq = ++pollSeq.current;
    try {
      const next = await loadStatus();
      if (seq === pollSeq.current) {
        setStatus(next);
        setNow(Date.now());
        setPolling(false);
      }
    } catch (e) {
      if (seq === pollSeq.current) setError(errorText(e));
    }
  }, []);

  useEffect(() => {
    let alive = true;
    void debugChecksEnabled().then(on => {
      if (!alive) return;
      setEnabled(on);
      if (on) {
        void refreshList();
        void poll();
      }
    });
    return () => { alive = false; };
  }, [refreshList, poll]);

  useEffect(() => {
    if (!active && !polling) return;
    const timer = setInterval(() => { void poll(); }, pollMs);
    return () => clearInterval(timer);
  }, [active, polling, poll, pollMs]);

  const wasActive = useRef(false);
  useEffect(() => {
    if (wasActive.current && !active) void refreshList();
    wasActive.current = active;
  }, [active, refreshList]);

  const start = async (kind: RunKind, box = '', tier = '', test = '', base = '') => {
    setError('');
    try {
      await startRun(kind, box, tier, test, base);
    } catch (e) {
      setError(errorText(e));
      return;
    }
    setPolling(true);
    await poll();
  };

  const cancel = async () => {
    try {
      await cancelRun();
      await poll();
    } catch (e) {
      setError(errorText(e));
    }
  };

  const showTests = async (box: CheckBox) => {
    setOpen(prev => ({ ...prev, [box.id]: true }));
    try {
      const next = await loadTests(box.id, tiers[box.id] ?? defaultTier(box));
      setTestLists(prev => ({ ...prev, [box.id]: next }));
    } catch (e) {
      setError(errorText(e));
    }
  };

  const tierFor = (box: CheckBox) => tiers[box.id] ?? defaultTier(box);
  const resultFor = (id: string): RunResult | undefined => run?.results.find(result => result.box === id);
  const canRun = !off && !active && enabled === true;

  const renderBox = (box: CheckBox) => {
    const last = box.last;
    const result = resultFor(box.id);
    const failing = result?.detail?.failures.length ? result.detail.failures : last.failing;
    const testList = testLists[box.id];
    const live = last.status === 'RUNNING' ? box.running : null;
    const isOpen = open[box.id] === true;
    const running = active && run !== null &&
      (run.kind === 'box' ? run.box === box.id : run.tests.some(test => test.box === box.id) && !result);
    return (
      <div key={box.id} className="dbg-box">
        <div className="dbg-box-row">
          <button className="dbg-toggle" disabled={failing.length === 0 && !result?.detail?.excerpt && !testList}
                  onClick={() => setOpen(prev => ({ ...prev, [box.id]: !isOpen }))}
                  title="Failing tests and the failure excerpt"
                  aria-label={`Details of ${box.id}`} aria-expanded={isOpen}>
            {isOpen ? <ChevronDown size={14} /> : <ChevronRight size={14} />}
          </button>
          <span className="dbg-id">{box.id}</span>
          <span className="dbg-title">{box.title}</span>
          <span className={`dbg-badge dbg-${(running ? 'RUNNING' : last.status).toLowerCase()}`}>
            {running ? 'RUNNING' : last.status}
          </span>
          <span className="dbg-meta">
            {last.status === 'NONE' ? 'never run' : live ? [
              live.phase,
              live.current,
              live.total > 0 ? `${live.passed + live.failed + live.skipped}/${live.total}` : '',
              last.at ? ageText(last.at, now) : '',
            ].filter(Boolean).join('  ') : [
              last.sha.slice(0, 7),
              `${last.passed}/${last.total}`,
              last.skip > 0 ? `${last.skip} skipped` : '',
              `${last.seconds}s`,
              last.at ? ageText(last.at, now) : '',
              last.error,
            ].filter(Boolean).join('  ')}
          </span>
          <select className="dbg-tier" value={tierFor(box)} disabled={!canRun} aria-label={`Tier of ${box.id}`}
                  onChange={e => setTiers(prev => ({ ...prev, [box.id]: e.target.value }))}>
            {box.tiers.map(tier => <option key={tier} value={tier}>{tier}</option>)}
          </select>
          <button className="dbg-rerun" disabled={!canRun || !tierFor(box)} title="List the tests of this tier" aria-label={`List tests of ${box.id}`}
                  onClick={() => void showTests(box)}>
            <List size={14} />
          </button>
          <button className="sidebar-action-btn" disabled={!canRun || !tierFor(box)}
                  onClick={() => void start('box', box.id, tierFor(box))}>
            <Play size={12} /> Run
          </button>
        </div>
        {isOpen && (
          <div className="dbg-detail">
            {failing.map(name => <div key={name} className="dbg-fail">{name}</div>)}
            {result?.detail?.excerpt && <pre className="dbg-excerpt">{result.detail.excerpt}</pre>}
            {testList && !testList.available && (
              <div className="dbg-hint">Tests are not listed: {testList.reason || 'unavailable'}</div>
            )}
            {testList?.available && testList.tests.map(name => (
              <div key={name} className="dbg-test">
                <span className="dbg-test-name">{name}</span>
                <button className="dbg-rerun" disabled={!canRun} title="Run only this test" aria-label={`Run test ${name}`}
                        onClick={() => void start('box', box.id, testList.tier, name)}>
                  <Play size={12} />
                </button>
              </div>
            ))}
          </div>
        )}
      </div>
    );
  };

  const renderTest = (test: RunTest) => {
    const box = test.box || run?.box || '';
    const found = list?.boxes.find(item => item.id === box);
    const tier = run?.kind === 'box' ? run.tier : found ? tierFor(found) : '';
    return (
      <div key={`${test.box}|${test.name}`} className="dbg-test">
        <span className={`dbg-badge dbg-${test.status.toLowerCase()}`}>{test.status}</span>
        <span className="dbg-test-name">{test.name}</span>
        <span className="dbg-meta">{test.seconds}s</span>
        <button className="dbg-rerun" disabled={!canRun || !box || !tier} title="Re-run only this test" aria-label={`Re-run test ${test.name}`}
                onClick={() => void start('box', box, tier, test.name)}>
          <RefreshCw size={12} />
        </button>
      </div>
    );
  };

  const groups = list ? groupBoxes(list.boxes) : [];
  const elapsed = run && run.startedMs > 0 ? Math.max(0, Math.round((now - run.startedMs) / 1000)) : 0;

  return (
    <div className="dbg-overlay" onClick={onClose}>
      <div className="dbg-modal glass-panel" role="dialog" aria-modal="true" aria-label="Debug checks" onClick={e => e.stopPropagation()}>
        <div className="dbg-header">
          <span className="dbg-title-main"><ListChecks size={16} /> Debug checks</span>
          <div className="dbg-actions">
            <button className="sidebar-action-btn" disabled={!canRun} onClick={() => void start('all')}>Run all</button>
            <input className="dbg-base" value={baseRef} placeholder="base (origin/nomad/dev)" disabled={!canRun}
                   onChange={e => setBaseRef(e.target.value.trim())} title="Compare against this ref for Run changed" />
            <button className="sidebar-action-btn" disabled={!canRun} onClick={() => void start('changed', '', '', '', baseRef)}>
              Run changed
            </button>
            <button className="sidebar-action-btn" disabled={!active} onClick={() => void cancel()}>
              <Square size={12} /> Cancel
            </button>
            <button className="sidebar-action-btn" disabled={off || active} onClick={() => void refreshList()}>
              <RefreshCw size={12} /> Refresh
            </button>
          </div>
          <button className="dbg-close" onClick={onClose} title="Close" aria-label="Close debug checks"><X size={16} /></button>
        </div>

        {off && (
          <div className="dbg-warn">Debug checks are off: start FO4IDE with FO4IDE_DEBUG_CHECKS=1.</div>
        )}
        {status && !status.scriptFound && !off && (
          <div className="dbg-warn">rdf.ps1 not found at {status.script}. Set RDF_SCRIPT to its full path.</div>
        )}
        {listError && <div className="dbg-warn">{listError}</div>}
        {error && <div className="dbg-warn">{error}</div>}

        <div className="dbg-body">
          {run && run.kind !== '' && (
            <div className="dbg-run">
              <div className="dbg-run-head">
                {active ? 'Running' : run.cancelled ? 'Cancelled' : 'Last run'}: {run.kind}
                {run.box ? ` ${run.box}` : ''}{run.tier ? `/${run.tier}` : ''}{run.test ? ` (${run.test})` : ''}
                {run.phase ? ` - ${run.phase}` : ''}
                {run.startedMs > 0 ? ` - ${elapsed}s` : ''}
                {!active && run.exitCode >= 0 ? ` - exit ${run.exitCode}` : ''}
              </div>
              {run.planSeen && (
                <div className="dbg-result">
                  {run.plan.length > 0 ? `Plan: ${run.plan.join(', ')}` : 'No checks affected.'}
                  {run.unknown ? ' (the change could not be mapped, so every box runs)' : ''}
                </div>
              )}
              {run.error && <div className="dbg-warn">{run.error}</div>}
              {run.results.map(result => (
                <div key={`${result.box}|${result.resultPath}`} className="dbg-result">{result.line}</div>
              ))}
              {run.tests.map(renderTest)}
              {run.log.length > 0 && <pre className="dbg-log">{run.log.join('\n')}</pre>}
            </div>
          )}

          {groups.map(([tag, boxes]) => (
            <div key={tag} className="dbg-group">
              <div className="dbg-group-title">{tag}</div>
              {boxes.map(renderBox)}
            </div>
          ))}
          {enabled === true && list && list.boxes.length === 0 && <div className="dbg-hint">rdf lists no boxes.</div>}
        </div>

        <div className="dbg-footer">
          {list?.resultsRoot ? `Results folder: ${list.resultsRoot}` : 'Results are written by rdf.ps1 on the data host.'}
        </div>
      </div>
    </div>
  );
}

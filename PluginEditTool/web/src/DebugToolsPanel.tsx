import { useCallback, useEffect, useState } from 'react';
import { Wrench, X, Play, Square, RefreshCw, ChevronDown, ChevronRight, ListChecks, Search } from 'lucide-react';
import {
  cancelRun, debugToolsProblem, loadContext, outputDirProblem, loadList, startRun,
  type ToolInfo, type ToolList,
} from './debugToolsClient';
import { getArchive } from './backend';
import { useSuiteGate } from './useSuiteGate';
import { useToolRuns } from './useToolRuns';
import { filterGroups, isDryRun, planSuite, runQueue } from './debugToolsSuite';
import {
  BANNER, CANCELLED_NOTE, aggregateVerdict, flattenRuns, redactText, runVerdict, sessionReport, summariseRuns,
  type DiagContext, type DiagReportInput, type NotRun, type RunRecord,
} from './diagnosticModel';
import { ContextCard, Inventory, ReportActions, ResultList, RunDot, SummaryLine, VerdictBadge } from './DebugToolsResults';
import { errorText } from './errors';
import './DebugChecksPanel.css';
import './DebugToolsPanel.css';

type Tab = 'tools' | 'history' | 'inventory';
type HistorySel = { kind: 'run'; runId: string } | { kind: 'suite'; id: string } | null;

interface SuiteRun {
  id: string;
  label: string;
  at: string;
  records: RunRecord[];
  notRun: NotRun[];
  stopped: boolean;
}

interface SuiteLive {
  label: string;
  total: number;
  index: number;
  title: string;
  stopping: boolean;
}

const SUITE_STOPPED_NOTE = 'Suite was cancelled: the tools after the stop did not run';

export default function DebugToolsPanel({ onClose, onOpenChecks, pollMs = 1000 }: {
  onClose: () => void; onOpenChecks?: () => void; pollMs?: number;
}) {
  const [problem, setProblem] = useState<string | null>(null);
  const hostOk = problem === '';
  const [context, setContext] = useState<DiagContext | null>(null);
  const [list, setList] = useState<ToolList | null>(null);
  const [tab, setTab] = useState<Tab>('tools');
  const [closed, setClosed] = useState<Record<string, boolean>>({});
  const [values, setValues] = useState<Record<string, string>>({});
  const [outDirs, setOutDirs] = useState<Record<string, string>>({});
  const [query, setQuery] = useState('');
  const [selectedId, setSelectedId] = useState('');
  const [includeExpensive, setIncludeExpensive] = useState(false);
  const [suites, setSuites] = useState<SuiteRun[]>([]);
  const [suiteLive, setSuiteLive] = useState<SuiteLive | null>(null);
  const [historySel, setHistorySel] = useState<HistorySel>(null);
  const [error, setError] = useState('');
  const [contextError, setContextError] = useState('');
  const [listError, setListError] = useState('');
  const suiteGate = useSuiteGate();
  const { status, history, setPolling, poll, waitFor, rememberTools } = useToolRuns({
    pollMs, currentSuiteId: suiteGate.currentId, setError,
  });
  const active = status?.active === true;
  const busy = active || suiteLive !== null;

  const loadAll = useCallback(async () => {
    const [ctx, tools] = await Promise.allSettled([loadContext(), loadList()]);
    if (ctx.status === 'fulfilled') { setContext(ctx.value); setContextError(''); } else setContextError(errorText(ctx.reason));
    if (tools.status === 'fulfilled') {
      rememberTools(tools.value.groups);
      setList(tools.value);
      setListError('');
    } else setListError(errorText(tools.reason));
  }, [rememberTools]);

  const [probeTick, setProbeTick] = useState(0);
  useEffect(() => {
    let alive = true;
    void debugToolsProblem().then(found => {
      if (!alive) return;
      setProblem(found);
      if (found === '') void loadAll().then(() => poll());
    });
    return () => { alive = false; };
  }, [loadAll, poll, probeTick]);

  const { requestStop } = suiteGate;
  useEffect(() => requestStop, [requestStop]);

  const valueOf = (tool: ToolInfo, name: string) => (values[`${tool.id}|${name}`] ?? '').trim();
  const outDirOf = (tool: ToolInfo) => (outDirs[tool.id] ?? '').trim();

  // Constraint: a writing tool never starts without a typed absolute output folder, and never into the Data folder.
  const blockReason = (tool: ToolInfo): string => {
    if (!tool.available) return tool.unavailableReason || 'unavailable';
    const missing = tool.params.find(param => param.required && !valueOf(tool, param.name));
    if (missing) return `required: ${missing.label}`;
    if (tool.writes && !tool.needsOutputDir) return 'this writing tool offers no output folder';
    if (tool.needsOutputDir) {
      if (!outDirOf(tool)) return 'type an output folder';
      return outputDirProblem(outDirOf(tool), context?.dataDir ?? '');
    }
    return '';
  };

  const startTool = async (tool: ToolInfo): Promise<string> => {
    const params: Record<string, string> = {};
    for (const param of tool.params) {
      const value = valueOf(tool, param.name);
      if (value) params[param.name] = value;
    }
    if (tool.needsOutputDir) params.outputDir = outDirOf(tool);
    const runId = await startRun(tool.id, params);
    setPolling(true);
    void poll();
    return runId;
  };

  const start = async (tool: ToolInfo) => {
    setError('');
    try {
      await startTool(tool);
    } catch (e) {
      setError(errorText(e));
    }
  };

  const cancel = async () => {
    try {
      await cancelRun();
      await poll();
    } catch (e) {
      setError(errorText(e));
    }
  };

  const runSuite = async (label: string, groupTools: ToolInfo[]) => {
    if (busy) return;
    const id = suiteGate.begin();
    if (!id) return;
    const plan = planSuite(groupTools, blockReason, includeExpensive);
    let index = 0;
    setError('');
    setSuiteLive({ label, total: plan.tools.length, index: 0, title: '', stopping: false });
    const outcome = await runQueue(plan.tools, {
      stopped: suiteGate.stopped,
      start: async tool => {
        index += 1;
        setSuiteLive(prev => prev && { ...prev, index, title: tool.title });
        return waitFor(await startTool(tool));
      },
    });
    suiteGate.end();
    setSuites(prev => [{ id, label, at: new Date().toISOString(), records: outcome.records, notRun: [...plan.notRun, ...outcome.notRun], stopped: outcome.stopped }, ...prev]);
    setSuiteLive(null);
    setHistorySel({ kind: 'suite', id });
    setTab('history');
  };

  const stopSuite = async () => {
    suiteGate.requestStop();
    setSuiteLive(prev => prev && { ...prev, stopping: true });
    if (active && runTool?.cancellable) await cancel();
  };

  const groups = list?.groups ?? [];
  const tools = groups.flatMap(group => group.tools);
  const shown = filterGroups(groups, query);
  const filtering = query.trim() !== '';
  const selected = tools.find(tool => tool.id === selectedId) ?? null;
  const runTool = tools.find(tool => tool.id === status?.toolId);
  const latestOf = (toolId: string) => history.find(run => run.toolId === toolId) ?? null;
  const oldestFirst = [...history].reverse();

  const runInput = (run: RunRecord): DiagReportInput =>
    ({ context, results: run.results, toolId: run.toolId, error: run.error, cancelled: run.cancelled });
  const suiteInput = (suite: SuiteRun) => sessionReport(context, suite.records, suite.notRun, `suite ${suite.label}`);
  const sessionInput = () => sessionReport(context, oldestFirst, suites.flatMap(suite => suite.notRun), 'session');

  const browseOutputDir = async (tool: ToolInfo) => {
    try {
      const dir = await getArchive()?.BrowseForFolder('Choose an output folder');
      if (dir) setOutDirs(prev => ({ ...prev, [tool.id]: dir }));
    } catch (e) {
      setError(errorText(e));
    }
  };

  const renderParam = (tool: ToolInfo, param: ToolInfo['params'][number]) => {
    const key = `${tool.id}|${param.name}`;
    const set = (value: string) => setValues(prev => ({ ...prev, [key]: value }));
    const plugins = param.kind === 'plugin' ? context?.loadOrder ?? [] : [];
    return (
      <div key={param.name} className="dt-param">
        <span className="dbg-meta">{param.required ? `${param.label} *` : param.label}</span>
        {plugins.length > 0 ? (
          <select className="dbg-tier" aria-label={param.label} value={values[key] ?? ''} onChange={e => set(e.target.value)}>
            <option value="">(choose a plugin)</option>
            {plugins.map(name => <option key={name} value={name}>{name}</option>)}
          </select>
        ) : (
          <input className="dbg-base dt-input" aria-label={param.label} value={values[key] ?? ''}
                 placeholder={param.kind} onChange={e => set(e.target.value)} />
        )}
      </div>
    );
  };

  const toolBadges = (tool: ToolInfo) => (
    <>
      {tool.readOnly && <span className="dbg-badge dt-badge-readonly">READ-ONLY</span>}
      {tool.writes && <span className="dbg-badge dt-badge-writes">WRITES</span>}
      {isDryRun(tool) && <span className="dbg-badge dt-badge-dry">DRY-RUN</span>}
      {tool.expensive && <span className="dbg-badge dt-badge-expensive">EXPENSIVE</span>}
      {!tool.available && <span className="dbg-badge dbg-error">UNAVAILABLE</span>}
    </>
  );

  const renderRun = (run: RunRecord, heading: string) => (
    <div className="dt-run-view">
      <div className="dbg-box-row">
        <span className="dbg-title">{`${heading}: ${redactText(run.title)} (run ${run.runId})`}</span>
        <VerdictBadge verdict={runVerdict(run)} />
        <span className="dbg-meta">{new Date(run.at).toLocaleTimeString()}</span>
        <ReportActions scope="report" base={`fo4ide-${run.toolId || 'diagnostics'}`} build={() => runInput(run)} onError={setError} />
      </div>
      {run.cancelled && <div className="dt-banner" role="status">{CANCELLED_NOTE}</div>}
      {run.error && <div className="dbg-warn">{redactText(run.error)}</div>}
      {run.results.length > 0 && <ResultList results={run.results} />}
    </div>
  );

  const renderTool = (tool: ToolInfo) => {
    const block = blockReason(tool);
    const out = outDirOf(tool);
    const last = latestOf(tool.id);
    return (
      <div className="dt-tool">
        <div className="dbg-box-row">
          <span className="dt-tool-title">{tool.title}</span>
          {toolBadges(tool)}
          {tool.needsOutputDir && <span className="dbg-meta">output folder required</span>}
          <button className="sidebar-action-btn" disabled={busy || block !== ''} aria-label={`Run ${tool.title}`}
                  title={busy ? 'Another run is active' : block} onClick={() => void start(tool)}>
            <Play size={12} /> Run
          </button>
        </div>
        <div className="dt-desc">{tool.description}</div>
        {tool.evidence.length > 0 && <div className="dt-desc dbg-meta">{`Evidence: ${tool.evidence.join(', ')}`}</div>}
        {tool.params.map(param => renderParam(tool, param))}
        {tool.needsOutputDir && (
          <div className="dt-param">
            <span className="dbg-meta">Output folder *</span>
            <input className="dbg-base dt-input" aria-label="Output folder" value={outDirs[tool.id] ?? ''} placeholder="dir"
                   onChange={e => setOutDirs(prev => ({ ...prev, [tool.id]: e.target.value }))} />
            {getArchive() && <button className="sidebar-action-btn" onClick={() => void browseOutputDir(tool)}>Browse...</button>}
          </div>
        )}
        {tool.needsOutputDir && out && (
          <div className="dt-dest">{redactText(`will create debugtools-<time> inside: ${out} (folder must exist)`)}</div>
        )}
        {block && <div className="dbg-hint">{`Run disabled: ${block}`}</div>}
        {last ? renderRun(last, 'Last run') : <div className="dbg-hint">Not run yet this session.</div>}
      </div>
    );
  };

  const renderTree = () => (
    <div className="dt-left" role="navigation" aria-label="Tool tree">
      <div className="dt-search">
        <Search size={13} />
        <input aria-label="Filter tools" placeholder="Filter tools" value={query} onChange={e => setQuery(e.target.value)} />
        {filtering && <button className="dbg-close" aria-label="Clear filter" onClick={() => setQuery('')}><X size={12} /></button>}
      </div>
      <div className="dt-suite-bar">
        <button className="sidebar-action-btn" aria-label="Run suite all" disabled={busy || !hostOk || tools.length === 0}
                title="Run every read-only tool one after another" onClick={() => void runSuite('all read-only tools', tools)}>
          <Play size={12} /> Run suite: all
        </button>
        <label className="dbg-meta">
          <input type="checkbox" checked={includeExpensive} onChange={e => setIncludeExpensive(e.target.checked)} />
          Include expensive tools
        </label>
      </div>
      {shown.map(group => {
        const open = filtering || closed[group.id] !== true;
        return (
          <div key={group.id} className="dbg-group">
            <div className="dt-group-head">
              <button className="dt-group-toggle" aria-expanded={open} aria-label={`Group ${group.title}`}
                      onClick={() => setClosed(prev => ({ ...prev, [group.id]: open }))}>
                {open ? <ChevronDown size={14} /> : <ChevronRight size={14} />}
                <span className="dbg-group-title">{group.title}</span>
              </button>
              <button className="sidebar-action-btn" aria-label={`Run suite ${group.title}`} disabled={busy}
                      title="Run the read-only tools of this group one after another"
                      onClick={() => void runSuite(group.title, groups.find(entry => entry.id === group.id)?.tools ?? [])}>
                Suite
              </button>
            </div>
            {open && group.tools.map(tool => {
              const last = latestOf(tool.id);
              return (
                <button key={tool.id} className={`dt-tree-item${tool.id === selectedId ? ' selected' : ''}`}
                        aria-current={tool.id === selectedId} onClick={() => { setSelectedId(tool.id); setTab('tools'); }}>
                  <RunDot verdict={last ? runVerdict(last) : null} />
                  <span className="dt-tree-title">{tool.title}</span>
                  {toolBadges(tool)}
                </button>
              );
            })}
          </div>
        );
      })}
      {hostOk && list && tools.length === 0 && <div className="dbg-hint">The host lists no tools.</div>}
      {tools.length > 0 && shown.length === 0 && <div className="dbg-hint">No tools match the filter.</div>}
    </div>
  );

  const renderHistoryList = () => (
    <div className="dt-left" role="navigation" aria-label="Run history">
      {history.length === 0 && suites.length === 0 && <div className="dbg-hint">No runs yet this session.</div>}
      {suites.length > 0 && <div className="dbg-group-title">Suites</div>}
      {suites.map(suite => (
        <button key={suite.id} className={`dt-tree-item${historySel?.kind === 'suite' && historySel.id === suite.id ? ' selected' : ''}`}
                aria-label={`Open suite: ${suite.label}`} onClick={() => setHistorySel({ kind: 'suite', id: suite.id })}>
          <VerdictBadge verdict={aggregateVerdict(suite.records, suite.notRun)} />
          <span className="dt-tree-title">{`Suite: ${suite.label}`}</span>
        </button>
      ))}
      {history.length > 0 && <div className="dbg-group-title">Runs</div>}
      {history.map(run => (
        <button key={run.runId} className={`dt-tree-item${historySel?.kind === 'run' && historySel.runId === run.runId ? ' selected' : ''}`}
                aria-label={`Open run ${run.runId}: ${run.title}`} onClick={() => setHistorySel({ kind: 'run', runId: run.runId })}>
          <VerdictBadge verdict={runVerdict(run)} />
          <span className="dt-tree-title">{run.title}</span>
          <span className="dbg-meta">{new Date(run.at).toLocaleTimeString()}</span>
        </button>
      ))}
    </div>
  );

  const renderSuite = (suite: SuiteRun) => {
    const sum = summariseRuns(suite.records, suite.notRun);
    const v = sum.verdicts;
    return (
      <div className="dt-suite-view">
        <div className="dbg-box-row">
          <span className="dbg-title">{`Suite: ${redactText(suite.label)}`}</span>
          <VerdictBadge verdict={aggregateVerdict(suite.records, suite.notRun)} />
          <ReportActions scope="suite report" base={`fo4ide-suite-${suite.label}`} build={() => suiteInput(suite)} onError={setError} />
        </div>
        {suite.stopped && <div className="dt-banner" role="status">{SUITE_STOPPED_NOTE}</div>}
        <div className="dt-banner" role="note">{BANNER}</div>
        <div className="dbg-meta">{`Tools run: ${sum.runs} | Not run: ${sum.notRun} (never counted as passed)`}</div>
        <div className="dbg-meta">
          {`Runs: pass ${v.pass} | pass (structural only) ${v.structural} | fail ${v.fail} | cancelled ${v.cancelled} | unverified ${v.unverified} | incomplete ${v.incomplete} | no results ${v.empty}`}
        </div>
        <SummaryLine results={flattenRuns(suite.records)} />
        <div className="dbg-group-title">{`Runs (${suite.records.length})`}</div>
        {suite.records.map(run => (
          <button key={run.runId} className="dt-tree-item" aria-label={`Show result of ${run.title}`}
                  onClick={() => setHistorySel({ kind: 'run', runId: run.runId })}>
            <VerdictBadge verdict={runVerdict(run)} />
            <span className="dt-tree-title">{run.title}</span>
          </button>
        ))}
        {suite.notRun.length > 0 && <div className="dbg-group-title">{`Not run (${suite.notRun.length})`}</div>}
        <ul className="dt-notrun">
          {suite.notRun.map(item => <li key={item.toolId}>{redactText(`${item.title}: ${item.reason}`)}</li>)}
        </ul>
      </div>
    );
  };

  const renderHistoryView = () => {
    if (historySel?.kind === 'suite') {
      const suite = suites.find(entry => entry.id === historySel.id);
      if (suite) return renderSuite(suite);
    }
    if (historySel?.kind === 'run') {
      const run = history.find(entry => entry.runId === historySel.runId);
      if (run) return renderRun(run, 'Run');
    }
    return <div className="dbg-hint">Pick a run or a suite to reopen its result.</div>;
  };

  const renderRunBar = () => (
    <div className="dt-runbar" aria-label="Run bar">
      <div className="dbg-run-head">
        {suiteLive ? `Suite ${suiteLive.label}: tool ${suiteLive.index} of ${suiteLive.total}` : 'Running'}: {runTool?.title ?? status?.toolId ?? suiteLive?.title}
        <button className="sidebar-action-btn dt-run-btn" disabled={!active || runTool?.cancellable !== true} onClick={() => void cancel()}>
          <Square size={12} /> Cancel
        </button>
        {suiteLive && (
          <button className="sidebar-action-btn dt-run-btn" disabled={suiteLive.stopping} onClick={() => void stopSuite()}>Stop suite</button>
        )}
      </div>
      {suiteLive?.stopping && <div className="dbg-hint">Stopping after the current tool finishes.</div>}
      {active && status && (
        <div className="dbg-result">
          {redactText(status.progress.text)}{status.progress.total > 0 ? ` (${status.progress.done}/${status.progress.total})` : ''}
          {status.progress.total > 0 && <progress className="dt-progress" value={status.progress.done} max={status.progress.total} />}
        </div>
      )}
    </div>
  );

  return (
    <div className="dt-workspace" role="region" aria-label="Debug tools">
      <div className="dbg-header">
        <span className="dbg-title-main"><Wrench size={16} /> Debug tools</span>
        <div className="dbg-actions" role="tablist">
          <button className="sidebar-action-btn" role="tab" aria-selected={tab === 'tools'} onClick={() => setTab('tools')}>Tools</button>
          <button className="sidebar-action-btn" role="tab" aria-selected={tab === 'history'} onClick={() => setTab('history')}>
            {`Run history (${history.length})`}
          </button>
          <button className="sidebar-action-btn" role="tab" aria-selected={tab === 'inventory'} onClick={() => setTab('inventory')}>
            Capability inventory
          </button>
        </div>
        <ReportActions scope="session report" base="fo4ide-session" build={sessionInput} disabled={history.length === 0} onError={setError} />
        <div className="dbg-actions">
          {onOpenChecks && (
            <button className="sidebar-action-btn" onClick={onOpenChecks}><ListChecks size={12} /> Debug checks</button>
          )}
          <button className="sidebar-action-btn" disabled={!hostOk || busy} onClick={() => void loadAll()}>
            <RefreshCw size={12} /> Refresh
          </button>
        </div>
        <button className="dbg-close" onClick={onClose} title="Close" aria-label="Close debug tools"><X size={16} /></button>
      </div>

      {problem && (
        <div className="dbg-warn">
          {redactText(`Debug tools are not available: ${problem}`)}
          <button className="sidebar-action-btn" onClick={() => setProbeTick(tick => tick + 1)}>Retry</button>
        </div>
      )}
      {contextError && <div className="dbg-warn">{redactText(contextError)}</div>}
      {listError && <div className="dbg-warn">{redactText(listError)}</div>}
      {error && <div className="dbg-warn">{redactText(error)}</div>}

      {hostOk && context && <ContextCard context={context} />}
      {(active || suiteLive) && renderRunBar()}

      {tab === 'inventory' ? (
        <div className="dt-page">{list && <Inventory rows={list.inventory} />}</div>
      ) : (
        <div className="dt-main">
          {tab === 'tools' ? renderTree() : renderHistoryList()}
          <div className="dt-centre" role="region" aria-label={tab === 'tools' ? 'Selected tool' : 'Run result'}>
            {tab === 'tools'
              ? (selected ? renderTool(selected) : <div className="dbg-hint">Pick a tool in the tree to set its parameters and run it, or use Run suite.</div>)
              : renderHistoryView()}
          </div>
        </div>
      )}
    </div>
  );
}

import { useCallback, useEffect, useRef, useState } from 'react';
import { loadStatus, type ToolGroup, type ToolInfo, type ToolStatus } from './debugToolsClient';
import type { RunRecord } from './diagnosticModel';
import { errorText } from './errors';

// Constraint: the host keeps the last run after it finishes, so a panel that first sees it later must stamp it with the host's finish time, not the time it looked.
function finishedAtOrNow(finishedAt: string | undefined): string {
  const parsed = finishedAt ? Date.parse(finishedAt) : Number.NaN;
  return Number.isNaN(parsed) ? new Date().toISOString() : new Date(parsed).toISOString();
}

export function useToolRuns({ pollMs, currentSuiteId, setError }: {
  pollMs: number; currentSuiteId: () => string; setError: (message: string) => void;
}) {
  const [status, setStatus] = useState<ToolStatus | null>(null);
  const [history, setHistory] = useState<RunRecord[]>([]);
  const [polling, setPolling] = useState(false);
  // Invariant: only the newest poll may write status, so a slow older answer never replaces a newer one.
  const pollSeq = useRef(0);
  const toolsRef = useRef<{ tool: ToolInfo; groupId: string }[]>([]);
  const recorded = useRef(new Map<string, RunRecord>());
  const waiters = useRef(new Map<string, (record: RunRecord | null) => void>());
  const inFlight = useRef(0);

  const rememberTools = useCallback((groups: ToolGroup[]) => {
    toolsRef.current = groups.flatMap(group => group.tools.map(tool => ({ tool, groupId: group.id })));
  }, []);

  // Invariant: a finished run is recorded once, keyed by runId, however many pollers see it.
  const recordIfDone = useCallback((next: ToolStatus) => {
    if (next.active || !next.toolId || !next.runId || next.runId === '0' || recorded.current.has(next.runId)) return;
    const info = toolsRef.current.find(entry => entry.tool.id === next.toolId);
    const record: RunRecord = {
      runId: next.runId, toolId: next.toolId, title: info?.tool.title ?? next.toolId, groupId: info?.groupId ?? '',
      at: finishedAtOrNow(next.finishedAt), suiteId: currentSuiteId(), results: next.results, error: next.error, cancelled: next.cancelled,
    };
    recorded.current.set(next.runId, record);
    setHistory(prev => [record, ...prev]);
    waiters.current.get(next.runId)?.(record);
    waiters.current.delete(next.runId);
  }, [currentSuiteId]);

  const poll = useCallback(async () => {
    const seq = ++pollSeq.current;
    inFlight.current += 1;
    try {
      const next = await loadStatus();
      if (seq === pollSeq.current) {
        setStatus(next);
        setError('');
        setPolling(false);
        recordIfDone(next);
        if (!next.active) {
          waiters.current.forEach((resolve, runId) => {
            if (runId !== next.runId) { resolve(null); waiters.current.delete(runId); }
          });
        }
      }
    } catch (e) {
      if (seq === pollSeq.current) setError(errorText(e));
    } finally {
      inFlight.current -= 1;
    }
  }, [recordIfDone, setError]);

  useEffect(() => {
    const pending = waiters.current;
    return () => {
      pending.forEach(resolve => resolve(null));
      pending.clear();
    };
  }, []);

  const running = status?.active === true;
  useEffect(() => {
    if (!running && !polling) return;
    const timer = setInterval(() => { if (inFlight.current === 0) void poll(); }, pollMs);
    return () => clearInterval(timer);
  }, [running, polling, poll, pollMs]);

  // Invariant: the recorded-map check and the waiter registration are one synchronous step, so a fast run is never missed.
  const waitFor = useCallback((runId: string) => new Promise<RunRecord | null>(resolve => {
    const hit = recorded.current.get(runId);
    if (hit) { resolve(hit); return; }
    waiters.current.set(runId, resolve);
    void poll();
  }), [poll]);

  return { status, history, polling, setPolling, poll, waitFor, rememberTools };
}

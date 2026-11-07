import { useEffect, useRef, useState } from 'react';
import { getBackend, type Backend } from '../../backend';
import { errorText } from '../../errors';
import type { FriendlyMessage } from '../friendlyErrors';
import { readQuestScope, type QuestSnapshot } from './questBackend';
import { CHECK_LIMIT, localFindings, nativeFindings, parseCheckRows, sortFindings, type CheckFinding } from './questChecks';

export const BACKEND_TIMEOUT_MS = 60_000;
export const TIMEOUT_TEXT = 'The check did not finish. The backend did not answer.';
const FAILED_TEXT = 'Checking this quest failed.';

class BackendTimeout extends Error {}

export interface CheckResult {
  findings: CheckFinding[];
  checkedAt: Date;
  snapshot: QuestSnapshot;
  backendDone: boolean;
}

async function remoteFindings(b: Backend, plugin: string, formKey: string): Promise<CheckFinding[]> {
  const [raw, scope] = await Promise.all([b.CheckQuest(plugin, formKey, CHECK_LIMIT), readQuestScope(b, plugin, formKey)]);
  return nativeFindings(parseCheckRows(raw), scope);
}

export function useQuestCheck(plugin: string, formKey: string, snapshot: QuestSnapshot | null) {
  const backend = getBackend();
  const [result, setResult] = useState<CheckResult | null>(null);
  const [running, setRunning] = useState(false);
  const [error, setError] = useState<FriendlyMessage | null>(null);
  const request = useRef(0);
  const timer = useRef<ReturnType<typeof setTimeout> | undefined>(undefined);

  useEffect(() => () => {
    ++request.current;
    clearTimeout(timer.current);
  }, []);

  const run = () => {
    if (!backend || !snapshot) return;
    const id = ++request.current;
    const checked = snapshot;
    const local = localFindings(checked);
    clearTimeout(timer.current);
    setRunning(true);
    setError(null);
    setResult({ findings: sortFindings(local), checkedAt: new Date(), snapshot: checked, backendDone: false });
    const deadline = new Promise<never>((_, reject) => {
      timer.current = setTimeout(() => reject(new BackendTimeout()), BACKEND_TIMEOUT_MS);
    });
    Promise.race([remoteFindings(backend, plugin, formKey), deadline]).then(remote => {
      if (id !== request.current) return;
      clearTimeout(timer.current);
      setResult({ findings: sortFindings([...local, ...remote]), checkedAt: new Date(), snapshot: checked, backendDone: true });
      setRunning(false);
    }, (e: unknown) => {
      if (id !== request.current) return;
      clearTimeout(timer.current);
      setResult({ findings: sortFindings(local), checkedAt: new Date(), snapshot: checked, backendDone: false });
      setError(e instanceof BackendTimeout ? { text: TIMEOUT_TEXT } : { text: FAILED_TEXT, details: errorText(e) });
      setRunning(false);
    });
  };

  return { run, result, running, error, outOfDate: result !== null && result.snapshot !== snapshot };
}

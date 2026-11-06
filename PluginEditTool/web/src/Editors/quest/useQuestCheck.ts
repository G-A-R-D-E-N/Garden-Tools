import { useRef, useState } from 'react';
import { getBackend } from '../../backend';
import { errorText } from '../../errors';
import type { FriendlyMessage } from '../friendlyErrors';
import { readQuestScope, type QuestSnapshot } from './questBackend';
import { CHECK_LIMIT, localFindings, nativeFindings, parseCheckRows, sortFindings, type CheckFinding } from './questChecks';

export interface CheckResult {
  findings: CheckFinding[];
  checkedAt: Date;
  snapshot: QuestSnapshot;
}

export function useQuestCheck(plugin: string, formKey: string, snapshot: QuestSnapshot | null) {
  const backend = getBackend();
  const [result, setResult] = useState<CheckResult | null>(null);
  const [running, setRunning] = useState(false);
  const [error, setError] = useState<FriendlyMessage | null>(null);
  const request = useRef(0);

  const run = () => {
    if (!backend || !snapshot) return;
    const id = ++request.current;
    const checked = snapshot;
    setRunning(true);
    setError(null);
    Promise.all([backend.CheckForErrors(plugin, CHECK_LIMIT), readQuestScope(backend, plugin, formKey)]).then(([raw, scope]) => {
      if (id !== request.current) return;
      const findings = sortFindings([...localFindings(checked), ...nativeFindings(parseCheckRows(raw), scope)]);
      setResult({ findings, checkedAt: new Date(), snapshot: checked });
      setRunning(false);
    }).catch((e: unknown) => {
      if (id !== request.current) return;
      setResult(null);
      setError({ text: 'Checking this quest failed.', details: errorText(e) });
      setRunning(false);
    });
  };

  return { run, result, running, error, outOfDate: result !== null && result.snapshot !== snapshot };
}

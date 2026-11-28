import type { ToolGroup, ToolInfo } from './debugToolsClient';
import type { NotRun, RunRecord } from './diagnosticModel';
import { errorText } from './errors';

export const isDryRun = (tool: ToolInfo): boolean => /dry[\s._-]?run/i.test(`${tool.id} ${tool.title}`);

export function filterGroups(groups: ToolGroup[], query: string): ToolGroup[] {
  const needle = query.trim().toLowerCase();
  if (!needle) return groups;
  const has = (value: string) => value.toLowerCase().includes(needle);
  return groups
    .map(group => (has(group.title) || has(group.id) ? group
      : { ...group, tools: group.tools.filter(tool => has(tool.id) || has(tool.title) || has(tool.description)) }))
    .filter(group => group.tools.length > 0);
}

export interface SuitePlan {
  tools: ToolInfo[];
  notRun: NotRun[];
}

// Constraint: a suite runs only read-only tools with no output folder, so it can never write.
export function planSuite(tools: ToolInfo[], blockReason: (tool: ToolInfo) => string, includeExpensive: boolean): SuitePlan {
  const plan: SuitePlan = { tools: [], notRun: [] };
  for (const tool of tools) {
    const skip = (reason: string) => plan.notRun.push({ toolId: tool.id, title: tool.title, reason });
    if (tool.writes || !tool.readOnly) skip('writes or is not read-only: a suite never runs it');
    else if (tool.needsOutputDir) skip('needs an output folder: run it by hand');
    else if (blockReason(tool)) skip(blockReason(tool));
    else if (tool.expensive && !includeExpensive) skip('expensive: tick "Include expensive tools" to run it');
    else plan.tools.push(tool);
  }
  return plan;
}

export interface QueueHooks {
  start: (tool: ToolInfo) => Promise<RunRecord | null>;
  stopped: () => boolean;
}

export interface QueueOutcome {
  records: RunRecord[];
  notRun: NotRun[];
  stopped: boolean;
}

// Invariant: strictly one tool at a time; a stop, a cancelled run or a missing report ends the queue and the rest is not run.
export async function runQueue(tools: ToolInfo[], hooks: QueueHooks): Promise<QueueOutcome> {
  const outcome: QueueOutcome = { records: [], notRun: [], stopped: false };
  const rest = (from: number, reason: string, byStop = false) => {
    const left = tools.slice(from);
    for (const tool of left) outcome.notRun.push({ toolId: tool.id, title: tool.title, reason });
    if (byStop && left.length > 0) outcome.stopped = true;
  };
  for (let i = 0; i < tools.length; i += 1) {
    if (hooks.stopped()) { rest(i, 'the queue was cancelled before this tool started', true); break; }
    let record: RunRecord | null;
    try {
      record = await hooks.start(tools[i]);
    } catch (e) {
      outcome.notRun.push({ toolId: tools[i].id, title: tools[i].title, reason: `could not start: ${errorText(e)}` });
      continue;
    }
    if (!record) { rest(i, 'the run did not report back, so the queue stopped'); break; }
    outcome.records.push(record);
    if (record.cancelled) { rest(i + 1, 'the previous tool was cancelled, so the queue stopped', true); break; }
  }
  return outcome;
}

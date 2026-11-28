export type LogSeverity = 'error' | 'warn' | 'ok';

const WARN_PATTERN = /missing|not found|unavailable|skipped|cancelled|ignored|chosen|[1-9]\d* failed/i;
const BLOCKED_PREFIX = /(^|\] )x /;

export function logSeverity(line: string): LogSeverity {
  if (line.includes('✗')) return 'error';
  if (line.includes('ℹ')) return 'ok';
  if (line.includes('⚠') || BLOCKED_PREFIX.test(line) || WARN_PATTERN.test(line)) return 'warn';
  return 'ok';
}

const RANK: Record<LogSeverity, number> = { ok: 0, warn: 1, error: 2 };

export function raiseSeverity(current: LogSeverity | null, next: LogSeverity): LogSeverity | null {
  if (next === 'ok') return current;
  return current !== null && RANK[current] >= RANK[next] ? current : next;
}

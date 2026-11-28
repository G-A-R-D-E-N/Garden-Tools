export interface FlagOption {
  name: string;
  bit: number;
  editable: boolean;
}

export interface FlagCheckbox {
  bit: number;
  name: string;
  checked: boolean;
  editable: boolean;
}

export interface LockDraft {
  level: number;
  key: string | null;
  flags: number;
}

export type LockChange = LockDraft | null;

export const LOCK_LEVELS: { level: number; name: string }[] = [
  { level: 0, name: 'None' },
  { level: 1, name: 'Novice 1' },
  { level: 25, name: 'Novice 25' },
  { level: 50, name: 'Advanced' },
  { level: 75, name: 'Expert' },
  { level: 100, name: 'Master' },
  { level: 251, name: 'Barred' },
  { level: 252, name: 'Chained' },
  { level: 253, name: 'Requires Terminal' },
  { level: 254, name: 'Inaccessible' },
  { level: 255, name: 'Requires Key' },
];

export function lockLevelLabel(level: number): string {
  return LOCK_LEVELS.find(entry => entry.level === level)?.name ?? `Level ${level}`;
}

export function flagMask(bit: number): number {
  return bit >= 0 && bit < 32 ? (1 << bit) >>> 0 : 0;
}

export function flagIsSet(raw: number, bit: number): boolean {
  return (raw & flagMask(bit)) !== 0;
}

export function withFlag(raw: number, bit: number, on: boolean): number {
  const mask = flagMask(bit);
  return (on ? raw | mask : raw & ~mask) >>> 0;
}

export function flagCheckboxes(raw: number, options: FlagOption[]): FlagCheckbox[] {
  return options.map(option => ({
    bit: option.bit,
    name: option.name,
    checked: flagIsSet(raw, option.bit),
    editable: option.editable,
  }));
}

export function editableFlagMask(options: FlagOption[]): number {
  return options.reduce((mask, option) => (option.editable ? (mask | flagMask(option.bit)) >>> 0 : mask), 0);
}

export function flagsChangeAllowed(current: number, next: number, options: FlagOption[]): boolean {
  const changed = (current ^ next) >>> 0;
  return (changed & ~editableFlagMask(options)) === 0;
}

export function formatRawFlags(raw: number): string {
  return `0x${(raw >>> 0).toString(16).toUpperCase().padStart(8, '0')}`;
}

export function parseRawFlags(text: string): number | null {
  const match = /^(?:0x)?([0-9a-f]{1,8})$/i.exec(text.trim());
  return match ? parseInt(match[1], 16) : null;
}

export function parseByte(text: string): number | null {
  const trimmed = text.trim();
  if (!/^\d{1,3}$/.test(trimmed)) return null;
  const value = Number(trimmed);
  return value <= 255 ? value : null;
}

export function lockDraftOf(lock: { level: number; key: { formKey: string } | null; flags: number }): LockDraft {
  return { level: lock.level, key: lock.key?.formKey ?? null, flags: lock.flags };
}

function sameLock(a: LockDraft | null, b: LockDraft | null): boolean {
  if (a === null || b === null) return a === b;
  return a.level === b.level && a.key === b.key && a.flags === b.flags;
}

export function lockChange(current: LockDraft | null, next: LockDraft | null): LockChange | undefined {
  return sameLock(current, next) ? undefined : next;
}

import type { ScalarPresentation } from './backend';

const pow2 = (bit: number) => 2 ** bit;

export const flagIsSet = (raw: number, bit: number) => Math.floor(raw / pow2(bit)) % 2 === 1;

export const setFlag = (raw: number, bit: number, on: boolean) => {
  if (flagIsSet(raw, bit) === on) return raw;
  return on ? raw + pow2(bit) : raw - pow2(bit);
};

const hexOf = (n: number) => `0x${n.toString(16).toUpperCase()}`;

export const rawEditable = (raw: string, p: ScalarPresentation): boolean =>
  (p.kind === 'enum' ? /^-?\d+$/ : /^\d+$/).test(raw);

export function scalarLabel(raw: string, presentation?: ScalarPresentation): string {
  if (!presentation || !rawEditable(raw, presentation)) return raw;
  const n = Number(raw);
  if (presentation.kind === 'enum') {
    const hit = presentation.names.find(e => e.value === n);
    return hit ? hit.name : n < 0 ? raw : hexOf(n);
  }
  const set = presentation.names.filter(e => flagIsSet(n, e.bit));
  const unknown = set.reduce((rest, e) => rest - pow2(e.bit), n);
  const parts = set.map(e => e.name);
  if (unknown > 0) parts.push(hexOf(unknown));
  return parts.length ? parts.join(', ') : 'none';
}

import type { ScalarPresentation } from './backend';

const pow2 = (bit: number) => 2 ** bit;

export const flagIsSet = (raw: number, bit: number) => Math.floor(raw / pow2(bit)) % 2 === 1;

export const setFlag = (raw: number, bit: number, on: boolean) => {
  if (flagIsSet(raw, bit) === on) return raw;
  return on ? raw + pow2(bit) : raw - pow2(bit);
};

const unknownLabel = (n: number): string => {
  const hex = BigInt.asUintN(64, BigInt(n)).toString(16).toUpperCase().replace(/^0+/, '');
  let label = `<Unknown: ${n}`;
  if (hex) label += ` $${hex}`;
  if (hex.length === 8) {
    const signature = [6, 4, 2, 0].map(i => String.fromCharCode(parseInt(hex.slice(i, i + 2), 16))).join('');
    if (!/[a-z]/.test(signature)) label += ` ${signature}`;
  }
  return label + '>';
};

export const rawEditable = (raw: string, p: ScalarPresentation): boolean =>
  (p.kind === 'enum' ? /^-?\d+$/ : /^\d+$/).test(raw);

export function scalarLabel(raw: string, presentation?: ScalarPresentation): string {
  if (!presentation || !rawEditable(raw, presentation)) return raw;
  const n = Number(raw);
  if (presentation.kind === 'enum') {
    const hit = presentation.names.find(e => e.value === n);
    return hit ? hit.name : unknownLabel(n);
  }
  const parts: string[] = [];
  for (let bit = 0; pow2(bit) <= n; bit++) {
    if (!flagIsSet(n, bit)) continue;
    const hit = presentation.names.find(e => e.bit === bit);
    parts.push(hit ? hit.name : unknownLabel(bit));
  }
  return parts.length ? parts.join(', ') : 'none';
}

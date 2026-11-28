export type Rgb = [number, number, number];

export const tripletText = (value: readonly number[] | null | undefined): string =>
  value && value.length === 3 ? value.join(' ') : '';

export function parseTriplet(text: string): Rgb | null {
  const parts = text.split(/[\s,]+/).filter(t => t !== '');
  if (parts.length !== 3) return null;
  const channels = parts.map(Number);
  return channels.every(c => Number.isInteger(c) && c >= 0 && c <= 255)
    ? [channels[0], channels[1], channels[2]]
    : null;
}

export const rgbToHex = ([r, g, b]: Rgb): string =>
  '#' + [r, g, b].map(c => c.toString(16).padStart(2, '0')).join('');

export const hexToRgb = (hex: string): Rgb =>
  [1, 3, 5].map(i => parseInt(hex.slice(i, i + 2), 16)) as Rgb;

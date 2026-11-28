import type { CellPropertyValue } from '../backend';

export const tokens = (text: string): string[] => text.split(/[\s,]+/).filter(t => t !== '');

export type ParsedValue = { value: CellPropertyValue } | { error: string };

export const parseNumberText = (text: string, list: boolean): ParsedValue => {
  const numbers = list ? tokens(text).map(Number) : [Number(text)];
  if (numbers.length === 0 || !numbers.every(Number.isFinite)) return { error: 'Enter numbers only' };
  return { value: list ? numbers : numbers[0] };
};

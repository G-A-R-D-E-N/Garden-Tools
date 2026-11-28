import type { ChangeEvent } from 'react';

export const numOf = (e: ChangeEvent<HTMLInputElement>): number => (e.target.value === '' ? 0 : Number(e.target.value));

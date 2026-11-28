import { useEffect } from 'react';
import { getCell, type CellHost } from '../backend';
import { buildBaseObjectQuery, DEFAULT_BROWSER_QUERY } from './baseObjects';

let started = false;

export function prewarmBaseObjects(host?: Pick<CellHost, 'ListBaseObjects'>): void {
  if (started) return;
  const cell = host ?? getCell();
  if (!cell) return;
  started = true;
  const query = buildBaseObjectQuery(DEFAULT_BROWSER_QUERY, 0, 1);
  Promise.resolve().then(() => cell.ListBaseObjects(query)).catch(() => undefined);
}

export function usePrewarmBaseObjects(): void {
  useEffect(() => {
    prewarmBaseObjects();
  }, []);
}

import { useEffect, useRef, useState } from 'react';
import type { CellHost, CellSearchHit } from './backend';
import { errorText } from './errors';

export type CellSearch = {
  matches: CellSearchHit[] | null;
  matchesLoading: boolean;
  wsMatches: CellSearchHit[] | null;
  wsLoading: boolean;
};

export default function useCellSearch(
  cell: CellHost | null | undefined, noEnv: boolean, cellId: string, worldspace: string,
  pickMode: 'cell' | 'grid', onError: (message: string) => void,
): CellSearch {
  const [matches, setMatches] = useState<CellSearchHit[] | null>(null);
  const [matchesLoading, setMatchesLoading] = useState(false);
  const [wsMatches, setWsMatches] = useState<CellSearchHit[] | null>(null);
  const [wsLoading, setWsLoading] = useState(false);
  const searchSeq = useRef(0);
  const wsSeq = useRef(0);

  useEffect(() => {
    if (!cell || noEnv) return;
    const mySeq = ++searchSeq.current;
    const t = setTimeout(async () => {
      setMatchesLoading(true);
      try {
        const raw = await cell.SearchCells(cellId.trim(), 30);
        if (searchSeq.current !== mySeq) return;
        setMatches(JSON.parse(raw) as CellSearchHit[]);
      } catch (e) {
        if (searchSeq.current === mySeq) { setMatches([]); onError('Could not search cells: ' + errorText(e)); }
      } finally {
        if (searchSeq.current === mySeq) setMatchesLoading(false);
      }
    }, 250);
    return () => clearTimeout(t);
    // eslint-disable-next-line react-hooks/exhaustive-deps -- onError is a stable state setter
  }, [cellId, cell, noEnv]);

  useEffect(() => {
    if (!cell || noEnv || pickMode !== 'grid') return;
    const mySeq = ++wsSeq.current;
    const t = setTimeout(async () => {
      setWsLoading(true);
      try {
        const raw = await cell.SearchWorldspaces(worldspace.trim(), 100);
        if (wsSeq.current !== mySeq) return;
        setWsMatches(JSON.parse(raw) as CellSearchHit[]);
      } catch (e) {
        if (wsSeq.current === mySeq) { setWsMatches([]); onError('Could not search worldspaces: ' + errorText(e)); }
      } finally {
        if (wsSeq.current === mySeq) setWsLoading(false);
      }
    }, 250);
    return () => clearTimeout(t);
    // eslint-disable-next-line react-hooks/exhaustive-deps -- onError is a stable state setter
  }, [worldspace, cell, noEnv, pickMode]);

  return { matches, matchesLoading, wsMatches, wsLoading };
}

import { useEffect, useState } from 'react';
import type { PapyrusHost, PapyrusWorkspaceResult, PapyrusWorkspaceSymbol } from './backend';

const NO_MATCH: { results: PapyrusWorkspaceSymbol[]; error: string | null } = { results: [], error: null };

export function usePapyrusWorkspaceSearch(papyrus: PapyrusHost | undefined, query: string, imports: string) {
  const [found, setFound] = useState(NO_MATCH);
  const wanted = query.trim();

  useEffect(() => {
    if (!papyrus || !wanted) return;
    let cancelled = false;
    const timer = setTimeout(async () => {
      try {
        const reply = JSON.parse(await papyrus.WorkspaceSymbols(wanted, 0, imports)) as PapyrusWorkspaceResult;
        if (!cancelled) setFound({ results: reply.results ?? [], error: reply.error ?? null });
      } catch (e) {
        if (!cancelled) setFound({ results: [], error: e instanceof Error ? e.message : String(e) });
      }
    }, 200);
    return () => { cancelled = true; clearTimeout(timer); };
  }, [papyrus, wanted, imports]);

  return wanted ? found : NO_MATCH;
}

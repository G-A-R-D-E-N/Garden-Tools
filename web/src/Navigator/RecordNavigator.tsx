import { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import { ChevronDown, ChevronRight, Database, FileBox } from 'lucide-react';
import type { Backend, RecordTypeEntry, SearchHit } from '../backend';
import { pluginColorVar } from '../util/pluginColor';
import type { NavigatorFilterState } from './filters';
import './RecordNavigator.css';

interface RecordNavigatorProps {
  backend: Backend | undefined;
  types: RecordTypeEntry[];
  loadingTypes: boolean;
  filters: NavigatorFilterState;
  /** Restrict to these plugins; empty means the whole load order. */
  pluginFilter: Set<string>;
  onOpenRecord: (formKey: string, plugin: string) => void;
  selectedFormKey: string;
}

const PAGE = 300;

/** Cached rows plus the filter they were fetched for, so staleness is derivable rather than
 *  something an effect has to clear by setting state. */
interface TypeRows { needle: string; hits: SearchHit[]; truncated: boolean; }

/**
 * The type-first tree: record type -> records, across the whole load order.
 *
 * The old explorer was plugin -> group -> record, which answers "what is in this file". This one
 * answers "where is this thing", which is what you actually have when you are chasing a conflict
 * and know the type but not the owning plugin.
 */
export default function RecordNavigator({
  backend, types, loadingTypes, filters, pluginFilter, onOpenRecord, selectedFormKey,
}: RecordNavigatorProps) {
  const [expanded, setExpanded] = useState<Set<string>>(new Set());
  const [rows, setRows] = useState<Record<string, TypeRows>>({});
  // Guards against a slow response for a superseded filter landing after a newer one.
  const reqRef = useRef<Record<string, number>>({});

  const visibleTypes = useMemo(
    () => (filters.type ? types.filter(t => t.Type === filters.type) : types),
    [types, filters.type]);

  // A type filter naming exactly one type shows it open, without having to store that in state.
  const isOpen = useCallback(
    (sig: string) => expanded.has(sig) || filters.type === sig,
    [expanded, filters.type]);

  const isStale = useCallback(
    (sig: string) => rows[sig]?.needle !== filters.needle,
    [rows, filters.needle]);

  const load = useCallback(async (sig: string, needle: string) => {
    if (!backend) return;
    const seq = (reqRef.current[sig] ?? 0) + 1;
    reqRef.current[sig] = seq;
    try {
      const json = await backend.GetRecordsOfType(sig, needle, PAGE, 0);
      if (reqRef.current[sig] !== seq) return;   // a newer request superseded this one
      const hits: SearchHit[] = JSON.parse(json);
      setRows(r => ({ ...r, [sig]: { needle, hits, truncated: hits.length >= PAGE } }));
    } catch {
      if (reqRef.current[sig] === seq) {
        setRows(r => ({ ...r, [sig]: { needle, hits: [], truncated: false } }));
      }
    }
  }, [backend]);

  // Fetch whatever is open and stale. Every state write happens after an await, so opening a type
  // or changing the filter does not cascade renders.
  useEffect(() => {
    const open = filters.type ? [filters.type] : [...expanded];
    for (const sig of open) {
      // load() writes state only after its await, so this does not cascade renders. The lint rule
      // cannot see through the async boundary and flags the call site regardless.
      // eslint-disable-next-line react-hooks/set-state-in-effect
      if (rows[sig]?.needle !== filters.needle) void load(sig, filters.needle);
    }
  }, [expanded, filters.type, filters.needle, rows, load]);

  const toggle = (sig: string) => {
    setExpanded(prev => {
      const next = new Set(prev);
      if (next.has(sig)) next.delete(sig); else next.add(sig);
      return next;
    });
  };

  const visibleRows = (sig: string) => {
    const all = rows[sig]?.hits ?? [];
    return pluginFilter.size === 0 ? all : all.filter(h => pluginFilter.has(h.Plugin));
  };

  if (loadingTypes) return <div className="nav-empty">Indexing record types...</div>;
  if (types.length === 0) return <div className="nav-empty">No records loaded. Use Load Env or Open MO2.</div>;

  return (
    <div className="record-navigator">
      {visibleTypes.map(t => {
        const open = isOpen(t.Type);
        const list = visibleRows(t.Type);
        return (
          <div key={t.Type} className="rn-group">
            <div className="rn-type" onClick={() => toggle(t.Type)} title={`${t.Type} - ${t.Count} records`}>
              {open ? <ChevronDown size={13} /> : <ChevronRight size={13} />}
              <Database size={13} className="rn-type-icon" />
              <span className="rn-type-name">{t.FriendlyName}</span>
              <span className="rn-type-count">{t.Count.toLocaleString()}</span>
            </div>

            {open && (
              <div className="rn-records">
                {isStale(t.Type) ? (
                  <div className="nav-empty">Loading...</div>
                ) : list.length === 0 ? (
                  <div className="nav-empty">
                    {pluginFilter.size > 0 ? 'No records from the selected files.' : 'No matching records.'}
                  </div>
                ) : (
                  <>
                    {list.map(h => (
                      <div
                        key={h.FormKey}
                        className={`rn-record ${h.FormKey === selectedFormKey ? 'selected' : ''}`}
                        onClick={() => onOpenRecord(h.FormKey, h.Plugin)}
                        title={`${h.FormKey} - ${h.Plugin}`}
                      >
                        <FileBox size={12} className="rn-record-icon" />
                        <span className="rn-record-name">{h.EditorID || h.FormKey}</span>
                        <span className="rn-record-plugin" style={{ background: pluginColorVar(h.Plugin) }} />
                      </div>
                    ))}
                    {rows[t.Type]?.truncated && (
                      <div className="rn-more">
                        Showing the first {PAGE.toLocaleString()}. Narrow the filter to see the rest.
                      </div>
                    )}
                  </>
                )}
              </div>
            )}
          </div>
        );
      })}
    </div>
  );
}

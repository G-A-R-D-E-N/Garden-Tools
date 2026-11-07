import { useState, useEffect, useLayoutEffect, useRef, useCallback } from 'react';
import {
  Files, Search, GitCompare, AlertCircle, ScrollText, Box,
  Database, FileBox, X, Zap, Rocket,
  Link2, Archive as ArchiveIcon, Music, Boxes, Table2, Workflow, Languages, Pencil, Clock,
} from 'lucide-react';
import PapyrusPanel from './PapyrusPanel';
import SwfPanel from './SwfPanel';
import NifPanel from './NifPanel';
import MastersPanel from './MastersPanel';
import ArchivePanel from './ArchivePanel';
import LocalizationPanel from './LocalizationPanel';
import AudioPanel from './AudioPanel';
import CellPanel from './CellPanel';
import SpreadsheetPanel from './SpreadsheetPanel';
import BlueprintPanel from './Blueprint/BlueprintPanel';
import HkxPanel from './HkxPanel';
import { readFavourites, removeFavourite, FAVOURITES_CHANGED } from './favourites';

interface McpLiveMsg {
  Tool: string;
  Plugin: string;
  Record: string;
  Field: string;
  Summary: string;
  IsWrite: boolean;
  ts: number;
  isNew?: boolean;
}
interface HostMessage {
  Type?: string;
  Text?: string;
  Value?: number;
}
import './MainShell.css';
import SettingsModal from './SettingsModal';
import RecordView from './RecordView';
import ChatPanel from './ChatPanel';
import TopBar, { type ShellTab } from './Shell/TopBar';
import DetailRail from './Shell/DetailRail';
import StatusBar from './Shell/StatusBar';
import HelpModal from './Shell/HelpModal';
import BrokenRefsModal from './Shell/BrokenRefsModal';
import EnvLoadingOverlay from './Shell/EnvLoadingOverlay';
import ConflictsPanel from './Shell/ConflictsPanel';
import FavouritesList from './Shell/FavouritesList';
import { useRecordOpening } from './Shell/useRecordOpening';
import { useEnvironment } from './Shell/useEnvironment';
import { useCompareSelected, useConflictBatch } from './Shell/useConflictBatch';
import CompareView from './Shell/CompareView';
import { modeOf, selectRow, type ConflictSelection, type SelectMode } from './Shell/conflictSelection';
import type { SearchHit } from './backend';
import type { RecordTab } from './RecordView';
import { errorMessage } from './errors';
import ExplorerTree from './Shell/ExplorerTree';
import EditorsHome from './Editors/EditorsHome';

const hitKey = (h: SearchHit) => `${h.FormKey}|${h.Plugin}`;

export default function MainShell() {
  const [activeTab, setActiveTab] = useState('explorer');

  const [treeMode, setTreeMode] = useState<'files' | 'conflicts'>(
    () => (localStorage.getItem('treeMode') as 'files' | 'conflicts') || 'files');
  useEffect(() => { localStorage.setItem('treeMode', treeMode); }, [treeMode]);
  const [showSettings, setShowSettings] = useState(false);
  const [showPapyrus, setShowPapyrus] = useState(false);
  const [showSwf, setShowSwf] = useState(false);
  const [showNif, setShowNif] = useState(false);
  const [showMasters, setShowMasters] = useState(false);
  const [showArchive, setShowArchive] = useState(false);
  const [showLocalization, setShowLocalization] = useState(false);
  const [showAudio, setShowAudio] = useState(false);
  const [showCell, setShowCell] = useState(false);
  const [showSpreadsheet, setShowSpreadsheet] = useState(false);
  const [showBlueprint, setShowBlueprint] = useState(false);
  const [showEditors, setShowEditors] = useState(false);
  const [showHkx, setShowHkx] = useState(false);
  const [showHelp, setShowHelp] = useState(false);

  const [shellTab, setShellTab] = useState<ShellTab>('home');
  const [railVisible, setRailVisible] = useState(true);
  const [chatVisible, setChatVisible] = useState(true);
  const [chatWidth, setChatWidth] = useState(() => Number(localStorage.getItem('chatWidth') || 320));

  const onChatResizeStart = useCallback((e: React.MouseEvent) => {
    e.preventDefault();
    const startX = e.clientX;
    const startW = chatWidth;
    const onMove = (mv: MouseEvent) => {
      const w = Math.max(240, Math.min(800, startW + (startX - mv.clientX)));
      setChatWidth(w);
      localStorage.setItem('chatWidth', String(w));
    };
    const onUp = () => {
      document.removeEventListener('mousemove', onMove);
      document.removeEventListener('mouseup', onUp);
      document.body.style.cursor = '';
      document.body.style.userSelect = '';
    };
    document.addEventListener('mousemove', onMove);
    document.addEventListener('mouseup', onUp);
    document.body.style.cursor = 'col-resize';
    document.body.style.userSelect = 'none';
  }, [chatWidth]);

  const [sidebarWidth, setSidebarWidth] = useState(() => Number(localStorage.getItem('sidebarWidth') || 280));

  const onSidebarResizeStart = useCallback((e: React.MouseEvent) => {
    e.preventDefault();
    const startX = e.clientX;
    const startW = sidebarWidth;
    const onMove = (mv: MouseEvent) => {
      const w = Math.max(200, Math.min(620, startW + (mv.clientX - startX)));
      setSidebarWidth(w);
      localStorage.setItem('sidebarWidth', String(w));
    };
    const onUp = () => {
      document.removeEventListener('mousemove', onMove);
      document.removeEventListener('mouseup', onUp);
      document.body.style.cursor = '';
      document.body.style.userSelect = '';
    };
    document.addEventListener('mousemove', onMove);
    document.addEventListener('mouseup', onUp);
    document.body.style.cursor = 'col-resize';
    document.body.style.userSelect = 'none';
  }, [sidebarWidth]);

  const [isDark, setIsDark] = useState(() => localStorage.getItem('theme') !== 'light');
  useEffect(() => {
    document.documentElement.setAttribute('data-theme', isDark ? 'dark' : 'light');
    localStorage.setItem('theme', isDark ? 'dark' : 'light');
  }, [isDark]);

  const [recordTab, setRecordTab] = useState<RecordTab>('grid');
  const [errorStr, setErrorStr] = useState<string>("");
  const [status, setStatus] = useState<string>("");
  const [progress, setProgress] = useState<number | null>(null);

  const {
    openRecords, activeKey, setActiveKey, recordLoading, openRecord, closeRecordTab,
    openRecordInView, openHit, openByFormKey, reloadMatrix,
  } = useRecordOpening(setErrorStr);

  const {
    plugins, setPlugins, backend, envLoading, mo2Status, recordTypes, loadOrderSummary,
    refreshNavigator, refreshPlugins, handleLoadEnv, handleLoadMo2,
  } = useEnvironment({ setErrorStr, setProgress });

  const {
    conflicts, setConflicts, conflictSel, conflictBusy, batchCopyAsOverride,
    selectConflict, clearConflictSelection, copyMany,
  } = useConflictBatch({ activeTab, treeMode, setStatus, setErrorStr });

  const loadOrderKey = loadOrderSummary ? JSON.stringify(loadOrderSummary) : '';
  const [prevLoadOrderKey, setPrevLoadOrderKey] = useState(loadOrderKey);
  if (prevLoadOrderKey !== loadOrderKey) {
    setPrevLoadOrderKey(loadOrderKey);
    if (prevLoadOrderKey) {
      setConflicts(null);
      clearConflictSelection();
    }
  }

  const [favourites, setFavourites] = useState(readFavourites);
  const [favouritesOpen, setFavouritesOpen] = useState(
    () => localStorage.getItem('favouritesOpen') !== 'false');
  useEffect(() => { localStorage.setItem('favouritesOpen', String(favouritesOpen)); }, [favouritesOpen]);
  useEffect(() => {
    const sync = () => setFavourites(readFavourites());
    window.addEventListener(FAVOURITES_CHANGED, sync);
    return () => window.removeEventListener(FAVOURITES_CHANGED, sync);
  }, []);

  const [mcpFeed, setMcpFeed] = useState<McpLiveMsg[]>([]);
  const [mcpActive, setMcpActive] = useState(false);
  const mcpActiveTimerRef = useRef<ReturnType<typeof setTimeout> | null>(null);

  const [aiHighlightField, setAiHighlightField] = useState('');
  const highlightTimerRef = useRef<ReturnType<typeof setTimeout> | null>(null);
  const [aiHighlightSeq, setAiHighlightSeq] = useState(0);

  const openRecordRef = useRef(openRecord);
  const reloadMatrixRef = useRef<() => Promise<void>>(async () => {});
  const openByFormKeyRef = useRef<(formKey: string, pluginName: string) => Promise<void>>(async () => {});
  useLayoutEffect(() => { openRecordRef.current = openRecord; });

  const [search, setSearch] = useState('');
  const [searchResults, setSearchResults] = useState<SearchHit[] | null>(null);

  useEffect(() => {
    const handleRejection = (event: PromiseRejectionEvent) => {
      let msg = "Unhandled Promise: ";
      if (event.reason && typeof event.reason === 'object') {
        try {
          msg += JSON.stringify(event.reason, Object.getOwnPropertyNames(event.reason));
        } catch {
          msg += String(event.reason);
        }
      } else {
        msg += String(event.reason);
      }
      setErrorStr(msg);
    };
    window.addEventListener('unhandledrejection', handleRejection);

    const onMessage = (e: MessageEvent) => {
      const data = (typeof e.data === 'string' ? JSON.parse(e.data) : e.data) as HostMessage | null;
      if (!data || !data.Type) return;
      if (data.Type === 'SetStatus') setStatus(data.Text || "");
      if (data.Type === 'SetProgress') setProgress((data.Value ?? 0) < 0 ? null : data.Value ?? null);
      if (data.Type === 'McpLive') {
        const ev: McpLiveMsg = { ...(data as Omit<McpLiveMsg, 'ts' | 'isNew'>), ts: Date.now(), isNew: true };
        setMcpFeed(prev => [ev, ...prev].slice(0, 12));
        setMcpActive(true);
        if (mcpActiveTimerRef.current) clearTimeout(mcpActiveTimerRef.current);
        mcpActiveTimerRef.current = setTimeout(() => setMcpActive(false), 2000);
        if (ev.IsWrite && ev.Record) {
          const cur = openRecordRef.current;
          if (cur && cur.matrix.FormKey === ev.Record) {
            reloadMatrixRef.current();
          } else {
            openByFormKeyRef.current(ev.Record, ev.Plugin);
          }

          if (ev.Field) {
            if (highlightTimerRef.current) clearTimeout(highlightTimerRef.current);
            setAiHighlightField(ev.Field);
            setAiHighlightSeq(s => s + 1);
            setRecordTab('grid');
            highlightTimerRef.current = setTimeout(() => setAiHighlightField(''), 3000);
          }
        } else if (ev.IsWrite && openRecordRef.current) {
          reloadMatrixRef.current();
        }
      }
    };

    window.chrome?.webview?.addEventListener('message', onMessage);

    return () => {
      window.removeEventListener('unhandledrejection', handleRejection);
      window.chrome?.webview?.removeEventListener('message', onMessage);
    };
  }, []);

  const [brokenRefsReport, setBrokenRefsReport] = useState<string | null>(null);
  const [brokenRefsScanning, setBrokenRefsScanning] = useState(false);

  const handleScanConflicts = async () => {
    const b = window.chrome?.webview?.hostObjects?.appInterop;
    if (!b) return;
    try {
      const summary = await b.ScanConflicts();
      setStatus(summary);
      await refreshPlugins();
    } catch (err) {
      setErrorStr(errorMessage(err) ?? JSON.stringify(err));
    } finally {
      setProgress(null);
    }
  };

  const handleScanBrokenRefs = async () => {
    const b = window.chrome?.webview?.hostObjects?.appInterop;
    if (!b) return;
    setBrokenRefsScanning(true);
    setStatus('Scanning for broken references...');
    try {
      const report = await b.ScanBrokenRefs();
      setBrokenRefsReport(report);
      setStatus('Broken-ref scan complete.');
    } catch (err) {
      setErrorStr(errorMessage(err) ?? JSON.stringify(err));
    } finally {
      setBrokenRefsScanning(false);
      setProgress(null);
    }
  };

  useEffect(() => {
    const q = search.trim();
    if (q.length < 2) return;
    const t = setTimeout(async () => {
      const back = window.chrome?.webview?.hostObjects?.backend;
      if (!back) return;
      try { setSearchResults(JSON.parse(await back.SearchRecords(q, ''))); }
      catch (err) { setSearchResults([]); setErrorStr('Search failed: ' + (errorMessage(err) ?? JSON.stringify(err))); }
    }, 300);
    return () => clearTimeout(t);
  }, [search]);
  const changeSearch = (v: string) => { setSearch(v); if (v.trim().length < 2) setSearchResults(null); };

  const [recSel, setRecSel] = useState<ConflictSelection>({});
  const [recAnchor, setRecAnchor] = useState<string | null>(null);
  const [recHits, setRecHits] = useState<Record<string, SearchHit>>({});
  const { comparing: recComparing, view: compareView, compare: compareRecords, closeView: closeCompareView } = useCompareSelected();
  const visibleHits = searchResults ?? [];
  const visibleKeys = new Set(visibleHits.map(hitKey));
  const loadedSet = loadOrderSummary?.Plugins ? new Set(loadOrderSummary.Plugins) : null;
  const recPicked = Object.keys(recSel).filter(k => recSel[k]).map(k => recHits[k])
    .filter(h => !loadedSet || loadedSet.has(h.Plugin));
  const recHidden = recPicked.filter(h => !visibleKeys.has(hitKey(h))).length;
  const recDupes = new Set(recPicked.map(h => h.FormKey)).size !== recPicked.length;
  const selectHit = (h: SearchHit, mode: SelectMode) => {
    const next = selectRow(visibleHits.map(hitKey), recSel, recAnchor, hitKey(h), mode);
    setRecSel(next.sel);
    setRecAnchor(next.anchor);
    setRecHits(prev => ({
      ...Object.fromEntries(visibleHits.filter(v => next.sel[hitKey(v)] && !(hitKey(v) in prev)).map(v => [hitKey(v), v])),
      ...prev,
      [hitKey(h)]: h,
    }));
  };
  const clearRecSel = () => { setRecSel({}); setRecAnchor(null); setRecHits({}); };
  const copyRecs = async () => {
    if (await copyMany(recPicked.map(h => ({ formKey: h.FormKey, source: h.Plugin, editorId: h.EditorID })))) clearRecSel();
  };

  const handleRefresh = async () => {
    const b = window.chrome?.webview?.hostObjects?.appInterop;
    if (!b) return;
    try {
      const json = await b.RefreshTree();
      setPlugins(JSON.parse(json));
      await refreshNavigator();
      if (openRecord) await reloadMatrix();
      setStatus('Refreshed.');
    } catch (err) {
      setErrorStr(errorMessage(err) ?? JSON.stringify(err));
    }
  };

  useLayoutEffect(() => {
    reloadMatrixRef.current = reloadMatrix;
    openByFormKeyRef.current = openByFormKey;
  });

  const [prevOpenKey, setPrevOpenKey] = useState(activeKey);
  if (prevOpenKey !== activeKey) {
    setPrevOpenKey(activeKey);
    if (openRecord) { setShellTab('record'); setRecordTab('grid'); }
  }

  const commandSearch = useCallback(async (q: string): Promise<SearchHit[]> => {
    const back = window.chrome?.webview?.hostObjects?.backend;
    if (!back) return [];
    try { return JSON.parse(await back.SearchRecords(q, '')); }
    catch (err) { setErrorStr('Search failed: ' + (errorMessage(err) ?? JSON.stringify(err))); return []; }
  }, []);

  const closeRecord = () => {
    if (!openRecord) { setShellTab('home'); return; }
    const remaining = openRecords.length - 1;
    closeRecordTab(openRecord.matrix.FormKey);
    if (remaining === 0) setShellTab('home');
  };

  return (
    <div
      className={`shell-container animate-fade-in ${railVisible ? 'rail-on' : 'rail-off'} ${chatVisible ? 'chat-on' : 'chat-off'}`}
    >
      {envLoading && <EnvLoadingOverlay status={status} progress={progress} />}

      <TopBar
        activeTab={shellTab}
        hasRecord={!!openRecord}
        recordTitle={openRecord?.title ?? ''}
        onSelectTab={setShellTab}
        onCloseRecord={closeRecord}
        onOpenSettings={() => setShowSettings(true)}
        onOpenHelp={() => setShowHelp(true)}
        onToggleRail={() => setRailVisible(v => !v)}
        railVisible={railVisible}
        onToggleChat={() => setChatVisible(v => !v)}
        chatVisible={chatVisible}
        isDark={isDark}
        onToggleTheme={() => setIsDark(v => !v)}
        onSearch={commandSearch}
        onOpenHit={openHit}
      />

      { }
      <div className="shell-left">
      <div className="activity-bar">
        <div className="activity-top">
          <button className={`activity-btn ${activeTab === 'explorer' ? 'active' : ''}`} onClick={() => setActiveTab('explorer')} title="Explorer: records, files, search and errors">
            <Files size={22} strokeWidth={1.5} />
          </button>
          <button className={`activity-btn ${showPapyrus ? 'active' : ''}`} onClick={() => setShowPapyrus(true)} title="Papyrus (compile / decompile)">
            <ScrollText size={22} strokeWidth={1.5} />
          </button>          <button className={`activity-btn ${showSwf ? 'active' : ''}`} onClick={() => setShowSwf(true)} title="Interface (SWF)">
            <Boxes size={22} strokeWidth={1.5} />
          </button>
          <button className={`activity-btn ${showNif ? 'active' : ''}`} onClick={() => setShowNif(true)} title="NIF (author / inspect / verify / fix)">
            <Box size={22} strokeWidth={1.5} />
          </button>
          <button className={`activity-btn ${showMasters ? 'active' : ''}`} onClick={() => setShowMasters(true)} title="Masters (inspect / reorder / ESL flag)">
            <Link2 size={22} strokeWidth={1.5} />
          </button>
          <button className={`activity-btn ${showArchive ? 'active' : ''}`} onClick={() => setShowArchive(true)} title="Archive (BA2/BSA browse / extract)">
            <ArchiveIcon size={22} strokeWidth={1.5} />
          </button>
          <button className={`activity-btn ${showLocalization ? 'active' : ''}`} onClick={() => setShowLocalization(true)} title="Localization (read-only string tables)">
            <Languages size={22} strokeWidth={1.5} />
          </button>
          <button className={`activity-btn ${showAudio ? 'active' : ''}`} onClick={() => setShowAudio(true)} title="Audio (convert to/from xWMA, merge/split fuz)">
            <Music size={22} strokeWidth={1.5} />
          </button>
          <button className={`activity-btn ${showCell ? 'active' : ''}`} onClick={() => setShowCell(true)} title="Cell Viewer (3D view with placed-reference and cell edits, like Creation Kit's render window)">
            <Boxes size={22} strokeWidth={1.5} />
          </button>
          <button className={`activity-btn ${showSpreadsheet ? 'active' : ''}`} onClick={() => setShowSpreadsheet(true)} title="Spreadsheet (bulk-edit every record of one type)">
            <Table2 size={22} strokeWidth={1.5} />
          </button>
          <button className={`activity-btn ${showBlueprint ? 'active' : ''}`} onClick={() => setShowBlueprint(true)} title="Blueprint (node graph that compiles to Papyrus)">
            <Workflow size={22} strokeWidth={1.5} />
          </button>
          <button className={`activity-btn ${showEditors ? 'active' : ''}`} onClick={() => setShowEditors(true)} title="Editors (quest, dialogue and scene)">
            <Pencil size={22} strokeWidth={1.5} />
          </button>
          <button className={`activity-btn ${showHkx ? 'active' : ''}`} onClick={() => setShowHkx(true)} title="Animation (HKX skeleton and clip timeline)">
            <Clock size={22} strokeWidth={1.5} />
          </button>
        </div>
      </div>

      { }
      <div className="sidebar" style={{ width: sidebarWidth }}>
        <div className="sidebar-header">
          <h2>EXPLORER</h2>
          {activeTab === 'explorer' && (
            <div className="tree-mode">
              <button
                className={treeMode === 'files' ? 'on' : ''}
                onClick={() => setTreeMode('files')}
                title="The load order, plugin by plugin"
              >
                <Files size={12} /> Files
              </button>
              <button
                className={treeMode === 'conflicts' ? 'on' : ''}
                onClick={() => setTreeMode('conflicts')}
                title="Records more than one plugin touches"
              >
                <GitCompare size={12} /> Conflicts
              </button>
            </div>
          )}
        </div>
        <div className="sidebar-actions">
          <button className="sidebar-action-btn" onClick={handleLoadEnv}>Load Env</button>
          <button className="sidebar-action-btn" onClick={handleLoadMo2}>Open MO2</button>
          <button className="sidebar-action-btn" onClick={handleScanConflicts}>Scan Conflicts</button>
          <button className="sidebar-action-btn sidebar-action-btn--warn" onClick={handleScanBrokenRefs} disabled={brokenRefsScanning} title="Scan all plugins for dangling FormLinks (crash risks)">
            {brokenRefsScanning ? 'Scanning...' : 'Scan Crash Risks'}
          </button>
          <button className="sidebar-action-btn" onClick={handleRefresh}>Refresh</button>
        </div>
        {treeMode === 'files' && (
        <div className="sidebar-search">
          <Search size={13} />
          <input
            value={search}
            onChange={e => changeSearch(e.target.value)}
            placeholder="Search plugins & records…"
          />
          {search && <button className="sidebar-search-clear" onClick={() => changeSearch('')} title="Clear"><X size={13} /></button>}
        </div>
        )}

        {activeTab === 'explorer' && errorStr && (
          <div className="sidebar-error">
            <AlertCircle size={13} />
            <span className="sidebar-error-text">{errorStr}</span>
            <button className="sidebar-error-clear" onClick={() => setErrorStr('')} title="Dismiss">
              <X size={12} />
            </button>
          </div>
        )}

        { }
        {activeTab === 'explorer' && treeMode === 'files' && favourites.length > 0 && (
          <FavouritesList
            favourites={favourites}
            open={favouritesOpen}
            onToggle={() => setFavouritesOpen(o => !o)}
            onOpen={openByFormKey}
            onRemove={key => { removeFavourite(key); setFavourites(readFavourites()); }}
          />
        )}

        {activeTab === 'explorer' && treeMode === 'files' && (
        <div className="sidebar-tree">
          {plugins.length === 0 && !errorStr ? (
            <div className="sidebar-empty">
              No plugins loaded. Click "Load Env" or "Open MO2".
            </div>
          ) : (
            <>
              { }
              {plugins
                .filter(p => !search.trim() || p.Key.toLowerCase().includes(search.trim().toLowerCase()))
                .map((p, i) => (
                  <ExplorerTree key={`${p.Key}:${i}`} path={p.Key} node={p} backend={backend} onOpenRecord={openRecordInView} />
                ))}

              { }
              {(search.trim().length >= 2 || recPicked.length > 0) && (
                <div className="sidebar-results" aria-label="Records" tabIndex={0} onKeyDown={e => { if (e.key === 'Escape') clearRecSel(); }}>
                  {recPicked.length > 0 && (
                    <div className="conflicts-actions">
                      <button
                        className="sidebar-action-btn"
                        disabled={conflictBusy || recDupes}
                        title={recDupes ? 'A patch can hold only one version of a record; untick one of the duplicates' : undefined}
                        onClick={copyRecs}
                      >
                        Copy selected as override…
                      </button>
                      <button
                        className="sidebar-action-btn"
                        disabled={conflictBusy || recComparing || recDupes || recPicked.length < 2}
                        title={recDupes ? 'Both selections are the same record; open it to see every plugin\'s version' : recPicked.length >= 2 ? 'Show the selected records side by side' : 'Select at least two records to compare'}
                        onClick={() => compareRecords(recPicked.map(h => ({ formKey: h.FormKey, plugin: h.Plugin }))).catch(err => setErrorStr(errorMessage(err) ?? JSON.stringify(err)))}
                      >
                        Compare selected
                      </button>
                      <button className="sidebar-action-btn" onClick={clearRecSel}>Clear</button>
                      <span className="conflicts-selected">{`${recPicked.length} selected (${recHidden} hidden)`}</span>
                    </div>
                  )}
                  {search.trim().length >= 2 && (
                    <>
                      <div className="sidebar-results-head">
                        RECORDS {searchResults === null ? '…' : `(${searchResults.length})`}
                      </div>
                      {searchResults === null ? (
                        <div className="sidebar-empty">Searching…</div>
                      ) : searchResults.length === 0 ? (
                        <div className="sidebar-empty">No matching records.</div>
                      ) : (
                        searchResults.map((h, i) => (
                          <div
                            key={`${h.Plugin}:${h.FormKey}:${i}`}
                            className={`sidebar-result${recSel[hitKey(h)] ? ' selected' : ''}`}
                            onClick={e => { const mode = modeOf(e); selectHit(h, mode); if (mode === 'plain') void openHit(h); }}
                            title={`${h.FormKey} · ${h.Plugin}`}
                          >
                            <input type="checkbox" aria-label={h.EditorID || h.FormKey} checked={!!recSel[hitKey(h)]} onClick={e => e.stopPropagation()} onChange={() => selectHit(h, 'toggle')} />
                            <FileBox size={13} color="#569CD6" />
                            <span className="sr-id">{h.EditorID || h.FormKey}</span>
                            <span className="sr-type">{h.Type}</span>
                          </div>
                        ))
                      )}
                    </>
                  )}
                </div>
              )}
            </>
          )}
        </div>
        )}
        {activeTab === 'explorer' && treeMode === 'conflicts' && (
          <ConflictsPanel
            conflicts={conflicts}
            conflictSel={conflictSel}
            conflictBusy={conflictBusy}
            onRescan={() => setConflicts(null)}
            onBatchCopy={batchCopyAsOverride}
            onSelect={selectConflict}
            onClear={clearConflictSelection}
            onOpen={openByFormKey}
          />
        )}
      </div>
      <div className="sidebar-resizer" onMouseDown={onSidebarResizeStart} />
      </div>{ }

      { }
      <div className="main-area">
        {shellTab === 'home' ? (
          <div className="home-view">
            <div className="home-card glass-panel">
              <Rocket size={56} color="var(--accent-color)" />
              <h1>FO4 Record Editor</h1>
              <p>The next-generation modding IDE for Fallout 4 plugins.</p>
              <div className="home-hints">
                <p>Load your environment from the Explorer on the left.</p>
                <p>Click a record to open it in the Record Viewer.</p>
                <p>Press <kbd>Ctrl K</kbd> to search records, fields, or formIDs.</p>
              </div>
            </div>
          </div>
        ) : (
          <>
            <div className="editor-tabs">
              {openRecords.length === 0 ? (
                <div className="editor-tab active">
                  <Zap size={14} color="#F44747"/> Welcome
                </div>
              ) : openRecords.map(r => (
                <div
                  key={r.matrix.FormKey}
                  className={`editor-tab ${r.matrix.FormKey === activeKey ? 'active' : ''}`}
                  onClick={() => setActiveKey(r.matrix.FormKey)}
                  title={`${r.matrix.FormKey} - ${r.plugin}`}
                >
                  <Database size={14} color="#569CD6"/>
                  <span className="editor-tab-label">{r.title}</span>
                  <button
                    className="tab-close"
                    title="Close"
                    onClick={e => { e.stopPropagation(); closeRecordTab(r.matrix.FormKey); }}
                  >
                    <X size={14} />
                  </button>
                </div>
              ))}
            </div>

            <div className={`editor-content ${openRecord && !recordLoading ? 'has-record' : ''}`}>
              {compareView && <CompareView view={compareView} onClose={closeCompareView} />}
              {recordLoading ? (
                <div className="welcome-screen glass-panel">
                  <Database size={48} color="var(--border-color)" />
                  <p>Loading record...</p>
                </div>
              ) : openRecord ? (
                <RecordView
                  matrix={openRecord.matrix}
                  plugin={openRecord.plugin}
                  onReload={reloadMatrix}
                  onOpenRecord={openByFormKey}
                  activeTab={recordTab}
                  onTabChange={setRecordTab}
                  highlightField={aiHighlightField}
                  highlightSeq={aiHighlightSeq}
                  onPluginsChanged={() => { void refreshPlugins(); }}
                  selectedRecords={recPicked.map(h => ({ plugin: h.Plugin, formKey: h.FormKey }))}
                  setErrorStr={setErrorStr}
                />
              ) : (
                <div className="welcome-screen glass-panel">
                  <Database size={64} color="var(--border-color)" />
                  <h1>No record open</h1>
                  <p>Pick a record from the Explorer or search with Ctrl K.</p>
                </div>
              )}
            </div>

            {mcpFeed.length > 0 && (
              <div className="mcp-feed">
                <div className="mcp-feed-header">
                  <span className={`mcp-dot ${mcpActive ? 'mcp-dot-active' : ''}`} />
                  <span>MCP TOOL CALLS</span>
                  <button className="mcp-feed-clear" onClick={() => setMcpFeed([])}>✕</button>
                </div>
                <div className="mcp-feed-rows">
                  {mcpFeed.map((ev, i) => (
                    <div key={i} className={`mcp-row ${ev.IsWrite ? 'mcp-row-write' : 'mcp-row-read'} ${i === 0 ? 'mcp-row-new' : ''}`}>
                      <span className="mcp-badge">{ev.Tool}</span>
                      <span className="mcp-summary">{ev.Summary}</span>
                      <span className="mcp-time">{new Date(ev.ts).toLocaleTimeString()}</span>
                    </div>
                  ))}
                </div>
              </div>
            )}
          </>
        )}
      </div>

      { }
      {railVisible && (
        <DetailRail
          matrix={openRecord?.matrix ?? null}
          onOpenConflictsTab={() => { setRecordTab('conflicts'); setShellTab('record'); }}
          onOpenRecord={openByFormKey}
          onAddToFilter={needle => {
            changeSearch(needle);
            setActiveTab('explorer');
            setTreeMode('files');
          }}
        />
      )}

      { }
      <div className="shell-chat" style={{ display: chatVisible ? undefined : 'none', width: chatVisible ? chatWidth : undefined }}>
        <div className="chat-resizer" onMouseDown={onChatResizeStart} />
        <ChatPanel />
      </div>

      { }
      <StatusBar
        status={status}
        progress={progress}
        recordCount={loadOrderSummary?.TotalRecords ?? 0}
        visibleCount={recordTypes.reduce((n, t) => n + t.Count, 0)}
        selectedCount={openRecord ? 1 : 0}
        loadOrder={loadOrderSummary?.Plugins ?? plugins.map(p => p.Key)}
        mo2Loaded={Boolean(mo2Status?.loaded)}
        mo2Stale={Boolean(mo2Status?.loaded && mo2Status.stale)}
        mo2StaleConfig={Boolean(mo2Status?.staleConfig)}
        mo2StaleLoadOrder={Boolean(mo2Status?.staleLoadOrder)}
        mo2Profile={mo2Status?.profile}
      />

      {brokenRefsReport !== null && (
        <BrokenRefsModal report={brokenRefsReport} onClose={() => setBrokenRefsReport(null)} />
      )}

      {showHelp && <HelpModal onClose={() => setShowHelp(false)} />}

      {showSettings && <SettingsModal onClose={() => setShowSettings(false)} />}
      {showPapyrus && <PapyrusPanel onClose={() => setShowPapyrus(false)} />}
      {showSwf && <SwfPanel onClose={() => setShowSwf(false)} />}
      {showNif && <NifPanel onClose={() => setShowNif(false)} />}
      {showMasters && <MastersPanel onClose={() => setShowMasters(false)} />}
      {showArchive && <ArchivePanel onClose={() => setShowArchive(false)} />}
      {showLocalization && <LocalizationPanel onClose={() => setShowLocalization(false)} />}
      {showAudio && <AudioPanel onClose={() => setShowAudio(false)} />}
      {showCell && <CellPanel onClose={() => setShowCell(false)} />}
      {showSpreadsheet && <SpreadsheetPanel onClose={() => setShowSpreadsheet(false)} />}
      {showBlueprint && <BlueprintPanel onClose={() => setShowBlueprint(false)} />}
      {showEditors && <EditorsHome onClose={() => setShowEditors(false)} />}
      {showHkx && <HkxPanel onClose={() => setShowHkx(false)} />}
    </div>
  );
}

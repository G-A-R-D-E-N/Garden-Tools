import { useState, useEffect, useRef, useCallback } from 'react';
import {
  Files, Search, GitCompare, AlertCircle, ScrollText, Box,
  ChevronRight, ChevronDown, Database, FileBox, X, Zap, Rocket,
  Link2, Archive as ArchiveIcon, Music, Boxes,
} from 'lucide-react';
import PapyrusPanel from './PapyrusPanel';
import NifPanel from './NifPanel';
import MastersPanel from './MastersPanel';
import ArchivePanel from './ArchivePanel';
import AudioPanel from './AudioPanel';
import CellPanel from './CellPanel';

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
import './MainShell.css';
import SettingsModal from './SettingsModal';
import RecordView from './RecordView';
import ChatPanel from './ChatPanel';
import TopBar, { type ShellTab } from './Shell/TopBar';
import DetailRail from './Shell/DetailRail';
import StatusBar from './Shell/StatusBar';
import type {
  ConflictMatrix, SearchHit, ConflictEntry, RecordTypeEntry, LoadOrderSummary,
} from './backend';
import type { RecordTab } from './RecordView';

interface RecordNode {
  Key: string;
  ConflictStatus: number;
  IsRecordNode: boolean;
  HasChildren: boolean;
  FilePath: string;
}

const ExplorerTree = ({ path, node, backend, onOpenRecord }: { path: string, node: RecordNode, backend: any, onOpenRecord: (path: string, node: RecordNode) => void }) => {
  const [expanded, setExpanded] = useState(false);
  const [children, setChildren] = useState<RecordNode[]>([]);
  const [loading, setLoading] = useState(false);

  const toggle = async () => {
    if (!expanded) {
      if (node.HasChildren && children.length === 0) {
        setLoading(true);
        const json = await backend.GetChildren(path);
        setChildren(JSON.parse(json));
        setLoading(false);
      }
    }
    setExpanded(!expanded);
  };

  return (
    <div className="tree-node">
      <div
        className={`tree-row ${node.IsRecordNode ? 'is-record' : ''}`}
        onClick={node.IsRecordNode ? () => onOpenRecord(path, node) : toggle}
      >
        <div className="tree-icon-container">
          {node.HasChildren ? (
            expanded ? <ChevronDown size={14} /> : <ChevronRight size={14} />
          ) : <div style={{width: 14}}/>}
        </div>
        <div className="tree-icon">
          {node.IsRecordNode ? <FileBox size={14} color="#569CD6"/> : <Database size={14} color="#DCDCAA" />}
        </div>
        <span
          className="tree-label"
          style={node.ConflictStatus === 1 ? { color: '#c79a5e' }       // conflict winner -> amber
            : node.ConflictStatus === 2 ? { color: '#b07a7a' }          // conflict loser -> red
            : undefined}
        >{node.Key}</span>
        {loading && <span className="tree-loading">...</span>}
      </div>
      {expanded && (
        <div className="tree-children">
          {children.map((c, i) => (
            <ExplorerTree key={`${c.Key}:${i}`} path={`${path}\\${c.Key}`} node={c} backend={backend} onOpenRecord={onOpenRecord} />
          ))}
        </div>
      )}
    </div>
  );
};

export default function MainShell() {
  const [activeTab, setActiveTab] = useState('explorer');
  // Everything the left pane does lives under Explorer: the plugin tree, search, errors and the
  // conflict list. Separate activity-bar entries for these rendered the same pane or nearly so.
  const [treeMode, setTreeMode] = useState<'files' | 'conflicts'>(
    () => (localStorage.getItem('treeMode') as 'files' | 'conflicts') || 'files');
  useEffect(() => { localStorage.setItem('treeMode', treeMode); }, [treeMode]);
  const [showSettings, setShowSettings] = useState(false);
  const [showPapyrus, setShowPapyrus] = useState(false);
  const [showNif, setShowNif] = useState(false);
  const [showMasters, setShowMasters] = useState(false);
  const [showArchive, setShowArchive] = useState(false);
  const [showAudio, setShowAudio] = useState(false);
  const [showCell, setShowCell] = useState(false);

  // Top-bar workspace tab (Home vs Record Viewer) + right-rail / chat visibility.
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

  // Sidebar width, persisted like the chat width. Task 1.4 asked for resizable splitters; the
  // chat already had one, so this is the same mechanism on the other side.
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

  // Theme: dark by default; persisted in localStorage.
  const [isDark, setIsDark] = useState(() => localStorage.getItem('theme') !== 'light');
  useEffect(() => {
    document.documentElement.setAttribute('data-theme', isDark ? 'dark' : 'light');
    localStorage.setItem('theme', isDark ? 'dark' : 'light');
  }, [isDark]);

  // Record-level tab (Record View / Conflicts / References / History / Dependencies).
  const [recordTab, setRecordTab] = useState<RecordTab>('grid');
  const [plugins, setPlugins] = useState<RecordNode[]>([]);
  const [backend, setBackend] = useState<any>(null);
  const [errorStr, setErrorStr] = useState<string>("");
  const [status, setStatus] = useState<string>("");
  const [progress, setProgress] = useState<number | null>(null);   // 0-100, or null = hidden

  // Open records, one per tab. The active one is what the centre viewport and the rail show;
  // keeping them in an array rather than a single slot is what lets a jump to a referenced record
  // leave the record you came from open behind you.
  interface OpenRec { title: string; plugin: string; matrix: ConflictMatrix }
  const [openRecords, setOpenRecords] = useState<OpenRec[]>([]);
  const [activeKey, setActiveKey] = useState<string>('');
  const [recordLoading, setRecordLoading] = useState(false);

  const openRecord = openRecords.find(r => r.matrix.FormKey === activeKey) ?? null;

  /** Add or focus a record tab. Re-opening an already-open record refreshes it in place. */
  const putRecord = useCallback((rec: OpenRec) => {
    setOpenRecords(prev => {
      const at = prev.findIndex(r => r.matrix.FormKey === rec.matrix.FormKey);
      if (at === -1) return [...prev, rec];
      const next = [...prev];
      next[at] = rec;
      return next;
    });
    setActiveKey(rec.matrix.FormKey);
  }, []);

  const closeRecordTab = useCallback((formKey: string) => {
    setOpenRecords(prev => {
      const at = prev.findIndex(r => r.matrix.FormKey === formKey);
      if (at === -1) return prev;
      const next = prev.filter(r => r.matrix.FormKey !== formKey);
      // Focus the neighbour rather than dumping the user on the welcome screen.
      setActiveKey(cur => (cur !== formKey ? cur : (next[at] ?? next[at - 1])?.matrix.FormKey ?? ''));
      return next;
    });
  }, []);

  // MCP live feed -- updated by tool-call events pushed from C# via PostWebMessageAsJson.
  const [mcpFeed, setMcpFeed] = useState<McpLiveMsg[]>([]);
  const [mcpActive, setMcpActive] = useState(false);
  const mcpActiveTimerRef = useRef<ReturnType<typeof setTimeout> | null>(null);
  // Field path of the last AI write -- passed to RecordView so it can flash that row.
  const [aiHighlightField, setAiHighlightField] = useState('');
  const highlightTimerRef = useRef<ReturnType<typeof setTimeout> | null>(null);

  // Stable refs so the message-handler closure (which runs only once on mount) can always
  // read the latest openRecord and call the latest reloadMatrix without stale captures.
  const openRecordRef = useRef(openRecord);
  openRecordRef.current = openRecord;

  // Explorer search: filters the plugin list by name and searches records across the load order.
  const [search, setSearch] = useState('');
  const [searchResults, setSearchResults] = useState<SearchHit[] | null>(null);

  // Load-order totals for the status bar. The type index is what the Visible count is computed
  // from; it is not a tree of its own any more.
  const [recordTypes, setRecordTypes] = useState<RecordTypeEntry[]>([]);
  const [loadOrderSummary, setLoadOrderSummary] = useState<LoadOrderSummary | null>(null);

  /** Refresh the load-order totals the status bar reads. */
  const refreshNavigator = useCallback(async () => {
    const b = window.chrome?.webview?.hostObjects?.backend;
    if (!b) return;
    try {
      const [typesJson, summaryJson] = await Promise.all([
        b.GetRecordTypeIndex(), b.GetLoadOrderSummary(),
      ]);
      setRecordTypes(JSON.parse(typesJson));
      setLoadOrderSummary(JSON.parse(summaryJson));
    } catch (e) {
      setErrorStr(`Load-order summary failed: ${e}`);
    }
  }, []);

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

    // C# pushes loader progress via PostWebMessageAsJson: {Type:"SetStatus",Text} / {Type:"SetProgress",Value}.
    // Value is 0-100, or negative to hide the bar. e.data is the parsed object (string fallback just in case).
    const onMessage = (e: any) => {
      const data = typeof e.data === 'string' ? JSON.parse(e.data) : e.data;
      if (!data || !data.Type) return;
      if (data.Type === 'SetStatus') setStatus(data.Text || "");
      if (data.Type === 'SetProgress') setProgress(data.Value < 0 ? null : data.Value);
      if (data.Type === 'McpLive') {
        const ev: McpLiveMsg = { ...data, ts: Date.now(), isNew: true };
        setMcpFeed(prev => [ev, ...prev].slice(0, 12));
        setMcpActive(true);
        if (mcpActiveTimerRef.current) clearTimeout(mcpActiveTimerRef.current);
        mcpActiveTimerRef.current = setTimeout(() => setMcpActive(false), 2000);
        if (ev.IsWrite && ev.Record) {
          const cur = openRecordRef.current;
          if (cur && cur.matrix.FormKey === ev.Record) {
            reloadMatrixRef.current();
          } else {
            // Navigate to the record being edited -- auto-switches to record tab.
            openByFormKeyRef.current(ev.Record, ev.Plugin);
          }
          // Flash the exact field the AI just wrote.
          if (ev.Field) {
            if (highlightTimerRef.current) clearTimeout(highlightTimerRef.current);
            setAiHighlightField(ev.Field);
            highlightTimerRef.current = setTimeout(() => setAiHighlightField(''), 3000);
          }
        } else if (ev.IsWrite && openRecordRef.current) {
          reloadMatrixRef.current();
        }
      }
      // AI streaming events are handled inside ChatPanel's own listener.
    };
    // @ts-ignore
    window.chrome?.webview?.addEventListener('message', onMessage);

    const init = async () => {
      try {
        // @ts-ignore
        if (window.chrome?.webview?.hostObjects?.appInterop) {
          // @ts-ignore
          const b = window.chrome.webview.hostObjects.appInterop;
          // WebView2 host objects are CALLABLE proxies (typeof === 'function'), so the plain
          // setBackend(b) form makes React treat b as a state-updater and invoke it as a function.
          // That calls the COM object itself with no method name -> "Invalid number of parameters"
          // (0x8002000E). The functional-updater form stores the proxy without calling it.
          setBackend(() => b);
          await refreshPlugins();
        }
      } catch (err: any) {
        console.error("Init Error:", err);
        setErrorStr(err?.message || JSON.stringify(err) || String(err));
      }
    };
    init();

    return () => {
      window.removeEventListener('unhandledrejection', handleRejection);
      // @ts-ignore
      window.chrome?.webview?.removeEventListener('message', onMessage);
    };
  }, []);

  // Read the host object fresh each call (no stale closure) and refresh the plugin tree once.
  const refreshPlugins = async () => {
    const b = window.chrome?.webview?.hostObjects?.appInterop;
    if (!b) return;
    const json = await b.GetPlugins();
    setPlugins(JSON.parse(json));
    setErrorStr("");
    // The navigator reads the load order independently of the plugin tree, so it has to be told.
    await refreshNavigator();
  };

  const handleLoadEnv = async () => {
    const b = window.chrome?.webview?.hostObjects?.appInterop;
    if (!b) return;
    try {
      await b.LoadEnvironment();   // awaits the full load; progress arrives via web messages
      await refreshPlugins();
    } catch (err: any) {
      setErrorStr(err?.message || JSON.stringify(err));
    } finally {
      setProgress(null);
    }
  };

  const handleLoadMo2 = async () => {
    const b = window.chrome?.webview?.hostObjects?.appInterop;
    if (!b) return;
    try {
      const path = await b.BrowseForMo2Folder();   // native folder picker
      if (!path) return;                            // cancelled or not a valid MO2 instance
      await b.OpenMo2Profile(path);                 // awaits the full load
      await refreshPlugins();
    } catch (err: any) {
      setErrorStr(err?.message || JSON.stringify(err));
    } finally {
      setProgress(null);
    }
  };

  // Re-read the tree (and the open record) from the current state -- picks up the AI's latest edits
  // without the heavy Open MO2 reload.
  const handleRefresh = async () => {
    const b = window.chrome?.webview?.hostObjects?.appInterop;
    if (!b) return;
    try {
      const json = await b.RefreshTree();
      setPlugins(JSON.parse(json));
      await refreshNavigator();
      if (openRecord) await reloadMatrix();   // refresh the open record's values too
      setStatus('Refreshed.');
    } catch (err: any) {
      setErrorStr(err?.message || JSON.stringify(err));
    }
  };

  const handleScanConflicts = async () => {
    const b = window.chrome?.webview?.hostObjects?.appInterop;
    if (!b) return;
    try {
      const summary = await b.ScanConflicts();   // progress arrives via web messages
      setStatus(summary);
      await refreshPlugins();                     // re-tint records as they reload
    } catch (err: any) {
      setErrorStr(err?.message || JSON.stringify(err));
    } finally {
      setProgress(null);
    }
  };

  const [brokenRefsReport, setBrokenRefsReport] = useState<string | null>(null);
  const [brokenRefsScanning, setBrokenRefsScanning] = useState(false);

  const handleScanBrokenRefs = async () => {
    const b = window.chrome?.webview?.hostObjects?.appInterop;
    if (!b) return;
    setBrokenRefsScanning(true);
    setStatus('Scanning for broken references...');
    try {
      const report = await b.ScanBrokenRefs();
      setBrokenRefsReport(report);
      setStatus('Broken-ref scan complete.');
    } catch (err: any) {
      setErrorStr(err?.message || JSON.stringify(err));
    } finally {
      setBrokenRefsScanning(false);
      setProgress(null);
    }
  };

  // Open a record in the center viewport: resolve its FormKey, then fetch its populated field tree.
  const openRecordInView = async (path: string, node: RecordNode) => {
    const app = window.chrome?.webview?.hostObjects?.appInterop;
    const back = window.chrome?.webview?.hostObjects?.backend;
    if (!app || !back) { setErrorStr("Record bridge unavailable."); return; }
    setRecordLoading(true);
    try {
      const formKey = await app.OpenRecord(path);   // node's FormKey ("" if unresolved)
      if (!formKey) { setErrorStr(`Couldn't resolve a FormKey for ${node.Key}.`); return; }
      const plugin = path.split(/[\\/]/)[0];
      // xEdit-style conflict matrix: every plugin that touches this record becomes a column.
      const matrix: ConflictMatrix = JSON.parse(await back.GetConflictMatrix(formKey));
      putRecord({ title: node.Key, plugin, matrix });
    } catch (err: any) {
      setErrorStr(err?.message || String(err));
    } finally {
      setRecordLoading(false);
    }
  };

  // Debounced record search across the load order (EditorID / FormID). Plugin-name filtering is local.
  useEffect(() => {
    const q = search.trim();
    if (q.length < 2) { setSearchResults(null); return; }
    const t = setTimeout(async () => {
      const back = window.chrome?.webview?.hostObjects?.backend;
      if (!back) return;
      try { setSearchResults(JSON.parse(await back.SearchRecords(q, ''))); }
      catch { setSearchResults([]); }
    }, 300);
    return () => clearTimeout(t);
  }, [search]);

  // Conflicts panel
  const [conflicts, setConflicts] = useState<ConflictEntry[] | null>(null);
  const [conflictSel, setConflictSel] = useState<Record<string, boolean>>({});
  const [conflictBusy, setConflictBusy] = useState(false);

  const loadConflicts = async () => {
    const b = window.chrome?.webview?.hostObjects?.backend;
    if (!b) return;
    setConflicts(null);
    try { setConflicts(JSON.parse(await b.GetConflicts())); }
    catch (e: any) { setErrorStr(e?.message || String(e)); setConflicts([]); }
  };

  // Load the conflict list the first time the Conflicts tab is opened.
  useEffect(() => {
    if (activeTab === 'explorer' && treeMode === 'conflicts' && conflicts === null) loadConflicts();
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [activeTab, treeMode]);

  const batchCopyAsOverride = async () => {
    const b = window.chrome?.webview?.hostObjects?.backend;
    if (!b || !conflicts) return;
    const picked = conflicts.filter(c => conflictSel[c.FormKey]);
    if (picked.length === 0) { setStatus('Select at least one conflict first.'); return; }
    const patch = window.prompt(`Copy ${picked.length} record(s) as override into which patch plugin?`, 'ConflictPatch.esp');
    if (!patch) return;
    setConflictBusy(true);
    try {
      const items = picked.map(c => ({ formKey: c.FormKey, source: c.Winner }));
      const res = JSON.parse(await b.CopyAsOverrideMany(JSON.stringify(items), patch));
      setStatus(`Copied ${res.ok}/${res.total} into ${patch}.${res.failed ? ` ${res.failed} failed.` : ''} Save ${patch} from a record's context menu, then enable it last.`);
      if (!res.failed) setConflictSel({});
    } catch (e: any) {
      setStatus('Batch copy failed: ' + (e?.message || e));
    } finally { setConflictBusy(false); }
  };

  // Open a record directly by FormKey (from a search hit).
  const openHit = async (hit: SearchHit) => {
    const back = window.chrome?.webview?.hostObjects?.backend;
    if (!back) return;
    setRecordLoading(true);
    try {
      const json = await back.GetConflictMatrix(hit.FormKey);
      const matrix: ConflictMatrix | null = json ? JSON.parse(json) : null;
      if (!matrix) { setErrorStr(`No record for ${hit.FormKey}.`); return; }
      putRecord({ title: hit.EditorID || hit.FormKey, plugin: hit.Plugin, matrix });
    } catch (err: any) {
      setErrorStr(err?.message || String(err));
    } finally {
      setRecordLoading(false);
    }
  };

  // Open any record by FormKey (used by Referenced-By jump navigation and the Conflicts panel).
  const openByFormKey = async (formKey: string, pluginName: string) => {
    const b = window.chrome?.webview?.hostObjects?.backend;
    if (!b) return;
    setRecordLoading(true);
    try {
      const json = await b.GetConflictMatrix(formKey);
      const matrix: ConflictMatrix | null = json ? JSON.parse(json) : null;
      if (!matrix) { setErrorStr(`No record for ${formKey}.`); return; }
      putRecord({ title: matrix.EditorID || formKey, plugin: pluginName || matrix.Winner, matrix });
    } catch (err: any) {
      setErrorStr(err?.message || String(err));
    } finally {
      setRecordLoading(false);
    }
  };

  // Re-fetch the open record's conflict matrix after an edit (a new patch column may appear).
  const reloadMatrix = async () => {
    const b = window.chrome?.webview?.hostObjects?.backend;
    if (!b || !openRecord) return;
    try {
      const json = await b.GetConflictMatrix(openRecord.matrix.FormKey);
      const matrix: ConflictMatrix | null = json ? JSON.parse(json) : null;
      // Record gone (e.g. deleted from its only plugin) -> close that tab.
      if (!matrix || !matrix.Plugins || matrix.Plugins.length === 0) closeRecordTab(openRecord.matrix.FormKey);
      else setOpenRecords(prev => prev.map(r => (r.matrix.FormKey === matrix.FormKey ? { ...r, matrix } : r)));
    } catch (err: any) {
      setErrorStr(err?.message || String(err));
    }
  };

  // Keep reloadMatrix and openByFormKey refs current so the onMessage handler (mounted once)
  // always calls the latest version with a fresh closure over current state.
  const reloadMatrixRef = useRef(reloadMatrix);
  reloadMatrixRef.current = reloadMatrix;
  const openByFormKeyRef = useRef(openByFormKey);
  openByFormKeyRef.current = openByFormKey;

  // Opening any record switches to the Record Viewer and resets the inner tab.
  useEffect(() => {
    if (openRecord) { setShellTab('record'); setRecordTab('grid'); }
  }, [openRecord?.matrix?.FormKey]);

  // Command-bar search for the top bar: reuse the existing SearchRecords bridge.
  const commandSearch = useCallback(async (q: string): Promise<SearchHit[]> => {
    const back = window.chrome?.webview?.hostObjects?.backend;
    if (!back) return [];
    try { return JSON.parse(await back.SearchRecords(q, '')); }
    catch { return []; }
  }, []);

  /** The top bar's close button acts on the active tab; Home is only reached when none remain. */
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
      <TopBar
        activeTab={shellTab}
        hasRecord={!!openRecord}
        recordTitle={openRecord?.title ?? ''}
        onSelectTab={setShellTab}
        onCloseRecord={closeRecord}
        onOpenSettings={() => setShowSettings(true)}
        onToggleRail={() => setRailVisible(v => !v)}
        railVisible={railVisible}
        onToggleChat={() => setChatVisible(v => !v)}
        chatVisible={chatVisible}
        isDark={isDark}
        onToggleTheme={() => setIsDark(v => !v)}
        onSearch={commandSearch}
        onOpenHit={openHit}
      />

      {/* LEFT pane: slim activity rail + navigator sidebar */}
      <div className="shell-left">
      <div className="activity-bar">
        <div className="activity-top">
          <button className={`activity-btn ${activeTab === 'explorer' ? 'active' : ''}`} onClick={() => setActiveTab('explorer')} title="Explorer: records, files, search and errors">
            <Files size={22} strokeWidth={1.5} />
          </button>
          <button className={`activity-btn ${showPapyrus ? 'active' : ''}`} onClick={() => setShowPapyrus(true)} title="Papyrus (compile / decompile)">
            <ScrollText size={22} strokeWidth={1.5} />
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
          <button className={`activity-btn ${showAudio ? 'active' : ''}`} onClick={() => setShowAudio(true)} title="Audio (convert to/from xWMA, merge/split fuz)">
            <Music size={22} strokeWidth={1.5} />
          </button>
          <button className={`activity-btn ${showCell ? 'active' : ''}`} onClick={() => setShowCell(true)} title="Cell Viewer (read-only 3D, like Creation Kit's render window)">
            <Boxes size={22} strokeWidth={1.5} />
          </button>
        </div>
      </div>

      {/* Sidebar */}
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
            onChange={e => setSearch(e.target.value)}
            placeholder="Search plugins & records…"
          />
          {search && <button className="sidebar-search-clear" onClick={() => setSearch('')} title="Clear"><X size={13} /></button>}
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

        {activeTab === 'explorer' && treeMode === 'files' && (
        <div className="sidebar-tree">
          {plugins.length === 0 && !errorStr ? (
            <div className="sidebar-empty">
              No plugins loaded. Click "Load Env" or "Open MO2".
            </div>
          ) : (
            <>
              {/* Plugins (filtered by name when searching) */}
              {plugins
                .filter(p => !search.trim() || p.Key.toLowerCase().includes(search.trim().toLowerCase()))
                .map((p, i) => (
                  <ExplorerTree key={`${p.Key}:${i}`} path={p.Key} node={p} backend={backend} onOpenRecord={openRecordInView} />
                ))}

              {/* Record search results */}
              {search.trim().length >= 2 && (
                <div className="sidebar-results">
                  <div className="sidebar-results-head">
                    RECORDS {searchResults === null ? '…' : `(${searchResults.length})`}
                  </div>
                  {searchResults === null ? (
                    <div className="sidebar-empty">Searching…</div>
                  ) : searchResults.length === 0 ? (
                    <div className="sidebar-empty">No matching records.</div>
                  ) : (
                    searchResults.map((h, i) => (
                      <div key={`${h.Plugin}:${h.FormKey}:${i}`} className="sidebar-result" onClick={() => openHit(h)} title={`${h.FormKey} · ${h.Plugin}`}>
                        <FileBox size={13} color="#569CD6" />
                        <span className="sr-id">{h.EditorID || h.FormKey}</span>
                        <span className="sr-type">{h.Type}</span>
                      </div>
                    ))
                  )}
                </div>
              )}
            </>
          )}
        </div>
        )}
        {activeTab === 'explorer' && treeMode === 'conflicts' && (
          <div className="conflicts-panel">
            <div className="conflicts-actions">
              <button className="sidebar-action-btn" onClick={loadConflicts}>Rescan</button>
              <button className="sidebar-action-btn" disabled={conflictBusy} onClick={batchCopyAsOverride}>
                Copy selected → patch
              </button>
            </div>
            {conflicts === null ? (
              <div className="sidebar-empty">Scanning conflicts…</div>
            ) : conflicts.length === 0 ? (
              <div className="sidebar-empty">No conflicts. Load a modlist and Scan Conflicts.</div>
            ) : (
              <div className="conflicts-list">
                {conflicts.map((c, i) => (
                  <div key={`${c.FormKey}:${i}`} className="conflict-row">
                    <input
                      type="checkbox"
                      checked={!!conflictSel[c.FormKey]}
                      onChange={e => setConflictSel(s => ({ ...s, [c.FormKey]: e.target.checked }))}
                    />
                    <span className="conflict-id" onClick={() => openByFormKey(c.FormKey, c.Winner)} title={`${c.FormKey} · ${c.Plugins.length} plugins`}>
                      {c.EditorID || c.FormKey}
                    </span>
                    <span className="conflict-type">{c.Type}</span>
                    <span className="conflict-count">{c.Plugins.length}</span>
                  </div>
                ))}
              </div>
            )}
          </div>
        )}
      </div>
      <div className="sidebar-resizer" onMouseDown={onSidebarResizeStart} />
      </div>{/* /shell-left */}

      {/* CENTER pane: Home placeholder or the record workspace */}
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

      {/* RIGHT pane: detail rail (Phase 5 fills this in) */}
      {railVisible && (
        <DetailRail
          matrix={openRecord?.matrix ?? null}
          onOpenConflictsTab={() => { setRecordTab('conflicts'); setShellTab('record'); }}
          onOpenRecord={openByFormKey}
          onAddToFilter={needle => {
            setSearch(needle);
            setActiveTab('explorer');
            setTreeMode('files');
          }}
        />
      )}

      {/* AI Panel - Claude (always mounted so the live session survives a toggle; hidden via CSS) */}
      <div className="shell-chat" style={{ display: chatVisible ? undefined : 'none', width: chatVisible ? chatWidth : undefined }}>
        <div className="chat-resizer" onMouseDown={onChatResizeStart} />
        <ChatPanel />
      </div>

      {/* Status bar (bottom): loader status + load-order parity */}
      <StatusBar
        status={status}
        progress={progress}
        recordCount={loadOrderSummary?.TotalRecords ?? 0}
        visibleCount={recordTypes.reduce((n, t) => n + t.Count, 0)}
        selectedCount={openRecord ? 1 : 0}
        loadOrder={loadOrderSummary?.Plugins ?? plugins.map(p => p.Key)}
      />

      {brokenRefsReport !== null && (
        <div className="broken-refs-overlay" onClick={() => setBrokenRefsReport(null)}>
          <div className="broken-refs-modal" onClick={e => e.stopPropagation()}>
            <div className="broken-refs-header">
              <span>Crash Risk Report</span>
              <div className="broken-refs-header-actions">
                <button className="sidebar-action-btn" onClick={() => navigator.clipboard.writeText(brokenRefsReport)}>Copy</button>
                <button className="sidebar-action-btn" onClick={() => {
                  const blob = new Blob([brokenRefsReport], { type: 'text/plain' });
                  const url = URL.createObjectURL(blob);
                  const a = document.createElement('a');
                  a.href = url;
                  a.download = 'crash_risk_report.txt';
                  a.click();
                  URL.revokeObjectURL(url);
                }}>Export</button>
                <button className="broken-refs-close" onClick={() => setBrokenRefsReport(null)}>✕</button>
              </div>
            </div>
            <pre className="broken-refs-body">{brokenRefsReport}</pre>
          </div>
        </div>
      )}

      {showSettings && <SettingsModal onClose={() => setShowSettings(false)} />}
      {showPapyrus && <PapyrusPanel onClose={() => setShowPapyrus(false)} />}
      {showNif && <NifPanel onClose={() => setShowNif(false)} />}
      {showMasters && <MastersPanel onClose={() => setShowMasters(false)} />}
      {showArchive && <ArchivePanel onClose={() => setShowArchive(false)} />}
      {showAudio && <AudioPanel onClose={() => setShowAudio(false)} />}
      {showCell && <CellPanel onClose={() => setShowCell(false)} />}
    </div>
  );
}

import { useState, useEffect, useRef } from 'react';
import {
  Files, Search, GitCompare, Settings, AlertCircle,
  ChevronRight, ChevronDown, Database, FileBox, X, Zap
} from 'lucide-react';

interface McpLiveMsg {
  Tool: string;
  Plugin: string;
  Record: string;
  Summary: string;
  IsWrite: boolean;
  ts: number;
  isNew?: boolean;
}
import './MainShell.css';
import SettingsModal from './SettingsModal';
import RecordView from './RecordView';
import ChatPanel from './ChatPanel';
import type { ConflictMatrix, SearchHit } from './backend';

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
  const [showSettings, setShowSettings] = useState(false);
  const [plugins, setPlugins] = useState<RecordNode[]>([]);
  const [backend, setBackend] = useState<any>(null);
  const [errorStr, setErrorStr] = useState<string>("");
  const [status, setStatus] = useState<string>("");
  const [progress, setProgress] = useState<number | null>(null);   // 0-100, or null = hidden

  // The record currently shown in the center viewport (null = welcome screen).
  const [openRecord, setOpenRecord] = useState<{ title: string; plugin: string; matrix: ConflictMatrix } | null>(null);
  const [recordLoading, setRecordLoading] = useState(false);

  // MCP live feed — updated by tool-call events pushed from C# via PostWebMessageAsJson.
  const [mcpFeed, setMcpFeed] = useState<McpLiveMsg[]>([]);
  const [mcpActive, setMcpActive] = useState(false);
  const mcpActiveTimerRef = useRef<ReturnType<typeof setTimeout> | null>(null);

  // Stable refs so the message-handler closure (which runs only once on mount) can always
  // read the latest openRecord and call the latest reloadMatrix without stale captures.
  const openRecordRef = useRef(openRecord);
  openRecordRef.current = openRecord;

  // Explorer search: filters the plugin list by name and searches records across the load order.
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
        if (ev.IsWrite && openRecordRef.current) {
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

  // Re-read the tree (and the open record) from the current state — picks up the AI's latest edits
  // without the heavy Open MO2 reload.
  const handleRefresh = async () => {
    const b = window.chrome?.webview?.hostObjects?.appInterop;
    if (!b) return;
    try {
      const json = await b.RefreshTree();
      setPlugins(JSON.parse(json));
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
      setOpenRecord({ title: node.Key, plugin, matrix });
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

  // Open a record directly by FormKey (from a search hit).
  const openHit = async (hit: SearchHit) => {
    const back = window.chrome?.webview?.hostObjects?.backend;
    if (!back) return;
    setRecordLoading(true);
    try {
      const json = await back.GetConflictMatrix(hit.FormKey);
      const matrix: ConflictMatrix | null = json ? JSON.parse(json) : null;
      if (!matrix) { setErrorStr(`No record for ${hit.FormKey}.`); return; }
      setOpenRecord({ title: hit.EditorID || hit.FormKey, plugin: hit.Plugin, matrix });
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
      // Record gone (e.g. deleted from its only plugin) -> close the editor pane.
      if (!matrix || !matrix.Plugins || matrix.Plugins.length === 0) setOpenRecord(null);
      else setOpenRecord(o => (o ? { ...o, matrix } : o));
    } catch (err: any) {
      setErrorStr(err?.message || String(err));
    }
  };

  // Keep reloadMatrix ref current so the onMessage handler (mounted once) always calls the
  // latest version, which has a fresh closure over the current openRecord state.
  const reloadMatrixRef = useRef(reloadMatrix);
  reloadMatrixRef.current = reloadMatrix;

  return (
    <div className="shell-container animate-fade-in">
      {/* Activity Bar */}
      <div className="activity-bar">
        <div className="activity-top">
          <button className={`activity-btn ${activeTab === 'explorer' ? 'active' : ''}`} onClick={() => setActiveTab('explorer')}>
            <Files size={24} strokeWidth={1.5} />
          </button>
          <button className={`activity-btn ${activeTab === 'search' ? 'active' : ''}`} onClick={() => setActiveTab('search')}>
            <Search size={24} strokeWidth={1.5} />
          </button>
          <button className={`activity-btn ${activeTab === 'errors' ? 'active' : ''}`} onClick={() => setActiveTab('errors')}>
            <AlertCircle size={24} strokeWidth={1.5} />
          </button>
          <button className={`activity-btn ${activeTab === 'compare' ? 'active' : ''}`} onClick={() => setActiveTab('compare')}>
            <GitCompare size={24} strokeWidth={1.5} />
          </button>
        </div>
        <div className="activity-bottom">
          <button className={`activity-btn ${showSettings ? 'active' : ''}`} onClick={() => setShowSettings(true)} title="Settings">
            <Settings size={24} strokeWidth={1.5} />
          </button>
        </div>
      </div>

      {/* Sidebar */}
      <div className="sidebar">
        <div className="sidebar-header">
          <h2>EXPLORER</h2>
        </div>
        <div className="sidebar-actions">
          <button className="sidebar-action-btn" onClick={handleLoadEnv}>Load Env</button>
          <button className="sidebar-action-btn" onClick={handleLoadMo2}>Open MO2</button>
          <button className="sidebar-action-btn" onClick={handleScanConflicts}>Scan Conflicts</button>
          <button className="sidebar-action-btn" onClick={handleRefresh}>Refresh</button>
        </div>
        <div className="sidebar-search">
          <Search size={13} />
          <input
            value={search}
            onChange={e => setSearch(e.target.value)}
            placeholder="Search plugins & records…"
          />
          {search && <button className="sidebar-search-clear" onClick={() => setSearch('')} title="Clear"><X size={13} /></button>}
        </div>
        <div className="sidebar-tree">
          {errorStr && <div className="sidebar-empty" style={{ color: '#f44747' }}>{errorStr}</div>}
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
      </div>

      {/* Main Content Area */}
      <div className="main-area">
        <div className="editor-tabs">
          <div className="editor-tab active">
            {openRecord
              ? <><Database size={14} color="#569CD6"/> {openRecord.title}</>
              : <><Zap size={14} color="#F44747"/> Welcome</>}
            {openRecord && (
              <button className="tab-close" onClick={() => setOpenRecord(null)} title="Close"><X size={14} /></button>
            )}
          </div>
        </div>

        <div className={`editor-content ${openRecord && !recordLoading ? 'has-record' : ''}`}>
          {recordLoading ? (
            <div className="welcome-screen glass-panel">
              <Database size={48} color="var(--border-color)" />
              <p>Loading record…</p>
            </div>
          ) : openRecord ? (
            <RecordView matrix={openRecord.matrix} plugin={openRecord.plugin} onReload={reloadMatrix} />
          ) : (
            <div className="welcome-screen glass-panel">
              <Database size={64} color="var(--border-color)" />
              <h1>FO4 Record Editor</h1>
              <p>Welcome to the next-generation Modding IDE.</p>
              <div className="welcome-shortcuts">
                <p>Load your environment from the Explorer sidebar.</p>
                <p>Click a record to view its fields.</p>
              </div>
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
      </div>

      {/* AI Panel — Claude (sessions + slash commands) */}
      <ChatPanel />

      {/* Status bar (bottom): loader status text + progress bar */}
      <div className="status-bar">
        {progress !== null && (
          <div className="status-progress">
            <div
              className={`status-progress-fill ${progress === 0 ? 'indeterminate' : ''}`}
              style={{ width: `${Math.max(0, Math.min(100, progress))}%` }}
            />
          </div>
        )}
        <span className="status-text">{status || (progress !== null ? 'Working…' : 'Ready')}</span>
      </div>

      {showSettings && <SettingsModal onClose={() => setShowSettings(false)} />}
    </div>
  );
}

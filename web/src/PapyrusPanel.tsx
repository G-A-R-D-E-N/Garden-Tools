import { useState, useEffect, useCallback, type DragEvent, type ReactNode } from 'react';
import { ScrollText, X, FileCode2, Hammer, Play, FolderOpen, CheckCircle2, AlertTriangle, XCircle } from 'lucide-react';
import { getPapyrus } from './backend';
import './PapyrusPanel.css';

type Mode = 'decompile' | 'compile';
const LS = (k: string, d: string) => localStorage.getItem('papyrus.' + k) ?? d;
const LSB = (k: string, d: boolean) => { const v = localStorage.getItem('papyrus.' + k); return v === null ? d : v === '1'; };
const setLS = (k: string, v: string | boolean) => localStorage.setItem('papyrus.' + k, typeof v === 'boolean' ? (v ? '1' : '0') : v);

export default function PapyrusPanel({ onClose }: { onClose: () => void }) {
  const [mode, setMode] = useState<Mode>(() => (LS('mode', 'decompile') as Mode));
  const [source, setSource] = useState(() => LS('source', ''));
  const [output, setOutput] = useState(() => LS('output', ''));
  const [busy, setBusy] = useState(false);
  const [result, setResult] = useState('');
  const [log, setLog] = useState<string[]>([]);
  const [dragOver, setDragOver] = useState(false);
  const [lastOutDir, setLastOutDir] = useState('');

  // decompile options — default Write ON (smarter default: people expect files on disk)
  const [assembly, setAssembly] = useState(() => LSB('assembly', false));
  const [write, setWrite] = useState(() => LSB('write', true));

  // compile options
  const [imports, setImports] = useState(() => LS('imports', ''));
  const [flags, setFlags] = useState(() => LS('flags', ''));
  const [all, setAll] = useState(() => LSB('all', true));
  const [optimize, setOptimize] = useState(() => LSB('optimize', true));
  const [release, setRelease] = useState(() => LSB('release', false));
  const [compilerPath, setCompilerPath] = useState(() => LS('compilerPath', ''));

  const papyrus = getPapyrus();
  const unavailable = !papyrus;

  // persist on change
  useEffect(() => setLS('mode', mode), [mode]);
  useEffect(() => setLS('source', source), [source]);
  useEffect(() => setLS('output', output), [output]);
  useEffect(() => { setLS('assembly', assembly); }, [assembly]);
  useEffect(() => { setLS('write', write); }, [write]);
  useEffect(() => setLS('imports', imports), [imports]);
  useEffect(() => setLS('flags', flags), [flags]);
  useEffect(() => { setLS('all', all); setLS('optimize', optimize); setLS('release', release); }, [all, optimize, release]);
  useEffect(() => setLS('compilerPath', compilerPath), [compilerPath]);

  const appendLog = (line: string) =>
    setLog(prev => [`[${new Date().toLocaleTimeString()}] ${line}`, ...prev].slice(0, 200));
  const baseName = (p: string) => p.replace(/[\\/]+$/, '').split(/[\\/]/).pop() || p;

  const browseSourceFile = async () => {
    if (!papyrus) return;
    const filter = mode === 'decompile'
      ? 'Compiled Papyrus (*.pex)|*.pex|All files|*.*'
      : 'Papyrus source (*.psc)|*.psc|All files|*.*';
    const p = await papyrus.BrowseForFile('Select a script', filter);
    if (p) setSource(p);
  };
  const browseSourceFolder = async () => { if (papyrus) { const p = await papyrus.BrowseForFolder('Select a scripts folder'); if (p) setSource(p); } };
  const browseOutput = async () => { if (papyrus) { const p = await papyrus.BrowseForFolder('Select an output folder'); if (p) { setOutput(p); setWrite(true); } } };
  const browseCompiler = async () => { if (papyrus) { const p = await papyrus.BrowseForFile('Select PapyrusCompiler.exe', 'PapyrusCompiler|PapyrusCompiler.exe|Executables|*.exe'); if (p) setCompilerPath(p); } };

  // drag & drop: read the dropped file's bytes and stage them to a temp path the backend can use
  // (WebView2 doesn't expose dropped files' real OS paths to JS). Folders can't be read this way —
  // use the Folder button for those.
  const onDrop = useCallback(async (e: DragEvent) => {
    e.preventDefault(); setDragOver(false);
    if (!papyrus) return;
    const f = e.dataTransfer.files?.[0];
    if (!f) return;
    try {
      const buf = await f.arrayBuffer();
      let bin = ''; const bytes = new Uint8Array(buf);
      for (let i = 0; i < bytes.length; i++) bin += String.fromCharCode(bytes[i]);
      const b64 = btoa(bin);
      const path = await papyrus.StageDroppedFile(f.name, b64);
      if (path.startsWith('ERR:')) { appendLog('✗ drop failed — ' + path); return; }
      setSource(path);
      // auto-switch mode to match the dropped file type
      if (/\.pex$/i.test(f.name)) setMode('decompile');
      else if (/\.psc$/i.test(f.name)) setMode('compile');
      appendLog('• dropped ' + f.name + ' -> source set');
    } catch (err) {
      appendLog('✗ drop failed — ' + (err instanceof Error ? err.message : String(err)));
    }
  }, [papyrus]);

  const run = async () => {
    if (!papyrus || !source.trim()) return;
    setBusy(true); setResult('Working…');
    const name = baseName(source);
    try {
      const out = mode === 'decompile'
        ? await papyrus.Decompile(source, output, assembly, write)
        : await papyrus.Compile(source, output, imports, flags, all, optimize, release, compilerPath);
      const text = out || '(no output)';
      setResult(text);
      // remember an output dir for the "Open folder" button
      const savedMatch = text.match(/(?:SAVED ->|OUTPUT:|output -> )\s*(.+)/);
      if (savedMatch) setLastOutDir(savedMatch[1].trim());
      else if (output.trim()) setLastOutDir(output.trim());
      // log summary
      const isErr = bannerKind(text) === 'error';
      if (mode === 'decompile') {
        const kind = assembly ? 'disassembled' : 'decompiled';
        const savedLine = text.split('\n').find(l => /^(SAVED ->|RESULT:)/.test(l)) || `${text.split('\n').length} lines`;
        appendLog(`${isErr ? '✗' : '✓'} ${kind} ${name} — ${savedLine}`);
      } else {
        const r = text.split('\n').find(l => l.startsWith('RESULT:')) || (text.split('\n').find(l => l.trim()) ?? '');
        appendLog(`${isErr ? '✗' : '✓'} compiled ${name} — ${r}`);
      }
    } catch (e) {
      const msg = 'Error: ' + (e instanceof Error ? e.message : String(e));
      setResult(msg); appendLog(`✗ ${mode} ${name} — ${msg}`);
    } finally { setBusy(false); }
  };

  const openOut = async () => { if (papyrus && lastOutDir) await papyrus.OpenFolder(lastOutDir); };

  const banner = result ? makeBanner(result, mode) : null;

  return (
    <div className="papyrus-overlay" onClick={onClose}>
      <div className="papyrus-modal glass-panel" onClick={e => e.stopPropagation()}>
        <div className="papyrus-header">
          <span className="papyrus-title"><ScrollText size={16} /> Papyrus</span>
          <div className="papyrus-modes">
            <button className={`papyrus-mode ${mode === 'decompile' ? 'active' : ''}`} onClick={() => setMode('decompile')}><FileCode2 size={14} /> Decompile</button>
            <button className={`papyrus-mode ${mode === 'compile' ? 'active' : ''}`} onClick={() => setMode('compile')}><Hammer size={14} /> Compile</button>
          </div>
          <button className="papyrus-close" onClick={onClose} title="Close"><X size={16} /></button>
        </div>

        {unavailable && <div className="papyrus-warn">Papyrus bridge not available — run the desktop app (not the browser dev server).</div>}

        <div className="papyrus-body">
          <div className="papyrus-form">
            <label className="papyrus-field">
              <span>{mode === 'decompile' ? 'Source .pex (file, folder, or whole mod)' : 'Source .psc (file or folder)'}</span>
              <div
                className={`papyrus-drop ${dragOver ? 'over' : ''}`}
                onDragOver={e => { e.preventDefault(); setDragOver(true); }}
                onDragLeave={() => setDragOver(false)}
                onDrop={onDrop}
              >
                <input value={source} onChange={e => setSource(e.target.value)} placeholder="Path, or drag a .pex/.psc file here…" />
              </div>
              <div className="papyrus-input-row">
                <button className="sidebar-action-btn" onClick={browseSourceFile} disabled={unavailable}>File…</button>
                <button className="sidebar-action-btn" onClick={browseSourceFolder} disabled={unavailable}>Folder…</button>
              </div>
            </label>

            <label className="papyrus-field">
              <span>Output folder {mode === 'decompile' ? '(saving on by default)' : '(default: source folder)'}</span>
              <div className="papyrus-input-row">
                <input value={output} onChange={e => setOutput(e.target.value)} placeholder="(default: alongside the source)" />
                <button className="sidebar-action-btn" onClick={browseOutput} disabled={unavailable}>Folder…</button>
              </div>
            </label>

            {mode === 'decompile' ? (
              <div className="papyrus-opts">
                <label><input type="checkbox" checked={write} onChange={e => setWrite(e.target.checked)} /> Write files to disk</label>
                <label><input type="checkbox" checked={assembly} onChange={e => setAssembly(e.target.checked)} /> Assembly listing (.pas, inspect-only)</label>
              </div>
            ) : (
              <>
                <label className="papyrus-field">
                  <span>Extra import roots (F4SE + base added automatically)</span>
                  <input value={imports} onChange={e => setImports(e.target.value)} placeholder="e.g. E:\…\SomeFramework\Scripts\Source" />
                </label>
                <label className="papyrus-field">
                  <span>Flags file (default: Institute_Papyrus_Flags.flg)</span>
                  <input value={flags} onChange={e => setFlags(e.target.value)} placeholder="Institute_Papyrus_Flags.flg" />
                </label>
                <label className="papyrus-field">
                  <span>Compiler path (default: CK compiler)</span>
                  <div className="papyrus-input-row">
                    <input value={compilerPath} onChange={e => setCompilerPath(e.target.value)} placeholder="(auto-detect)" />
                    <button className="sidebar-action-btn" onClick={browseCompiler} disabled={unavailable}>Exe…</button>
                  </div>
                </label>
                <div className="papyrus-opts">
                  <label><input type="checkbox" checked={all} onChange={e => setAll(e.target.checked)} /> Compile all in folder</label>
                  <label><input type="checkbox" checked={optimize} onChange={e => setOptimize(e.target.checked)} /> Optimize (-op)</label>
                  <label><input type="checkbox" checked={release} onChange={e => setRelease(e.target.checked)} /> Release (-r)</label>
                </div>
              </>
            )}

            <button className="papyrus-run" onClick={run} disabled={busy || unavailable || !source.trim()}>
              <Play size={14} /> {busy ? 'Working…' : mode === 'decompile' ? 'Decompile' : 'Compile'}
            </button>
          </div>

          <div className="papyrus-output">
            {banner && (
              <div className={`papyrus-banner ${banner.kind}`}>
                {banner.kind === 'ok' ? <CheckCircle2 size={15} /> : banner.kind === 'warn' ? <AlertTriangle size={15} /> : <XCircle size={15} />}
                <span className="papyrus-banner-text">{banner.text}</span>
                {lastOutDir && <button className="papyrus-openfolder" onClick={openOut} title={lastOutDir}><FolderOpen size={13} /> Open folder</button>}
              </div>
            )}
            <div className="papyrus-output-head">
              <span>OUTPUT — {mode === 'decompile' ? (assembly ? 'assembly (.pas)' : 'decompiled source (.psc)') : 'compiler'}</span>
              {result && <button className="papyrus-copy" onClick={() => navigator.clipboard.writeText(result)}>Copy</button>}
            </div>
            <pre className="papyrus-output-body">{result ? renderHighlighted(result, mode, assembly) : 'Pick a script (or drag one in) and run.'}</pre>
            <div className="papyrus-log-head">
              <span>LOG ({log.length})</span>
              {log.length > 0 && <button className="papyrus-copy" onClick={() => setLog([])}>Clear</button>}
            </div>
            <div className="papyrus-log-body">
              {log.length === 0 ? <div className="papyrus-log-empty">No runs yet.</div>
                : log.map((l, i) => <div key={i} className={`papyrus-log-row ${l.includes('✗') ? 'err' : 'ok'}`}>{l}</div>)}
            </div>
          </div>
        </div>
      </div>
    </div>
  );
}

// ---- result banner ----
function bannerKind(text: string): 'ok' | 'warn' | 'error' {
  if (/\b0 failed\b/.test(text) || /Compilation succeeded/.test(text) || /^SAVED ->/m.test(text)) {
    if (/\b([1-9]\d*) failed\b/.test(text)) return 'warn';
    return 'ok';
  }
  if (/NOT SAVED/.test(text)) return 'warn';
  if (/FAILED|error|cannot|unknown|not a function|not found/i.test(text)) return 'error';
  return 'ok';
}
function makeBanner(text: string, mode: Mode): { kind: 'ok' | 'warn' | 'error'; text: string } {
  const result = text.split('\n').find(l => l.startsWith('RESULT:'));
  if (result) {
    const failed = (text.match(/(\d+)\s+failed/) || [])[1];
    const kind = failed && failed !== '0' ? 'warn' : 'ok';
    return { kind, text: result.replace('RESULT:', '').trim() };
  }
  if (/^SAVED ->/m.test(text)) return { kind: 'ok', text: text.split('\n').find(l => l.startsWith('SAVED ->'))!.replace('SAVED ->', 'Saved:').trim() };
  if (/NOT SAVED/.test(text)) return { kind: 'warn', text: 'Not saved — shown below only. Tick "Write files to disk" or set an Output folder.' };
  const k = bannerKind(text);
  return { kind: k, text: k === 'error' ? (mode === 'compile' ? 'Compile failed — see output' : 'Failed — see output') : 'Done' };
}

// ---- lightweight syntax highlighting (no deps) ----
const PSC_KEYWORDS = new Set(['scriptname', 'extends', 'import', 'function', 'endfunction', 'event', 'endevent',
  'if', 'else', 'elseif', 'endif', 'while', 'endwhile', 'return', 'property', 'endproperty', 'auto', 'autoreadonly',
  'const', 'native', 'global', 'self', 'parent', 'none', 'true', 'false', 'as', 'is', 'new', 'state', 'endstate',
  'struct', 'endstruct', 'group', 'endgroup', 'mandatory', 'hidden', 'conditional']);
const PSC_TYPES = new Set(['int', 'float', 'bool', 'string', 'var', 'actor', 'form', 'quest', 'objectreference',
  'scriptobject', 'message', 'keyword', 'perk', 'weapon', 'armor', 'potion', 'spell', 'globalvariable', 'activemagiceffect']);

function renderHighlighted(text: string, mode: Mode, assembly: boolean) {
  if (text.length > 200000) return text; // too big to tokenize cheaply
  const lines = text.replace(/\r/g, '').split('\n');
  const sourceMode = mode === 'decompile' && !assembly;
  return lines.map((line, i) => {
    if (!sourceMode) {
      // compile / assembly output: color whole lines by status
      let cls = '';
      if (/error|cannot|unknown|FAILED|No output|is not a function|DEPENDENCY HELP/i.test(line)) cls = 'tk-err';
      else if (/succeeded|^RESULT:.*0 failed|^SAVED ->|^OUTPUT:/i.test(line)) cls = 'tk-ok';
      else if (/RESULT:|Nexus:|https?:\/\//i.test(line)) cls = 'tk-key';
      return <div key={i} className={cls}>{line || ' '}</div>;
    }
    return <div key={i}>{tokenizePsc(line)}</div>;
  });
}

function tokenizePsc(line: string) {
  const out: ReactNode[] = [];
  // line comment (Papyrus: ';' to end). Naive but fine for decompiled output (no ';' in our strings).
  let code = line, comment = '';
  const semi = line.indexOf(';');
  if (semi >= 0) { code = line.slice(0, semi); comment = line.slice(semi); }
  const re = /("(?:[^"\\]|\\.)*")|([A-Za-z_][A-Za-z0-9_]*)|(\s+)|([^\sA-Za-z0-9_"]+)/g;
  let m: RegExpExecArray | null; let k = 0;
  while ((m = re.exec(code)) !== null) {
    if (m[1]) out.push(<span key={k++} className="tk-str">{m[1]}</span>);
    else if (m[2]) {
      const low = m[2].toLowerCase();
      if (PSC_KEYWORDS.has(low)) out.push(<span key={k++} className="tk-key">{m[2]}</span>);
      else if (PSC_TYPES.has(low)) out.push(<span key={k++} className="tk-type">{m[2]}</span>);
      else out.push(<span key={k++}>{m[2]}</span>);
    }
    else out.push(<span key={k++}>{m[3] || m[4]}</span>);
  }
  if (comment) out.push(<span key={k++} className="tk-comment">{comment}</span>);
  return out.length ? out : ' ';
}

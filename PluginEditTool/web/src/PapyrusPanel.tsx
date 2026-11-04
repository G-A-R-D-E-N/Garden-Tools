import {
  useState, useEffect, useCallback, useRef,
  type DragEvent, type ReactNode, type KeyboardEvent as ReactKeyboardEvent,
  type MouseEvent as ReactMouseEvent, type UIEvent as ReactUIEvent,
} from 'react';
import {
  ScrollText, X, FileCode2, Hammer, BookOpen, Play, FolderOpen, CheckCircle2, AlertTriangle,
  XCircle, Stethoscope, Save, ListTree, Crosshair,
} from 'lucide-react';
import {
  getPapyrus, type PapyrusAnalyzeResult, type PapyrusCheckResult, type PapyrusCompletionItem, type PapyrusCompletionResult,
  type PapyrusDiagnostic, type PapyrusReferencesResult, type PapyrusRenameEdit, type PapyrusRenameResult,
  type PapyrusSignature, type PapyrusSignatureResult, type PapyrusSymbolResult,
} from './backend';
import { useDialogs } from './useDialogs';
import PapyrusLocations, { type LocationRow } from './PapyrusLocations';
import { usePapyrusWorkspaceSearch } from './usePapyrusWorkspaceSearch';
import { applyByteEdits, byteOffset, samePath, utf8ToUtf16Index } from './papyrusOffsets';
import './PapyrusPanel.css';

type Mode = 'decompile' | 'compile' | 'lookup' | 'analyze';
type LookupKind = 'function' | 'script';
type CompletionView = { items: PapyrusCompletionItem[]; caret: number; replaceLength: number; active: number };
type LiveCheck = PapyrusCheckResult & { text: string };
type References = { name: string; rows: LocationRow[] };


type Engine = 'auto' | 'builtin' | 'creationkit';


const EDITOR_LINE_HEIGHT = 19;

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


  const [assembly, setAssembly] = useState(() => LSB('assembly', false));
  const [write, setWrite] = useState(() => LSB('write', true));


  const [imports, setImports] = useState(() => LS('imports', ''));
  const [flags, setFlags] = useState(() => LS('flags', ''));
  const [all, setAll] = useState(() => LSB('all', true));
  const [optimize, setOptimize] = useState(() => LSB('optimize', true));
  const [release, setRelease] = useState(() => LSB('release', false));
  const [compilerPath, setCompilerPath] = useState(() => LS('compilerPath', ''));
  const [engine, setEngine] = useState<Engine>(() => (LS('engine', 'auto') as Engine));


  const [lookupKind, setLookupKind] = useState<LookupKind>(() => (LS('lookupKind', 'function') as LookupKind));
  const [lookupScript, setLookupScript] = useState(() => LS('lookupScript', ''));
  const [lookupFunction, setLookupFunction] = useState(() => LS('lookupFunction', ''));



  const [buffer, setBuffer] = useState('');
  const [bufferPath, setBufferPath] = useState('');
  const [dirty, setDirty] = useState(false);
  const [analysis, setAnalysis] = useState<PapyrusAnalyzeResult | null>(null);
  const [analyzing, setAnalyzing] = useState(false);
  const [symbolInfo, setSymbolInfo] = useState<PapyrusSymbolResult | null>(null);
  const [liveCheck, setLiveCheck] = useState<LiveCheck | null>(null);
  const [completion, setCompletion] = useState<CompletionView | null>(null);
  const [signature, setSignature] = useState<PapyrusSignatureResult | null>(null);
  const [refs, setRefs] = useState<References | null>(null);
  const [note, setNote] = useState('');
  const [wsQuery, setWsQuery] = useState('');
  const wsInput = useRef<HTMLInputElement | null>(null);
  const editorRef = useRef<HTMLTextAreaElement | null>(null);
  const gutterRef = useRef<HTMLDivElement | null>(null);
  const checkRevision = useRef(0);
  const completionSeq = useRef(0);
  const signatureSeq = useRef(0);

  const papyrus = getPapyrus();
  const unavailable = !papyrus;
  const { confirm: askConfirm, prompt: askPrompt } = useDialogs();
  const workspace = usePapyrusWorkspaceSearch(papyrus, wsQuery, imports);


  useEffect(() => setLS('mode', mode), [mode]);
  useEffect(() => setLS('source', source), [source]);
  useEffect(() => setLS('output', output), [output]);
  useEffect(() => { setLS('assembly', assembly); }, [assembly]);
  useEffect(() => { setLS('write', write); }, [write]);
  useEffect(() => setLS('imports', imports), [imports]);
  useEffect(() => setLS('flags', flags), [flags]);
  useEffect(() => { setLS('all', all); setLS('optimize', optimize); setLS('release', release); }, [all, optimize, release]);
  useEffect(() => setLS('compilerPath', compilerPath), [compilerPath]);
  useEffect(() => setLS('engine', engine), [engine]);
  useEffect(() => setLS('lookupKind', lookupKind), [lookupKind]);
  useEffect(() => setLS('lookupScript', lookupScript), [lookupScript]);
  useEffect(() => setLS('lookupFunction', lookupFunction), [lookupFunction]);

  const appendLog = (line: string) =>
    setLog(prev => [`[${new Date().toLocaleTimeString()}] ${line}`, ...prev].slice(0, 200));
  const baseName = (p: string) => p.replace(/[\\/]+$/, '').split(/[\\/]/).pop() || p;
  const failure = (e: unknown) => setNote('Error: ' + (e instanceof Error ? e.message : String(e)));

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

  const loadIntoEditor = useCallback(async (path: string) => {
    if (!papyrus || !path.trim()) return;
    if (!/\.psc$/i.test(path.trim())) { appendLog('✗ analyze -- needs a .psc (this reads source, not compiled .pex)'); return; }
    if (dirty && !await askConfirm({
      title: 'Discard unsaved edits?', danger: true, confirmLabel: 'Discard',
      message: `Opening ${baseName(path)} replaces the buffer and discards unsaved edits to ${bufferPath ? baseName(bufferPath) : 'the untitled buffer'}.`,
    })) return;
    const text = await papyrus.ReadScript(path.trim());
    if (text.startsWith('ERR:')) { appendLog('✗ open -- ' + text.slice(4)); return; }
    setBuffer(text); setBufferPath(path.trim()); setDirty(false); setSymbolInfo(null);
    appendLog('• opened ' + baseName(path));
  }, [papyrus, dirty, bufferPath, askConfirm]);




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
      if (path.startsWith('ERR:')) { appendLog('✗ drop failed -- ' + path); return; }
      setSource(path);


      if (mode === 'analyze' && /\.psc$/i.test(f.name)) { await loadIntoEditor(path); return; }
      if (/\.pex$/i.test(f.name)) setMode('decompile');
      else if (/\.psc$/i.test(f.name)) setMode('compile');
      appendLog('• dropped ' + f.name + ' -> source set');
    } catch (err) {
      appendLog('✗ drop failed -- ' + (err instanceof Error ? err.message : String(err)));
    }
  }, [papyrus, mode, loadIntoEditor]);






  useEffect(() => {
    if (mode !== 'analyze' || !papyrus) return;
    let cancelled = false;
    const timer = setTimeout(async () => {


      if (!buffer.trim()) { setAnalysis(null); return; }
      setAnalyzing(true);
      try {
        const json = await papyrus.Analyze(buffer, bufferPath);
        if (!cancelled) setAnalysis(JSON.parse(json) as PapyrusAnalyzeResult);
      } catch (e) {
        if (!cancelled) setAnalysis({ error: e instanceof Error ? e.message : String(e) });
      } finally {
        if (!cancelled) setAnalyzing(false);
      }
    }, 300);
    return () => { cancelled = true; clearTimeout(timer); };
  }, [buffer, bufferPath, mode, papyrus]);

  useEffect(() => {
    if (mode !== 'analyze' || !papyrus) return;
    let cancelled = false;
    const timer = setTimeout(async () => {
      const revision = ++checkRevision.current;
      if (!buffer.trim()) { setLiveCheck(null); return; }
      const reply = (check: PapyrusCheckResult) => {
        if (!cancelled && revision === checkRevision.current) setLiveCheck({ ...check, text: buffer });
      };
      try {
        const check = JSON.parse(await papyrus.CheckText(buffer, bufferPath, revision, imports)) as PapyrusCheckResult;
        if (check.revision === revision || check.error) reply(check);
      } catch (e) {
        reply({ error: e instanceof Error ? e.message : String(e) });
      }
    }, 350);
    return () => { cancelled = true; clearTimeout(timer); };
  }, [buffer, bufferPath, mode, papyrus, imports]);

  const saveEditor = async () => {
    if (!papyrus || !bufferPath) return;
    const err = await papyrus.WriteScript(bufferPath, buffer);
    if (err) { appendLog('✗ save -- ' + err.replace(/^ERR:/, '')); return; }
    setDirty(false);
    appendLog('✓ saved ' + baseName(bufferPath));
  };


  const revealRange = (start: number, length: number) => {
    const el = editorRef.current;
    if (!el) return;
    const from = utf8ToUtf16Index(buffer, start);
    el.focus();
    el.setSelectionRange(from, utf8ToUtf16Index(buffer, start + Math.max(length, 0)));


    const line = buffer.slice(0, from).split('\n').length;
    el.scrollTop = Math.max(0, (line - 4) * EDITOR_LINE_HEIGHT);
    if (gutterRef.current) gutterRef.current.scrollTop = el.scrollTop;
  };


  const resolveAtCaret = async (): Promise<PapyrusSymbolResult | null> => {
    const el = editorRef.current;
    if (!papyrus || !el) return null;
    let symbol: PapyrusSymbolResult;
    try {
      const json = await papyrus.SymbolAt(buffer, bufferPath, byteOffset(el.value, el.selectionStart), imports);
      symbol = JSON.parse(json) as PapyrusSymbolResult;
    } catch (e) {
      symbol = { resolved: false, error: e instanceof Error ? e.message : String(e) };
    }
    setSymbolInfo(symbol);
    return symbol;
  };

  const openFileAt = async (file: string, start: number, length: number, name: string) => {
    if (!papyrus) return;
    if (dirty && !await askConfirm({
      title: 'Discard unsaved edits?', danger: true, confirmLabel: 'Discard',
      message: `Opening ${baseName(file)} replaces the buffer and discards unsaved edits.`,
    })) return;
    const text = await papyrus.ReadScript(file);
    if (text.startsWith('ERR:')) { appendLog('✗ open -- ' + text.slice(4)); return; }
    setBuffer(text); setBufferPath(file); setDirty(false); setSource(file);
    appendLog('• followed ' + name + ' -> ' + baseName(file));
    const from = utf8ToUtf16Index(text, start);
    const to = utf8ToUtf16Index(text, start + length);

    setTimeout(() => {
      const el = editorRef.current;
      if (!el) return;
      el.focus();
      el.setSelectionRange(from, to);
      el.scrollTop = Math.max(0, (text.slice(0, from).split('\n').length - 4) * EDITOR_LINE_HEIGHT);
      if (gutterRef.current) gutterRef.current.scrollTop = el.scrollTop;
    }, 0);
  };

  const goToDefinition = async (symbol: PapyrusSymbolResult | null) => {
    if (!symbol?.resolved) return;
    if (symbol.sameFile) { revealRange(symbol.start ?? 0, symbol.length ?? 0); return; }
    if (!symbol.file) return;
    await openFileAt(symbol.file, symbol.start ?? 0, symbol.length ?? 0, symbol.name ?? '');
  };

  const jumpTo = async (file: string | null, start: number, length: number, name: string) => {
    if (!file || (bufferPath && samePath(file, bufferPath))) { revealRange(start, length); return; }
    await openFileAt(file, start, length, name);
  };

  const findReferences = async () => {
    const el = editorRef.current;
    if (!papyrus || !el) return;
    const reply = JSON.parse(await papyrus.References(el.value, bufferPath, byteOffset(el.value, el.selectionStart), imports)) as PapyrusReferencesResult;
    if (!reply.declaration || !reply.references) {
      setRefs(null);
      setNote(reply.error ?? (reply.reason === 'unsupported'
        ? 'Only locals, parameters and script names have references.'
        : 'No declaration is at the caret.'));
      return;
    }
    setNote('');
    setRefs({
      name: reply.declaration.name,
      rows: reply.references.map((hit, i) => ({
        key: String(i),
        title: `${hit.file ? baseName(hit.file) : 'this buffer'}:${hit.line}:${hit.column}`,
        detail: hit.isDeclaration ? 'declaration' : 'reference',
        where: hit.file ?? 'this buffer',
        file: hit.file,
        start: hit.start,
        length: hit.length,
      })),
    });
  };

  const renameAtCaret = async () => {
    const el = editorRef.current;
    if (!papyrus || !el) return;
    const requested = await askPrompt({
      title: 'Rename symbol', label: 'New name', confirmLabel: 'Rename',
      validate: value => /^[A-Za-z_]\w*$/.test(value.trim()) ? null : 'Use letters, digits and underscores, not starting with a digit.',
    });
    const newName = requested?.trim();
    if (!newName) return;
    const text = el.value;
    const reply = JSON.parse(await papyrus.Rename(text, bufferPath, byteOffset(text, el.selectionStart), newName, imports)) as PapyrusRenameResult;
    if (!reply.ok || !reply.edits) {
      setNote('Rename refused: ' + (reply.reason ?? reply.error ?? 'no result'));
      return;
    }
    const here: PapyrusRenameEdit[] = [];
    const elsewhere = new Map<string, PapyrusRenameEdit[]>();
    for (const edit of reply.edits) {
      if (!edit.file || (bufferPath && samePath(edit.file, bufferPath))) here.push(edit);
      else elsewhere.set(edit.file, [...(elsewhere.get(edit.file) ?? []), edit]);
    }
    const names = [...elsewhere.keys()].map(baseName);
    if (names.length && !await askConfirm({
      title: 'Rename across files?', confirmLabel: 'Rename',
      message: `The rename also rewrites ${names.join(', ')} on disk. Nothing is written unless you confirm.`,
    })) return;

    if (here.length) { setBuffer(applyByteEdits(text, here)); setDirty(true); }
    const problems: string[] = [];
    for (const [file, edits] of elsewhere) {
      const source = await papyrus.ReadScript(file);
      const err = source.startsWith('ERR:') ? source.slice(4) : (await papyrus.WriteScript(file, applyByteEdits(source, edits))).replace(/^ERR:/, '');
      if (err) problems.push(`${baseName(file)}: ${err}`);
    }
    setRefs(null);
    const moved = reply.fileRename
      ? ` Rename ${baseName(reply.fileRename.from)} to ${baseName(reply.fileRename.to)} yourself; the panel does not rename files.`
      : '';
    setNote(`Renamed to ${newName}.${moved}${problems.length ? ' Not written: ' + problems.join('; ') + '.' : ''}`);
  };

  const closeCompletion = () => { completionSeq.current++; setCompletion(null); };
  const hideSignature = () => { signatureSeq.current++; setSignature(null); };

  const requestCompletion = async (el: HTMLTextAreaElement, memberOnly: boolean) => {
    if (!papyrus) return;
    const seq = ++completionSeq.current;
    const caret = el.selectionStart;
    try {
      const reply = JSON.parse(await papyrus.Complete(el.value, bufferPath, byteOffset(el.value, caret), imports)) as PapyrusCompletionResult;
      if (seq !== completionSeq.current) return;
      const items = reply.context === 'none' || (memberOnly && reply.context !== 'member') ? [] : reply.items ?? [];
      setCompletion(items.length
        ? { items, caret, replaceLength: reply.replaceLength ?? 0, active: 0 }
        : null);
    } catch {
      if (seq === completionSeq.current) setCompletion(null);
    }
  };

  const acceptCompletion = (item: PapyrusCompletionItem) => {
    const el = editorRef.current;
    if (!el || !completion) return;
    const start = completion.caret - completion.replaceLength;
    const at = start + item.insertText.length;
    closeCompletion();
    setBuffer(buffer.slice(0, start) + item.insertText + buffer.slice(completion.caret));
    setDirty(true);
    setTimeout(() => el.setSelectionRange(at, at), 0);
  };

  const requestSignature = async (el: HTMLTextAreaElement) => {
    if (!papyrus) return;
    const seq = ++signatureSeq.current;
    try {
      const reply = JSON.parse(await papyrus.SignatureHelp(el.value, bufferPath, byteOffset(el.value, el.selectionStart), imports)) as PapyrusSignatureResult;
      if (seq === signatureSeq.current) setSignature(reply.active && reply.signatures?.length ? reply : null);
    } catch {
      if (seq === signatureSeq.current) setSignature(null);
    }
  };

  const onEditorInput = (el: HTMLTextAreaElement) => {
    const typed = el.value.charAt(el.selectionStart - 1);
    setBuffer(el.value);
    setDirty(true);
    if (typed === '.' || (completion && /\w/.test(typed))) void requestCompletion(el, typed === '.');
    else closeCompletion();
    if (typed === '(' || typed === ',' || typed === ')') void requestSignature(el);
  };

  const onEditorKeyDown = (e: ReactKeyboardEvent<HTMLTextAreaElement>) => {
    if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === 's') { e.preventDefault(); void saveEditor(); return; }
    if (e.key === 'Escape') hideSignature();
    if (completion) {
      if (e.key === 'ArrowDown' || e.key === 'ArrowUp') {
        e.preventDefault();
        const count = completion.items.length;
        setCompletion({ ...completion, active: (completion.active + (e.key === 'ArrowDown' ? 1 : count - 1)) % count });
        return;
      }
      if (e.key === 'Enter' || e.key === 'Tab') { e.preventDefault(); acceptCompletion(completion.items[completion.active]); return; }
      if (e.key === 'Escape') { e.preventDefault(); closeCompletion(); return; }
    }
    if (e.ctrlKey && e.key === ' ') { e.preventDefault(); void requestCompletion(e.currentTarget, false); return; }
    if (e.key.startsWith('Arrow')) hideSignature();

    if (e.key === 'F12' && e.shiftKey) { e.preventDefault(); void findReferences().catch(failure); return; }
    if (e.key === 'F12') { e.preventDefault(); void resolveAtCaret().then(goToDefinition); return; }
    if (e.key === 'F2') { e.preventDefault(); void renameAtCaret().catch(failure); return; }
    if (e.key === 'Tab') {

      e.preventDefault();
      const el = e.currentTarget;
      const { selectionStart: s, selectionEnd: t } = el;
      const next = buffer.slice(0, s) + '\t' + buffer.slice(t);
      setBuffer(next); setDirty(true);
      requestAnimationFrame(() => el.setSelectionRange(s + 1, s + 1));
    }
  };

  const onEditorClick = (e: ReactMouseEvent<HTMLTextAreaElement>) => {
    closeCompletion();
    hideSignature();
    if (e.ctrlKey || e.metaKey) void resolveAtCaret().then(goToDefinition);
    else void resolveAtCaret();
  };

  const onPanelKeyDown = (e: ReactKeyboardEvent<HTMLDivElement>) => {
    if (mode === 'analyze' && (e.ctrlKey || e.metaKey) && e.key.toLowerCase() === 't') { e.preventDefault(); wsInput.current?.focus(); }
  };
  const hints = liveCheck?.hints ?? [];
  const wsRows: LocationRow[] = workspace.results.map((sym, i) => ({
    key: String(i),
    title: sym.name,
    detail: `${sym.kind}  ${sym.signature}${sym.container ? '  in ' + sym.container : ''}`,
    where: `${sym.file ? baseName(sym.file) : ''}:${sym.line}`,
    file: sym.file,
    start: sym.start,
    length: sym.length,
  }));
  const diagnostics = mergeDiagnostics(analysis?.diagnostics ?? [], liveCheck?.diagnostics ?? []);
  const symbols = analysis?.symbols ?? [];
  const errorLines = new Set(diagnostics.filter(d => d.severity === 'error').map(d => d.line));
  const errorTotal = diagnostics.filter(d => d.severity === 'error').length;
  const lineCount = Math.max(1, buffer.split('\n').length);
  const checking = buffer.trim() !== '' && liveCheck?.text !== buffer;
  const liveSkipped = liveCheck && !liveCheck.complete && !liveCheck.diagnostics?.length && !liveCheck.error;
  const activeSignature = signature ? signature.signatures?.[signature.activeSignature ?? 0] : undefined;

  const runLookup = async () => {
    if (!papyrus) return;
    const query = lookupKind === 'function' ? lookupFunction.trim() : lookupScript.trim();
    if (!query) return;
    setBusy(true); setResult('Looking up…');
    try {
      const text = lookupKind === 'function'
        ? await papyrus.LookupFunction(lookupScript.trim(), lookupFunction.trim())
        : await papyrus.LookupScriptInfo(lookupScript.trim());
      setResult(text || '(no result)');
      appendLog(`${isLookupError(text) ? '✗' : '✓'} wiki lookup -- ${query}`);
    } catch (e) {
      const msg = 'Error: ' + (e instanceof Error ? e.message : String(e));
      setResult(msg); appendLog(`✗ wiki lookup ${query} -- ${msg}`);
    } finally { setBusy(false); }
  };

  const run = async () => {
    if (mode === 'lookup') { await runLookup(); return; }

    if (mode === 'analyze') { await saveEditor(); return; }
    if (!papyrus || !source.trim()) return;
    setBusy(true); setResult('Working…');
    const name = baseName(source);
    try {
      const out = mode === 'decompile'
        ? await papyrus.Decompile(source, output, assembly, write)
        : await papyrus.Compile(source, output, imports, flags, all, optimize, release, compilerPath, engine);
      const text = out || '(no output)';
      setResult(text);

      const savedMatch = text.match(/(?:SAVED ->|OUTPUT:|output -> )\s*(.+)/);
      if (savedMatch) setLastOutDir(savedMatch[1].trim());
      else if (output.trim()) setLastOutDir(output.trim());

      const isErr = bannerKind(text) === 'error';
      if (mode === 'decompile') {
        const kind = assembly ? 'disassembled' : 'decompiled';
        const savedLine = text.split('\n').find(l => /^(SAVED ->|RESULT:)/.test(l)) || `${text.split('\n').length} lines`;
        appendLog(`${isErr ? '✗' : '✓'} ${kind} ${name} -- ${savedLine}`);
      } else {
        const r = text.split('\n').find(l => l.startsWith('RESULT:')) || (text.split('\n').find(l => l.trim()) ?? '');
        appendLog(`${isErr ? '✗' : '✓'} compiled ${name} -- ${r}`);
      }
    } catch (e) {
      const msg = 'Error: ' + (e instanceof Error ? e.message : String(e));
      setResult(msg); appendLog(`✗ ${mode} ${name} -- ${msg}`);
    } finally { setBusy(false); }
  };

  const openOut = async () => { if (papyrus && lastOutDir) await papyrus.OpenFolder(lastOutDir); };

  const requestClose = async () => {
    if (dirty && !await askConfirm({
      title: 'Discard unsaved edits?', danger: true, confirmLabel: 'Discard',
      message: `${bufferPath ? baseName(bufferPath) : 'The untitled buffer'} has unsaved changes. Closing the panel discards them.`,
    })) return;
    onClose();
  };

  const banner = result ? (mode === 'lookup' ? makeLookupBanner(result) : makeBanner(result, mode)) : null;

  return (
    <div className="papyrus-overlay" onClick={requestClose}>
      <div className="papyrus-modal glass-panel" onClick={e => e.stopPropagation()} onKeyDown={onPanelKeyDown}>
        <div className="papyrus-header">
          <span className="papyrus-title"><ScrollText size={16} /> Papyrus</span>
          <div className="papyrus-modes">
            <button className={`papyrus-mode ${mode === 'decompile' ? 'active' : ''}`} onClick={() => setMode('decompile')}><FileCode2 size={14} /> Decompile</button>
            <button className={`papyrus-mode ${mode === 'compile' ? 'active' : ''}`} onClick={() => setMode('compile')}><Hammer size={14} /> Compile</button>
            <button className={`papyrus-mode ${mode === 'lookup' ? 'active' : ''}`} onClick={() => setMode('lookup')}><BookOpen size={14} /> Wiki Lookup</button>
            <button className={`papyrus-mode ${mode === 'analyze' ? 'active' : ''}`} onClick={() => setMode('analyze')}><Stethoscope size={14} /> Analyze</button>
          </div>
          <button className="papyrus-close" onClick={requestClose} title="Close"><X size={16} /></button>
        </div>

        {unavailable && <div className="papyrus-warn">Papyrus bridge not available -- run the desktop app (not the browser dev server).</div>}

        <div className="papyrus-body">
          <div className="papyrus-form">
            {mode !== 'lookup' && (
              <>
                <label className="papyrus-field">
                  <span>{mode === 'decompile' ? 'Source .pex (file, folder, or whole mod)'
                    : mode === 'analyze' ? 'Script .psc to open' : 'Source .psc (file or folder)'}</span>
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

                {mode !== 'analyze' && (
                  <label className="papyrus-field">
                    <span>Output folder {mode === 'decompile' ? '(saving on by default)' : '(default: source folder)'}</span>
                    <div className="papyrus-input-row">
                      <input value={output} onChange={e => setOutput(e.target.value)} placeholder="(default: alongside the source)" />
                      <button className="sidebar-action-btn" onClick={browseOutput} disabled={unavailable}>Folder…</button>
                    </div>
                  </label>
                )}
              </>
            )}

            {mode === 'analyze' ? (
              <>
                <div className="papyrus-input-row">
                  <button className="sidebar-action-btn" onClick={() => loadIntoEditor(source)} disabled={unavailable || !source.trim()}>Open in editor</button>
                </div>
                <label className="papyrus-field">
                  <span>Extra source roots for go-to-definition (semicolon-separated)</span>
                  <input value={imports} onChange={e => setImports(e.target.value)} placeholder="(the script's own folder and the base scripts are searched already)" />
                </label>

                <input ref={wsInput} className="papyrus-ws-input" value={wsQuery} onChange={e => setWsQuery(e.target.value)}
                       placeholder="Find a symbol in any script (Ctrl+T)" />
                {wsQuery.trim() && (workspace.error
                  ? <div className="papyrus-log-row err">{workspace.error}</div>
                  : <PapyrusLocations rows={wsRows} empty="No symbols match." onPick={row => void jumpTo(row.file, row.start, row.length, row.title)} />)}

                <div className="papyrus-outline-head">
                  <span><ListTree size={13} /> OUTLINE{symbols.length ? ` (${symbols.length})` : ''}</span>
                  {analyzing && <span className="papyrus-outline-busy">parsing…</span>}
                </div>
                <div className="papyrus-outline">
                  {symbols.length === 0
                    ? <div className="papyrus-log-empty">{buffer.trim() ? 'Nothing declared yet.' : 'Open a .psc to see what it declares.'}</div>
                    : symbols.map((sym, i) => (
                      <button
                        key={i}
                        className={`papyrus-outline-row kind-${sym.kind.toLowerCase()}`}
                        title={sym.documentation || sym.signature}
                        onClick={() => revealRange(sym.nameLength ? sym.nameStart : sym.start, sym.nameLength)}
                      >
                        <span className="papyrus-outline-kind">{sym.kind}</span>
                        <span className="papyrus-outline-sig">{sym.signature}</span>
                        <span className="papyrus-outline-line">{sym.line}</span>
                      </button>
                    ))}
                </div>
              </>
            ) : mode === 'lookup' ? (
              <>
                <div className="papyrus-opts">
                  <label><input type="radio" checked={lookupKind === 'function'} onChange={() => setLookupKind('function')} /> Function</label>
                  <label><input type="radio" checked={lookupKind === 'script'} onChange={() => setLookupKind('script')} /> Script overview</label>
                </div>
                <label className="papyrus-field">
                  <span>Script {lookupKind === 'function' ? '(optional -- disambiguates a function defined on several scripts)' : ''}</span>
                  <input value={lookupScript} onChange={e => setLookupScript(e.target.value)}
                         placeholder="e.g. ActiveMagicEffect, ObjectReference…"
                         onKeyDown={e => { if (e.key === 'Enter' && lookupKind === 'script') run(); }} />
                </label>
                {lookupKind === 'function' && (
                  <label className="papyrus-field">
                    <span>Function</span>
                    <input value={lookupFunction} onChange={e => setLookupFunction(e.target.value)}
                           placeholder="e.g. GetBaseObject"
                           onKeyDown={e => { if (e.key === 'Enter') run(); }} />
                  </label>
                )}
              </>
            ) : mode === 'decompile' ? (
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
                  <span>Engine</span>
                  <select value={engine} onChange={e => setEngine(e.target.value as Engine)}>
                    <option value="auto">Auto -- Creation Kit if installed, otherwise built in</option>
                    <option value="builtin">Built in -- no PapyrusCompiler.exe needed</option>
                    <option value="creationkit">Creation Kit -- PapyrusCompiler.exe</option>
                  </select>
                </label>
                {engine !== 'builtin' && (
                  <label className="papyrus-field">
                    <span>Compiler path (default: CK compiler)</span>
                    <div className="papyrus-input-row">
                      <input value={compilerPath} onChange={e => setCompilerPath(e.target.value)} placeholder="(auto-detect)" />
                      <button className="sidebar-action-btn" onClick={browseCompiler} disabled={unavailable}>Exe…</button>
                    </div>
                  </label>
                )}
                <div className="papyrus-opts">
                  <label><input type="checkbox" checked={all} onChange={e => setAll(e.target.checked)} /> Compile all in folder</label>
                  {engine !== 'builtin' && (
                    <label><input type="checkbox" checked={optimize} onChange={e => setOptimize(e.target.checked)} /> Optimize (-op)</label>
                  )}
                  <label><input type="checkbox" checked={release} onChange={e => setRelease(e.target.checked)} /> Release (-r)</label>
                </div>
                {engine === 'builtin' && (
                  <p className="papyrus-hint">
                    The built-in compiler needs no PapyrusCompiler.exe, but it does need the vanilla base script
                    sources on the import path -- set them once in Settings &gt; Papyrus base imports. It refuses
                    rather than guessing when a script it calls into is not on the roots, because a call's length
                    depends on that script's optional parameters, so a failure naming PAP0050 or PAP0051 is
                    usually a missing import root and not bad source.
                  </p>
                )}
              </>
            )}

            <button className="papyrus-run" onClick={run} disabled={
              busy || unavailable ||
              (mode === 'lookup' ? !(lookupKind === 'function' ? lookupFunction.trim() : lookupScript.trim())
                : mode === 'analyze' ? !(dirty && bufferPath)
                : !source.trim())
            }>
              {mode === 'analyze' ? <Save size={14} /> : <Play size={14} />}
              {' '}
              {busy ? 'Working…'
                : mode === 'decompile' ? 'Decompile'
                : mode === 'compile' ? 'Compile'
                : mode === 'analyze' ? (dirty ? 'Save' : 'Saved') : 'Look Up'}
            </button>
          </div>

          {mode === 'analyze' ? (
            <div className="papyrus-output papyrus-analyze">
              <div className="papyrus-output-head">
                <span>
                  {bufferPath ? baseName(bufferPath) : 'untitled'}{dirty ? ' •' : ''}
                  {analysis?.script ? ` -- ${analysis.script}${analysis.extends ? ' extends ' + analysis.extends : ''}` : ''}
                </span>
                <span className={`papyrus-status ${errorTotal > 0 ? 'err' : 'ok'}`}>
                  {analysis?.error ? 'parser error'
                    : !buffer.trim() ? ''
                    : errorTotal > 0 ? `${errorTotal} error${errorTotal === 1 ? '' : 's'}`
                    : 'no errors'}
                </span>
              </div>

              <div className="papyrus-editor-wrap">
                <div className="papyrus-gutter" ref={gutterRef}>
                  {Array.from({ length: lineCount }, (_, i) => (
                    <div key={i} className={errorLines.has(i + 1) ? 'err' : undefined}>{i + 1}</div>
                  ))}
                </div>
                <textarea
                  ref={editorRef}
                  className="papyrus-editor"
                  spellCheck={false}
                  value={buffer}
                  placeholder={'Open a .psc, or drag one in, or just start typing.\n\nSyntax errors, the outline and go-to-definition update as you type.\nCtrl+click or F12 follows a symbol; Ctrl+S saves.'}
                  onChange={e => onEditorInput(e.target)}
                  onBlur={closeCompletion}
                  onKeyDown={onEditorKeyDown}
                  onClick={onEditorClick}
                  onScroll={(e: ReactUIEvent<HTMLTextAreaElement>) => {
                    if (gutterRef.current) gutterRef.current.scrollTop = e.currentTarget.scrollTop;
                  }}
                />
                {completion && (
                  <div className="papyrus-complete" role="listbox">
                    {completion.items.map((item, i) => (
                      <button key={i} role="option" aria-selected={i === completion.active}
                              className={`papyrus-complete-row ${i === completion.active ? 'active' : ''}`}
                              onMouseDown={e => { e.preventDefault(); acceptCompletion(item); }}>
                        <span className="papyrus-complete-label">{item.label}</span>
                        <span className="papyrus-complete-kind">{item.kind}</span>
                        <span className="papyrus-complete-detail">{item.detail}</span>
                      </button>
                    ))}
                  </div>
                )}
              </div>

              {activeSignature && (
                <div className="papyrus-symbol papyrus-signature">
                  <span className="papyrus-symbol-sig">{signatureLabel(activeSignature, signature?.activeParameter ?? 0)}</span>
                  {activeSignature.documentation && <span className="papyrus-symbol-doc">{activeSignature.documentation.trim()}</span>}
                </div>
              )}

              {symbolInfo && (
                <div className={`papyrus-symbol ${symbolInfo.resolved ? '' : 'unresolved'}`}>
                  <Crosshair size={13} />
                  {symbolInfo.resolved ? (
                    <>
                      <span className="papyrus-symbol-sig">{symbolInfo.signature}</span>
                      {symbolInfo.container && <span className="papyrus-symbol-in">in {symbolInfo.container}</span>}
                      {symbolInfo.documentation && <span className="papyrus-symbol-doc">{symbolInfo.documentation.trim()}</span>}
                      <button className="papyrus-copy" onClick={() => void goToDefinition(symbolInfo)}>
                        {symbolInfo.sameFile ? 'Go to definition' : `Open ${baseName(symbolInfo.file || '')}`}
                      </button>
                      <button className="papyrus-copy" title="Shift+F12" onClick={() => void findReferences().catch(failure)}>Find references</button>
                      <button className="papyrus-copy" title="F2" onClick={() => void renameAtCaret().catch(failure)}>Rename</button>
                    </>
                  ) : (



                    <span className="papyrus-symbol-sig">
                      No declaration found for that position. Names are resolved without a type checker,
                      so a member reached through an expression (GetOwner().Foo) cannot be followed.
                    </span>
                  )}
                </div>
              )}

              {note && <div className="papyrus-hint">{note}</div>}
              {refs && (
                <div className="papyrus-refs">
                  <div className="papyrus-log-head">
                    <span>REFERENCES to {refs.name} ({refs.rows.length})</span>
                    <button className="papyrus-copy" onClick={() => setRefs(null)}>Close</button>
                  </div>
                  <PapyrusLocations rows={refs.rows} empty="No references." onPick={row => void jumpTo(row.file, row.start, row.length, refs.name)} />
                </div>
              )}
              <div className="papyrus-log-head">
                <span>PROBLEMS ({diagnostics.length}){checking && <span className="papyrus-outline-busy"> checking…</span>}</span>
                <span className="papyrus-hint">{liveCheck?.complete
                  ? 'names and types are checked as you type -- a clean result is not proof it compiles'
                  : 'syntax only -- this does not type-check, so a clean result is not proof it compiles'}</span>
              </div>
              <div className="papyrus-log-body">
                {liveCheck?.error && <div className="papyrus-log-row err">{liveCheck.error}</div>}
                {liveSkipped && <div className="papyrus-hint">Name and type checks were skipped: a script this one refers to is not on the import roots.</div>}
                {analysis?.error ? <div className="papyrus-log-row err">{analysis.error}</div>
                  : diagnostics.length === 0
                    ? <div className="papyrus-log-empty">{buffer.trim() ? 'No errors.' : 'Nothing to check yet.'}</div>
                    : diagnostics.map((d, i) => (
                      <button key={i} className={`papyrus-log-row problem ${d.severity === 'error' ? 'err' : 'warn'}`}
                              onClick={() => revealRange(d.start, d.length)}>
                        <span className="papyrus-problem-pos">{d.line}:{d.column}</span>
                        <span className="papyrus-problem-code">{d.code}</span>
                        <span>{d.message}</span>
                      </button>
                    ))}
                {hints.map((h, i) => (
                  <button key={`hint-${i}`} className="papyrus-log-row problem hint" onClick={() => revealRange(h.start, h.length)}>
                    <span className="papyrus-problem-pos">{h.line}:{h.column}</span>
                    <span className="papyrus-problem-code">{h.code}</span>
                    <span>{h.message}</span>
                  </button>
                ))}
              </div>
            </div>
          ) : (
          <div className="papyrus-output">
            {banner && (
              <div className={`papyrus-banner ${banner.kind}`}>
                {banner.kind === 'ok' ? <CheckCircle2 size={15} /> : banner.kind === 'warn' ? <AlertTriangle size={15} /> : <XCircle size={15} />}
                <span className="papyrus-banner-text">{banner.text}</span>
                {lastOutDir && <button className="papyrus-openfolder" onClick={openOut} title={lastOutDir}><FolderOpen size={13} /> Open folder</button>}
              </div>
            )}
            <div className="papyrus-output-head">
              <span>OUTPUT -- {mode === 'lookup' ? 'CK wiki' : mode === 'decompile' ? (assembly ? 'assembly (.pas)' : 'decompiled source (.psc)') : 'compiler'}</span>
              {result && <button className="papyrus-copy" onClick={() => navigator.clipboard.writeText(result)}>Copy</button>}
            </div>
            <pre className="papyrus-output-body">
              {result ? (mode === 'lookup' ? result : renderHighlighted(result, mode, assembly)) : (mode === 'lookup' ? 'Enter a function or script name and Look Up.' : 'Pick a script (or drag one in) and run.')}
            </pre>
            <div className="papyrus-log-head">
              <span>LOG ({log.length})</span>
              {log.length > 0 && <button className="papyrus-copy" onClick={() => setLog([])}>Clear</button>}
            </div>
            <div className="papyrus-log-body">
              {log.length === 0 ? <div className="papyrus-log-empty">No runs yet.</div>
                : log.map((l, i) => <div key={i} className={`papyrus-log-row ${l.includes('✗') ? 'err' : 'ok'}`}>{l}</div>)}
            </div>
          </div>
          )}
        </div>
      </div>
    </div>
  );
}






function mergeDiagnostics(a: PapyrusDiagnostic[], b: PapyrusDiagnostic[]): PapyrusDiagnostic[] {
  const seen = new Set<string>();
  return [...a, ...b].filter(d => {
    const key = `${d.code}:${d.start}`;
    if (seen.has(key)) return false;
    seen.add(key);
    return true;
  });
}

function signatureLabel(sig: PapyrusSignature, active: number): ReactNode {
  let from = sig.label.indexOf('(') + 1;
  for (let i = 0; i <= active && i < sig.parameters.length; i++) {
    const param = sig.parameters[i].label;
    const at = sig.label.indexOf(param, from);
    if (at < 0) break;
    if (i === active) {
      return <>{sig.label.slice(0, at)}<strong>{param}</strong>{sig.label.slice(at + param.length)}</>;
    }
    from = at + param.length;
  }
  return sig.label;
}

function isLookupError(text: string): boolean {
  if (!text) return false;
  if (text.charCodeAt(0) === 0x91) return true;
  return /^(No CK wiki|No .* found for|Provide a|Error:)/i.test(text) || /is defined on multiple scripts/.test(text);
}
function makeLookupBanner(text: string): { kind: 'ok' | 'error'; text: string } {
  return isLookupError(text) ? { kind: 'error', text: 'Lookup failed -- see output' } : { kind: 'ok', text: 'Done' };
}


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
  if (/NOT SAVED/.test(text)) return { kind: 'warn', text: 'Not saved -- shown below only. Tick "Write files to disk" or set an Output folder.' };
  const k = bannerKind(text);
  return { kind: k, text: k === 'error' ? (mode === 'compile' ? 'Compile failed -- see output' : 'Failed -- see output') : 'Done' };
}


const PSC_KEYWORDS = new Set(['scriptname', 'extends', 'import', 'function', 'endfunction', 'event', 'endevent',
  'if', 'else', 'elseif', 'endif', 'while', 'endwhile', 'return', 'property', 'endproperty', 'auto', 'autoreadonly',
  'const', 'native', 'global', 'self', 'parent', 'none', 'true', 'false', 'as', 'is', 'new', 'state', 'endstate',
  'struct', 'endstruct', 'group', 'endgroup', 'mandatory', 'hidden', 'conditional']);
const PSC_TYPES = new Set(['int', 'float', 'bool', 'string', 'var', 'actor', 'form', 'quest', 'objectreference',
  'scriptobject', 'message', 'keyword', 'perk', 'weapon', 'armor', 'potion', 'spell', 'globalvariable', 'activemagiceffect']);

function renderHighlighted(text: string, mode: Mode, assembly: boolean) {
  if (text.length > 200000) return text;
  const lines = text.replace(/\r/g, '').split('\n');
  const sourceMode = mode === 'decompile' && !assembly;
  return lines.map((line, i) => {
    if (!sourceMode) {

      let cls = '';
      if (/error|cannot|unknown|FAILED|No output|is not a function|DEPENDENCY HELP/i.test(line)) cls = 'tk-err';
      else if (/succeeded|^RESULT:.*0 failed|^SAVED ->|^OUTPUT:/i.test(line)) cls = 'tk-ok';
      else if (new RegExp('RESULT:|Nexus:|https?:\\/\\/', 'i').test(line)) cls = 'tk-key';
      return <div key={i} className={cls}>{line || ' '}</div>;
    }
    return <div key={i}>{tokenizePsc(line)}</div>;
  });
}

function tokenizePsc(line: string) {
  const out: ReactNode[] = [];

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
  if (comment) out.push(<span key={k} className="tk-comment">{comment}</span>);
  return out.length ? out : ' ';
}

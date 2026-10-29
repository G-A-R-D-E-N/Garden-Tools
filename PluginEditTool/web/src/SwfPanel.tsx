import { useCallback, useEffect, useRef, useState } from 'react';
import { AlertTriangle, RotateCcw, RotateCw, Save as SaveIcon } from 'lucide-react';
import './SwfPanel.css';

type CharacterRow = { id: number; kind: string; tagIndex: number; name: string; imported: string };
type LabelRow = { name: string; labelIndex: number; frame: number };
type InstanceRow = {
  depth: number; character: number; name: string; x: number; y: number;
  scaleX: number; scaleY: number; rotation: number; instanceId: number;
  boundsX: number; boundsY: number; boundsWidth: number; boundsHeight: number;
  hasBounds: boolean; clipDepth: number;
};
type StateReply = { changed?: boolean; revision: number; undo: number; redo: number; dirty: boolean; error?: string };
type ErrorReply = { error?: string };

type PlacementRow = { tagIndex: number; depth: number; character: number | null; insideSprite: boolean };
type AbcMethodRow = { index: number; name: string; returnType: string; hasBody: boolean };
type AbcBlockRow = { abc: number; name: string; classes: string[]; scripts: string[]; methods: AbcMethodRow[] };
type ActionTagRow = { tagIndex: number; code: number; insideSprite: boolean };

type SwfHost = {
  Open(path: string): Promise<string>;
  Characters(): Promise<string>;
  FrameLabels(timeline: number): Promise<string>;
  Frame(timeline: number, frame: number): Promise<string>;
  Timeline(index: number): Promise<string>;
  RenameLabel(timeline: number, labelIndex: number, name: string): Promise<string>;
  Undo(): Promise<string>;
  Redo(): Promise<string>;
  State(): Promise<string>;
  Save(): Promise<string>;
  AddLabel(frame: number, name: string, timeline: number): Promise<string>;
  RemoveLabel(timeline: number, labelIndex: number): Promise<string>;
  Placements(): Promise<string>;
  SetDepth(tagIndex: number, depth: number): Promise<string>;
  PlacementMatrix(tagIndex: number): Promise<string>;
  SetTransform(tagIndex: number, x: number, y: number, scaleX: number, scaleY: number, rotate0: number, rotate1: number): Promise<string>;
  InsertKeyframe(tagIndex: number, frame: number): Promise<string>;
  ReadEditText(tagIndex: number): Promise<string>;
  EditEditText(tagIndex: number, text: string, colorHex: string, x0: number, x1: number, y0: number, y1: number): Promise<string>;
  AbcBlocks(): Promise<string>;
  Disassemble(abc: number, methodIndex: number): Promise<string>;
  ShapeSvg(characterId: number): Promise<string>;
  FrameSvg(frame: number): Promise<string>;
  BitmapPng(tagIndex: number): Promise<string>;
  BitmapJpeg(tagIndex: number): Promise<string>;
  InspectText(): Promise<string>;
  ActionTags(): Promise<string>;
  Avm1Disassemble(tagIndex: number): Promise<string>;
};

function host(): SwfHost | null {
  const bridge = (globalThis as { chrome?: { webview?: { hostObjects?: Record<string, SwfHost> } } }).chrome;
  return bridge?.webview?.hostObjects?.swf ?? null;
}

function parse<T>(reply: string): T & ErrorReply {
  return JSON.parse(reply) as T & ErrorReply;
}

// Constraint: every refusal from Radicle is shown verbatim next to the field that caused it.
export default function SwfPanel({ onClose }: { onClose: () => void }) {
  const [path, setPath] = useState('');
  const [characters, setCharacters] = useState<CharacterRow[]>([]);
  const [labels, setLabels] = useState<LabelRow[]>([]);
  const [instances, setInstances] = useState<InstanceRow[]>([]);
  const [timeline, setTimeline] = useState(0);
  const [frame, setFrame] = useState(0);
  const [selectedLabel, setSelectedLabel] = useState<(LabelRow & { timeline: number }) | null>(null);
  const [labelOwner, setLabelOwner] = useState(0);
  const [labelName, setLabelName] = useState('');
  const [state, setState] = useState<StateReply>({ revision: 0, undo: 0, redo: 0, dirty: false });
  const [error, setError] = useState('');
  const [busy, setBusy] = useState(false);
  const [editTag, setEditTag] = useState<number | null>(null);
  const [placements, setPlacements] = useState<PlacementRow[]>([]);
  const [placementTag, setPlacementTag] = useState<number | null>(null);
  const [placementDepth, setPlacementDepth] = useState(0);
  const [matrix, setMatrix] = useState({ x: 0, y: 0, scaleX: 0, scaleY: 0, rotate0: 0, rotate1: 0 });
  const [editText, setEditText] = useState({ text: '', color: '', x0: 0, x1: 0, y0: 0, y1: 0 });
  const [abcBlocks, setAbcBlocks] = useState<AbcBlockRow[]>([]);
  const [abcError, setAbcError] = useState('');
  const [instructions, setInstructions] = useState('');
  const [selectedMethod, setSelectedMethod] = useState<{ key: string; label: string } | null>(null);
  const selectedMethodKey = useRef('');
  const labelRequest = useRef(0);
  const [shapeSvg, setShapeSvg] = useState('');
  const shapeRequest = useRef(0);
  const [bitmapSrc, setBitmapSrc] = useState('');
  const [rootFrames, setRootFrames] = useState(0);
  const [previewFrame, setPreviewFrame] = useState(0);
  const [frameSvg, setFrameSvg] = useState('');
  const frameRequest = useRef(0);
  const previewRevision = useRef<number | undefined>(undefined);
  const [inspectText, setInspectText] = useState('');
  const [inspectError, setInspectError] = useState('');
  const [actionTags, setActionTags] = useState<ActionTagRow[]>([]);
  const [avm1, setAvm1] = useState<{ tagIndex: number; text: string } | null>(null);
  const [avm1Error, setAvm1Error] = useState('');
  const avm1Request = useRef(0);

  const loadCharacters = useCallback(async () => {
    const bridge = host();
    if (!bridge) return;
    const chars = parse<{ characters: CharacterRow[] }>(await bridge.Characters());
    setCharacters(chars.characters ?? []);
  }, []);

  // Constraint: Frame takes the timeline index while the label calls take the DefineSprite character id that Timeline reports.
  const loadLabels = useCallback(async (nextTimeline: number) => {
    const bridge = host();
    if (!bridge) return;
    const request = ++labelRequest.current;
    const owner = parse<{ characterId: number }>(await bridge.Timeline(nextTimeline));
    if (owner.error) { setError(owner.error); return; }
    const reply = parse<{ labels: LabelRow[] }>(await bridge.FrameLabels(owner.characterId));
    if (request !== labelRequest.current) return;
    const rows = reply.labels ?? [];
    setLabelOwner(owner.characterId);
    setLabels(rows);
    setSelectedLabel(current => {
      if (!current || current.timeline !== nextTimeline) return null;
      const row = rows.find(item => item.labelIndex === current.labelIndex);
      return row ? { ...row, timeline: nextTimeline } : null;
    });
    if (reply.error) setError(reply.error);
  }, []);

  const showFrame = useCallback(async (requested: number) => {
    const bridge = host();
    if (!bridge) return;
    const next = Math.min(Math.max(0, Math.trunc(requested)), Math.max(0, rootFrames - 1));
    const request = ++frameRequest.current;
    setPreviewFrame(next);
    setFrameSvg('');
    const reply = parse<{ svg?: string; error?: string }>(await bridge.FrameSvg(next));
    if (request !== frameRequest.current) return;
    if (reply.error) { setError(reply.error); return; }
    setError('');
    setFrameSvg(reply.svg ?? '');
  }, [rootFrames]);

  // Requires a document-change handler: showFrame sets state, which the lint forbids inside an effect.
  const redrawPreview = useCallback(async () => {
    if (rootFrames > 0) await showFrame(previewFrame);
  }, [rootFrames, previewFrame, showFrame]);

  // Invariant: every edit, undo and redo bumps revision, so a changed revision alone triggers the preview redraw.
  const applyRevision = useCallback(async (revision: number | undefined) => {
    if (previewRevision.current === revision) return;
    previewRevision.current = revision;
    await redrawPreview();
  }, [redrawPreview]);

  const refresh = useCallback(async () => {
    const bridge = host();
    if (!bridge) return;
    const nextState = parse<StateReply>(await bridge.State());
    setState(nextState);
    await applyRevision(nextState.revision);
    setSelectedLabel(null);
    await loadCharacters();
    await loadLabels(timeline);
  }, [timeline, loadCharacters, loadLabels, applyRevision]);

  const open = useCallback(async () => {
    const bridge = host();
    if (!bridge || !path.trim()) return;
    setBusy(true);
    setError('');
    setAbcBlocks([]);
    setAbcError('');
    setInstructions('');
    setSelectedMethod(null);
    selectedMethodKey.current = '';
    shapeRequest.current += 1;
    setShapeSvg('');
    setBitmapSrc('');
    frameRequest.current += 1;
    setFrameSvg('');
    setPreviewFrame(0);
    avm1Request.current += 1;
    setAvm1(null);
    setAvm1Error('');
    setActionTags([]);
    setInspectText('');
    setInspectError('');
    try {
      const opened = parse<StateReply>(await bridge.Open(path.trim()));
      if (opened.error) { setError(opened.error); return; }
      setState(opened);
      previewRevision.current = opened.revision;
      setSelectedLabel(null);
      await loadCharacters();
      await loadLabels(timeline);
      const root = parse<{ frameCount?: number }>(await bridge.Timeline(0));
      if (root.error) setError(root.error);
      const rootCount = root.frameCount ?? 0;
      setRootFrames(rootCount);
      const rows = parse<{ rows: InstanceRow[] }>(await bridge.Frame(timeline, frame));
      setInstances(rows.rows ?? []);
      const placementReply = parse<{ placements: PlacementRow[] }>(await bridge.Placements());
      setPlacements(placementReply.placements ?? []);
      const abcReply = parse<{ blocks: AbcBlockRow[] }>(await bridge.AbcBlocks());
      setAbcError(abcReply.error ?? '');
      setAbcBlocks(abcReply.blocks ?? []);
      const inspect = parse<{ text?: string }>(await bridge.InspectText());
      setInspectText(inspect.text ?? '');
      setInspectError(inspect.error ?? '');
      const actionReply = parse<{ tags?: ActionTagRow[] }>(await bridge.ActionTags());
      setActionTags(actionReply.tags ?? []);
      if (actionReply.error) setInspectError(actionReply.error);
      if (rootCount > 0) await showFrame(0);
    } finally {
      setBusy(false);
    }
  }, [path, timeline, frame, loadCharacters, loadLabels, showFrame]);

  const pickFrame = useCallback(async (nextTimeline: number, nextFrame: number) => {
    const bridge = host();
    if (!bridge) return;
    setTimeline(nextTimeline);
    setFrame(nextFrame);
    if (nextTimeline !== timeline) await loadLabels(nextTimeline);
    const rows = parse<{ rows: InstanceRow[] }>(await bridge.Frame(nextTimeline, nextFrame));
    if (rows.error) { setError(rows.error); return; }
    setInstances(rows.rows ?? []);
  }, [timeline, loadLabels]);

  const renameLabel = useCallback(async () => {
    const bridge = host();
    if (!bridge || !selectedLabel) return;
    const reply = parse<StateReply>(await bridge.RenameLabel(labelOwner, selectedLabel.labelIndex, labelName));
    if (reply.error) { setError(reply.error); return; }
    setError('');
    setState(reply);
    await applyRevision(reply.revision);
    await loadCharacters();
    await loadLabels(timeline);
  }, [timeline, labelOwner, selectedLabel, labelName, loadCharacters, loadLabels, applyRevision]);

  const pickCharacter = useCallback(async (character: CharacterRow) => {
    const bridge = host();
    if (!bridge) return;
    const request = ++shapeRequest.current;
    setShapeSvg('');
    setBitmapSrc('');
    if (character.kind === 'Shape') {
      const shape = parse<{ svg?: string; error?: string }>(await bridge.ShapeSvg(character.id));
      if (request !== shapeRequest.current) return;
      if (shape.error) { setError(shape.error); return; }
      setError('');
      setShapeSvg(shape.svg ?? '');
      return;
    }
    if (character.kind === 'Bitmap') {
      const bitmap = parse<{ png?: string; error?: string; status?: string }>(await bridge.BitmapPng(character.tagIndex));
      if (request !== shapeRequest.current) return;
      if (bitmap.status === 'NotFound') {
        // Invariant: BitmapPng refuses a JPEG-defined bitmap as NotFound, and only that status falls back to BitmapJpeg.
        const jpeg = parse<{ jpeg?: string; error?: string }>(await bridge.BitmapJpeg(character.tagIndex));
        if (request !== shapeRequest.current) return;
        if (jpeg.error) { setError(jpeg.error); return; }
        setError('');
        setBitmapSrc(`data:image/jpeg;base64,${jpeg.jpeg ?? ''}`);
        return;
      }
      if (bitmap.error) { setError(bitmap.error); return; }
      setError('');
      setBitmapSrc(`data:image/png;base64,${bitmap.png ?? ''}`);
      return;
    }
    if (character.kind !== 'EditText') return;
    const reply = parse<{ id: number; text: string; color: string; x0: number; x1: number; y0: number; y1: number; error?: string }>(
      await bridge.ReadEditText(character.tagIndex));
    if (request !== shapeRequest.current) return;
    if (reply.error) { setError(reply.error); return; }
    setError('');
    setEditTag(character.tagIndex);
    setEditText({ text: reply.text, color: reply.color, x0: reply.x0, x1: reply.x1, y0: reply.y0, y1: reply.y1 });
  }, []);

  const applyEditText = useCallback(async () => {
    const bridge = host();
    if (!bridge || editTag === null) return;
    const reply = parse<StateReply>(await bridge.EditEditText(
      editTag, editText.text, editText.color, editText.x0, editText.x1, editText.y0, editText.y1));
    if (reply.error) { setError(reply.error); return; }
    setError('');
    setState(reply);
    await applyRevision(reply.revision);
  }, [editTag, editText, applyRevision]);
  const addLabel = useCallback(async () => {
    const bridge = host();
    if (!bridge || !labelName.trim()) return;
    const reply = parse<StateReply>(await bridge.AddLabel(frame, labelName.trim(), labelOwner));
    if (reply.error) { setError(reply.error); return; }
    setError('');
    setState(reply);
    await applyRevision(reply.revision);
    setSelectedLabel(null);
    await loadCharacters();
    await loadLabels(timeline);
  }, [frame, labelName, labelOwner, timeline, loadCharacters, loadLabels, applyRevision]);
  const pickPlacement = useCallback(async (item: PlacementRow) => {
    const bridge = host();
    if (!bridge) return;
    setPlacementTag(item.tagIndex);
    setPlacementDepth(item.depth);
    const reply = parse<{ x: number; y: number; scaleX: number; scaleY: number; rotate0: number; rotate1: number; error?: string }>(
      await bridge.PlacementMatrix(item.tagIndex));
    if (reply.error) { setError(reply.error); return; }
    setMatrix({ x: reply.x, y: reply.y, scaleX: reply.scaleX, scaleY: reply.scaleY, rotate0: reply.rotate0, rotate1: reply.rotate1 });
  }, []);

  const applyTransform = useCallback(async () => {
    const bridge = host();
    if (!bridge || placementTag === null) return;
    const reply = parse<StateReply>(await bridge.SetTransform(
      placementTag, matrix.x, matrix.y, matrix.scaleX, matrix.scaleY, matrix.rotate0, matrix.rotate1));
    if (reply.error) { setError(reply.error); return; }
    setError('');
    setState(reply);
    await applyRevision(reply.revision);
  }, [placementTag, matrix, applyRevision]);
  const insertKeyframe = useCallback(async () => {
    const bridge = host();
    if (!bridge || placementTag === null) return;
    const reply = parse<StateReply>(await bridge.InsertKeyframe(placementTag, frame));
    if (reply.error) { setError(reply.error); return; }
    setError('');
    setState(reply);
    await applyRevision(reply.revision);
  }, [placementTag, frame, applyRevision]);
  const pickInstance = useCallback((row: InstanceRow) => {
    const match = placements.find(item => item.depth === row.depth &&
      (item.character === row.character || row.character === 0));
    if (match) void pickPlacement(match);
  }, [placements, pickPlacement]);
  const applyDepth = useCallback(async () => {
    const bridge = host();
    if (!bridge || placementTag === null) return;
    const reply = parse<StateReply>(await bridge.SetDepth(placementTag, placementDepth));
    if (reply.error) { setError(reply.error); return; }
    setError('');
    setState(reply);
    await applyRevision(reply.revision);
    const placementReply = parse<{ placements: PlacementRow[] }>(await bridge.Placements());
    setPlacements(placementReply.placements ?? []);
  }, [placementTag, placementDepth, applyRevision]);
  const removeLabel = useCallback(async () => {
    const bridge = host();
    if (!bridge || !selectedLabel) return;
    const reply = parse<StateReply>(await bridge.RemoveLabel(labelOwner, selectedLabel.labelIndex));
    if (reply.error) { setError(reply.error); return; }
    setError('');
    setState(reply);
    await applyRevision(reply.revision);
    setSelectedLabel(null);
    await loadCharacters();
    await loadLabels(timeline);
  }, [timeline, labelOwner, selectedLabel, loadCharacters, loadLabels, applyRevision]);
  const pickMethod = useCallback(async (block: AbcBlockRow, method: AbcMethodRow) => {
    const bridge = host();
    if (!bridge) return;
    const key = `${block.abc}:${method.index}`;
    selectedMethodKey.current = key;
    setSelectedMethod({ key, label: method.name || `method ${method.index}` });
    setInstructions('');
    try {
      const reply = parse<{ text: string }>(await bridge.Disassemble(block.abc, method.index));
      if (selectedMethodKey.current !== key) return;
      if (reply.error) { setAbcError(reply.error); setInstructions(''); return; }
      setAbcError('');
      setInstructions(reply.text);
    } catch (caught) {
      if (selectedMethodKey.current !== key) return;
      setAbcError(caught instanceof Error ? caught.message : String(caught));
    }
  }, []);
  const pickActionTag = useCallback(async (item: ActionTagRow) => {
    const bridge = host();
    if (!bridge) return;
    const request = ++avm1Request.current;
    setAvm1({ tagIndex: item.tagIndex, text: '' });
    setAvm1Error('');
    const reply = parse<{ text?: string }>(await bridge.Avm1Disassemble(item.tagIndex));
    if (request !== avm1Request.current) return;
    if (reply.error) { setAvm1Error(reply.error); return; }
    setAvm1({ tagIndex: item.tagIndex, text: reply.text ?? '' });
  }, []);
  const command = useCallback(async (action: 'undo' | 'redo' | 'save') => {
    const bridge = host();
    if (!bridge) return;
    const reply = action === 'undo' ? await bridge.Undo() : action === 'redo' ? await bridge.Redo() : await bridge.Save();
    const parsed = parse<StateReply & { saved?: string }>(reply);
    if (parsed.error) { setError(parsed.error); return; }
    setError('');
    await refresh();
  }, [refresh]);

  useEffect(() => {
    const onKey = (event: KeyboardEvent) => {
      if (!(event.ctrlKey || event.metaKey)) return;
      if (event.key === 'z' && !event.shiftKey) { event.preventDefault(); void command('undo'); }
      else if ((event.key === 'z' && event.shiftKey) || event.key === 'y') { event.preventDefault(); void command('redo'); }
    };
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
  }, [command]);


  return (
    <div className="swf-panel">
      <header className="swf-head">
        <strong>Interface (SWF)</strong>
        <button type="button" onClick={onClose}>Close</button>
        {state.dirty && <span className="swf-dirty">modified</span>}
        <button type="button" disabled={busy || !path.trim()} onClick={() => void open()}>Open</button>
        <input value={path} onChange={event => setPath(event.target.value)} placeholder="Interface/Test.swf" />
        <button type="button" onClick={() => void command('undo')} disabled={state.undo === 0} title="Undo"><RotateCcw size={14} /></button>
        <button type="button" onClick={() => void command('redo')} disabled={state.redo === 0} title="Redo"><RotateCw size={14} /></button>
        <button type="button" onClick={() => void command('save')} title="Save"><SaveIcon size={14} /></button>
      </header>
      {error && <div className="swf-error"><AlertTriangle size={14} /> {error}</div>}
      <div className="swf-body">
        <aside className="swf-left">
          <h4>Characters</h4>
          <ul>
            {characters.map(character => (
              <li key={character.id}>
                <button type="button" onClick={() => void pickCharacter(character)}>
                  {character.kind} #{character.id} {character.name || character.imported}
                </button>
              </li>
            ))}
          </ul>
          <h4>Frame labels</h4>
          <ul>
            {labels.map(label => (
              <li key={label.labelIndex}>
                <button type="button" onClick={() => { setSelectedLabel({ ...label, timeline }); setLabelName(label.name); }}>
                  {labelOwner === 0 ? 'root' : `sprite ${labelOwner}`} · {label.name}
                </button>
              </li>
            ))}
          </ul>
          <h4>Placements</h4>
          <ul>
            {placements.map(item => (
              <li key={item.tagIndex}>
                <button type="button" onClick={() => void pickPlacement(item)}>
                  {item.insideSprite ? `sprite @${item.tagIndex}` : `depth ${item.depth}`} · {item.character ?? 0}
                </button>
              </li>
            ))}
          </ul>          <h4>Label</h4>
          <input value={labelName} onChange={event => setLabelName(event.target.value)} disabled={!selectedLabel} />
          <button type="button" onClick={() => void renameLabel()} disabled={!selectedLabel}>Rename</button>
          <button type="button" onClick={() => void addLabel()} disabled={!labelName.trim()}>Add at frame</button>
          <button type="button" onClick={() => void removeLabel()} disabled={!selectedLabel}>Remove</button>
        </aside>
        <main className="swf-centre">
          <div className="swf-scrubber">
            <label>Timeline <input type="number" min={0} value={timeline} onChange={event => void pickFrame(Number(event.target.value), 0)} /></label>
            <label>Frame <input type="number" min={0} value={frame} onChange={event => void pickFrame(timeline, Number(event.target.value))} /></label>
          </div>
          <div className="swf-stage" aria-label="bounds">
            {instances.filter(row => row.hasBounds).map(row => (
              <div
                key={`box-${row.instanceId}`}
                className="swf-box"
                data-testid={`swf-box-${row.instanceId}`}
                style={{
                  left: `${row.boundsX}px`, top: `${row.boundsY}px`,
                  width: `${Math.max(2, row.boundsWidth)}px`, height: `${Math.max(2, row.boundsHeight)}px`,
                }}
              >
                <span>{row.depth}</span>
              </div>
            ))}
          </div>          <table>
            <thead><tr><th>Depth</th><th>Character</th><th>Name</th><th>X</th><th>Y</th><th>Scale</th><th>Rotate</th></tr></thead>
            <tbody>
              {instances.map(row => (
                <tr key={row.instanceId} data-testid={`swf-row-${row.instanceId}`} onClick={() => pickInstance(row)}>
                  <td>{row.depth}</td><td>{row.character}</td><td>{row.name}</td>
                  <td>{row.x.toFixed(1)}</td><td>{row.y.toFixed(1)}</td>
                  <td>{row.scaleX.toFixed(2)}×{row.scaleY.toFixed(2)}</td><td>{row.rotation.toFixed(3)}</td>
                </tr>
              ))}
            </tbody>
          </table>
        </main>
        <aside className="swf-right">
          <h4>Properties</h4>
          {editTag === null ? <p>Select an edit-text character to edit its fields.</p> : (
            <div className="swf-fields">
              <label>Text <input value={editText.text} onChange={event => setEditText({ ...editText, text: event.target.value })} /></label>
              <label>Colour <input value={editText.color} onChange={event => setEditText({ ...editText, color: event.target.value })} /></label>
              <label>X0 <input type="number" value={editText.x0} onChange={event => setEditText({ ...editText, x0: Number(event.target.value) })} /></label>
              <label>X1 <input type="number" value={editText.x1} onChange={event => setEditText({ ...editText, x1: Number(event.target.value) })} /></label>
              <label>Y0 <input type="number" value={editText.y0} onChange={event => setEditText({ ...editText, y0: Number(event.target.value) })} /></label>
              <label>Y1 <input type="number" value={editText.y1} onChange={event => setEditText({ ...editText, y1: Number(event.target.value) })} /></label>
              <button type="button" onClick={() => void applyEditText()}>Apply</button>
            </div>
          )}
          {placementTag !== null && (
            <div className="swf-fields">
              <label>Depth <input type="number" min={0} value={placementDepth} onChange={event => setPlacementDepth(Number(event.target.value))} /></label>
              <button type="button" onClick={() => void applyDepth()}>Apply depth</button>
              <button type="button" onClick={() => void insertKeyframe()}>Keyframe at frame</button>              <label>X <input type="number" value={matrix.x} onChange={event => setMatrix({ ...matrix, x: Number(event.target.value) })} /></label>
              <label>Y <input type="number" value={matrix.y} onChange={event => setMatrix({ ...matrix, y: Number(event.target.value) })} /></label>
              <label>ScaleX <input type="number" value={matrix.scaleX} onChange={event => setMatrix({ ...matrix, scaleX: Number(event.target.value) })} /></label>
              <label>ScaleY <input type="number" value={matrix.scaleY} onChange={event => setMatrix({ ...matrix, scaleY: Number(event.target.value) })} /></label>
              <label>Rotate0 <input type="number" value={matrix.rotate0} onChange={event => setMatrix({ ...matrix, rotate0: Number(event.target.value) })} /></label>
              <label>Rotate1 <input type="number" value={matrix.rotate1} onChange={event => setMatrix({ ...matrix, rotate1: Number(event.target.value) })} /></label>
              <button type="button" onClick={() => void applyTransform()}>Apply transform</button>
            </div>
          )}          <p>Revision {state.revision} · undo {state.undo} · redo {state.redo}</p>
          {shapeSvg && <img className="swf-shape" alt="Shape preview" src={`data:image/svg+xml;utf8,${encodeURIComponent(shapeSvg)}`} />}
          {bitmapSrc && <img className="swf-bitmap" alt="Bitmap preview" src={bitmapSrc} />}
          <section className="swf-frame" aria-label="Frame preview">
            <h4>Frame preview</h4>
            <button type="button" onClick={() => void showFrame(previewFrame - 1)} disabled={previewFrame <= 0}>Prev</button>
            <input
              type="number"
              min={0}
              max={Math.max(0, rootFrames - 1)}
              aria-label="Preview frame"
              value={previewFrame}
              onChange={event => {
                const next = Number(event.target.value);
                if (event.target.value !== '' && Number.isFinite(next)) void showFrame(next);
              }}
            />
            <button type="button" onClick={() => void showFrame(previewFrame + 1)} disabled={previewFrame >= rootFrames - 1}>Next</button>
            {frameSvg && <img className="swf-frame-image" alt="Frame preview" src={`data:image/svg+xml;utf8,${encodeURIComponent(frameSvg)}`} />}
          </section>
          <section className="swf-inspect" aria-label="Inspect">
            <h4>Inspect</h4>
            {inspectError && <div className="swf-error" role="alert"><AlertTriangle size={14} /> {inspectError}</div>}
            <pre aria-label="inspect text">{inspectText}</pre>
            <h5>AVM1 actions</h5>
            {!inspectError && actionTags.length === 0 && <p>No DoAction or DoInitAction tags in this SWF.</p>}
            <ul>
              {actionTags.map(item => (
                <li key={item.tagIndex}>
                  <button
                    type="button"
                    aria-pressed={avm1?.tagIndex === item.tagIndex}
                    onClick={() => void pickActionTag(item)}
                  >
                    {item.code === 59 ? 'DoInitAction' : 'DoAction'} @{item.tagIndex}{item.insideSprite ? ' in sprite' : ''}
                  </button>
                </li>
              ))}
            </ul>
            {avm1Error && <div className="swf-error" role="alert"><AlertTriangle size={14} /> {avm1Error}</div>}
            {avm1 && (
              <>
                <h5>Tag {avm1.tagIndex}</h5>
                <pre aria-label="avm1 disassembly">{avm1.text}</pre>
              </>
            )}
          </section>
          <section className="swf-abc" aria-label="ABC scripts">
            <h4>Scripts (ABC)</h4>
            {abcError && <div className="swf-error" role="alert"><AlertTriangle size={14} /> {abcError}</div>}
            {!abcError && abcBlocks.length === 0 && <p>No ABC scripts in this SWF.</p>}
            <ul>
              {abcBlocks.map(block => (
                <li key={block.abc}>
                  <strong>{block.name || `ABC block ${block.abc}`}</strong>
                  <ul>
                    {block.classes.map((name, index) => <li key={`class-${index}`}>class {name}</li>)}
                    {block.scripts.map((name, index) => <li key={`script-${index}`}>script {name}</li>)}
                    {block.methods.map(method => (
                      <li key={method.index}>
                        <button
                          type="button"
                          disabled={!method.hasBody}
                          aria-pressed={selectedMethod?.key === `${block.abc}:${method.index}`}
                          onClick={() => void pickMethod(block, method)}
                        >
                          {method.name || `method ${method.index}`} : {method.returnType}
                        </button>
                      </li>
                    ))}
                  </ul>
                </li>
              ))}
            </ul>
            {selectedMethod && <h5>{selectedMethod.label}</h5>}
            <pre aria-label="instructions">{instructions}</pre>
          </section>
        </aside>
      </div>
    </div>
  );
}
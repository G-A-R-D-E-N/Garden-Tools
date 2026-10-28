import { useCallback, useEffect, useRef, useState } from 'react';
import { AlertTriangle, RotateCcw, RotateCw, Save as SaveIcon } from 'lucide-react';
import './SwfPanel.css';

type CharacterRow = { id: number; kind: string; tagIndex: number; name: string; imported: string };
type LabelRow = { name: string; tagIndex: number; spriteId: number; insideSprite: boolean };
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

type SwfHost = {
  Open(path: string): Promise<string>;
  Characters(): Promise<string>;
  FrameLabels(): Promise<string>;
  Frame(timeline: number, frame: number): Promise<string>;
  RenameLabel(tagIndex: number, name: string): Promise<string>;
  Undo(): Promise<string>;
  Redo(): Promise<string>;
  State(): Promise<string>;
  Save(): Promise<string>;
  AddLabel(frame: number, name: string, timeline: number): Promise<string>;
  RemoveLabel(tagIndex: number): Promise<string>;
  Placements(): Promise<string>;
  SetDepth(tagIndex: number, depth: number): Promise<string>;
  PlacementMatrix(tagIndex: number): Promise<string>;
  SetTransform(tagIndex: number, x: number, y: number, scaleX: number, scaleY: number, rotate0: number, rotate1: number): Promise<string>;
  InsertKeyframe(tagIndex: number, frame: number): Promise<string>;
  ReadEditText(tagIndex: number): Promise<string>;
  EditEditText(tagIndex: number, text: string, colorHex: string, x0: number, x1: number, y0: number, y1: number): Promise<string>;
  AbcBlocks(): Promise<string>;
  Disassemble(abc: number, methodIndex: number): Promise<string>;
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
  const [selectedLabel, setSelectedLabel] = useState<LabelRow | null>(null);
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

  const refresh = useCallback(async () => {
    const bridge = host();
    if (!bridge) return;
    const nextState = parse<StateReply>(await bridge.State());
    setState(nextState);
  }, []);

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
    try {
      const opened = parse<StateReply>(await bridge.Open(path.trim()));
      if (opened.error) { setError(opened.error); return; }
      setState(opened);
      const chars = parse<{ characters: CharacterRow[] }>(await bridge.Characters());
      setCharacters(chars.characters ?? []);
      const labelReply = parse<{ labels: LabelRow[] }>(await bridge.FrameLabels());
      setLabels(labelReply.labels ?? []);
      const rows = parse<{ rows: InstanceRow[] }>(await bridge.Frame(timeline, frame));
      setInstances(rows.rows ?? []);
      const placementReply = parse<{ placements: PlacementRow[] }>(await bridge.Placements());
      setPlacements(placementReply.placements ?? []);
      const abcReply = parse<{ blocks: AbcBlockRow[] }>(await bridge.AbcBlocks());
      setAbcError(abcReply.error ?? '');
      setAbcBlocks(abcReply.blocks ?? []);
    } finally {
      setBusy(false);
    }
  }, [path, timeline, frame]);

  const pickFrame = useCallback(async (nextTimeline: number, nextFrame: number) => {
    const bridge = host();
    if (!bridge) return;
    setTimeline(nextTimeline);
    setFrame(nextFrame);
    const rows = parse<{ rows: InstanceRow[] }>(await bridge.Frame(nextTimeline, nextFrame));
    if (rows.error) { setError(rows.error); return; }
    setInstances(rows.rows ?? []);
  }, []);

  const renameLabel = useCallback(async () => {
    const bridge = host();
    if (!bridge || !selectedLabel) return;
    const reply = parse<StateReply>(await bridge.RenameLabel(selectedLabel.tagIndex, labelName));
    if (reply.error) { setError(reply.error); return; }
    setError('');
    setState(reply);
    const labelsReply = parse<{ labels: LabelRow[] }>(await bridge.FrameLabels());
    setLabels(labelsReply.labels ?? []);
  }, [selectedLabel, labelName]);

  const pickCharacter = useCallback(async (character: CharacterRow) => {
    if (character.kind !== 'EditText') return;
    const bridge = host();
    if (!bridge) return;
    const reply = parse<{ id: number; text: string; color: string; x0: number; x1: number; y0: number; y1: number; error?: string }>(
      await bridge.ReadEditText(character.tagIndex));
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
  }, [editTag, editText]);
  const addLabel = useCallback(async () => {
    const bridge = host();
    if (!bridge || !labelName.trim()) return;
    const reply = parse<StateReply>(await bridge.AddLabel(frame, labelName.trim(), timeline));
    if (reply.error) { setError(reply.error); return; }
    setError('');
    setState(reply);
    const labelsReply = parse<{ labels: LabelRow[] }>(await bridge.FrameLabels());
    setLabels(labelsReply.labels ?? []);
  }, [frame, labelName, timeline]);
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
  }, [placementTag, matrix]);
  const insertKeyframe = useCallback(async () => {
    const bridge = host();
    if (!bridge || placementTag === null) return;
    const reply = parse<StateReply>(await bridge.InsertKeyframe(placementTag, frame));
    if (reply.error) { setError(reply.error); return; }
    setError('');
    setState(reply);
  }, [placementTag, frame]);
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
    const placementReply = parse<{ placements: PlacementRow[] }>(await bridge.Placements());
    setPlacements(placementReply.placements ?? []);
  }, [placementTag, placementDepth]);
  const removeLabel = useCallback(async () => {
    const bridge = host();
    if (!bridge || !selectedLabel) return;
    const reply = parse<StateReply>(await bridge.RemoveLabel(selectedLabel.tagIndex));
    if (reply.error) { setError(reply.error); return; }
    setError('');
    setState(reply);
    setSelectedLabel(null);
    const labelsReply = parse<{ labels: LabelRow[] }>(await bridge.FrameLabels());
    setLabels(labelsReply.labels ?? []);
  }, [selectedLabel]);
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
          <h4>Timelines</h4>
          <ul>
            {labels.map(label => (
              <li key={`${label.spriteId}-${label.tagIndex}`}>
                <button type="button" onClick={() => { setSelectedLabel(label); setLabelName(label.name); }}>
                  {label.insideSprite ? `sprite ${label.spriteId}` : 'root'} · {label.name}
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
          <p>Shape rendering is not part of this round; the table above is the display list.</p>
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
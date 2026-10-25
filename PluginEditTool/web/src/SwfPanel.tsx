import { useCallback, useEffect, useState } from 'react';
import { AlertTriangle, RotateCcw, RotateCw, Save as SaveIcon } from 'lucide-react';
import './SwfPanel.css';

type CharacterRow = { id: number; kind: string; tagIndex: number; name: string; imported: string };
type LabelRow = { name: string; tagIndex: number; spriteId: number; insideSprite: boolean };
type InstanceRow = {
  depth: number; character: number; name: string; x: number; y: number;
  scaleX: number; scaleY: number; rotation: number; instanceId: number;
};
type StateReply = { changed?: boolean; revision: number; undo: number; redo: number; dirty: boolean; error?: string };
type ErrorReply = { error?: string };

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
              <li key={character.id}>{character.kind} #{character.id} {character.name || character.imported}</li>
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
          <h4>Label</h4>
          <input value={labelName} onChange={event => setLabelName(event.target.value)} disabled={!selectedLabel} />
          <button type="button" onClick={() => void renameLabel()} disabled={!selectedLabel}>Rename</button>
        </aside>
        <main className="swf-centre">
          <div className="swf-scrubber">
            <label>Timeline <input type="number" min={0} value={timeline} onChange={event => void pickFrame(Number(event.target.value), 0)} /></label>
            <label>Frame <input type="number" min={0} value={frame} onChange={event => void pickFrame(timeline, Number(event.target.value))} /></label>
          </div>
          <table>
            <thead><tr><th>Depth</th><th>Character</th><th>Name</th><th>X</th><th>Y</th><th>Scale</th><th>Rotate</th></tr></thead>
            <tbody>
              {instances.map(row => (
                <tr key={row.instanceId}>
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
          <p>Revision {state.revision} · undo {state.undo} · redo {state.redo}</p>
          <p>Shape rendering is not part of this round; the table above is the display list.</p>
        </aside>
      </div>
    </div>
  );
}
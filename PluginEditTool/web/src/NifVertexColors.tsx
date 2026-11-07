import { useEffect, useState } from 'react';
import type { NifHost } from './backend';
import { numOf } from './util/numInput';
import { fromHex, toHex } from './util/colorHex';

interface VertexColorShape { block: number; name: string; vertices: number; color: { present: boolean } }
interface VertexColorInfo { shapes?: VertexColorShape[]; error?: string }
type Channel = 'red' | 'green' | 'blue' | 'alpha';
type Axis = 'x' | 'y' | 'z';
type Paint = { kind: string; channel?: Channel; axis?: Axis; value?: number; color?: number[]; endColor?: number[]; vertices?: number[] };
type Selection = { block?: number; verts: readonly number[] } | null;

const message = (e: unknown) => (e instanceof Error ? e.message : String(e));

const fetchShapes = async (nif: NifHost, nifPath: string): Promise<VertexColorShape[]> => {
  const parsed = JSON.parse(await nif.VertexColorGet(nifPath)) as VertexColorInfo;
  if (parsed.error) throw new Error(parsed.error);
  return parsed.shapes ?? [];
};

function ColourField({ label, value, onChange }: { label: string; value: number[]; onChange: (next: number[]) => void }) {
  return (
    <span className="nif-vc-colour">
      <span className="nif-vc-swatch" style={{ background: `rgba(${value.slice(0, 3).map(c => Math.round(c * 255)).join(', ')}, ${value[3]})` }} />
      <input type="color" aria-label={label} value={toHex(value)} onChange={e => onChange([...fromHex(e.target.value), value[3]])} />
      <input className="nif-in nif-uv-num" type="number" min={0} max={1} step="any" aria-label={`${label} alpha`}
        value={value[3]} onChange={e => onChange([...value.slice(0, 3), numOf(e)])} />
    </span>
  );
}

export default function NifVertexColors(
  { nif, nifPath, selection, disabled, onSaved, appendLog }: {
    nif: NifHost;
    nifPath: string;
    selection: Selection;
    disabled: boolean;
    onSaved: () => void;
    appendLog: (line: string) => void;
  }
) {
  const [shapes, setShapes] = useState<VertexColorShape[]>([]);
  const [block, setBlock] = useState<number | null>(null);
  const [color, setColor] = useState<number[]>([1, 1, 1, 1]);
  const [endColor, setEndColor] = useState<number[]>([0, 0, 0, 1]);
  const [channel, setChannel] = useState<Channel>('red');
  const [value, setValue] = useState(1);
  const [axis, setAxis] = useState<Axis>('x');
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState('');

  useEffect(() => {
    let live = true;
    fetchShapes(nif, nifPath).then(list => {
      if (!live) return;
      setShapes(list);
      setBlock(list[0]?.block ?? null);
      setError('');
    }).catch(e => { if (live) setError(message(e)); });
    return () => { live = false; };
  }, [nif, nifPath]);

  const current = shapes.find(s => s.block === block);
  const hasColours = current?.color.present ?? false;
  const locked = disabled || busy || block === null;
  const selectedHere = selection !== null && selection.block === block && selection.verts.length > 0;

  const send = async (label: string, op: Paint) => {
    if (block === null || busy) return;
    const p = { channel: null, axis: null, value: null, color: null, endColor: null, vertices: null, ...op };
    setBusy(true);
    try {
      const res = await nif.VertexColorSet(nifPath, block, p.kind, p.channel, p.axis, p.value, p.color, p.endColor, p.vertices, '');
      const ok = /vertex color OK/.test(res);
      appendLog(ok ? `✓ ${label}` : `✗ ${label} failed -- ${res}`);
      setError(ok ? '' : res);
      if (ok) {
        setShapes(await fetchShapes(nif, nifPath));
        onSaved();
      }
    } catch (e) {
      appendLog(`✗ ${label} failed -- ${message(e)}`);
      setError(message(e));
    } finally {
      setBusy(false);
    }
  };

  return (
    <div className="nif-vc">
      <div className="nif-uv-toolbar">
        <label className="nif-uv-field">Shape
          <select aria-label="Colour shape" value={block ?? ''} disabled={!shapes.length} onChange={e => setBlock(Number(e.target.value))}>
            {shapes.map(s => <option key={s.block} value={s.block}>{s.name || `shape ${s.block}`}</option>)}
          </select>
        </label>
        {current && <span className="nif-uv-label">{`${current.vertices.toLocaleString()} verts · ${hasColours ? 'has colours' : 'no colours'}`}</span>}
      </div>
      <div className="nif-uv-toolbar">
        <ColourField label="Colour" value={color} onChange={setColor} />
        <button className="sidebar-action-btn" disabled={locked || !hasColours} title="Paint every vertex with Colour"
          onClick={() => send('filled all vertices', { kind: 'fill', color })}>Fill all</button>
        <button className="sidebar-action-btn" disabled={locked || !hasColours || !selectedHere}
          title="Paint the vertices selected in the viewport or UV editor with Colour"
          onClick={() => send('filled selected vertices', { kind: 'fill', color, vertices: [...(selection?.verts ?? [])] })}>Fill selected</button>
      </div>
      <div className="nif-uv-toolbar">
        <label className="nif-uv-field">Channel
          <select aria-label="Channel" value={channel} onChange={e => setChannel(e.target.value as Channel)}>
            <option value="red">Red</option>
            <option value="green">Green</option>
            <option value="blue">Blue</option>
            <option value="alpha">Alpha</option>
          </select>
        </label>
        <input className="nif-in nif-uv-num" type="number" min={0} max={1} step="any" aria-label="Channel value"
          value={value} onChange={e => setValue(numOf(e))} />
        <button className="sidebar-action-btn" disabled={locked || !hasColours} title="Set one channel on every vertex"
          onClick={() => send(`set ${channel} on every vertex`, { kind: 'channel', channel, value })}>Set channel</button>
      </div>
      <div className="nif-uv-toolbar">
        <label className="nif-uv-field">Gradient
          <select aria-label="Gradient axis" value={axis} onChange={e => setAxis(e.target.value as Axis)}>
            <option value="x">X</option>
            <option value="y">Y</option>
            <option value="z">Z</option>
          </select>
        </label>
        <ColourField label="End colour" value={endColor} onChange={setEndColor} />
        <button className="sidebar-action-btn" disabled={locked || !hasColours}
          title="Ramp from Colour to End colour along the axis, across the whole shape"
          onClick={() => send(`gradient along ${axis}`, { kind: 'gradient', axis, color, endColor })}>Gradient</button>
      </div>
      <div className="nif-uv-toolbar">
        <button className="sidebar-action-btn" disabled={disabled || busy || block === null || hasColours}
          title="Add white vertex colours to a shape that has none"
          onClick={() => send('enabled colours', { kind: 'enable' })}>Enable colours</button>
      </div>
      {error && (
        <div className="nif-uv-toolbar">
          <span className="papyrus-warn" title={error}>{`Paint failed -- ${error.split('\n')[0]}`}</span>
        </div>
      )}
    </div>
  );
}

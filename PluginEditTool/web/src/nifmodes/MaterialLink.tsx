import { useState } from 'react';
import { Link2, Play } from 'lucide-react';
import type { NifHost } from '../backend';
import { useDialogs } from '../useDialogs';
import './MaterialLink.css';

interface Mismatch { field: string; nif: string; material: string; }
interface CompareReport { shape: number; material: string; found: boolean; format: string; mismatches: Mismatch[]; }
interface EditsReport { shape: number; material: string; found: boolean; edits: { id: number; key: string; value: string }[]; }
interface ShapesReport { material: string; shapes: { block: number; name: string }[]; }
type Reply<T> = T & { error?: string };

export interface MaterialLinkProps {
  nif: NifHost;
  nifPath: string;
  appendLog: (line: string) => void;
}

const baseName = (path: string) => path.replace(/[\\/]+$/, '').split(/[\\/]/).pop() || path;
const message = (error: unknown) => (error instanceof Error ? error.message : String(error));
const shown = (value: string) => value || '(empty)';

export default function MaterialLink({ nif, nifPath, appendLog }: MaterialLinkProps) {
  const { confirm } = useDialogs();
  const [source, setSource] = useState(nifPath);
  const [path, setPath] = useState(nifPath);
  const [block, setBlock] = useState('');
  const [report, setReport] = useState<CompareReport | null>(null);
  const [material, setMaterial] = useState('');
  const [shapes, setShapes] = useState<ShapesReport | null>(null);
  const [status, setStatus] = useState('');
  const [busy, setBusy] = useState(false);

  if (source !== nifPath) {
    setSource(nifPath);
    setPath(nifPath);
  }

  const shape = Number(block);
  const ready = path.trim() !== '' && block.trim() !== '' && Number.isInteger(shape) && shape >= 0;

  const compare = async (): Promise<CompareReport> => {
    const parsed = JSON.parse(await nif.MaterialCompare(path, shape, '')) as Reply<CompareReport>;
    if (parsed.error) throw new Error(parsed.error);
    setReport(parsed);
    setMaterial(parsed.material);
    return parsed;
  };

  const run = async (task: () => Promise<void>) => {
    setBusy(true);
    setStatus('');
    try {
      await task();
    } catch (error) {
      setStatus('Failed: ' + message(error));
    } finally {
      setBusy(false);
    }
  };

  const onCompare = () => run(async () => {
    const parsed = await compare();
    appendLog(`• compared shape ${shape} with ${parsed.material || 'no material'}`);
  });

  const onFill = () => run(async () => {
    const plan = JSON.parse(await nif.MaterialTextureEdits(path, shape, '')) as Reply<EditsReport>;
    if (plan.error) throw new Error(plan.error);
    if (!plan.found) {
      setStatus(`Material file not found: ${plan.material}`);
      return;
    }
    if (plan.edits.length === 0) {
      setStatus('Texture slots already match the material.');
      return;
    }
    const cleared = plan.edits.filter(edit => edit.value === '').length;
    const clearing = cleared > 0 ? ` ${cleared} of them will be cleared: the material has no path for those slots.` : '';
    const approved = await confirm({
      title: 'Fill texture slots from the material?',
      message: `Write ${plan.edits.length} texture slot(s) from ${baseName(plan.material)} into ${baseName(path)}.${clearing} The file is overwritten in place.`,
      confirmLabel: 'Fill',
      danger: true,
    });
    if (!approved) return;
    const result = await nif.ApplyEdits(path, JSON.stringify(plan.edits), '');
    const saved = /set OK/.test(result);
    appendLog(`${saved ? '✓' : '✗'} material fill on shape ${shape}${saved ? ` -- ${plan.edits.length} slot(s)` : ` -- ${result}`}`);
    if (!saved) {
      setStatus(result);
      return;
    }
    setStatus(`Filled ${plan.edits.length} texture slot(s).`);
    await compare();
  });

  const onFind = () => run(async () => {
    const parsed = JSON.parse(await nif.MaterialShapes(path, material)) as Reply<ShapesReport>;
    if (parsed.error) throw new Error(parsed.error);
    setShapes(parsed);
  });

  return (
    <div className="matlink">
      <label className="matlink-field">
        <span>NIF</span>
        <input value={path} onChange={e => setPath(e.target.value)} placeholder="Path to a .nif" />
      </label>
      <div className="matlink-row">
        <label className="matlink-field matlink-block">
          <span>Shape block</span>
          <input type="number" min={0} value={block} onChange={e => setBlock(e.target.value)} placeholder="Block id" />
        </label>
        <button className="papyrus-run" onClick={onCompare} disabled={busy || !ready}>
          <Play size={13} /> Compare
        </button>
        <button className="sidebar-action-btn" onClick={onFill} disabled={busy || !ready}>
          <Link2 size={13} /> Fill texture slots
        </button>
      </div>
      {report && <CompareTable report={report} />}
      <div className="matlink-field">
        <span>Shapes using a material</span>
        <div className="matlink-row">
          <input value={material} onChange={e => setMaterial(e.target.value)} placeholder="Materials\...\thing.bgsm" />
          <button className="sidebar-action-btn" onClick={onFind} disabled={busy || path.trim() === '' || material.trim() === ''}>
            Find shapes
          </button>
        </div>
      </div>
      {shapes && <ShapeList shapes={shapes} onPick={setBlock} />}
      {status && <div className="matlink-status" role="status">{status}</div>}
    </div>
  );
}

function CompareTable({ report }: { report: CompareReport }) {
  if (!report.material) return <p className="matlink-note">This shape has no material.</p>;
  if (!report.found) return <p className="matlink-note warn">Material file not found: {report.material}</p>;
  if (report.mismatches.length === 0) {
    return <p className="matlink-note ok">{report.material} ({report.format}) matches this shape.</p>;
  }
  return (
    <table className="matlink-table">
      <thead>
        <tr><th>Field</th><th>NIF</th><th>{report.format}</th></tr>
      </thead>
      <tbody>
        {report.mismatches.map(m => (
          <tr key={m.field}><td>{m.field}</td><td>{shown(m.nif)}</td><td>{shown(m.material)}</td></tr>
        ))}
      </tbody>
    </table>
  );
}

function ShapeList({ shapes, onPick }: { shapes: ShapesReport; onPick: (block: string) => void }) {
  if (shapes.shapes.length === 0) return <p className="matlink-note">No shape uses {shapes.material}.</p>;
  return (
    <ul className="matlink-shapes">
      {shapes.shapes.map(s => (
        <li key={s.block}>
          <button className="matlink-pick" onClick={() => onPick(String(s.block))}>
            Block {s.block} · {s.name || '(unnamed)'}
          </button>
        </li>
      ))}
    </ul>
  );
}

import { useRef, useState } from 'react';
import { AlertTriangle, Play, Trash2, Wrench } from 'lucide-react';
import type { NifHost } from '../backend';
import './Collision.css';

export interface CollisionShape { kind: string; class: string; vertices: number; triangles: number; }
export interface CollisionBody {
  index: number; motion: 'static' | 'dynamic'; mass: number; layer: number; layerName: string; materialCrcs: string[]; shape: CollisionShape;
}
export interface CollisionConstraint { kind: string; child: number; parent: number; }
export interface CollisionSystem {
  block: number; owned: boolean; bodyId: number; node: string; error?: string;
  bodies: CollisionBody[]; constraints: CollisionConstraint[]; unsupported: { class: string; object: number }[];
}
export interface CollisionInfo { systems: CollisionSystem[]; findings: { issue: string; block: number }[]; error?: string; }

type Shape = 'box' | 'hull' | 'mesh';

const ISSUE_TEXT: Record<string, string> = {
  dangling_object: 'Collision object names a missing node or system',
  system_without_object: 'Physics system that no collision object names',
  body_out_of_range: 'Collision object names a body the system does not have',
  layer_zero: 'Static body on layer 0',
  bsx_mismatch: 'Collision present without the Havok BSX flag',
  one_child_compound: 'Dynamic compound with one child',
  unsupported_shape: 'Shape class the decoder does not read',
  unread_atom: 'Constraint atom the decoder stopped on',
  undecodable_system: 'Physics system the decoder cannot read',
};

const NIF_FILTER = 'NIF mesh (*.nif)|*.nif|All files|*.*';
const errorText = (e: unknown) => (e instanceof Error ? e.message : String(e));
const baseName = (p: string) => p.split(/[\\/]/).pop() || p;
const normal = (p: string) => p.trim().replace(/\//g, '\\').toLowerCase();

const parseMass = (text: string): number | null => {
  if (text.trim() === '') return 0;
  const value = Number(text);
  return Number.isFinite(value) && value >= 0 ? value : null;
};

const donorBodyText = (index: number, system: CollisionSystem): string => {
  const body = system.bodies.find(b => b.index === system.bodyId);
  return `${index}: node ${system.node || '(not linked)'}${body ? ` · ${body.motion} · ${body.shape.kind}` : ''}`;
};

const writeRefusal = (node: string, target: string, source: string): string => {
  if (!node) return 'Pick a node.';
  if (!target) return 'Pick a Save As path for the new NIF.';
  if (normal(target) === normal(source)) {
    return 'Refused: the Save As path is the source NIF. Choose a new file so the source is not overwritten.';
  }
  return '';
};

export default function Collision({ nif, appendLog }: { nif: NifHost | undefined; appendLog: (line: string) => void }) {
  const [path, setPath] = useState('');
  const [info, setInfo] = useState<CollisionInfo | null>(null);
  const [nodes, setNodes] = useState<string[]>([]);
  const [node, setNode] = useState('');
  const [fromDonor, setFromDonor] = useState(false);
  const [donor, setDonor] = useState('');
  const [donorBodies, setDonorBodies] = useState<CollisionSystem[]>([]);
  const [donorBody, setDonorBody] = useState(0);
  const [donorListed, setDonorListed] = useState('');
  const donorLoad = useRef(0);
  const [shape, setShape] = useState<Shape>('box');
  const [mass, setMass] = useState('');
  const [outPath, setOutPath] = useState('');
  const [output, setOutput] = useState('');
  const [error, setError] = useState('');
  const [busy, setBusy] = useState(false);

  if (!nif) return <div className="nif-view-empty">NIF bridge unavailable.</div>;

  const browseSource = async () => { const p = await nif.BrowseForFile('Select a NIF', NIF_FILTER); if (p) setPath(p); };
  const loadDonor = async (p: string) => {
    if (p === donorListed) return;
    const token = ++donorLoad.current;
    setDonorBodies([]); setDonorBody(0); setDonorListed('');
    if (!p.trim()) return;
    try {
      const parsed = JSON.parse(await nif.CollisionInfo(p)) as CollisionInfo;
      if (parsed.error) throw new Error(parsed.error);
      if (token === donorLoad.current) {
        setDonorBodies(parsed.systems.filter(system => system.owned));
        setDonorListed(p);
      }
    } catch (e) {
      if (token === donorLoad.current) setOutput('Could not read donor bodies: ' + errorText(e));
    }
  };
  const browseDonor = async () => { const p = await nif.BrowseForFile('Select a donor NIF', NIF_FILTER); if (p) { setDonor(p); await loadDonor(p); } };
  const browseOut = async () => { const p = await nif.BrowseForSave('Save NIF as', 'NIF mesh (*.nif)|*.nif'); if (p) setOutPath(p); };

  const load = async () => {
    if (!path.trim()) return;
    setBusy(true); setError('');
    try {
      const parsed = JSON.parse(await nif.CollisionInfo(path)) as CollisionInfo;
      if (parsed.error) throw new Error(parsed.error);
      const tree = JSON.parse(await nif.Tree(path)) as { blocks?: { name: string; group: string }[]; error?: string };
      if (tree.error) throw new Error(tree.error);
      const names = [...new Set((tree.blocks ?? []).filter(b => b.group === 'Nodes').map(b => b.name))];
      setInfo(parsed);
      setNodes(names);
      setNode(names.includes(node) ? node : (names[0] ?? ''));
      appendLog(`• collision ${baseName(path)}`);
    } catch (e) {
      setInfo(null);
      setError('Could not read collision: ' + errorText(e));
    } finally { setBusy(false); }
  };

  const write = async (action: 'set' | 'remove') => {
    const target = outPath.trim();
    const refusal = writeRefusal(node, target, path);
    if (refusal) { setOutput(refusal); return; }
    const massKg = fromDonor || shape !== 'hull' ? 0 : parseMass(mass);
    if (massKg === null) { setOutput('Mass must be a number of kilograms, or blank for static.'); return; }
    if (action === 'set' && fromDonor && !donor.trim()) { setOutput('Pick a donor NIF to copy its collision from.'); return; }
    setBusy(true);
    try {
      let out: string;
      if (action === 'remove') out = await nif.CollisionRemove(path, node, target);
      else if (fromDonor) out = await nif.CollisionSet(path, node, target, 'donor_nif', donor, '', 0, donorBody);
      else out = await nif.CollisionSet(path, node, target, 'kind', '', shape, massKg);
      setOutput(out || '(no output)');
      appendLog(`${/RESULT: collision/.test(out) ? '✓' : '✗'} ${action === 'remove' ? 'remove' : 'set'} collision on ${node} → ${baseName(target)}`);
    } catch (e) {
      setOutput('Error: ' + errorText(e));
      appendLog(`✗ collision on ${node} -- ${errorText(e)}`);
    } finally { setBusy(false); }
  };

  return (
    <div className="nif-collision">
      <div className="nif-collision-warning">
        <AlertTriangle size={14} /> Authored collision has not been tested in the game until the owner confirms it. Compressed mesh collision is static only.
      </div>
      <div className="nif-collision-row">
        <input aria-label="NIF path" value={path} onChange={e => setPath(e.target.value)} placeholder="Path to a .nif" />
        <button className="sidebar-action-btn" onClick={browseSource} disabled={busy}>NIF…</button>
        <button className="papyrus-run" onClick={load} disabled={busy || !path.trim()}><Play size={13} /> Load collision</button>
      </div>
      {error && <div className="nif-collision-error">{error}</div>}
      {info && (
        <>
          <h4>Bodies</h4>
          {info.systems.length === 0 && <p className="nif-collision-note">This NIF has no collision.</p>}
          {info.systems.map(system => (
            <div key={system.block} className="nif-collision-system">
              <div className="nif-collision-note">
                Node {system.node || '(not linked)'} · physics system block {system.block} · body id {system.bodyId}
              </div>
              {system.error && <div className="nif-collision-error">Could not decode this system: {system.error}</div>}
              {system.bodies.length > 0 && (
                <table className="nif-collision-table">
                  <thead>
                    <tr><th>Body</th><th>Motion</th><th>Mass (kg)</th><th>Layer</th><th>Material CRCs</th><th>Shape</th><th>Vertices</th><th>Triangles</th></tr>
                  </thead>
                  <tbody>
                    {system.bodies.map(body => (
                      <tr key={body.index}>
                        <td>{body.index}</td>
                        <td>{body.motion}</td>
                        <td>{body.motion === 'dynamic' ? body.mass.toFixed(3) : '—'}</td>
                        <td>{body.layerName || body.layer}</td>
                        <td>{body.materialCrcs.join(', ')}</td>
                        <td>{body.shape.kind}</td>
                        <td>{body.shape.vertices}</td>
                        <td>{body.shape.triangles}</td>
                      </tr>
                    ))}
                  </tbody>
                </table>
              )}
              {system.constraints.length > 0 && (
                <ul>{system.constraints.map((c, i) => <li key={i}>{c.kind}: body {c.child} to body {c.parent}</li>)}</ul>
              )}
              {system.unsupported.length > 0 && (
                <ul className="nif-collision-error">
                  {system.unsupported.map(u => <li key={u.object}>Unsupported shape {u.class} (object {u.object})</li>)}
                </ul>
              )}
            </div>
          ))}
          <h4>Lint findings</h4>
          {info.findings.length === 0 && <p className="nif-collision-note">No lint findings.</p>}
          <ul>
            {info.findings.map(f => <li key={`${f.issue}-${f.block}`}>{ISSUE_TEXT[f.issue] ?? f.issue} (block {f.block})</li>)}
          </ul>
        </>
      )}
      <h4>Write collision</h4>
      <div className="nif-collision-form">
        <select aria-label="Node" value={node} onChange={e => setNode(e.target.value)} disabled={nodes.length === 0}>
          {nodes.length === 0 && <option value="">Load a NIF first</option>}
          {nodes.map(name => <option key={name} value={name}>{name}</option>)}
        </select>
        <select aria-label="Source" value={fromDonor ? 'donor' : 'generated'} onChange={e => setFromDonor(e.target.value === 'donor')}>
          <option value="generated">Generated from this NIF</option>
          <option value="donor">Donor NIF body</option>
        </select>
        {fromDonor ? (
          <>
            <input aria-label="Donor NIF" value={donor} onChange={e => setDonor(e.target.value)} onBlur={() => loadDonor(donor)} placeholder="Donor .nif" />
            <button className="sidebar-action-btn" onClick={browseDonor} disabled={busy}>Donor…</button>
            {donorBodies.length > 0 && (
              <select aria-label="Donor body" value={donorBody} onChange={e => setDonorBody(Number(e.target.value))}>
                {donorBodies.map((system, i) => <option key={system.block} value={i}>{donorBodyText(i, system)}</option>)}
              </select>
            )}
          </>
        ) : (
          <select aria-label="Shape" value={shape} onChange={e => setShape(e.target.value as Shape)}>
            <option value="box">Box (static)</option>
            <option value="hull">Convex hull</option>
            <option value="mesh">Mesh (static only)</option>
          </select>
        )}
        {!fromDonor && shape === 'hull' && (
          <input aria-label="Mass in kg" value={mass} onChange={e => setMass(e.target.value)} placeholder="Mass kg (blank = static)" />
        )}
      </div>
      <div className="nif-collision-row">
        <input aria-label="Save As NIF" value={outPath} onChange={e => setOutPath(e.target.value)} placeholder="New .nif path (never the source)" />
        <button className="sidebar-action-btn" onClick={browseOut} disabled={busy}>Save As…</button>
        <button className="papyrus-run" onClick={() => write('set')} disabled={busy || !node || !outPath.trim()}>
          <Wrench size={13} /> Add or replace collision
        </button>
        <button className="sidebar-action-btn" onClick={() => write('remove')} disabled={busy || !node || !outPath.trim()}>
          <Trash2 size={13} /> Remove collision
        </button>
      </div>
      {output && <pre className="nif-collision-output">{output}</pre>}
    </div>
  );
}

import { useEffect, useState } from 'react';
import { Boxes } from 'lucide-react';
import type { NifHost } from './backend';
import {
  applyPose, describePoseApplyReport, describePoseEntry, listPoses, type PoseEntry,
} from './poseToolsClient';

const message = (e: unknown) => (e instanceof Error ? e.message : String(e));

interface Props {
  nif: NifHost;
  nifPath: string;
  skins: { id: number; type: string; name: string }[];
  disabled: boolean;
  expectedSource?: string;
  appendLog: (line: string) => void;
  onSaved: (result: string) => void;
  onBusyChange?: (busy: boolean) => void;
}

export default function NifPoseTools(
  { nif, nifPath, skins, disabled, expectedSource = '', appendLog, onSaved, onBusyChange }: Props,
) {
  const [library, setLibrary] = useState('');
  const [block, setBlock] = useState(-1);
  const [poses, setPoses] = useState<PoseEntry[]>([]);
  const [pose, setPose] = useState('');
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState('');

  useEffect(() => { onBusyChange?.(busy); }, [busy, onBusyChange]);

  const hasLibrary = library.trim().length > 0;
  const idle = disabled || busy;
  const chosen = poses.find(entry => entry.name === pose);

  const browse = async () => {
    const picked = await nif.BrowseForFile('Select a pose library', 'All files|*.*');
    if (picked) { setLibrary(picked); setPoses([]); setPose(''); }
  };

  const list = async () => {
    if (idle || !hasLibrary) return;
    setBusy(true);
    setError('');
    try {
      const read = await listPoses(nif, library.trim(), block >= 0 ? nifPath : null, block >= 0 ? block : null);
      setPoses(read);
      setPose(read.some(entry => entry.name === pose) ? pose : (read[0]?.name ?? ''));
      appendLog(`✓ pose library: ${read.length} pose${read.length === 1 ? '' : 's'}`);
    } catch (e) {
      setError(message(e));
      appendLog(`✗ pose list refused -- ${message(e)}`);
    } finally {
      setBusy(false);
    }
  };

  const apply = async () => {
    if (idle || !hasLibrary || block < 0 || !pose) return;
    setBusy(true);
    setError('');
    try {
      const report = await applyPose(nif, nifPath, block, library.trim(), pose, expectedSource);
      if (!report.ok) {
        setError(report.refusal);
        appendLog(`✗ apply pose refused -- ${report.refusal}`);
        return;
      }
      appendLog(`✓ ${describePoseApplyReport(pose, report)}`);
      onSaved(report.saved);
    } catch (e) {
      setError(message(e));
      appendLog(`✗ apply pose failed -- ${message(e)}`);
    } finally {
      setBusy(false);
    }
  };

  return (
    <div className="nif-meshtools">
      <div className="nif-flags-head">
        <span className="nif-flabel"><Boxes size={12} /> Pose tools</span>
        <select className="nif-in" value={block} disabled={idle} aria-label="Pose tools skin instance"
                onChange={e => { setBlock(Number(e.target.value)); setPoses([]); setPose(''); }}>
          <option value={-1}>Select a skin instance…</option>
          {skins.map(s => <option key={s.id} value={s.id}>{s.id}: {s.type}{s.name ? ` ${s.name}` : ''}</option>)}
        </select>
      </div>
      <div className="nif-editor-actions">
        <input className="nif-in" type="text" value={library} disabled={idle} aria-label="Pose library path"
               placeholder="pose library file" onChange={e => { setLibrary(e.target.value); setPoses([]); setPose(''); }} />
        <button className="sidebar-action-btn" disabled={idle} onClick={() => void browse()}>
          Browse…
        </button>
        <button className="sidebar-action-btn" disabled={idle || !hasLibrary} onClick={() => void list()}
                title="Read the poses of the library; with a skin instance picked, also test each against it">
          List poses
        </button>
        <select className="nif-in" value={pose} disabled={idle || poses.length === 0} aria-label="Pose"
                onChange={e => setPose(e.target.value)}>
          {poses.length === 0 && <option value="">No poses listed</option>}
          {poses.map(entry => <option key={entry.name} value={entry.name}>{entry.name}</option>)}
        </select>
        <button className="sidebar-action-btn" disabled={idle || !hasLibrary || block < 0 || !pose}
                onClick={() => void apply()} title="Write the pose into the skin instance and save the file">
          Apply pose
        </button>
      </div>
      {chosen && <span className="nif-dirty-count">{describePoseEntry(chosen)}</span>}
      {error && <span className="papyrus-warn" title={error}>{error.split('\n')[0]}</span>}
    </div>
  );
}

import { useState } from 'react';
import { ListChecks } from 'lucide-react';
import type { HkxHost } from './backend';
import type { HkxChainSources } from './hkxChainClient';
import {
  crashCuts, describeCrash, describeDiff, describeModlist, describeSweep, diffCuts, readCrashSubgraph, readModlistScan, readSubgraphDiff, readSubgraphSweep, scanCuts, sweepCuts,
  type HkxCrashSubgraph, type HkxModlistScan, type HkxSubgraphDiff, type HkxSubgraphSweep, type HkxWeaponGap,
} from './hkxSweepClient';

type Mode = 'sweep' | 'diff' | 'crash' | 'modlist';
type ScanMode = 'characters' | 'clips';

const messageOf = (e: unknown) => (e instanceof Error ? e.message : String(e));

const gapLine = (gap: HkxWeaponGap) => `${gap.crash ? 'crash' : 'falls back'}: ${gap.message}`;

const Lines = ({ label, items, warn }: { label: string; items: string[]; warn?: boolean }) =>
  items.length > 0 ? (
    <ul className={warn ? 'papyrus-warn' : 'nif-flabel'} role="list" aria-label={label}>
      {items.map((item, i) => <li key={`${i}:${item}`}>{item}</li>)}
    </ul>
  ) : null;

interface Props {
  host?: HkxHost;
  disabled: boolean;
}

export default function HkxSweepTools({ host, disabled }: Props) {
  const [mode, setMode] = useState<Mode>('sweep');
  const [scanMode, setScanMode] = useState<ScanMode>('characters');
  const [dataFolder, setDataFolder] = useState('');
  const [mo2Folder, setMo2Folder] = useState('');
  const [profile, setProfile] = useState('');
  const [logPath, setLogPath] = useState('');
  const [logText, setLogText] = useState('');
  const [sweep, setSweep] = useState<HkxSubgraphSweep | null>(null);
  const [diff, setDiff] = useState<HkxSubgraphDiff | null>(null);
  const [crash, setCrash] = useState<HkxCrashSubgraph | null>(null);
  const [scan, setScan] = useState<HkxModlistScan | null>(null);
  const [notice, setNotice] = useState('');
  const [error, setError] = useState('');
  const [busy, setBusy] = useState(false);

  const sources: HkxChainSources = { dataFolder: dataFolder.trim(), mo2Folder: mo2Folder.trim(), profile: profile.trim() };
  const hasSources = sources.dataFolder !== '' || sources.mo2Folder !== '';
  const needsInstance = mode === 'diff' || mode === 'modlist';
  const missingInput = needsInstance ? sources.mo2Folder === '' : !hasSources || (mode === 'crash' && !logPath.trim() && !logText.trim());
  const off = disabled || busy;

  const run = async () => {
    if (!host) return;
    setBusy(true);
    setError('');
    setSweep(null);
    setDiff(null);
    setCrash(null);
    setScan(null);
    try {
      if (mode === 'sweep') {
        const found = await readSubgraphSweep(host, sources);
        setSweep(found);
        setNotice(describeSweep(found));
      } else if (mode === 'diff') {
        const found = await readSubgraphDiff(host, sources);
        setDiff(found);
        setNotice(describeDiff(found));
      } else if (mode === 'crash') {
        const found = await readCrashSubgraph(host, logPath.trim(), logText, sources);
        setCrash(found);
        setNotice(describeCrash(found));
      } else {
        const found = await readModlistScan(host, scanMode, sources);
        setScan(found);
        setNotice(describeModlist(found));
      }
    } catch (e) {
      setNotice('');
      setError(messageOf(e));
    } finally {
      setBusy(false);
    }
  };

  return (
    <div className="nif-flags">
      <div className="nif-flags-head">
        <span className="nif-flabel"><ListChecks size={12} /> Sweep and diff</span>
        <select className="nif-in" value={mode} disabled={off} aria-label="Sweep mode"
                onChange={e => setMode(e.target.value as Mode)}>
          <option value="sweep">Sweep weapon subgraphs</option>
          <option value="diff">Diff vanilla against mods</option>
          <option value="crash">Crash log subgraph</option>
          <option value="modlist">Modlist scan</option>
        </select>
        {mode === 'modlist' ? (
          <select className="nif-in" value={scanMode} disabled={off} aria-label="Modlist scan mode"
                  onChange={e => setScanMode(e.target.value as ScanMode)}>
            <option value="characters">Characters: declared animations</option>
            <option value="clips">Behaviours: clip animations</option>
          </select>
        ) : null}
        <input className="nif-in" value={dataFolder} disabled={off} aria-label="Sweep Data folder"
               placeholder="Fallout 4 Data folder"
               onChange={e => setDataFolder(e.target.value)} />
        <input className="nif-in" value={mo2Folder} disabled={off} aria-label="Sweep MO2 instance folder"
               placeholder={needsInstance ? 'MO2 instance folder (required)' : 'MO2 instance folder (optional)'}
               onChange={e => setMo2Folder(e.target.value)} />
        <input className="nif-in" value={profile} disabled={off} aria-label="Sweep MO2 profile"
               placeholder="MO2 profile (default: selected)"
               onChange={e => setProfile(e.target.value)} />
        {mode === 'crash' ? (
          <>
            <input className="nif-in" value={logPath} disabled={off} aria-label="Crash log path"
                   placeholder="Crash log file (or paste its text below)"
                   onChange={e => setLogPath(e.target.value)} />
            <textarea className="nif-in" rows={4} value={logText} disabled={off} aria-label="Crash log text"
                      placeholder="Crash log text"
                      onChange={e => setLogText(e.target.value)} />
          </>
        ) : null}
        <button className="sidebar-action-btn" disabled={off || missingInput} onClick={() => void run()}>
          Run
        </button>
      </div>

      {sweep ? (
        <>
          {sweep.failures.map(failure => (
            <Lines key={failure.id} label={`Sweep failure ${failure.id}`}
                   items={[`${failure.id}: ${failure.behavior}, ${failure.gap_total} gap(s), ${failure.crash_gaps} crash`, ...failure.gaps.map(gapLine)]} warn />
          ))}
          <Lines label="Sweep cuts" items={sweepCuts(sweep)} warn />
          {sweep.weapon_sets_reason ? <Lines label="Sweep weapon sets" items={[sweep.weapon_sets_reason]} warn /> : null}
          <Lines label="Sweep notes" items={sweep.notes} warn />
        </>
      ) : null}

      {diff ? (
        <>
          <Lines label="Diff new ids" items={diff.new_ids} />
          <Lines label="Diff gone ids" items={diff.gone_ids} warn />
          <Lines label="Diff new weapon behaviours" items={diff.new_weapon_behaviors} />
          <Lines label="Diff clusters" items={diff.clusters.map(c => `${c.behavior}: ${c.new_manifests} new manifest(s)`)} />
          <Lines label="Diff cuts" items={diffCuts(diff)} warn />
          <Lines label="Diff notes" items={diff.notes} warn />
        </>
      ) : null}

      {crash ? (
        <>
          <Lines label="Crash behaviours" items={crash.behavior_paths} />
          <Lines label="Crash missing animations" items={crash.missing} warn />
          <Lines label="Crash weapon gaps" items={crash.gaps.map(gapLine)} warn />
          <Lines label="Crash cuts" items={crashCuts(crash)} warn />
          <Lines label="Crash notes" items={crash.notes} warn />
        </>
      ) : null}

      {scan ? (
        <>
          {scan.findings.map((finding, i) => (
            <Lines key={`${i}:${finding.path}`} label={`Scan finding ${finding.kind}`}
                   items={[`${finding.kind} in ${finding.path}${finding.source ? ` (${finding.source})` : ''}`, ...finding.details]} warn />
          ))}
          <Lines label="Scan cuts" items={scanCuts(scan)} warn />
          <Lines label="Scan notes" items={scan.notes} warn />
        </>
      ) : null}

      {error ? <div className="papyrus-warn" role="alert">{error}</div> : null}
      {notice ? <div className="nif-flabel" role="status">{notice}</div> : null}
    </div>
  );
}

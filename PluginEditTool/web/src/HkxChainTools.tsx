import { useState } from 'react';
import { Link2 } from 'lucide-react';
import type { HkxHost } from './backend';
import {
  describeChain, describeCheck, describeFileProblems, describeSubgraphId, isCleanCheck, readProjectChain, readProjectCheck,
  readSubgraphId,
  type HkxChainLink, type HkxChainSources, type HkxProjectChain, type HkxProjectCheck, type HkxSubgraphIdReply,
} from './hkxChainClient';

const DEFAULT_LIMIT = '2000';

const messageOf = (e: unknown) => (e instanceof Error ? e.message : String(e));

const linkLine = (link: HkxChainLink) =>
  `${link.role}: ${link.declared}` +
  (link.blocked ? ' (blocked, outside meshes)' : link.exists ? ` (from ${link.source})` : ' (missing)') +
  (link.note ? `, ${link.note}` : '');

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

export default function HkxChainTools({ host, disabled }: Props) {
  const [hkxPath, setHkxPath] = useState('');
  const [dataFolder, setDataFolder] = useState('');
  const [mo2Folder, setMo2Folder] = useState('');
  const [profile, setProfile] = useState('');
  const [limit, setLimit] = useState(DEFAULT_LIMIT);
  const [subgraphId, setSubgraphId] = useState('');
  const [chain, setChain] = useState<HkxProjectChain | null>(null);
  const [check, setCheck] = useState<HkxProjectCheck | null>(null);
  const [subgraph, setSubgraph] = useState<HkxSubgraphIdReply | null>(null);
  const [reportText, setReportText] = useState('');
  const [notice, setNotice] = useState('');
  const [error, setError] = useState('');
  const [busy, setBusy] = useState(false);

  const sources: HkxChainSources = { dataFolder: dataFolder.trim(), mo2Folder: mo2Folder.trim(), profile: profile.trim() };
  const hasSources = sources.dataFolder !== '' || sources.mo2Folder !== '';
  const off = disabled || busy;

  const run = async (work: () => Promise<string>, clear: () => void) => {
    if (!host) return;
    setBusy(true);
    setError('');
    clear();
    try {
      setNotice(await work());
    } catch (e) {
      setNotice('');
      setError(messageOf(e));
    } finally {
      setBusy(false);
    }
  };

  const followChain = () => run(async () => {
    const found = await readProjectChain(host!, hkxPath.trim(), sources);
    setChain(found);
    return describeChain(found);
  }, () => { setChain(null); setCheck(null); });

  const checkProject = (format?: 'csv' | 'html') => {
    const count = Number(limit.trim());
    if (!Number.isInteger(count) || count < 1) {
      setError('limit must be a whole number of at least 1');
      return Promise.resolve();
    }
    return run(async () => {
      const found = await readProjectCheck(host!, hkxPath.trim(), sources, count, format);
      setCheck(found);
      setReportText(found.rendered ?? '');
      return describeCheck(found);
    }, () => { setChain(null); setCheck(null); setReportText(''); });
  };

  const readId = () => run(async () => {
    const found = await readSubgraphId(host!, subgraphId.trim(), sources);
    setSubgraph(found);
    return describeSubgraphId(found);
  }, () => setSubgraph(null));

  const needsPath = !hkxPath.trim() || !hasSources;
  const clean = check !== null && isCleanCheck(check);

  return (
    <div className="nif-flags">
      <div className="nif-flags-head">
        <span className="nif-flabel"><Link2 size={12} /> Project chain</span>
        <input className="nif-in" value={hkxPath} disabled={off} aria-label="Chain project hkx"
               placeholder="Data-relative .hkx below meshes, e.g. Actors/Foo/Foo.hkx"
               onChange={e => setHkxPath(e.target.value)} />
        <input className="nif-in" value={dataFolder} disabled={off} aria-label="Chain Data folder"
               placeholder="Fallout 4 Data folder"
               onChange={e => setDataFolder(e.target.value)} />
        <input className="nif-in" value={mo2Folder} disabled={off} aria-label="Chain MO2 instance folder"
               placeholder="MO2 instance folder (optional)"
               onChange={e => setMo2Folder(e.target.value)} />
        <input className="nif-in" value={profile} disabled={off} aria-label="Chain MO2 profile"
               placeholder="MO2 profile (default: selected)"
               onChange={e => setProfile(e.target.value)} />
        <input className="nif-in" value={limit} disabled={off} aria-label="Check file limit"
               placeholder="Most behaviour files to check (the host lowers a limit above its maximum)"
               onChange={e => setLimit(e.target.value)} />
        <button className="sidebar-action-btn" disabled={off || needsPath} onClick={() => void followChain()}>
          Follow chain
        </button>
        <button className="sidebar-action-btn" disabled={off || needsPath} onClick={() => void checkProject()}>
          Check project
        </button>
        <button className="sidebar-action-btn" disabled={off || needsPath} onClick={() => void checkProject('csv')}>
          Report CSV
        </button>
        <button className="sidebar-action-btn" disabled={off || needsPath} onClick={() => void checkProject('html')}>
          Report HTML
        </button>
      </div>

      {chain ? (
        <>
          <Lines label="Chain links" items={chain.links.map(linkLine)} />
          <Lines label="Chain animations" items={chain.animations.map(linkLine)} />
          <Lines label="Chain problems" items={chain.problems} warn />
          <Lines label="Chain warnings" items={chain.warnings} warn />
          <Lines label="Chain notes" items={chain.notes} warn />
        </>
      ) : null}

      {check ? (
        <>
          <div className={clean ? 'nif-flabel' : 'papyrus-warn'} aria-label="Check verdict">
            {check.truncated ? `Stopped at the file limit: ${check.read} of ${check.files} file(s) read. ` : ''}
            {clean ? 'Clean' : 'Not clean'}
          </div>
          <div className="nif-flabel" aria-label="Check summary">{check.summary}</div>
          <Lines label="Check chain problems" items={check.chain_problems} warn />
          <Lines label="Check chain warnings" items={check.chain_warnings} warn />
          <Lines label="Check findings"
                 items={check.findings.map(f => `${f.level} ${f.code} in ${f.file}: ${f.message}`)} warn />
          <Lines label="Check file problems" items={describeFileProblems(check.report)} warn />
          <Lines label="Check notes" items={check.notes} warn />
          {reportText ? (
            <textarea className="nif-in" readOnly rows={8} value={reportText} aria-label="Check report text" />
          ) : null}
        </>
      ) : null}

      <div className="nif-flags-head">
        <span className="nif-flabel"><Link2 size={12} /> Crash subgraph id</span>
        <input className="nif-in" value={subgraphId} disabled={off} aria-label="Crash subgraph id"
               placeholder="Decimal id from the crash log, typed here"
               onChange={e => setSubgraphId(e.target.value)} />
        <button className="sidebar-action-btn" disabled={off || !subgraphId.trim() || !hasSources}
                onClick={() => void readId()}>
          Read subgraph
        </button>
      </div>
      <div className="nif-flabel">
        Type the decimal id from the crash log. A crash log is not read here, and the animation offset and per-weapon lines are not shown.
      </div>

      {subgraph ? (
        <>
          {subgraph.manifest ? (
            <div className="nif-flabel" aria-label="Subgraph manifest">
              {subgraph.manifest.path} ({subgraph.manifest.source}): {describeSubgraphId(subgraph)}
            </div>
          ) : null}
          <Lines label="Subgraph behaviours" items={subgraph.manifest?.behavior_paths ?? []} />
          <Lines label="Subgraph unresolved animations" items={subgraph.manifest?.unresolved ?? []} warn />
          <Lines label="Subgraph notes" items={subgraph.notes} warn />
        </>
      ) : null}

      {error ? <div className="papyrus-warn" role="alert">{error}</div> : null}
      {notice ? <div className="nif-flabel" role="status">{notice}</div> : null}
    </div>
  );
}

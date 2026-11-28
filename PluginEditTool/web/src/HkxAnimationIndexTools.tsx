import { useState } from 'react';
import { Search } from 'lucide-react';
import type { HkxHost } from './backend';
import {
  describeAnimationIndex, extractAnimationAsset, looseFilePath, readAnimationIndex,
  type HkxAnimationActor, type HkxAnimationIndexReply, type HkxAnimationIndexRequest,
  type HkxAnimationProvider, type HkxIndexSources,
} from './hkxAnimationIndexClient';

const PAGE = 200;
const KINDS = ['animation', 'behaviour', 'project', 'character', 'skeleton', 'other'];

const messageOf = (e: unknown) => (e instanceof Error ? e.message : String(e));

interface Props {
  host?: HkxHost;
  disabled: boolean;
  onPickSkeleton: (path: string) => void;
  onPickAnimation: (path: string) => void;
  onPickBody?: (nifPath: string, textureRoot: string) => void;
}

export default function HkxAnimationIndexTools({ host, disabled, onPickSkeleton, onPickAnimation, onPickBody }: Props) {
  const [dataFolder, setDataFolder] = useState('');
  const [mo2Folder, setMo2Folder] = useState('');
  const [profile, setProfile] = useState('');
  const [kind, setKind] = useState('');
  const [project, setProject] = useState('');
  const [race, setRace] = useState('');
  const [character, setCharacter] = useState('');
  const [query, setQuery] = useState('');
  const [reply, setReply] = useState<HkxAnimationIndexReply | null>(null);
  const [actor, setActor] = useState<HkxAnimationActor | null>(null);
  const [notice, setNotice] = useState('');
  const [error, setError] = useState('');
  const [busy, setBusy] = useState(false);
  const [used, setUsed] = useState<HkxIndexSources | null>(null);

  const run = async (patch: Partial<HkxAnimationIndexRequest>) => {
    if (!host || (!dataFolder.trim() && !mo2Folder.trim())) return;
    setBusy(true);
    setError('');
    const request: HkxAnimationIndexRequest = {
      dataFolder: dataFolder.trim(), mo2Folder: mo2Folder.trim(), profile: profile.trim(), kind,
      project: project.trim(), race: race.trim(), character: character.trim(), query: query.trim(),
      resolvePath: '', offset: 0, limit: PAGE, refresh: false, ...patch,
    };
    try {
      const found = await readAnimationIndex(host, request);
      setReply(found);
      setUsed({ dataFolder: request.dataFolder, mo2Folder: request.mo2Folder, profile: request.profile });
      setNotice(describeAnimationIndex(found));
      if (found.actor) setActor(found.actor);
      else if (request.refresh) setActor(null);
    } catch (e) {
      setReply(null);
      setActor(null);
      setError(messageOf(e));
      setNotice('');
    } finally {
      setBusy(false);
    }
  };

  // Constraint: a loose winner is opened in place; an archive winner is extracted outside the game folders first, from the source set the index was built with.
  const fileOf = async (provider: HkxAnimationProvider | null, path: string): Promise<string> => {
    const loose = looseFilePath(provider, path);
    if (loose) return loose;
    if (!host || !used) throw new Error('index the animations before opening an archive file');
    return (await extractAnimationAsset(host, used, path)).path;
  };

  const open = async (provider: HkxAnimationProvider | null, path: string, pick: (file: string) => void) => {
    setBusy(true);
    setError('');
    try {
      pick(await fileOf(provider, path));
    } catch (e) {
      setError(messageOf(e));
    } finally {
      setBusy(false);
    }
  };

  const off = disabled || busy;
  const hidden = reply
    ? reply.facets.projects.hidden + reply.facets.races.hidden + reply.facets.characters.hidden
    : 0;

  return (
    <div className="nif-flags">
      <div className="nif-flags-head">
        <span className="nif-flabel"><Search size={12} /> Animation index</span>
        <input className="nif-in" value={dataFolder} disabled={off} aria-label="Index Data folder"
               placeholder="Fallout 4 Data folder"
               onChange={e => setDataFolder(e.target.value)} />
        <input className="nif-in" value={mo2Folder} disabled={off} aria-label="MO2 instance folder"
               placeholder="MO2 instance folder (optional)"
               onChange={e => setMo2Folder(e.target.value)} />
        <input className="nif-in" value={profile} disabled={off} aria-label="MO2 profile"
               placeholder="MO2 profile (default: selected)"
               onChange={e => setProfile(e.target.value)} />
        <button className="sidebar-action-btn" disabled={off || (!dataFolder.trim() && !mo2Folder.trim())}
                onClick={() => void run({ refresh: true })}>
          Index animations
        </button>
      </div>

      <div className="nif-flags-head">
        <select className="nif-in" value={kind} disabled={off} aria-label="Index kind"
                onChange={e => setKind(e.target.value)}>
          <option value="">any kind</option>
          {KINDS.map(k => <option key={k} value={k}>{k}</option>)}
        </select>
        <input className="nif-in" value={project} disabled={off} aria-label="Index project" list="hkx-index-projects"
               placeholder="Project" onChange={e => setProject(e.target.value)} />
        <input className="nif-in" value={race} disabled={off} aria-label="Index race" list="hkx-index-races"
               placeholder="Race" onChange={e => setRace(e.target.value)} />
        <input className="nif-in" value={character} disabled={off} aria-label="Index character" list="hkx-index-characters"
               placeholder="Character" onChange={e => setCharacter(e.target.value)} />
        <input className="nif-in" value={query} disabled={off} aria-label="Index search"
               placeholder="Path or provider contains"
               onChange={e => setQuery(e.target.value)} />
        <button className="sidebar-action-btn" disabled={off || !reply} onClick={() => void run({})}>
          Filter
        </button>
      </div>

      {reply ? (
        <>
          <datalist id="hkx-index-projects">{reply.facets.projects.values.map(v => <option key={v} value={v} />)}</datalist>
          <datalist id="hkx-index-races">{reply.facets.races.values.map(v => <option key={v} value={v} />)}</datalist>
          <datalist id="hkx-index-characters">{reply.facets.characters.values.map(v => <option key={v} value={v} />)}</datalist>
          <div className="nif-flabel" aria-label="Index sources">
            Winning order: {reply.sources.map(source => source.label).join(' > ')}
          </div>
          {reply.notes.length > 0 ? (
            <ul className="papyrus-warn" role="list" aria-label="Index notes">
              {reply.notes.map(note => <li key={note}>{note}</li>)}
            </ul>
          ) : null}
          {hidden > 0 ? (
            <div className="nif-flabel">{hidden} filter suggestion(s) are not listed; type the name to filter by it.</div>
          ) : null}
          <ul className="nif-flabel" role="list" aria-label="Animation index entries">
            {reply.entries.map(entry => {
              return (
                <li key={entry.path}>
                  {entry.path} [{entry.kind}] from {entry.provider.label}
                  {entry.provider.origin === 'archive' ? ' (archive)' : ''}
                  {entry.shadowed.length > 0 ? `, shadows ${entry.shadowed.map(p => p.label).join(', ')}` : ''}
                  {entry.kind === 'animation' ? (
                    <button className="sidebar-action-btn" disabled={off} aria-label={`Use animation ${entry.path}`}
                            onClick={() => void open(entry.provider, entry.path, onPickAnimation)}>Use as animation</button>
                  ) : null}
                  {entry.kind === 'skeleton' ? (
                    <button className="sidebar-action-btn" disabled={off} aria-label={`Use skeleton ${entry.path}`}
                            onClick={() => void open(entry.provider, entry.path, onPickSkeleton)}>Use as skeleton</button>
                  ) : null}
                  <button className="sidebar-action-btn" disabled={off} aria-label={`Match actor ${entry.path}`}
                          onClick={() => void run({ resolvePath: entry.path, offset: reply.offset })}>
                    Match actor
                  </button>
                </li>
              );
            })}
          </ul>
          <div className="hkx-controls">
            <button className="sidebar-action-btn" disabled={off || reply.offset === 0}
                    onClick={() => void run({ offset: Math.max(0, reply.offset - PAGE) })}>Previous page</button>
            <button className="sidebar-action-btn" disabled={off || !reply.truncated}
                    onClick={() => void run({ offset: reply.offset + PAGE })}>Next page</button>
          </div>
        </>
      ) : null}

      {actor ? (
        <div className="nif-flabel" aria-label="Actor assets">
          <div>
            Skeleton: {actor.skeleton ?? 'none chosen'}
            {actor.skeleton_provider ? ` (from ${actor.skeleton_provider.label})` : ''}
            {actor.skeleton ? (
              <button className="sidebar-action-btn" disabled={off}
                      onClick={() => void open(actor.skeleton_provider, actor.skeleton!, onPickSkeleton)}>
                Use matched skeleton
              </button>
            ) : null}
          </div>
          {actor.skeleton_nif ? (
            <div>Skeleton model: {actor.skeleton_nif}
              {actor.skeleton_nif_provider ? ` (from ${actor.skeleton_nif_provider.label})` : ''}</div>
          ) : null}
          {!actor.skeleton && actor.skeleton_candidates.length > 0 ? (
            <ul role="list" aria-label="Skeleton candidates">
              {actor.skeleton_candidates.map(candidate => <li key={candidate}>{candidate}</li>)}
            </ul>
          ) : null}
          <div>
            Body: {actor.body ?? 'none chosen'}
            {actor.body_provider ? ` (from ${actor.body_provider.label})` : ''}
            {actor.body && onPickBody ? (
              <button className="sidebar-action-btn" disabled={off} aria-label={`Show body ${actor.body}`}
                      onClick={() => void open(actor.body_provider, actor.body!, file => onPickBody(file, reply?.data_folder ?? used?.dataFolder ?? ''))}>
                Show body
              </button>
            ) : null}
          </div>
          {!actor.body && actor.body_candidates.length > 0 ? (
            <ul role="list" aria-label="Body candidates">
              {actor.body_candidates.map(candidate => (
                <li key={candidate}>{candidate}
                  {onPickBody ? (
                    <button className="sidebar-action-btn" disabled={off} aria-label={`Show body ${candidate}`}
                            onClick={() => void open(null, candidate, file => onPickBody(file, reply?.data_folder ?? used?.dataFolder ?? ''))}>
                      Show body
                    </button>
                  ) : null}
                </li>
              ))}
              {actor.body_candidate_total > actor.body_candidates.length ? (
                <li>{actor.body_candidate_total - actor.body_candidates.length} more not listed</li>
              ) : null}
            </ul>
          ) : null}
          {actor.notes.length > 0 ? (
            <ul className="papyrus-warn" role="list" aria-label="Actor notes">
              {actor.notes.map(note => <li key={note}>{note}</li>)}
            </ul>
          ) : null}
        </div>
      ) : null}

      {error ? <div className="papyrus-warn" role="alert">{error}</div> : null}
      {notice ? <div className="nif-flabel" role="status">{notice}</div> : null}
    </div>
  );
}

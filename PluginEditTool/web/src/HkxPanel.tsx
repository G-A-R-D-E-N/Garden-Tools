import { useState, useEffect, useCallback, useMemo, useRef } from 'react';
import { Clock, X, Play, ChevronLeft, ChevronRight, XCircle } from 'lucide-react';
import { getHkx, getNif, type HkxAnimationInspect, type HkxBone, type HkxClipInfo, type HkxPoseBoth } from './backend';

// The reply must carry the skeleton extras, so the panel types them beside the bone list.
type RigBone = HkxBone & { lock?: boolean };
type RigSlot = { name: string; named: boolean };
type RigFrame = { bone: number; has_frame: boolean };
type RigPartition = { name: string; named: boolean; start_bone: number; num_bones: number };
type Rig = {
  name: string;
  bones: RigBone[];
  reference_floats?: number[];
  float_slots?: RigSlot[];
  local_frames?: RigFrame[];
  partitions?: RigPartition[];
};
import HkxViewport, { type HkxBody } from './HkxViewport';
import { loadSkinnedBody } from './hkxBody';
import HkxSubgraphTools from './HkxSubgraphTools';
import HkxAnimationIndexTools from './HkxAnimationIndexTools';
import HkxChainTools from './HkxChainTools';
import HkxSweepTools from './HkxSweepTools';
import HkxRagdollTools from './HkxRagdollTools';
import type { RagdollScene } from './hkxRagdollScene';
import HkxBgsTools from './HkxBgsTools';
import HkxTimelineTools from './HkxTimelineTools';
import HkxTrackTable from './HkxTrackTable';
import HkxConvertTools from './HkxConvertTools';
import HkxSceneTools from './HkxSceneTools';
import HkxInspectTools from './HkxInspectTools';
import HkxGltfSavePanel from './HkxGltfSavePanel';
import { advanceTime } from './util/advanceTime';
import { referenceModelPose } from './util/hkxViewOptions';
import './PapyrusPanel.css';
import './NifPanel.css';
import './HkxPanel.css';

interface ClipInfo { duration: number; frameCount: number; }
interface ClipHandle { skeleton: string; animation: string; }

const messageOf = (e: unknown) => (e instanceof Error ? e.message : String(e));
const fmt = (values: number[]) => values.map(v => v.toFixed(3)).join(', ');

function parseHost<T extends { bones: HkxBone[] }>(raw: string | object): T {
  const parsed = (typeof raw === 'string' ? JSON.parse(raw) : raw) as T & { error?: string };
  if (parsed.error) throw new Error(parsed.error);
  if (!parsed.bones) throw new Error('no bones');
  return parsed;
}

const isFiniteNumber = (value: unknown): value is number => typeof value === 'number' && Number.isFinite(value);
const isFiniteVector = (value: unknown, length: number) =>
  Array.isArray(value) && value.length === length && value.every(isFiniteNumber);

function checkBones(bones: Array<Partial<HkxBone> | null>): void {
  for (const bone of bones) {
    if (!bone || !isFiniteVector(bone.translation, 3) || !isFiniteVector(bone.rotation, 4) || !isFiniteVector(bone.scale, 3)) {
      throw new Error(`no finite transform for bone "${bone?.name ?? ''}"`);
    }
  }
}

function parseBoth(raw: string | object): HkxPoseBoth {
  const parsed = (typeof raw === 'string' ? JSON.parse(raw) : raw) as Partial<HkxPoseBoth> & { error?: string };
  if (parsed.error) throw new Error(parsed.error);
  if (!isFiniteNumber(parsed.duration)) throw new Error('clip duration is missing or not finite');
  if (!parsed.local || !parsed.model) throw new Error('no bones');
  checkBones(parsed.local);
  checkBones(parsed.model);
  return parsed as HkxPoseBoth;
}

function parseClip(raw: string | object): HkxClipInfo {
  const parsed = (typeof raw === 'string' ? JSON.parse(raw) : raw) as Partial<HkxClipInfo> & { error?: string };
  if (parsed.error) throw new Error(parsed.error);
  if (!parsed.skeleton_match) throw new Error('no clip info');
  return parsed as HkxClipInfo;
}

function parseInspect(raw: string | object): HkxAnimationInspect {
  const parsed = (typeof raw === 'string' ? JSON.parse(raw) : raw) as Partial<HkxAnimationInspect> & { error?: string };
  if (parsed.error) throw new Error(parsed.error);
  if (typeof parsed.kind !== 'string' || !parsed.kind) throw new Error('inspected clip has no kind');
  if (!isFiniteNumber(parsed.track_count) || !isFiniteNumber(parsed.annotation_count)) {
    throw new Error('inspected clip has no track or annotation count');
  }
  if (!Array.isArray(parsed.annotations)) throw new Error('inspected clip has no annotation tracks');
  return parsed as HkxAnimationInspect;
}

function annotationSummary(inspect: HkxAnimationInspect): string {
  return inspect.annotations
    .map(track => `${track.name || 'Annotations'}: ${track.entries.map(entry => `${entry.time.toFixed(3)}s ${entry.text}`).join(', ')}`)
    .join(' · ');
}

function matchNote(info: HkxClipInfo): string {
  if (info.skeleton_match === 'match') {
    return `Clip was authored for skeleton "${info.original_skeleton_name}", which matches the selected skeleton.`;
  }
  if (info.skeleton_match === 'mismatch') {
    return `Clip was authored for skeleton "${info.original_skeleton_name}", not "${info.skeleton_name}". `
      + 'The preview applies the clip to the bone indices of the selected skeleton, so the bones shown may not correspond.';
  }
  return 'Skeleton compatibility unverified: the clip or skeleton name is empty, or the clip holds more than one binding.';
}

export default function HkxPanel({ onClose }: { onClose: () => void }) {
  const [skeletonPath, setSkeletonPath] = useState(() => localStorage.getItem('hkx.skeletonPath') ?? '');
  const [animationPath, setAnimationPath] = useState(() => localStorage.getItem('hkx.animationPath') ?? '');
  const [skeletonInfo, setSkeletonInfo] = useState('');
  const [rig, setRig] = useState<Rig | null>(null);
  const [clipInfo, setClipInfo] = useState<HkxClipInfo | null>(null);
  const [inspect, setInspect] = useState<HkxAnimationInspect | null>(null);
  const [inspectError, setInspectError] = useState('');
  const [loaded, setLoaded] = useState<ClipHandle | null>(null);
  const [clip, setClip] = useState<ClipInfo | null>(null);
  const [time, setTime] = useState(0);
  const [playing, setPlaying] = useState(false);
  const [modelBones, setModelBones] = useState<HkxBone[] | null>(null);
  const [ragdollScene, setRagdollScene] = useState<RagdollScene | null>(null);
  const [localBones, setLocalBones] = useState<HkxBone[] | null>(null);
  const [selected, setSelected] = useState(0);
  const [busy, setBusy] = useState(false);
  const [body, setBody] = useState<HkxBody | null>(null);
  const [textureMisses, setTextureMisses] = useState<string[]>([]);
  const [error, setError] = useState('');

  const clipRef = useRef<ClipHandle | null>(null);
  const wantedRef = useRef(0);
  const timeRef = useRef(0);
  const poseBusyRef = useRef(false);

  const unavailable = !getHkx();
  const duration = clip?.duration ?? 0;
  const frameStep = clip && clip.frameCount > 1 ? clip.duration / (clip.frameCount - 1) : 0;
  const frameIndex = frameStep > 0 ? Math.round(time / frameStep) : 0;
  const reference = useMemo(() => (rig ? referenceModelPose(rig.bones) : null), [rig]);
  const stale = loaded !== null && (skeletonPath.trim() !== loaded.skeleton || animationPath.trim() !== loaded.animation);

  useEffect(() => { localStorage.setItem('hkx.skeletonPath', skeletonPath); }, [skeletonPath]);
  useEffect(() => { localStorage.setItem('hkx.animationPath', animationPath); }, [animationPath]);
  const changeSkeletonPath = (path: string) => { setPlaying(false); setSkeletonPath(path); };
  const changeAnimationPath = (path: string) => { setPlaying(false); setAnimationPath(path); };
  const showBody = async (nifPath: string, textureRoot: string) => {
    const nif = getNif();
    if (!nif) { setError('NIF bridge unavailable.'); return; }
    try {
      setTextureMisses([]);
      setBody(await loadSkinnedBody(nif, nifPath, textureRoot, rel => setTextureMisses(found => (found.includes(rel) ? found : [...found, rel]))));
      setError('');
    } catch (e) {
      setError(messageOf(e));
    }
  };

  const pump = useCallback(async () => {
    const host = getHkx();
    if (!host || poseBusyRef.current) return;
    poseBusyRef.current = true;
    try {
      for (;;) {
        const handle = clipRef.current;
        if (!handle) return;
        const at = wantedRef.current;
        try {
          const both = parseBoth(await host.PoseBoth(handle.skeleton, handle.animation, at));
          if (clipRef.current !== handle) continue;
          setModelBones(both.model);
          setLocalBones(both.local);
          setClip({ duration: both.duration, frameCount: both.frame_count });
          setError('');
          if (wantedRef.current === at) return;
        } catch (e) {
          if (clipRef.current !== handle || wantedRef.current !== at) continue;
          setError(messageOf(e));
          setPlaying(false);
          return;
        }
      }
    } finally {
      poseBusyRef.current = false;
    }
  }, []);

  const moveTo = useCallback((next: number) => {
    timeRef.current = next;
    wantedRef.current = next;
    setTime(next);
    void pump();
  }, [pump]);

  useEffect(() => {
    if (!playing || duration <= 0) return;
    let last: number | undefined;
    let raf = 0;
    const tick = (now: number) => {
      const dt = last === undefined ? 0 : Math.max(0, now - last) / 1000;
      last = now;
      moveTo(advanceTime(timeRef.current, dt, duration));
      raf = requestAnimationFrame(tick);
    };
    raf = requestAnimationFrame(tick);
    return () => cancelAnimationFrame(raf);
  }, [playing, duration, moveTo]);

  const load = async () => {
    const host = getHkx();
    const sk = skeletonPath.trim();
    const an = animationPath.trim();
    if (!host || !sk || !an) return;
    clipRef.current = null;
    setPlaying(false);
    setBusy(true);
    setError('');
    setClip(null);
    setModelBones(null);
    setLocalBones(null);
    setSkeletonInfo('');
    setRig(null);
    setClipInfo(null);
    setInspect(null);
    setInspectError('');
    setLoaded(null);
    setSelected(0);
    try {
      const skeleton = parseHost<Rig>(await host.Skeleton(sk));
      const info = parseClip(await host.LoadClip(sk, an));
      clipRef.current = { skeleton: sk, animation: an };
      // Constraint: the extras are what names a partition and a locked bone, so they belong beside the bone count.
      const count = (n: number, one: string, many: string) => `${n} ${n === 1 ? one : many}`;
      const extras: string[] = [];
      const partitions = skeleton.partitions?.length ?? 0;
      const slots = skeleton.float_slots?.length ?? 0;
      const locked = skeleton.bones.filter(b => b.lock).length;
      if (partitions > 0) extras.push(count(partitions, 'partition', 'partitions'));
      if (slots > 0) extras.push(count(slots, 'float slot', 'float slots'));
      if (locked > 0) extras.push(count(locked, 'locked bone', 'locked bones'));
      setRig(skeleton);
      setSkeletonInfo([`${skeleton.name || 'Skeleton'} · ${skeleton.bones.length} bones`, ...extras].join(' · '));
      setClipInfo(info);
      setLoaded({ skeleton: sk, animation: an });
      try {
        setInspect(parseInspect(await host.InspectAnimation(an)));
      } catch (e) {
        // Constraint: an inspection the read refuses must not hide the clip that already loaded, so it reports separately.
        setInspect(null);
        setInspectError(messageOf(e));
      }
      moveTo(0);
    } catch (e) {
      setError(messageOf(e));
    } finally {
      setBusy(false);
    }
  };

  const browse = async (title: string, set: (path: string) => void) => {
    const picker = getNif();
    if (!picker) return;
    try {
      const path = await picker.BrowseForFile(title, 'Havok (*.hkx)|*.hkx|All files|*.*');
      if (path) set(path);
    } catch (e) {
      setError(messageOf(e));
    }
  };

  const stepFrame = (delta: number) => {
    if (!clip || frameStep <= 0) return;
    setPlaying(false);
    moveTo(Math.min(Math.max(frameIndex + delta, 0), clip.frameCount - 1) * frameStep);
  };

  const bone = localBones?.[selected];

  return (
    <div className="papyrus-overlay" onClick={onClose}>
      <div className="papyrus-modal glass-panel nif-modal-wide" onClick={e => e.stopPropagation()}>
        <div className="papyrus-header">
          <span className="papyrus-title"><Clock size={16} /> Animation (HKX)</span>
          <button className="papyrus-close" onClick={onClose} title="Close"><X size={16} /></button>
        </div>

        {unavailable && <div className="papyrus-warn">HKX bridge not available -- run the desktop app (not the browser dev server).</div>}

        <div className="papyrus-body">
          <div className="papyrus-form">
            <label className="papyrus-field">
              <span>Skeleton (.hkx)</span>
              <div className="papyrus-input-row">
                <input value={skeletonPath} onChange={e => changeSkeletonPath(e.target.value)} placeholder="Path to a skeleton .hkx" />
                <button className="sidebar-action-btn" disabled={unavailable}
                  onClick={() => browse('Select a skeleton .hkx', changeSkeletonPath)}>Browse…</button>
              </div>
            </label>
            <label className="papyrus-field">
              <span>Animation (.hkx)</span>
              <div className="papyrus-input-row">
                <input value={animationPath} onChange={e => changeAnimationPath(e.target.value)} placeholder="Path to an animation .hkx" />
                <button className="sidebar-action-btn" disabled={unavailable}
                  onClick={() => browse('Select an animation .hkx', changeAnimationPath)}>Browse…</button>
              </div>
            </label>
            <button className="papyrus-run" onClick={load}
              disabled={busy || unavailable || !skeletonPath.trim() || !animationPath.trim()}>
              <Play size={14} /> {busy ? 'Loading…' : 'Load clip'}
            </button>
            {skeletonInfo && <div className="nif-geo-info">{skeletonInfo}</div>}
            {clipInfo && <div className={clipInfo.skeleton_match === 'match' ? 'nif-geo-info' : 'papyrus-warn'}>{matchNote(clipInfo)}</div>}
            {inspect && <div className="nif-geo-info">{inspect.kind} · {inspect.track_count} tracks · annotations {inspect.annotation_count}</div>}
            {inspect && inspect.annotation_count > 0 && <div className="nif-geo-info">{annotationSummary(inspect)}</div>}
            {inspectError && <div className="papyrus-warn">Clip inspection failed: {inspectError}</div>}
            {body && textureMisses.length > 0 && (
              <div className="papyrus-warn">
                {textureMisses.length} body texture(s) were not found under the Data folder and are shown untextured (loose textures in other MO2 mods are not searched): {textureMisses.join(', ')}
              </div>
            )}
            {loaded && <div className="nif-geo-info">Loaded {loaded.skeleton} + {loaded.animation}</div>}
            {stale && <div className="papyrus-warn">Paths changed since load. The preview still shows the loaded pair; click Load clip to update it.</div>}

            <HkxSubgraphTools host={getHkx()} initialPlugin="" initialDataFolder="" disabled={unavailable} />
            <HkxAnimationIndexTools host={getHkx()} disabled={unavailable} onPickSkeleton={changeSkeletonPath} onPickAnimation={changeAnimationPath} onPickBody={showBody} />
            <HkxChainTools host={getHkx()} disabled={unavailable} />
            <HkxSweepTools host={getHkx()} disabled={unavailable} />
            <HkxRagdollTools host={getHkx()} disabled={unavailable} onScene={setRagdollScene} poseBones={modelBones} />

            {clip && (
              <div className="hkx-timeline">
                <div className="nif-geo-info">{clip.duration.toFixed(3)} s · {clip.frameCount} frames</div>
                <input type="range" aria-label="Time" min={0} max={clip.duration} step="any" value={time}
                  onChange={e => { setPlaying(false); moveTo(Number(e.target.value)); }} />
                <div className="hkx-controls">
                  <button className="sidebar-action-btn" title="Step back one frame" onClick={() => stepFrame(-1)}>
                    <ChevronLeft size={14} />
                  </button>
                  <button className="sidebar-action-btn" disabled={duration <= 0 || stale} onClick={() => setPlaying(p => !p)}>
                    {playing ? 'Pause' : <><Play size={13} /> Play</>}
                  </button>
                  <button className="sidebar-action-btn" title="Step forward one frame" onClick={() => stepFrame(1)}>
                    <ChevronRight size={14} />
                  </button>
                  <span className="hkx-time">{time.toFixed(3)} s · frame {frameIndex}</span>
                </div>
              </div>
            )}

            {localBones && (
              <>
                <div className="nif-geo-info">Bones (local pose at this time)</div>
                <ul className="hkx-bones">
                  {localBones.map((b, i) => (
                    <li key={i}>
                      <button className={i === selected ? 'active' : ''} onClick={() => setSelected(i)}>{b.name}</button>
                    </li>
                  ))}
                </ul>
                {bone && (
                  <div className="hkx-bone-detail">
                    <div>{bone.name}</div>
                    <div>translation: {fmt(bone.translation)}</div>
                    <div>rotation (x, y, z, w): {fmt(bone.rotation)}</div>
                  </div>
                )}
              </>
            )}

            {rig && (rig.partitions?.length ?? 0) > 0 && (
              <>
                <div className="nif-geo-info">Partitions</div>
                <ul className="hkx-bones">
                  {(rig.partitions ?? []).map((p, i) => (
                    <li key={i}>{p.named ? p.name : `partition ${i}`} · bones {p.start_bone}–{p.start_bone + p.num_bones - 1}</li>
                  ))}
                </ul>
              </>
            )}

            {rig && (rig.float_slots?.length ?? 0) > 0 && (
              <>
                <div className="nif-geo-info">Float slots</div>
                <ul className="hkx-bones">
                  {(rig.float_slots ?? []).map((s, i) => (
                    <li key={i}>{s.named ? s.name : `slot ${i}`}</li>
                  ))}
                </ul>
              </>
            )}

            <HkxBgsTools host={getHkx()} initialPath={animationPath.trim() || skeletonPath.trim()}
              skeletonPath={skeletonPath.trim()} animationPath={animationPath.trim()} disabled={unavailable} />
            <HkxSceneTools host={getHkx()} initialPath={animationPath.trim() || skeletonPath.trim()} disabled={unavailable} />
            <HkxInspectTools host={getHkx()} initialPath={animationPath.trim() || skeletonPath.trim()} disabled={unavailable} />
            <HkxGltfSavePanel host={getHkx()} skeletonPath={skeletonPath.trim()} templatePath={animationPath.trim()} disabled={unavailable} />
            <HkxTrackTable host={getHkx()} clipPath={animationPath.trim()} disabled={unavailable} />
            <HkxTimelineTools host={getHkx()} initialPath={animationPath.trim()} disabled={unavailable} />
            <HkxConvertTools host={getHkx()} initialPath={animationPath.trim() || skeletonPath.trim()} disabled={unavailable} />
          </div>

          <div className="papyrus-output hkx-output">
            {error && (
              <div className="papyrus-banner error">
                <XCircle size={15} />
                <span className="papyrus-banner-text">{error}</span>
              </div>
            )}
            {modelBones || ragdollScene?.bodies.length
              ? <HkxViewport bones={modelBones} ragdoll={ragdollScene} body={body} reference={reference}
                  frameKey={loaded ? `${loaded.skeleton}|${loaded.animation}` : ragdollScene?.frameKey ?? ''} />
              : <div className="nif-view-empty">{unavailable ? 'HKX bridge unavailable.' : 'Pick a skeleton and an animation, then Load clip.'}</div>}
          </div>
        </div>
      </div>
    </div>
  );
}

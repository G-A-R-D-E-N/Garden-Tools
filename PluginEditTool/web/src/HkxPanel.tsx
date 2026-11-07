import { useState, useEffect, useCallback, useRef } from 'react';
import { Clock, X, Play, ChevronLeft, ChevronRight, XCircle } from 'lucide-react';
import { getHkx, getNif, type HkxBone, type HkxPose, type HkxSkeleton } from './backend';
import HkxViewport from './HkxViewport';
import { advanceTime } from './util/advanceTime';
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

export default function HkxPanel({ onClose }: { onClose: () => void }) {
  const [skeletonPath, setSkeletonPath] = useState(() => localStorage.getItem('hkx.skeletonPath') ?? '');
  const [animationPath, setAnimationPath] = useState(() => localStorage.getItem('hkx.animationPath') ?? '');
  const [skeletonInfo, setSkeletonInfo] = useState('');
  const [loadedKey, setLoadedKey] = useState('');
  const [clip, setClip] = useState<ClipInfo | null>(null);
  const [time, setTime] = useState(0);
  const [playing, setPlaying] = useState(false);
  const [modelBones, setModelBones] = useState<HkxBone[] | null>(null);
  const [localBones, setLocalBones] = useState<HkxBone[] | null>(null);
  const [selected, setSelected] = useState(0);
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState('');

  const clipRef = useRef<ClipHandle | null>(null);
  const wantedRef = useRef(0);
  const timeRef = useRef(0);
  const poseBusyRef = useRef(false);

  const unavailable = !getHkx();
  const duration = clip?.duration ?? 0;
  const frameStep = clip && clip.frameCount > 1 ? clip.duration / (clip.frameCount - 1) : 0;
  const frameIndex = frameStep > 0 ? Math.round(time / frameStep) : 0;

  useEffect(() => { localStorage.setItem('hkx.skeletonPath', skeletonPath); }, [skeletonPath]);
  useEffect(() => { localStorage.setItem('hkx.animationPath', animationPath); }, [animationPath]);

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
          const [model, local] = await Promise.all([
            host.Pose(handle.skeleton, handle.animation, at, 'model'),
            host.Pose(handle.skeleton, handle.animation, at, 'local'),
          ]);
          const modelPose = parseHost<HkxPose>(model);
          const localPose = parseHost<HkxPose>(local);
          if (clipRef.current !== handle) continue;
          setModelBones(modelPose.bones);
          setLocalBones(localPose.bones);
          setClip({ duration: modelPose.duration, frameCount: modelPose.frame_count });
          setError('');
          if (wantedRef.current === at) return;
        } catch (e) {
          if (clipRef.current !== handle) continue;
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
    setLoadedKey('');
    setSelected(0);
    try {
      const skeleton = parseHost<HkxSkeleton>(await host.Skeleton(sk));
      clipRef.current = { skeleton: sk, animation: an };
      setSkeletonInfo(`${skeleton.name || 'Skeleton'} · ${skeleton.bones.length} bones`);
      setLoadedKey(`${sk}|${an}`);
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
                <input value={skeletonPath} onChange={e => setSkeletonPath(e.target.value)} placeholder="Path to a skeleton .hkx" />
                <button className="sidebar-action-btn" disabled={unavailable}
                  onClick={() => browse('Select a skeleton .hkx', setSkeletonPath)}>Browse…</button>
              </div>
            </label>
            <label className="papyrus-field">
              <span>Animation (.hkx)</span>
              <div className="papyrus-input-row">
                <input value={animationPath} onChange={e => setAnimationPath(e.target.value)} placeholder="Path to an animation .hkx" />
                <button className="sidebar-action-btn" disabled={unavailable}
                  onClick={() => browse('Select an animation .hkx', setAnimationPath)}>Browse…</button>
              </div>
            </label>
            <button className="papyrus-run" onClick={load}
              disabled={busy || unavailable || !skeletonPath.trim() || !animationPath.trim()}>
              <Play size={14} /> {busy ? 'Loading…' : 'Load clip'}
            </button>
            {skeletonInfo && <div className="nif-geo-info">{skeletonInfo}</div>}

            {clip && (
              <div className="hkx-timeline">
                <div className="nif-geo-info">{clip.duration.toFixed(3)} s · {clip.frameCount} frames</div>
                <input type="range" aria-label="Time" min={0} max={clip.duration} step="any" value={time}
                  onChange={e => { setPlaying(false); moveTo(Number(e.target.value)); }} />
                <div className="hkx-controls">
                  <button className="sidebar-action-btn" title="Step back one frame" onClick={() => stepFrame(-1)}>
                    <ChevronLeft size={14} />
                  </button>
                  <button className="sidebar-action-btn" disabled={duration <= 0} onClick={() => setPlaying(p => !p)}>
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
          </div>

          <div className="papyrus-output hkx-output">
            {error && (
              <div className="papyrus-banner error">
                <XCircle size={15} />
                <span className="papyrus-banner-text">{error}</span>
              </div>
            )}
            {modelBones
              ? <HkxViewport bones={modelBones} frameKey={loadedKey} />
              : <div className="nif-view-empty">{unavailable ? 'HKX bridge unavailable.' : 'Pick a skeleton and an animation, then Load clip.'}</div>}
          </div>
        </div>
      </div>
    </div>
  );
}

import { useEffect, useRef, useState, type PointerEvent as ReactPointerEvent, type WheelEvent as ReactWheelEvent } from 'react';
import { DETAIL_SIZE, type PreviewView } from './previewRenderer';
import { getPreviewRenderer } from './usePreviewQueue';

const RE_RENDER_DELAY_MS = 120;
const ORBIT_SPEED = 0.01;
const PITCH_LIMIT = 1.4;
const ZOOM_MIN = 0.3;
const ZOOM_MAX = 4;
const ZOOM_STEP = 0.001;

const DETAIL_VIEW: PreviewView = { yaw: 0.65, pitch: 0.35, zoom: 1, size: DETAIL_SIZE };

interface Shown {
  path: string;
  url: string | null;
}

export default function OrbitPreview({ modelPath }: { modelPath: string }) {
  const [view, setView] = useState<PreviewView>(DETAIL_VIEW);
  const [shown, setShown] = useState<Shown>({ path: '', url: null });
  const drag = useRef<{ x: number; y: number } | null>(null);

  useEffect(() => {
    if (modelPath === '') return;
    let alive = true;
    const handle = setTimeout(() => {
      getPreviewRenderer().render(modelPath, view).then(
        url => { if (alive) setShown({ path: modelPath, url }); },
        () => { if (alive) setShown({ path: modelPath, url: null }); },
      );
    }, RE_RENDER_DELAY_MS);
    return () => {
      alive = false;
      clearTimeout(handle);
    };
  }, [modelPath, view]);

  const onPointerDown = (e: ReactPointerEvent<HTMLDivElement>) => {
    drag.current = { x: e.clientX, y: e.clientY };
    e.currentTarget.setPointerCapture(e.pointerId);
  };

  const onPointerMove = (e: ReactPointerEvent<HTMLDivElement>) => {
    const start = drag.current;
    if (!start) return;
    const dx = e.clientX - start.x;
    const dy = e.clientY - start.y;
    drag.current = { x: e.clientX, y: e.clientY };
    setView(prev => ({
      ...prev,
      yaw: prev.yaw - dx * ORBIT_SPEED,
      pitch: Math.min(PITCH_LIMIT, Math.max(-PITCH_LIMIT, prev.pitch + dy * ORBIT_SPEED)),
    }));
  };

  const endDrag = (e: ReactPointerEvent<HTMLDivElement>) => {
    drag.current = null;
    if (e.currentTarget.hasPointerCapture(e.pointerId)) e.currentTarget.releasePointerCapture(e.pointerId);
  };

  const onWheel = (e: ReactWheelEvent<HTMLDivElement>) => {
    const factor = Math.exp(-e.deltaY * ZOOM_STEP);
    setView(prev => ({ ...prev, zoom: Math.min(ZOOM_MAX, Math.max(ZOOM_MIN, prev.zoom * factor)) }));
  };

  const url = shown.path === modelPath ? shown.url : null;

  return (
    <div className="ab-orbit">
      <div
        className="ab-orbit-stage"
        role="img"
        aria-label={url ? 'Model preview. Drag to orbit, scroll to zoom.' : 'Model preview unavailable'}
        onPointerDown={onPointerDown}
        onPointerMove={onPointerMove}
        onPointerUp={endDrag}
        onPointerCancel={endDrag}
        onWheel={onWheel}
      >
        {url ? <img src={url} alt="" draggable={false} /> : <span className="ab-orbit-empty">No preview</span>}
      </div>
      <div className="ab-orbit-tools">
        <span>Drag to orbit · wheel to zoom</span>
        <button type="button" onClick={() => setView(DETAIL_VIEW)}>Reset view</button>
      </div>
    </div>
  );
}

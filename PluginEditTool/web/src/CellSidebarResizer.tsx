import { useEffect, useRef, useState, type KeyboardEvent as ReactKeyboardEvent, type MouseEvent as ReactMouseEvent } from 'react';
import { clampSidebarWidth, maxSidebarWidth, SIDEBAR_DEFAULT, SIDEBAR_KEY_STEP, SIDEBAR_MIN } from './util/cellSidebar';

export function useViewportWidth(): number {
  const [width, setWidth] = useState(() => window.innerWidth);
  useEffect(() => {
    const onResize = () => setWidth(window.innerWidth);
    window.addEventListener('resize', onResize);
    return () => window.removeEventListener('resize', onResize);
  }, []);
  return width;
}

export default function CellSidebarResizer({ width, viewportWidth = window.innerWidth, onResize, onCommit }: {
  width: number;
  viewportWidth?: number;
  onResize: (width: number) => void;
  onCommit?: (width: number) => void;
}) {
  const [dragging, setDragging] = useState(false);
  const stopDrag = useRef<(() => void) | null>(null);

  useEffect(() => () => stopDrag.current?.(), []);

  const onMouseDown = (e: ReactMouseEvent) => {
    if (e.button !== 0) return;
    e.preventDefault();
    const startX = e.clientX;
    const startWidth = width;
    let latest = width;
    let moved = false;
    const move = (ev: MouseEvent) => {
      moved = true;
      latest = clampSidebarWidth(startWidth + ev.clientX - startX, viewportWidth);
      onResize(latest);
    };
    const stop = () => {
      window.removeEventListener('mousemove', move);
      window.removeEventListener('mouseup', stop);
      stopDrag.current = null;
      document.body.style.cursor = '';
      setDragging(false);
      if (moved) onCommit?.(latest);
    };
    window.addEventListener('mousemove', move);
    window.addEventListener('mouseup', stop);
    stopDrag.current = stop;
    document.body.style.cursor = 'col-resize';
    setDragging(true);
  };

  const step = (next: number) => {
    const clamped = clampSidebarWidth(next, viewportWidth);
    onResize(clamped);
    onCommit?.(clamped);
  };

  const onKeyDown = (e: ReactKeyboardEvent) => {
    if (e.key !== 'ArrowLeft' && e.key !== 'ArrowRight') return;
    e.preventDefault();
    step(width + (e.key === 'ArrowRight' ? SIDEBAR_KEY_STEP : -SIDEBAR_KEY_STEP));
  };

  return (
    <div
      className={`cell-sidebar-resizer${dragging ? ' dragging' : ''}`}
      role="separator"
      aria-orientation="vertical"
      aria-label="Resize Cell Viewer sidebar"
      aria-valuemin={SIDEBAR_MIN}
      aria-valuemax={maxSidebarWidth(viewportWidth)}
      aria-valuenow={width}
      tabIndex={0}
      title={`Drag to resize (double-click resets to ${SIDEBAR_DEFAULT}px)`}
      onMouseDown={onMouseDown}
      onDoubleClick={() => step(SIDEBAR_DEFAULT)}
      onKeyDown={onKeyDown}
    />
  );
}

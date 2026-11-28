import { useEffect, useRef, useState, type KeyboardEvent as ReactKeyboardEvent, type MouseEvent as ReactMouseEvent } from 'react';
import { clampSidebarWidth, maxSidebarWidth, SIDEBAR_DEFAULT, SIDEBAR_KEY_STEP, SIDEBAR_MIN } from './util/cellSidebar';

export type ResizerPanel = {
  label: string;
  min: number;
  max: number;
  defaultWidth: number;
  clamp: (width: number) => number;
  fromRight: boolean;
};

export default function CellSidebarResizer({ width, viewportWidth = window.innerWidth, onResize, onCommit, panel }: {
  width: number;
  viewportWidth?: number;
  onResize: (width: number) => void;
  onCommit?: (width: number) => void;
  panel?: ResizerPanel;
}) {
  const [dragging, setDragging] = useState(false);
  const stopDrag = useRef<(() => void) | null>(null);
  const spec: ResizerPanel = panel ?? {
    label: 'Resize Cell Viewer sidebar',
    min: SIDEBAR_MIN,
    max: maxSidebarWidth(viewportWidth),
    defaultWidth: SIDEBAR_DEFAULT,
    clamp: next => clampSidebarWidth(next, viewportWidth),
    fromRight: false,
  };
  const dir = spec.fromRight ? -1 : 1;

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
      latest = spec.clamp(startWidth + dir * (ev.clientX - startX));
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
    const clamped = spec.clamp(next);
    onResize(clamped);
    onCommit?.(clamped);
  };

  const onKeyDown = (e: ReactKeyboardEvent) => {
    if (e.key !== 'ArrowLeft' && e.key !== 'ArrowRight') return;
    e.preventDefault();
    const sign = e.key === 'ArrowRight' ? 1 : -1;
    step(width + dir * sign * SIDEBAR_KEY_STEP);
  };

  return (
    <div
      className={`cell-sidebar-resizer${dragging ? ' dragging' : ''}`}
      role="separator"
      aria-orientation="vertical"
      aria-label={spec.label}
      aria-valuemin={spec.min}
      aria-valuemax={spec.max}
      aria-valuenow={width}
      tabIndex={0}
      title={`Drag to resize (double-click resets to ${spec.defaultWidth}px)`}
      onMouseDown={onMouseDown}
      onDoubleClick={() => step(spec.defaultWidth)}
      onKeyDown={onKeyDown}
    />
  );
}

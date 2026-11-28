import { useEffect, useRef, type KeyboardEvent as ReactKeyboardEvent } from 'react';
import type { MenuAction, MenuEntry } from './menuModel';

export default function AssetBrowserMenu({ x, y, entries, onAction, onClose }: {
  x: number;
  y: number;
  entries: MenuEntry[];
  onAction: (action: MenuAction) => void;
  onClose: () => void;
}) {
  const rootRef = useRef<HTMLDivElement>(null);

  useEffect(() => {
    const first = rootRef.current?.querySelector<HTMLButtonElement>('[role="menuitem"]:not(:disabled)');
    first?.focus();
  }, []);

  const move = (step: number) => {
    const items = Array.from(rootRef.current?.querySelectorAll<HTMLButtonElement>('[role="menuitem"]:not(:disabled)') ?? []);
    if (items.length === 0) return;
    const at = items.indexOf(document.activeElement as HTMLButtonElement);
    const next = at < 0 ? 0 : (at + step + items.length) % items.length;
    items[next]?.focus();
  };

  const onKeyDown = (e: ReactKeyboardEvent<HTMLDivElement>) => {
    if (e.key === 'ArrowDown') { e.preventDefault(); move(1); }
    else if (e.key === 'ArrowUp') { e.preventDefault(); move(-1); }
    else if (e.key === 'Escape') { e.preventDefault(); e.stopPropagation(); onClose(); }
    else if (e.key === 'Tab') { onClose(); }
  };

  return (
    <div
      ref={rootRef}
      role="menu"
      aria-label="Object actions"
      className="ab-menu"
      style={{ left: x, top: y }}
      onKeyDown={onKeyDown}
    >
      {entries.map(entry => (
        <button
          key={entry.action}
          type="button"
          role="menuitem"
          tabIndex={-1}
          className="ab-menu-item"
          disabled={entry.disabled}
          onClick={() => onAction(entry.action)}
        >
          {entry.label}
        </button>
      ))}
    </div>
  );
}

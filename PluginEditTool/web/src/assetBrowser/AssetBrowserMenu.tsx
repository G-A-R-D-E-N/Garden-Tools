import { useEffect, useRef, type KeyboardEvent as ReactKeyboardEvent } from 'react';

export type MenuAction =
  | 'placeViewCentre'
  | 'placeOrigin'
  | 'copyHexId'
  | 'copyEditorId'
  | 'copyModelPath'
  | 'copyFormKey'
  | 'preview';

export interface MenuEntry {
  action: MenuAction;
  label: string;
  disabled: boolean;
}

export function menuEntries(count: number, anyModelPath: boolean): MenuEntry[] {
  const many = count > 1;
  return [
    { action: 'placeViewCentre', label: many ? `Place ${count} at view centre` : 'Place at view centre', disabled: false },
    { action: 'placeOrigin', label: many ? `Place ${count} at cell origin` : 'Place at cell origin', disabled: false },
    { action: 'copyHexId', label: many ? `Copy ${count} Form IDs` : 'Copy Form ID', disabled: false },
    { action: 'copyEditorId', label: many ? `Copy ${count} editor IDs` : 'Copy editor ID', disabled: false },
    { action: 'copyModelPath', label: many ? `Copy ${count} model paths` : 'Copy model path', disabled: !anyModelPath },
    { action: 'copyFormKey', label: many ? `Copy ${count} form keys` : 'Copy form key', disabled: false },
    { action: 'preview', label: 'Preview at view centre', disabled: false },
  ];
}

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

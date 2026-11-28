import { useEffect, useId, useRef, useState, type KeyboardEvent as ReactKeyboardEvent } from 'react';
import { ChevronDown, ChevronRight, Plus } from 'lucide-react';
import { useReadOnly } from '../readOnlyContext';
import { ACTION_TYPES } from './sceneModel';

interface Props {
  phaseCount: number;
  onAddPhase: () => void;
  onAddActor: () => void;
  onAddAction: (type: number) => void;
}

const ACTION_ITEM = 2;

const menuItems = (group: HTMLElement | null): HTMLElement[] =>
  group
    ? Array.from(group.children).filter((child): child is HTMLElement => child.getAttribute('role') === 'menuitem')
    : [];

export default function AddMenu({ phaseCount, onAddPhase, onAddActor, onAddAction }: Props) {
  const [open, setOpen] = useState(false);
  const [typesOpen, setTypesOpen] = useState(false);
  const [cursor, setCursor] = useState(0);
  const [typeCursor, setTypeCursor] = useState(0);
  const root = useRef<HTMLDivElement>(null);
  const trigger = useRef<HTMLButtonElement>(null);
  const menu = useRef<HTMLDivElement>(null);
  const types = useRef<HTMLDivElement>(null);
  const focusTypes = useRef(false);
  const menuId = useId();
  const hintId = useId();
  const noPhase = phaseCount === 0;
  const readOnly = useReadOnly();
  const blocked = readOnly || noPhase;

  useEffect(() => {
    if (open) menuItems(menu.current)[0]?.focus();
  }, [open]);

  useEffect(() => {
    if (!typesOpen || !focusTypes.current) return;
    focusTypes.current = false;
    menuItems(types.current)[0]?.focus();
  }, [typesOpen]);

  useEffect(() => {
    if (!open) return;
    const onPointer = (event: MouseEvent) => {
      if (!root.current?.contains(event.target as Node)) {
        setOpen(false);
        setTypesOpen(false);
      }
    };
    const onKey = (event: KeyboardEvent) => {
      if (event.key !== 'Escape') return;
      if (typesOpen) {
        menuItems(menu.current)[ACTION_ITEM]?.focus();
        setTypesOpen(false);
      } else {
        setOpen(false);
        trigger.current?.focus();
      }
    };
    document.addEventListener('mousedown', onPointer);
    document.addEventListener('keydown', onKey);
    return () => {
      document.removeEventListener('mousedown', onPointer);
      document.removeEventListener('keydown', onKey);
    };
  }, [open, typesOpen]);

  const closeMenu = (restoreFocus: boolean) => {
    setOpen(false);
    setTypesOpen(false);
    if (restoreFocus) trigger.current?.focus();
  };

  const finish = (add: () => void) => {
    if (readOnly) return;
    closeMenu(true);
    add();
  };

  const toggle = () => {
    if (open) closeMenu(false);
    else setOpen(true);
  };

  const onTriggerKey = (event: ReactKeyboardEvent<HTMLButtonElement>) => {
    if (event.key !== 'ArrowDown' || open) return;
    event.preventDefault();
    setOpen(true);
  };

  const onMenuKey = (event: ReactKeyboardEvent<HTMLDivElement>) => {
    const inTypes = types.current?.contains(event.target as Node) ?? false;
    const items = menuItems(inTypes ? types.current : menu.current);
    const at = items.indexOf(event.target as HTMLElement);
    const go = (to: number) => items[(to + items.length) % items.length]?.focus();
    if (event.key === 'ArrowDown') go(at + 1);
    else if (event.key === 'ArrowUp') go(at - 1);
    else if (event.key === 'Home') go(0);
    else if (event.key === 'End') go(items.length - 1);
    else if (event.key === 'ArrowRight' && !inTypes && !blocked && at === ACTION_ITEM) {
      if (typesOpen) menuItems(types.current)[0]?.focus();
      else {
        focusTypes.current = true;
        setTypesOpen(true);
      }
    } else if (event.key === 'ArrowLeft' && inTypes) {
      menuItems(menu.current)[ACTION_ITEM]?.focus();
      setTypesOpen(false);
    } else return;
    event.preventDefault();
  };

  return (
    <div className="scene-add" ref={root}
      onBlur={event => { if (!root.current?.contains(event.relatedTarget as Node | null)) closeMenu(false); }}>
      <button ref={trigger} type="button" className="sidebar-action-btn" aria-haspopup="menu" aria-expanded={open}
        aria-controls={menuId} onClick={toggle} onKeyDown={onTriggerKey}>
        <Plus size={14} /> Add <ChevronDown size={14} aria-hidden="true" />
      </button>
      {open && (
        <div id={menuId} ref={menu} role="menu" tabIndex={-1} aria-label="Add to scene" className="scene-add-menu"
          onKeyDown={onMenuKey}>
          <button type="button" role="menuitem" aria-disabled={readOnly} tabIndex={cursor === 0 ? 0 : -1}
            onFocus={() => setCursor(0)} onClick={() => finish(onAddPhase)}>Add phase</button>
          <button type="button" role="menuitem" aria-disabled={readOnly} tabIndex={cursor === 1 ? 0 : -1}
            onFocus={() => setCursor(1)} onClick={() => finish(onAddActor)}>Add actor</button>
          <button type="button" role="menuitem" aria-haspopup="menu" aria-expanded={typesOpen}
            aria-disabled={blocked} aria-describedby={noPhase ? hintId : undefined}
            tabIndex={cursor === ACTION_ITEM ? 0 : -1} onFocus={() => setCursor(ACTION_ITEM)}
            onClick={() => { if (!blocked) setTypesOpen(current => !current); }}>
            Add action
            {noPhase && <span id={hintId} className="scene-add-hint">Add a phase first</span>}
            <ChevronRight size={14} aria-hidden="true" />
          </button>
          {typesOpen && !blocked && (
            <div ref={types} role="group" tabIndex={-1} aria-label="Action type" className="scene-add-types">
              {ACTION_TYPES.map((entry, index) => (
                <button key={entry.type} type="button" role="menuitem" tabIndex={typeCursor === index ? 0 : -1}
                  onFocus={() => setTypeCursor(index)} onClick={() => finish(() => onAddAction(entry.type))}>
                  {entry.label}
                </button>
              ))}
            </div>
          )}
        </div>
      )}
    </div>
  );
}

import { useRef, type KeyboardEvent, type ReactNode } from 'react';

export interface EditorTab {
  id: string;
  label: string;
  count?: number;
  badge?: 'problem' | 'note';
}

export interface EditorTabsProps {
  tabs: EditorTab[];
  active: string;
  onChange: (id: string) => void;
  ariaLabel: string;
  idPrefix: string;
}

const tabDomId = (idPrefix: string, id: string) => `${idPrefix}-tab-${id}`;
const panelDomId = (idPrefix: string, id: string) => `${idPrefix}-panel-${id}`;

export function EditorTabs({ tabs, active, onChange, ariaLabel, idPrefix }: EditorTabsProps) {
  const buttons = useRef(new Map<string, HTMLButtonElement>());
  const selected = tabs.some(tab => tab.id === active) ? active : tabs[0]?.id;

  const move = (to: number) => {
    const next = tabs[(to + tabs.length) % tabs.length];
    if (!next) return;
    onChange(next.id);
    buttons.current.get(next.id)?.focus();
  };

  const onKeyDown = (event: KeyboardEvent<HTMLButtonElement>, at: number) => {
    if (event.key === 'ArrowRight') move(at + 1);
    else if (event.key === 'ArrowLeft') move(at - 1);
    else if (event.key === 'Home') move(0);
    else if (event.key === 'End') move(tabs.length - 1);
    else return;
    event.preventDefault();
  };

  return (
    <div role="tablist" aria-label={ariaLabel} className="editor-tabs">
      {tabs.map((tab, at) => {
        const isActive = tab.id === selected;
        return (
          <button key={tab.id} type="button" role="tab" id={tabDomId(idPrefix, tab.id)}
            ref={node => {
              if (node) buttons.current.set(tab.id, node);
              else buttons.current.delete(tab.id);
            }}
            aria-selected={isActive} aria-controls={panelDomId(idPrefix, tab.id)} tabIndex={isActive ? 0 : -1}
            className={isActive ? 'editor-tab editor-tab-active' : 'editor-tab'}
            onClick={() => onChange(tab.id)} onKeyDown={event => onKeyDown(event, at)}>
            <span className="editor-tab-label">{tab.label}</span>
            {tab.count !== undefined && <>{' '}<span className="editor-tab-count">{tab.count}</span></>}
            {tab.badge && <>{' '}<span className={`editor-tab-badge editor-tab-badge-${tab.badge}`}>{tab.badge}</span></>}
          </button>
        );
      })}
    </div>
  );
}

export function EditorTabPanel({ id, idPrefix, active, children }: {
  id: string;
  idPrefix: string;
  active: boolean;
  children: ReactNode;
}) {
  return (
    <div role="tabpanel" id={panelDomId(idPrefix, id)} aria-labelledby={tabDomId(idPrefix, id)} hidden={!active}
      className="editor-tab-panel">
      {children}
    </div>
  );
}

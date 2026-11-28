import { useEffect, useRef, type ComponentProps } from 'react';
import { EyeOff, X } from 'lucide-react';
import type { CellPlacedReference } from './backend';
import CellEditPanel from './CellEditPanel';
import CellSidebarResizer from './CellSidebarResizer';
import { PluginsTab, ReferenceTab, ScriptsTab, type ReferenceEditProps } from './ReferencePanel';
import type { ReferenceInspectorState } from './useReferenceInspector';
import { useReferenceData } from './useReferenceData';
import { clampInspectorWidth, INSPECTOR_DEFAULT, INSPECTOR_MIN, maxInspectorWidth, type InspectorTab } from './util/referenceInspector';
import './ReferenceInspector.css';

export type EditHandlers = Omit<ComponentProps<typeof CellEditPanel>, 'reference'> & Pick<ReferenceEditProps, 'patchChosen'>;

const TABS: { id: InspectorTab; label: string }[] = [
  { id: 'reference', label: 'Reference' },
  { id: 'scripts', label: 'Scripts' },
  { id: 'plugins', label: 'Plugins' },
  { id: 'edit', label: 'Edit' },
];

const isTextEntry = (target: EventTarget | null): boolean => {
  const tag = (target as HTMLElement | null)?.tagName;
  return tag === 'INPUT' || tag === 'TEXTAREA' || tag === 'SELECT';
};

export function SelectedSummary({ reference, open, onShow }: { reference: CellPlacedReference; open: boolean; onShow: () => void }) {
  const text = `Selected: ${reference.editorId || reference.formKey}${reference.editorId ? ` ${reference.formKey}` : ''}`;
  return (
    <div className="ref-selected-summary">
      <span className="ref-selected-text" title={text}>{text}</span>
      <button className="papyrus-copy ref-selected-btn" onClick={onShow}>{open ? 'Details' : 'Show details'}</button>
    </div>
  );
}

export interface ReferenceInspectorProps {
  reference: CellPlacedReference;
  refRevision: number;
  tab: InspectorTab;
  onTab: (tab: InspectorTab) => void;
  onClose: () => void;
  onDeselect: () => void;
  onHide: () => void;
  width: number;
  viewportWidth: number;
  onResize: (width: number) => void;
  onCommitWidth: (width: number) => void;
  edit: EditHandlers;
  focusNonce?: number;
}

export default function ReferenceInspector({
  reference, refRevision, tab, onTab, onClose, onDeselect, onHide, width, viewportWidth, onResize, onCommitWidth, edit, focusNonce = 0,
}: ReferenceInspectorProps) {
  const rootRef = useRef<HTMLElement>(null);
  const bodyRef = useRef<HTMLDivElement>(null);
  const data = useReferenceData(reference.formKey, refRevision);

  useEffect(() => {
    if (focusNonce === 0) return;
    rootRef.current?.focus();
    if (bodyRef.current) bodyRef.current.scrollTop = 0;
  }, [focusNonce]);

  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      if (e.key !== 'Escape' || e.defaultPrevented) return;
      const root = rootRef.current;
      const active = document.activeElement;
      const inside = !!root && active !== root && root.contains(active);
      if (!inside && isTextEntry(e.target)) return;
      e.preventDefault();
      onClose();
      if (inside) onDeselect();
    };
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
  }, [onClose, onDeselect]);

  const baseName = reference.baseEditorId || reference.baseFormKey;
  const meta = [reference.editorId ? reference.formKey : '', baseName ? `Base ${baseName}` : '']
    .filter(part => part !== '')
    .join(' · ');

  return (
    <aside ref={rootRef} className="ref-inspector" aria-label="Reference inspector" tabIndex={-1}>
      <CellSidebarResizer
        width={width}
        viewportWidth={viewportWidth}
        onResize={onResize}
        onCommit={onCommitWidth}
        panel={{
          label: 'Resize reference inspector',
          min: INSPECTOR_MIN,
          max: maxInspectorWidth(viewportWidth),
          defaultWidth: INSPECTOR_DEFAULT,
          clamp: next => clampInspectorWidth(next, viewportWidth),
          fromRight: true,
        }}
      />
      <div className="ref-inspector-head">
        <div className="ref-inspector-title">
          <strong title={reference.editorId || reference.formKey}>{reference.editorId || reference.formKey}</strong>
          {meta && <span className="ref-inspector-meta">{meta}</span>}
        </div>
        <div className="ref-inspector-actions">
          <button className="cell-hide-btn" onClick={onHide}>
            <EyeOff size={12} /> Hide this mesh <kbd>1</kbd>
          </button>
          <button className="ref-inspector-close" onClick={onClose} title="Close (Esc)" aria-label="Close reference inspector">
            <X size={16} />
          </button>
        </div>
      </div>
      <div className="ref-inspector-tabs" role="tablist">
        {TABS.map(item => (
          <button
            key={item.id}
            role="tab"
            aria-selected={tab === item.id}
            className={`cell-sidebar-tab${tab === item.id ? ' active' : ''}`}
            onClick={() => onTab(item.id)}
          >
            {item.label}
          </button>
        ))}
      </div>
      <div ref={bodyRef} className="ref-inspector-body" role="tabpanel">
        {tab === 'reference' && <ReferenceTab data={data} edit={edit} />}
        {tab === 'scripts' && <ScriptsTab data={data} />}
        {tab === 'plugins' && <PluginsTab data={data} />}
        {tab === 'edit' && <CellEditPanel reference={reference} {...edit} />}
      </div>
    </aside>
  );
}

export function ReferenceInspectorDock({ selected, inspector, refRevision, viewportWidth, onHide, onDeselect, edit }: {
  selected: CellPlacedReference | null;
  inspector: ReferenceInspectorState;
  refRevision: number;
  viewportWidth: number;
  onHide: () => void;
  onDeselect: () => void;
  edit: EditHandlers;
}) {
  if (!selected || !inspector.open) return null;
  return (
    <ReferenceInspector
      reference={selected}
      refRevision={refRevision}
      tab={inspector.tab}
      onTab={inspector.setTab}
      onClose={inspector.close}
      onDeselect={onDeselect}
      onHide={onHide}
      width={inspector.width}
      viewportWidth={viewportWidth}
      onResize={inspector.setWidth}
      onCommitWidth={inspector.commitWidth}
      edit={edit}
      focusNonce={inspector.focusNonce}
    />
  );
}

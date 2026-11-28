import { useState, type ReactNode } from 'react';
import { ChevronDown } from 'lucide-react';
import { loadSectionOpen, saveSectionOpen } from './util/cellSidebar';

export default function CellCollapsible({ storageKey, title, actions, children }: {
  storageKey: string;
  title: string;
  actions?: ReactNode;
  children: ReactNode;
}) {
  const [open, setOpen] = useState(() => loadSectionOpen(storageKey));
  const toggle = () => {
    const next = !open;
    setOpen(next);
    saveSectionOpen(storageKey, next);
  };
  return (
    <div className={`cell-section${open ? '' : ' collapsed'}`}>
      <div className="cell-layers-head cell-section-head">
        <button type="button" className="cell-section-toggle" aria-expanded={open} onClick={toggle}>
          <ChevronDown size={12} className="cell-section-chevron" aria-hidden="true" />
          <span>{title}</span>
        </button>
        {actions}
      </div>
      {open && children}
    </div>
  );
}

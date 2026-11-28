import { ChevronLeft, ChevronRight, Star } from 'lucide-react';
import type { BaseObjectItem } from './baseObjects';
import { typeIconKind, typeLabel } from './categoryTree';
import OrbitPreview from './OrbitPreview';
import TypeIcon from './TypeIcon';

export interface DetailPaneProps {
  item: BaseObjectItem | null;
  favourite: boolean;
  collapsed: boolean;
  onToggleCollapsed: () => void;
  onPlace: (item: BaseObjectItem) => void;
  onCopyKeys: (item: BaseObjectItem) => void;
  onToggleFavourite: (item: BaseObjectItem) => void;
}

export default function DetailPane(props: DetailPaneProps) {
  const { item, favourite, collapsed } = props;
  const toggle = (
    <button
      type="button"
      className="ab-detail-toggle"
      aria-label={collapsed ? 'Expand details' : 'Collapse details'}
      aria-expanded={!collapsed}
      onClick={props.onToggleCollapsed}
    >
      {collapsed ? <ChevronLeft size={14} /> : <ChevronRight size={14} />}
    </button>
  );

  if (collapsed) {
    return <aside className="ab-detail collapsed" aria-label="Object details">{toggle}</aside>;
  }

  if (!item) {
    return (
      <aside className="ab-detail" aria-label="Object details">
        <div className="ab-detail-head">{toggle}<span>Details</span></div>
        <p className="ab-detail-hint">Select an object to see its preview, keys and model path.</p>
      </aside>
    );
  }

  return (
    <aside className="ab-detail" aria-label="Object details">
      <div className="ab-detail-head">
        {toggle}
        <span className="ab-detail-title">{item.editorId || item.name}</span>
        <button
          type="button"
          className={`ab-detail-star${favourite ? ' on' : ''}`}
          aria-pressed={favourite}
          aria-label={favourite ? 'Remove from favourites' : 'Add to favourites'}
          onClick={() => props.onToggleFavourite(item)}
        >
          <Star size={14} fill={favourite ? 'currentColor' : 'none'} />
        </button>
      </div>
      {item.modelPath !== '' ? <OrbitPreview modelPath={item.modelPath} /> : (
        <div className="ab-detail-nomodel">
          <TypeIcon kind={typeIconKind(item.signature)} size={48} />
          <span>No model</span>
        </div>
      )}
      <dl className="ab-detail-facts">
        <dt>Name</dt><dd>{item.name || '--'}</dd>
        <dt>Type</dt><dd>{typeLabel(item.signature)}</dd>
        <dt>Form ID</dt><dd>{item.hexId}</dd>
        <dt>FormKey</dt><dd className="ab-detail-key">{item.formKey}</dd>
        <dt>Plugin</dt><dd>{item.winningPlugin || '--'}</dd>
        <dt>Model</dt><dd className="ab-detail-key">{item.modelPath || 'None'}</dd>
      </dl>
      <div className="ab-detail-actions">
        <button type="button" className="ab-detail-primary" onClick={() => props.onPlace(item)}>Place at view centre</button>
        <button type="button" onClick={() => props.onCopyKeys(item)}>Copy keys</button>
      </div>
    </aside>
  );
}

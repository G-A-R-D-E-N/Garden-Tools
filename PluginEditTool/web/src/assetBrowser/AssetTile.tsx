import { useRef, useState, type CSSProperties, type DragEvent, type MouseEvent } from 'react';
import type { BaseObjectItem } from './baseObjects';
import { typeIconKind, typeLabel } from './categoryTree';
import TypeIcon from './TypeIcon';
import { useInView, usePreviewUrl } from './usePreviewQueue';

export interface AssetTileProps {
  item: BaseObjectItem;
  style: CSSProperties;
  selected: boolean;
  active: boolean;
  onClick: (item: BaseObjectItem, e: MouseEvent<HTMLDivElement>) => void;
  onContextMenu: (item: BaseObjectItem, e: MouseEvent<HTMLDivElement>) => void;
  onDragStart: (item: BaseObjectItem, e: DragEvent<HTMLDivElement>) => void;
  onDragEnd: (e: DragEvent<HTMLDivElement>) => void;
}

export default function AssetTile(props: AssetTileProps) {
  const { item, style, selected, active } = props;
  const ref = useRef<HTMLDivElement>(null);
  const visible = useInView(ref);
  const url = usePreviewUrl(item.modelPath, visible);
  const [hovered, setHovered] = useState(false);
  const hasModel = item.modelPath !== '';
  const className = [
    'ab-tile',
    selected ? 'selected' : '',
    active ? 'active' : '',
    item.deleted ? 'deleted' : '',
  ].filter(Boolean).join(' ');

  return (
    <div
      ref={ref}
      role="option"
      aria-selected={selected}
      aria-label={item.editorId}
      data-key={item.formKey}
      className={className}
      style={style}
      draggable
      title={hasModel ? item.editorId : 'No model'}
      onClick={e => props.onClick(item, e)}
      onContextMenu={e => props.onContextMenu(item, e)}
      onDragStart={e => props.onDragStart(item, e)}
      onDragEnd={props.onDragEnd}
      onPointerEnter={() => setHovered(true)}
      onPointerLeave={() => setHovered(false)}
    >
      <div className="ab-tile-thumb">
        {url ? <img src={url} alt="" draggable={false} /> : <TypeIcon kind={typeIconKind(item.signature)} size={40} />}
        <span className="ab-tile-badge">{typeLabel(item.signature)}</span>
        {!hasModel && <span className="ab-tile-nomodel">No model</span>}
      </div>
      <div className="ab-tile-label">
        <span className="ab-tile-name">{item.editorId || item.name}</span>
        <span className="ab-tile-sub">{item.winningPlugin || item.hexId}</span>
      </div>
      {hovered && url && (
        <div className="ab-tile-card" aria-hidden="true">
          <img src={url} alt="" draggable={false} />
          <span className="ab-tile-card-path">{item.modelPath}</span>
        </div>
      )}
    </div>
  );
}

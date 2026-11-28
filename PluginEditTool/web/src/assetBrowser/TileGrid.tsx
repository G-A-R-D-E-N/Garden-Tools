import { useEffect, useRef, useState, type DragEvent, type KeyboardEvent, type MouseEvent } from 'react';
import AssetTile from './AssetTile';
import ListEmptyState from './ListEmptyState';
import type { BaseObjectItem } from './baseObjects';
import { columnsFor, itemRange, rowsFor, rowWindow, TILE_GAP } from './virtualWindow';
import './ContentBrowser.css';

const LABEL_HEIGHT = 44;
const OVERSCAN_ROWS = 2;
const FALLBACK_VIEWPORT = 600;

export interface TileGridProps {
  items: BaseObjectItem[];
  tileSize: number;
  loading: boolean;
  failed?: boolean;
  selectedKeys: ReadonlySet<string>;
  activeKey: string | null;
  onRowClick: (item: BaseObjectItem, e: MouseEvent<HTMLDivElement>) => void;
  onRowContext: (item: BaseObjectItem, e: MouseEvent<HTMLDivElement>) => void;
  onRowDragStart: (item: BaseObjectItem, e: DragEvent<HTMLDivElement>) => void;
  onRowDragEnd: (e: DragEvent<HTMLDivElement>) => void;
  onNavigate: (step: number, extend: boolean) => void;
  onKeyDown: (e: KeyboardEvent<HTMLDivElement>) => void;
}

interface Box {
  width: number;
  height: number;
  scrollTop: number;
}

export default function TileGrid(props: TileGridProps) {
  const { items, tileSize, loading, failed = false, selectedKeys, activeKey } = props;
  const wrapRef = useRef<HTMLDivElement>(null);
  const [box, setBox] = useState<Box>({ width: 0, height: 0, scrollTop: 0 });

  useEffect(() => {
    const wrap = wrapRef.current;
    if (!wrap || typeof ResizeObserver === 'undefined') return;
    const observer = new ResizeObserver(() => {
      setBox(prev => ({ ...prev, width: wrap.clientWidth, height: wrap.clientHeight }));
    });
    observer.observe(wrap);
    return () => observer.disconnect();
  }, []);

  const columns = columnsFor(box.width, tileSize);
  const rows = rowsFor(items.length, columns);
  const tileHeight = tileSize + LABEL_HEIGHT;
  const rowHeight = tileHeight + TILE_GAP;

  useEffect(() => {
    const wrap = wrapRef.current;
    if (!wrap || activeKey === null) return;
    const index = items.findIndex(item => item.formKey === activeKey);
    if (index < 0) return;
    const cols = columnsFor(wrap.clientWidth, tileSize);
    const top = Math.floor(index / cols) * rowHeight;
    if (top < wrap.scrollTop) wrap.scrollTop = top;
    else if (top + rowHeight > wrap.scrollTop + wrap.clientHeight) wrap.scrollTop = top + rowHeight - wrap.clientHeight;
  }, [activeKey, items, tileSize, rowHeight]);

  const win = rowWindow({
    rowCount: rows,
    rowHeight,
    scrollTop: box.scrollTop,
    viewportHeight: box.height || FALLBACK_VIEWPORT,
    overscan: OVERSCAN_ROWS,
  });
  const range = itemRange(win, columns, items.length);
  const shown = items.slice(range.start, range.end);

  const onScroll = () => {
    const wrap = wrapRef.current;
    if (!wrap) return;
    setBox(prev => ({ ...prev, scrollTop: wrap.scrollTop, height: wrap.clientHeight || prev.height }));
  };

  const onKey = (e: KeyboardEvent<HTMLDivElement>) => {
    const step = navigationStep(e.key, columns);
    if (step === null) {
      props.onKeyDown(e);
      return;
    }
    e.preventDefault();
    props.onNavigate(step, e.shiftKey);
  };

  return (
    <div
      ref={wrapRef}
      role="listbox"
      aria-multiselectable="true"
      aria-busy={loading}
      aria-label="Base objects"
      aria-rowcount={rows}
      tabIndex={0}
      className="ab-tiles"
      onScroll={onScroll}
      onKeyDown={onKey}
    >
      <div className="ab-tiles-spacer" style={{ height: rows * rowHeight }}>
        {shown.map((item, offset) => {
          const index = range.start + offset;
          const style = {
            left: (index % columns) * (tileSize + TILE_GAP),
            top: Math.floor(index / columns) * rowHeight,
            width: tileSize,
            height: tileHeight,
          };
          return (
            <AssetTile
              key={item.formKey}
              item={item}
              style={style}
              selected={selectedKeys.has(item.formKey)}
              active={item.formKey === activeKey}
              onClick={props.onRowClick}
              onContextMenu={props.onRowContext}
              onDragStart={props.onRowDragStart}
              onDragEnd={props.onRowDragEnd}
            />
          );
        })}
      </div>
      <ListEmptyState items={items.length} loading={loading} failed={failed} />
    </div>
  );
}

function navigationStep(key: string, columns: number): number | null {
  switch (key) {
    case 'ArrowLeft': return -1;
    case 'ArrowRight': return 1;
    case 'ArrowUp': return -columns;
    case 'ArrowDown': return columns;
    default: return null;
  }
}

import { useEffect, useRef, useState, type CSSProperties, type DragEvent as ReactDragEvent,
  type KeyboardEvent as ReactKeyboardEvent, type MouseEvent as ReactMouseEvent, type PointerEvent as ReactPointerEvent } from 'react';
import type { BaseObjectItem, BaseObjectSort, SortDir } from './baseObjects';
import { BROWSER_COLUMNS, clampWidth, type BrowserColumn, type ColumnPrefs } from './browserPrefs';

export const ROW_HEIGHT = 24;
export const HEADER_HEIGHT = 26;
const OVERSCAN = 8;
const FALLBACK_HEIGHT = 480;
const NEAR_END_ROWS = 20;

export const COLUMN_LABELS: Record<BrowserColumn, string> = {
  editorId: 'Editor ID',
  hexId: 'Form ID',
  name: 'Name',
  type: 'Type',
  modelPath: 'Model path',
  plugin: 'Plugin',
};

const SORT_LABELS: Record<BrowserColumn, string> = {
  editorId: 'Sort by editor ID',
  hexId: 'Sort by form ID',
  name: 'Sort by name',
  type: 'Sort by type',
  modelPath: 'Sort by model path',
  plugin: 'Sort by plugin',
};

export interface AssetBrowserTableProps {
  items: BaseObjectItem[];
  total: number;
  loading: boolean;
  columns: ColumnPrefs;
  sortBy: BaseObjectSort;
  sortDir: SortDir;
  selectedKeys: ReadonlySet<string>;
  activeKey: string | null;
  onSort: (column: BaseObjectSort) => void;
  onResize: (column: BrowserColumn, width: number) => void;
  onRowClick: (item: BaseObjectItem, e: ReactMouseEvent<HTMLDivElement>) => void;
  onRowContext: (item: BaseObjectItem, e: ReactMouseEvent<HTMLDivElement>) => void;
  onRowDragStart: (item: BaseObjectItem, e: ReactDragEvent<HTMLDivElement>) => void;
  onRowDragEnd: () => void;
  onNearEnd: () => void;
  onKeyDown: (e: ReactKeyboardEvent<HTMLDivElement>) => void;
}

export default function AssetBrowserTable(props: AssetBrowserTableProps) {
  const { items, total, loading, columns, sortBy, sortDir, selectedKeys, activeKey } = props;
  const wrapRef = useRef<HTMLDivElement>(null);
  const [scroll, setScroll] = useState({ top: 0, height: FALLBACK_HEIGHT });
  const visible = BROWSER_COLUMNS.filter(column => columns[column].visible);
  const template = visible.map(column => `${columns[column].width}px`).join(' ');
  const rowStyle = (extra: CSSProperties): CSSProperties => ({ gridTemplateColumns: template, ...extra });

  useEffect(() => {
    const wrap = wrapRef.current;
    if (!wrap || activeKey === null) return;
    const index = items.findIndex(item => item.formKey === activeKey);
    if (index < 0) return;
    const top = HEADER_HEIGHT + index * ROW_HEIGHT;
    if (top < wrap.scrollTop + HEADER_HEIGHT) wrap.scrollTop = top - HEADER_HEIGHT;
    else if (top + ROW_HEIGHT > wrap.scrollTop + wrap.clientHeight) wrap.scrollTop = top + ROW_HEIGHT - wrap.clientHeight;
  }, [activeKey, items]);

  const onScroll = () => {
    const wrap = wrapRef.current;
    if (!wrap) return;
    setScroll({ top: wrap.scrollTop, height: wrap.clientHeight || FALLBACK_HEIGHT });
    const loadedBottom = wrap.scrollTop + wrap.clientHeight;
    if (loadedBottom >= HEADER_HEIGHT + items.length * ROW_HEIGHT - NEAR_END_ROWS * ROW_HEIGHT) props.onNearEnd();
  };

  const first = Math.max(0, Math.floor(Math.max(0, scroll.top - HEADER_HEIGHT) / ROW_HEIGHT) - OVERSCAN);
  const last = Math.min(items.length, Math.ceil((scroll.top + scroll.height) / ROW_HEIGHT) + OVERSCAN);
  const rendered = items.slice(first, Math.max(first, last));

  const startResize = (column: BrowserColumn, e: ReactPointerEvent<HTMLSpanElement>) => {
    e.preventDefault();
    const startX = e.clientX;
    const startWidth = columns[column].width;
    const move = (ev: PointerEvent) => props.onResize(column, clampWidth(startWidth + ev.clientX - startX));
    const up = () => {
      window.removeEventListener('pointermove', move);
      window.removeEventListener('pointerup', up);
    };
    window.addEventListener('pointermove', move);
    window.addEventListener('pointerup', up);
  };

  return (
    <div
      ref={wrapRef}
      role="grid"
      aria-rowcount={total}
      aria-busy={loading}
      aria-label="Base objects"
      tabIndex={0}
      className="ab-table"
      onScroll={onScroll}
      onKeyDown={props.onKeyDown}
    >
      <div role="row" className="ab-header" style={rowStyle({ height: HEADER_HEIGHT })}>
        {visible.map(column => (
          <div role="columnheader" key={column} className="ab-cell ab-head-cell">
            <button
              type="button"
              tabIndex={-1}
              className="ab-sort"
              aria-label={SORT_LABELS[column]}
              aria-sort={sortBy === column ? (sortDir === 'asc' ? 'ascending' : 'descending') : 'none'}
              onClick={() => props.onSort(column)}
            >
              {COLUMN_LABELS[column]}
              {sortBy === column ? (sortDir === 'asc' ? ' \u25B2' : ' \u25BC') : ''}
            </button>
            <span
              role="separator"
              aria-orientation="vertical"
              aria-label={`Resize ${COLUMN_LABELS[column]} column`}
              className="ab-resize"
              onPointerDown={e => startResize(column, e)}
            />
          </div>
        ))}
      </div>
      <div className="ab-spacer" style={{ height: items.length * ROW_HEIGHT }}>
        {rendered.map((item, offset) => {
          const index = first + offset;
          const selected = selectedKeys.has(item.formKey);
          return (
            <div
              key={item.formKey}
              role="row"
              aria-selected={selected}
              aria-rowindex={index + 1}
              data-key={item.formKey}
              className={`ab-row${selected ? ' selected' : ''}${item.deleted ? ' deleted' : ''}`}
              style={rowStyle({ top: index * ROW_HEIGHT, height: ROW_HEIGHT })}
              draggable
              onClick={e => props.onRowClick(item, e)}
              onContextMenu={e => props.onRowContext(item, e)}
              onDragStart={e => props.onRowDragStart(item, e)}
              onDragEnd={props.onRowDragEnd}
            >
              {visible.map(column => (
                <div role="gridcell" key={column} className="ab-cell">
                  {cellText(item, column)}
                </div>
              ))}
            </div>
          );
        })}
      </div>
      {items.length === 0 && !loading ? <div className="ab-empty">No objects match the filter.</div> : null}
    </div>
  );
}

function cellText(item: BaseObjectItem, column: BrowserColumn): string {
  switch (column) {
    case 'editorId': return item.editorId;
    case 'hexId': return item.hexId;
    case 'name': return item.name;
    case 'type': return item.typeName || item.signature;
    case 'modelPath': return item.modelPath;
    case 'plugin': return item.winningPlugin;
  }
}

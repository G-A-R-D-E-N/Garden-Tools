import { useRef, useState, type KeyboardEvent as ReactKeyboardEvent, type PointerEvent as ReactPointerEvent } from 'react';
import { X } from 'lucide-react';
import AssetBrowser, { type AssetBrowserProps } from './AssetBrowser';
import { DRAWER_KEY_STEP, DRAWER_MIN_HEIGHT } from './drawerPrefs';
import type { DrawerController } from './useDrawerState';
import './ContentDrawer.css';

export interface ContentDrawerProps extends Omit<AssetBrowserProps, 'drawer'> {
  drawer: DrawerController;
}

export default function ContentDrawer(props: ContentDrawerProps) {
  const { drawer, ...browser } = props;
  const { open, prefs } = drawer;
  const rootRef = useRef<HTMLElement>(null);
  const [resizing, setResizing] = useState(false);

  const panelHeight = (): number => rootRef.current?.parentElement?.clientHeight ?? 0;

  const startResize = (e: ReactPointerEvent<HTMLDivElement>) => {
    e.preventDefault();
    const startY = e.clientY;
    const startHeight = prefs.height;
    const panel = panelHeight();
    setResizing(true);
    const move = (ev: PointerEvent) => drawer.setHeight(startHeight + startY - ev.clientY, panel);
    const up = () => {
      setResizing(false);
      window.removeEventListener('pointermove', move);
      window.removeEventListener('pointerup', up);
    };
    window.addEventListener('pointermove', move);
    window.addEventListener('pointerup', up);
  };

  const onHandleKey = (e: ReactKeyboardEvent<HTMLDivElement>) => {
    if (e.key !== 'ArrowUp' && e.key !== 'ArrowDown') return;
    e.preventDefault();
    drawer.setHeight(prefs.height + (e.key === 'ArrowUp' ? DRAWER_KEY_STEP : -DRAWER_KEY_STEP), panelHeight());
  };

  return (
    <section
      ref={rootRef}
      className={`content-drawer${open ? ' open' : ''}${resizing ? ' resizing' : ''}`}
      aria-label="Content browser"
      aria-hidden={!open}
      inert={!open}
      style={{ height: open ? prefs.height : 0 }}
    >
      <div
        className="content-drawer-handle"
        role="separator"
        aria-orientation="horizontal"
        aria-label="Resize content browser"
        aria-valuemin={DRAWER_MIN_HEIGHT}
        aria-valuenow={prefs.height}
        tabIndex={open ? 0 : -1}
        onPointerDown={startResize}
        onKeyDown={onHandleKey}
      />
      <header className="content-drawer-bar">
        <span className="content-drawer-title">Content browser</span>
        <span className="content-drawer-hint">Ctrl+Space or `</span>
        <button
          type="button"
          className={`content-drawer-pin${prefs.pinned ? ' on' : ''}`}
          aria-pressed={prefs.pinned}
          onClick={() => drawer.setPinned(!prefs.pinned)}
        >
          {prefs.pinned ? 'Pinned' : 'Pin open'}
        </button>
        <button type="button" className="content-drawer-close" aria-label="Close content browser" onClick={drawer.close}>
          <X size={13} />
        </button>
      </header>
      <div className="content-drawer-body">
        <AssetBrowser drawer={drawer} {...browser} />
      </div>
    </section>
  );
}

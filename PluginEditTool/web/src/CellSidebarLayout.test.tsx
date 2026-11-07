import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { act, cleanup, fireEvent, render, screen } from '@testing-library/react';
import CellSidebarResizer, { useViewportWidth } from './CellSidebarResizer';
import CellCollapsible from './CellCollapsible';
import { clampSidebarWidth, loadSidebarWidth, saveSidebarWidth, SIDEBAR_DEFAULT } from './util/cellSidebar';

const WIN = 1600;

beforeEach(() => { localStorage.clear(); });
afterEach(() => { cleanup(); localStorage.clear(); });

describe('cell sidebar width', () => {
  it('clamps to [300, 60% of the window]', () => {
    expect(clampSidebarWidth(100, WIN)).toBe(300);
    expect(clampSidebarWidth(5000, WIN)).toBe(960);
    expect(clampSidebarWidth(420.4, WIN)).toBe(420);
    expect(clampSidebarWidth(Number.NaN, WIN)).toBe(SIDEBAR_DEFAULT);
  });

  it('defaults to 400px and persists the width across loads', () => {
    expect(loadSidebarWidth(WIN)).toBe(400);
    saveSidebarWidth(520);
    expect(localStorage.getItem('cell.sidebarWidth')).toBe('520');
    expect(loadSidebarWidth(WIN)).toBe(520);
  });

  it('re-clamps a stored width that no longer fits the window', () => {
    localStorage.setItem('cell.sidebarWidth', '5000');
    expect(loadSidebarWidth(WIN)).toBe(960);
  });
});

describe('CellSidebarResizer', () => {
  it('drag from the handle reports the new width until mouse-up', () => {
    const onResize = vi.fn();
    render(<CellSidebarResizer width={400} onResize={onResize} />);
    const handle = screen.getByRole('separator', { name: 'Resize Cell Viewer sidebar' });
    fireEvent.mouseDown(handle, { button: 0, clientX: 400 });
    fireEvent.mouseMove(window, { clientX: 460 });
    expect(onResize).toHaveBeenLastCalledWith(460);
    fireEvent.mouseMove(window, { clientX: 0 });
    expect(onResize).toHaveBeenLastCalledWith(300);
    fireEvent.mouseUp(window);
    onResize.mockClear();
    fireEvent.mouseMove(window, { clientX: 500 });
    expect(onResize).not.toHaveBeenCalled();
  });

  it('arrow keys nudge the width and double-click resets to the default', () => {
    const onResize = vi.fn();
    render(<CellSidebarResizer width={400} onResize={onResize} />);
    const handle = screen.getByRole('separator');
    fireEvent.keyDown(handle, { key: 'ArrowRight' });
    expect(onResize).toHaveBeenLastCalledWith(416);
    fireEvent.keyDown(handle, { key: 'ArrowLeft' });
    expect(onResize).toHaveBeenLastCalledWith(384);
    fireEvent.doubleClick(handle);
    expect(onResize).toHaveBeenLastCalledWith(SIDEBAR_DEFAULT);
  });
});

describe('CellCollapsible', () => {
  it('toggles the section and remembers the collapsed state', () => {
    const { unmount } = render(
      <CellCollapsible storageKey="cell.section.test" title="Cell properties">
        <p>section body</p>
      </CellCollapsible>,
    );
    const toggle = screen.getByRole('button', { name: 'Cell properties' });
    expect(toggle.getAttribute('aria-expanded')).toBe('true');
    expect(screen.getByText('section body')).toBeTruthy();

    fireEvent.click(toggle);
    expect(toggle.getAttribute('aria-expanded')).toBe('false');
    expect(screen.queryByText('section body')).toBeNull();
    expect(localStorage.getItem('cell.section.test')).toBe('0');
    unmount();

    render(
      <CellCollapsible storageKey="cell.section.test" title="Cell properties">
        <p>section body</p>
      </CellCollapsible>,
    );
    expect(screen.getByRole('button', { name: 'Cell properties' }).getAttribute('aria-expanded')).toBe('false');
    expect(screen.queryByText('section body')).toBeNull();
  });

  it('keeps action buttons beside the toggle without toggling the section', () => {
    const onAction = vi.fn();
    render(
      <CellCollapsible storageKey="cell.section.test2" title="Layers (2)" actions={<button onClick={onAction}>Show all</button>}>
        <p>rows</p>
      </CellCollapsible>,
    );
    fireEvent.click(screen.getByRole('button', { name: 'Show all' }));
    expect(onAction).toHaveBeenCalledTimes(1);
    expect(screen.getByText('rows')).toBeTruthy();
  });
});

function setInnerWidth(value: number) {
  Object.defineProperty(window, 'innerWidth', { configurable: true, writable: true, value });
}

describe('sidebar width follows the window', () => {
  afterEach(() => { setInnerWidth(1024); });

  it('re-clamps the shown width and the aria maximum when the window shrinks, keeping the stored preference', () => {
    setInnerWidth(1600);
    saveSidebarWidth(900);
    function Harness() {
      const viewport = useViewportWidth();
      return <CellSidebarResizer width={clampSidebarWidth(loadSidebarWidth(), viewport)} viewportWidth={viewport} onResize={() => {}} />;
    }
    render(<Harness />);
    const handle = screen.getByRole('separator');
    expect(handle.getAttribute('aria-valuenow')).toBe('900');
    expect(handle.getAttribute('aria-valuemax')).toBe('960');

    setInnerWidth(700);
    act(() => { window.dispatchEvent(new Event('resize')); });
    expect(handle.getAttribute('aria-valuenow')).toBe('420');
    expect(handle.getAttribute('aria-valuemax')).toBe('420');
    expect(localStorage.getItem('cell.sidebarWidth')).toBe('900');
  });

  it('a press without a move does not commit, and a drag commits once on mouse-up', () => {
    const onCommit = vi.fn();
    render(<CellSidebarResizer width={400} onResize={() => {}} onCommit={onCommit} />);
    const handle = screen.getByRole('separator');
    fireEvent.mouseDown(handle, { button: 0, clientX: 400 });
    fireEvent.mouseUp(window);
    expect(onCommit).not.toHaveBeenCalled();
    fireEvent.mouseDown(handle, { button: 0, clientX: 400 });
    fireEvent.mouseMove(window, { clientX: 430 });
    fireEvent.mouseUp(window);
    expect(onCommit).toHaveBeenCalledTimes(1);
    expect(onCommit).toHaveBeenCalledWith(430);
  });

  it('persists the width on mouse-up, not on every move', () => {
    const onResize = vi.fn();
    const onCommit = vi.fn();
    render(<CellSidebarResizer width={400} onResize={onResize} onCommit={onCommit} />);
    fireEvent.mouseDown(screen.getByRole('separator'), { button: 0, clientX: 400 });
    fireEvent.mouseMove(window, { clientX: 430 });
    fireEvent.mouseMove(window, { clientX: 450 });
    expect(onCommit).not.toHaveBeenCalled();
    fireEvent.mouseUp(window);
    expect(onCommit).toHaveBeenCalledTimes(1);
    expect(onCommit).toHaveBeenCalledWith(450);
  });
});

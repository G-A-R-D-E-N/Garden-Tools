import { cleanup, fireEvent, render, screen } from '@testing-library/react';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import CellPanel from './CellPanel';

vi.mock('./backend', () => ({
  getCell: () => ({
    GetPlugins: async () => '["Fallout4.esm"]',
    SearchCells: async () => '[]',
    SearchWorldspaces: async () => '[]',
    ListBaseObjects: async () => JSON.stringify({
      total: 0, offset: 0, limit: 200, sortBy: 'editorId', sortDir: 'asc', typeCounts: {}, items: [],
    }),
  }),
  getBackend: () => null,
}));
vi.mock('./useDialogs', () => ({ useDialogs: () => ({ pickPlugin: vi.fn() }) }));
vi.mock('./CellViewport', () => ({ default: () => null }));

beforeEach(() => { localStorage.clear(); });

afterEach(() => { cleanup(); localStorage.clear(); });

describe('Cell Viewer content drawer', () => {
  it('mounts the content drawer inside the viewport output, closed by default', () => {
    render(<CellPanel onClose={() => {}} />);
    const drawer = document.querySelector('.papyrus-output .content-drawer');
    expect(drawer).not.toBeNull();
    expect(drawer?.classList.contains('open')).toBe(false);
  });

  it('opens the content drawer from the Objects tab and closes it again', () => {
    render(<CellPanel onClose={() => {}} />);
    const tab = screen.getByRole('button', { name: 'Objects' });
    expect(tab.classList.contains('active')).toBe(false);

    fireEvent.click(tab);
    expect(document.querySelector('.content-drawer.open')).not.toBeNull();
    expect(tab.classList.contains('active')).toBe(true);

    fireEvent.click(tab);
    expect(document.querySelector('.content-drawer.open')).toBeNull();
    expect(tab.classList.contains('active')).toBe(false);
  });

  it('Ctrl+F opens the closed drawer and focuses its object search', () => {
    render(<CellPanel onClose={() => {}} />);
    expect(document.querySelector('.content-drawer.open')).toBeNull();
    fireEvent.keyDown(document.body, { key: 'f', ctrlKey: true });
    expect(document.querySelector('.content-drawer.open')).not.toBeNull();
    expect(document.activeElement).toBe(screen.getByLabelText('Search objects'));
  });

  it('no longer renders an Objects sidebar pane', () => {
    render(<CellPanel onClose={() => {}} />);
    fireEvent.click(screen.getByRole('button', { name: 'Objects' }));
    expect(document.querySelector('.cell-objects-pane')).toBeNull();
    expect(document.querySelectorAll('.cell-sidebar-pane').length).toBe(1);
  });
});

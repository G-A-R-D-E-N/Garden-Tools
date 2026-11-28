import { cleanup, fireEvent, render, screen, within } from '@testing-library/react';
import { afterEach, beforeEach, expect, it, vi } from 'vitest';
import type { BaseObjectItem, BaseObjectQuery } from './baseObjects';
import ContentDrawer from './ContentDrawer';
import { useDrawerState } from './useDrawerState';

vi.mock('./previewRenderer', async importOriginal => {
  const actual = await importOriginal<typeof import('./previewRenderer')>();
  return { ...actual, createPreviewRenderer: () => ({ render: async () => 'data:image/png;base64,AAAA' }) };
});

const PANEL_HEIGHT = 1000;

function item(n: number): BaseObjectItem {
  return {
    formKey: `F${n}`,
    hexId: `0000000${n}`,
    editorId: `Obj00${n}`,
    name: `Object ${n}`,
    signature: 'STAT',
    typeName: 'Static',
    modelPath: `Meshes/object${n}.nif`,
    winningPlugin: 'Fallout4.esm',
    deleted: false,
    hasModel: true,
  };
}

function makeHost() {
  return {
    ListBaseObjects: vi.fn(async (query: BaseObjectQuery) => JSON.stringify({
      total: 2,
      offset: query.offset ?? 0,
      limit: query.limit ?? 200,
      sortBy: 'editorId',
      sortDir: 'asc',
      typeCounts: { STAT: 2 },
      items: [item(1), item(2)],
    })),
  };
}

function Shell({ host }: { host: ReturnType<typeof makeHost> }) {
  const drawer = useDrawerState();
  return (
    <div className="panel" data-testid="panel">
      <span data-testid="state">{drawer.open ? 'open' : 'closed'}</span>
      <button type="button" onClick={drawer.toggle}>Objects</button>
      <ContentDrawer drawer={drawer} host={host} onPlace={() => {}} onPreview={() => {}} />
    </div>
  );
}

function drawerEl(container: HTMLElement): HTMLElement {
  return container.querySelector('.content-drawer') as HTMLElement;
}

function renderShell() {
  const host = makeHost();
  const view = render(<Shell host={host} />);
  return { ...view, host, state: () => screen.getByTestId('state').textContent };
}

beforeEach(() => {
  localStorage.clear();
  Object.defineProperty(HTMLElement.prototype, 'clientHeight', { configurable: true, get: () => PANEL_HEIGHT });
});

afterEach(() => {
  cleanup();
  delete (HTMLElement.prototype as { clientHeight?: number }).clientHeight;
});

it('starts closed and hidden from assistive technology', () => {
  const { container } = renderShell();
  const el = drawerEl(container);
  expect(el.classList.contains('open')).toBe(false);
  expect(el.getAttribute('aria-hidden')).toBe('true');
  expect(el.hasAttribute('inert')).toBe(true);
  expect(el.style.height).toBe('0px');
});

it('opens with Ctrl+Space and the backtick key, and closes with the same keys', () => {
  const { container, state } = renderShell();
  fireEvent.keyDown(document.body, { key: ' ', ctrlKey: true });
  expect(state()).toBe('open');
  expect(drawerEl(container).getAttribute('aria-hidden')).toBe('false');
  expect(drawerEl(container).style.height).toBe('300px');

  fireEvent.keyDown(document.body, { key: '`' });
  expect(state()).toBe('closed');

  fireEvent.keyDown(document.body, { key: '`' });
  expect(state()).toBe('open');
});

it('focuses the search field with Ctrl+F while the drawer is open', () => {
  const { container, state } = renderShell();
  fireEvent.click(screen.getByRole('button', { name: 'Objects' }));
  fireEvent.keyDown(document.body, { key: 'f', ctrlKey: true });
  expect(state()).toBe('open');
  expect(document.activeElement).toBe(screen.getByLabelText('Search objects'));
  expect(drawerEl(container).classList.contains('open')).toBe(true);
});

it('closes on Escape while the drawer is open', () => {
  const { state } = renderShell();
  fireEvent.click(screen.getByRole('button', { name: 'Objects' }));
  expect(state()).toBe('open');
  fireEvent.keyDown(document.body, { key: 'Escape' });
  expect(state()).toBe('closed');
});

it('keeps the drawer open when Escape is pressed in the search field', () => {
  const { state } = renderShell();
  fireEvent.click(screen.getByRole('button', { name: 'Objects' }));
  const search = screen.getByLabelText('Search objects') as HTMLInputElement;
  fireEvent.change(search, { target: { value: 'Obj001' } });
  fireEvent.keyDown(search, { key: 'Escape' });
  expect(search.value).toBe('');
  expect(state()).toBe('open');
});

it('closes from the header close button', () => {
  const { state } = renderShell();
  fireEvent.click(screen.getByRole('button', { name: 'Objects' }));
  fireEvent.click(screen.getByRole('button', { name: 'Close content browser' }));
  expect(state()).toBe('closed');
});

it('pins the drawer open so a successful drop does not collapse it', async () => {
  const { container, state } = renderShell();
  fireEvent.click(screen.getByRole('button', { name: 'Objects' }));
  const tile = await screen.findByRole('option', { name: 'Obj001' });

  fireEvent.dragEnd(tile, { dataTransfer: { dropEffect: 'copy' } });
  expect(state()).toBe('closed');

  fireEvent.click(screen.getByRole('button', { name: 'Objects' }));
  const pin = within(drawerEl(container)).getByRole('button', { name: 'Pin open' });
  fireEvent.click(pin);
  expect(pin.getAttribute('aria-pressed')).toBe('true');
  expect(pin.textContent).toBe('Pinned');

  fireEvent.dragEnd(tile, { dataTransfer: { dropEffect: 'copy' } });
  expect(state()).toBe('open');
});

it('keeps the drawer open after a cancelled drag', async () => {
  const { state } = renderShell();
  fireEvent.click(screen.getByRole('button', { name: 'Objects' }));
  const tile = await screen.findByRole('option', { name: 'Obj002' });
  fireEvent.dragEnd(tile, { dataTransfer: { dropEffect: 'none' } });
  expect(state()).toBe('open');
});

it('resizes from the keyboard handle within the panel limits', () => {
  const { container } = renderShell();
  fireEvent.click(screen.getByRole('button', { name: 'Objects' }));
  const handle = within(drawerEl(container)).getByRole('separator', { name: 'Resize content browser' });
  expect(handle.getAttribute('aria-valuenow')).toBe('300');

  fireEvent.keyDown(handle, { key: 'ArrowUp' });
  expect(drawerEl(container).style.height).toBe('316px');
  expect(handle.getAttribute('aria-valuenow')).toBe('316');

  fireEvent.keyDown(handle, { key: 'ArrowDown' });
  fireEvent.keyDown(handle, { key: 'ArrowDown' });
  expect(drawerEl(container).style.height).toBe('284px');

  fireEvent.keyDown(handle, { key: 'ArrowUp' });
  fireEvent.keyDown(handle, { key: 'ArrowUp' });
  for (let i = 0; i < 60; i++) fireEvent.keyDown(handle, { key: 'ArrowUp' });
  expect(drawerEl(container).style.height).toBe('700px');
});

it('resizes with a pointer drag and clamps to the minimum height', () => {
  const { container } = renderShell();
  fireEvent.click(screen.getByRole('button', { name: 'Objects' }));
  const handle = within(drawerEl(container)).getByRole('separator', { name: 'Resize content browser' });

  fireEvent.pointerDown(handle, { clientY: 500 });
  fireEvent.pointerMove(window, { clientY: 400 });
  expect(drawerEl(container).style.height).toBe('400px');

  fireEvent.pointerMove(window, { clientY: 2000 });
  expect(drawerEl(container).style.height).toBe('160px');
  fireEvent.pointerUp(window);

  fireEvent.pointerMove(window, { clientY: 0 });
  expect(drawerEl(container).style.height).toBe('160px');
});

it('reports the browser region by name', () => {
  const { container } = renderShell();
  expect(drawerEl(container).getAttribute('aria-label')).toBe('Content browser');
});

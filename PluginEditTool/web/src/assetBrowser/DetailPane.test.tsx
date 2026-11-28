import { cleanup, fireEvent, render, screen, within } from '@testing-library/react';
import { afterEach, expect, it, vi } from 'vitest';
import type { BaseObjectItem } from './baseObjects';
import DetailPane, { type DetailPaneProps } from './DetailPane';

vi.mock('./previewRenderer', async importOriginal => {
  const actual = await importOriginal<typeof import('./previewRenderer')>();
  return { ...actual, createPreviewRenderer: () => ({ render: async () => null }) };
});

afterEach(() => { cleanup(); });

function item(patch: Partial<BaseObjectItem> = {}): BaseObjectItem {
  return {
    formKey: 'Fallout4.esm:000ABC',
    hexId: '000ABC',
    editorId: 'Crate01',
    name: 'Supply crate',
    signature: 'STAT',
    typeName: 'Static',
    modelPath: 'Meshes/Clutter/Crate01.nif',
    winningPlugin: 'Fallout4.esm',
    deleted: false,
    hasModel: true,
    ...patch,
  };
}

function props(overrides: Partial<DetailPaneProps> = {}): DetailPaneProps {
  return {
    item: item(),
    favourite: false,
    collapsed: false,
    onToggleCollapsed: vi.fn(),
    onPlace: vi.fn(),
    onCopyKeys: vi.fn(),
    onToggleFavourite: vi.fn(),
    ...overrides,
  };
}

it('asks for a selection when nothing is active', () => {
  render(<DetailPane {...props({ item: null })} />);
  expect(screen.getByText('Select an object to see its preview, keys and model path.')).toBeTruthy();
});

it('shows the keys, the plugin and the model path of the active object', () => {
  render(<DetailPane {...props()} />);
  const pane = screen.getByRole('complementary', { name: 'Object details' });
  expect(within(pane).getByText('Crate01', { selector: '.ab-detail-title' })).toBeTruthy();
  expect(within(pane).getByText('000ABC')).toBeTruthy();
  expect(within(pane).getByText('Fallout4.esm:000ABC')).toBeTruthy();
  expect(within(pane).getByText('Meshes/Clutter/Crate01.nif')).toBeTruthy();
});

it('shows a placeholder instead of a preview for an object without a model', () => {
  const { container } = render(<DetailPane {...props({ item: item({ modelPath: '', signature: 'NPC_' }) })} />);
  expect(screen.getByText('No model')).toBeTruthy();
  expect(container.querySelector('.ab-orbit')).toBeNull();
});

it('places, copies and favourites the active object through its callbacks', () => {
  const p = props();
  render(<DetailPane {...p} />);
  fireEvent.click(screen.getByRole('button', { name: 'Place at view centre' }));
  expect(p.onPlace).toHaveBeenCalledWith(expect.objectContaining({ editorId: 'Crate01' }));

  fireEvent.click(screen.getByRole('button', { name: 'Copy keys' }));
  expect(p.onCopyKeys).toHaveBeenCalledWith(expect.objectContaining({ formKey: 'Fallout4.esm:000ABC' }));

  fireEvent.click(screen.getByRole('button', { name: 'Add to favourites' }));
  expect(p.onToggleFavourite).toHaveBeenCalledWith(expect.objectContaining({ hexId: '000ABC' }));
});

it('shows the favourite star as pressed when the object is a favourite', () => {
  render(<DetailPane {...props({ favourite: true })} />);
  const star = screen.getByRole('button', { name: 'Remove from favourites' });
  expect(star.getAttribute('aria-pressed')).toBe('true');
});

it('collapses to a narrow strip with only the expand control', () => {
  const p = props({ collapsed: true });
  render(<DetailPane {...p} />);
  expect(screen.queryByText('Crate01')).toBeNull();
  fireEvent.click(screen.getByRole('button', { name: 'Expand details' }));
  expect(p.onToggleCollapsed).toHaveBeenCalledTimes(1);
});

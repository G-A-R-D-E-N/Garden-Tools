import { cleanup, fireEvent, render, screen } from '@testing-library/react';
import { afterEach, expect, it, vi } from 'vitest';
import type { BaseObjectItem } from './baseObjects';
import { buildCategoryTree, type CategoryInput, type CategorySelection } from './categoryTree';
import CategoryTree from './CategoryTreeView';

afterEach(() => { cleanup(); });

function item(n: number, modelPath: string, winningPlugin = 'Fallout4.esm'): BaseObjectItem {
  return {
    formKey: `F${n}`,
    hexId: `0000000${n}`,
    editorId: `Obj00${n}`,
    name: '',
    signature: 'STAT',
    typeName: 'Static',
    modelPath,
    winningPlugin,
    deleted: false,
    hasModel: modelPath !== '',
  };
}

function input(patch: Partial<CategoryInput> = {}): CategoryInput {
  const scanItems = [
    item(1, 'Meshes/Architecture/Walls/a.nif'),
    item(2, 'Meshes/Architecture/Gates/b.nif'),
    item(3, 'Meshes/Clutter/c.nif', 'MyMod.esp'),
  ];
  return {
    total: 3,
    typeCounts: { STAT: 3, DOOR: 1 },
    scanItems,
    activeType: 'STAT',
    favourites: 0,
    recent: 0,
    ...patch,
  };
}

function renderTree(selection: CategorySelection | null, onSelect = vi.fn(), patch: Partial<CategoryInput> = {}) {
  const nodes = buildCategoryTree(input(patch));
  render(<CategoryTree nodes={nodes} selection={selection} onSelect={onSelect} />);
  return { onSelect };
}

it('lists the library, record types and plugin sections as headers', () => {
  renderTree({ kind: 'all' });
  expect(screen.getByRole('tree', { name: 'Categories' })).toBeTruthy();
  expect(screen.getByText('Library')).toBeTruthy();
  expect(screen.getByText('Record types')).toBeTruthy();
  expect(screen.getByText('Plugins')).toBeTruthy();
});

it('shows the library entries with their counts and the active one pressed', () => {
  renderTree({ kind: 'favourites' }, vi.fn(), { favourites: 2 });
  expect(screen.getByRole('button', { name: /^All/ }).getAttribute('aria-pressed')).toBe('false');
  const favourites = screen.getByRole('button', { name: /^Favourites/ });
  expect(favourites.getAttribute('aria-pressed')).toBe('true');
  expect(favourites.textContent).toContain('2');
});

it('selects a record type with its signature', () => {
  const { onSelect } = renderTree({ kind: 'all' });
  fireEvent.click(screen.getByRole('button', { name: /^Doors/ }));
  expect(onSelect).toHaveBeenCalledWith({ kind: 'type', signature: 'DOOR' });
});

it('selects a model folder under the active record type', () => {
  const { onSelect } = renderTree({ kind: 'type', signature: 'STAT' });
  fireEvent.click(screen.getByRole('button', { name: /^Architecture/ }));
  expect(onSelect).toHaveBeenCalledWith({ kind: 'folder', signature: 'STAT', folder: ['Architecture'] });
});

it('lists the nested folders expanded at shallow depth', () => {
  renderTree({ kind: 'type', signature: 'STAT' });
  expect(screen.getByRole('button', { name: /^Walls/ })).toBeTruthy();
  expect(screen.getByRole('button', { name: /^Gates/ })).toBeTruthy();
});

it('collapses and expands a section from its twist button', () => {
  renderTree({ kind: 'all' });
  expect(screen.getByRole('button', { name: /^All/ })).toBeTruthy();

  fireEvent.click(screen.getByRole('button', { name: 'Collapse Library' }));
  expect(screen.queryByRole('button', { name: /^All/ })).toBeNull();

  fireEvent.click(screen.getByRole('button', { name: 'Expand Library' }));
  expect(screen.getByRole('button', { name: /^All/ })).toBeTruthy();
});

it('selects a plugin from the plugins section', () => {
  const { onSelect } = renderTree({ kind: 'all' });
  fireEvent.click(screen.getByRole('button', { name: /^MyMod\.esp/ }));
  expect(onSelect).toHaveBeenCalledWith({ kind: 'plugin', signature: 'STAT', plugin: 'MyMod.esp' });
});

it('exposes the tree structure to assistive technology', () => {
  renderTree({ kind: 'all' });
  const items = screen.getAllByRole('treeitem');
  expect(items[0].getAttribute('aria-level')).toBe('1');
  const all = screen.getByRole('button', { name: /^All/ }).closest('[role="treeitem"]');
  expect(all?.getAttribute('aria-level')).toBe('2');
  expect(all?.getAttribute('aria-selected')).toBe('true');
});

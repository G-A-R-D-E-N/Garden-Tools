import { act, cleanup, renderHook } from '@testing-library/react';
import { afterEach, beforeEach, describe, expect, it } from 'vitest';
import { DEFAULT_DRAWER_PREFS, DRAWER_PREFS_KEY } from './drawerPrefs';
import { reduceDrawer, useDrawerState, type DrawerState } from './useDrawerState';

const closed: DrawerState = { open: false, prefs: DEFAULT_DRAWER_PREFS, searchToken: 0 };

beforeEach(() => { localStorage.clear(); });

afterEach(() => {
  cleanup();
  localStorage.clear();
});

describe('drawer reducer', () => {
  it('opens, closes and toggles', () => {
    const open = reduceDrawer(closed, { type: 'open' });
    expect(open.open).toBe(true);
    expect(reduceDrawer(open, { type: 'toggle' }).open).toBe(false);
    expect(reduceDrawer(open, { type: 'close' }).open).toBe(false);
    expect(reduceDrawer(closed, { type: 'close' })).toBe(closed);
  });

  it('opens and bumps the search token on Ctrl+F', () => {
    const next = reduceDrawer(closed, { type: 'search' });
    expect(next.open).toBe(true);
    expect(next.searchToken).toBe(1);
  });

  it('clamps the height against the panel and keeps it when unchanged', () => {
    const tall = reduceDrawer(closed, { type: 'height', height: 2000, panelHeight: 1000 });
    expect(tall.prefs.height).toBe(700);
    const small = reduceDrawer(closed, { type: 'height', height: 10, panelHeight: 1000 });
    expect(small.prefs.height).toBe(160);
    expect(reduceDrawer(tall, { type: 'height', height: 700, panelHeight: 1000 })).toBe(tall);
  });

  it('collapses an unpinned drawer only after a successful drop', () => {
    const open = reduceDrawer(closed, { type: 'open' });
    expect(reduceDrawer(open, { type: 'dragEnd', dropEffect: 'none' }).open).toBe(true);
    expect(reduceDrawer(open, { type: 'dragEnd', dropEffect: 'copy' }).open).toBe(false);
    const pinned = reduceDrawer(open, { type: 'pinned', pinned: true });
    expect(reduceDrawer(pinned, { type: 'dragEnd', dropEffect: 'copy' }).open).toBe(true);
  });

  it('stores tile size, view and panel visibility inside the prefs', () => {
    const next = [
      { type: 'tileSize', size: 999 },
      { type: 'view', view: 'list' },
      { type: 'treeOpen', open: false },
      { type: 'detailOpen', open: false },
      { type: 'pinned', pinned: true },
    ] as const;
    const state = next.reduce(reduceDrawer, closed);
    expect(state.prefs).toMatchObject({ tileSize: 240, view: 'list', treeOpen: false, detailOpen: false, pinned: true });
  });

  it('ignores a drop on a closed drawer and closes an open one on a move', () => {
    expect(reduceDrawer(closed, { type: 'dragEnd', dropEffect: 'copy' })).toBe(closed);
    const open = reduceDrawer(closed, { type: 'open' });
    expect(reduceDrawer(open, { type: 'dragEnd', dropEffect: 'move' }).open).toBe(false);
  });

  it('falls back to the default height for a non-finite request', () => {
    const next = reduceDrawer(closed, { type: 'height', height: Number.NaN, panelHeight: 1000 });
    expect(next.prefs.height).toBe(300);
  });

  it('unpinning leaves the open state alone', () => {
    const pinned = reduceDrawer(closed, { type: 'pinned', pinned: true });
    const unpinned = reduceDrawer(pinned, { type: 'pinned', pinned: false });
    expect(unpinned.prefs.pinned).toBe(false);
    expect(unpinned.open).toBe(false);
  });
});

describe('drawer controller', () => {
  it('toggles from the Ctrl+Space and backtick hotkeys and saves the prefs', () => {
    const { result } = renderHook(() => useDrawerState());
    act(() => result.current.toggle());
    expect(result.current.open).toBe(true);

    act(() => { window.dispatchEvent(new KeyboardEvent('keydown', { key: '`' })); });
    expect(result.current.open).toBe(false);

    act(() => result.current.setTileSize(500));
    expect(result.current.prefs.tileSize).toBe(240);
    expect(JSON.parse(localStorage.getItem(DRAWER_PREFS_KEY) ?? '{}').tileSize).toBe(240);
  });

  it('Ctrl+F opens a closed drawer and requests the search focus', () => {
    const { result } = renderHook(() => useDrawerState());
    const event = new KeyboardEvent('keydown', { key: 'f', ctrlKey: true, bubbles: true, cancelable: true });
    act(() => { window.dispatchEvent(event); });
    expect(result.current.open).toBe(true);
    expect(result.current.searchToken).toBe(1);
    expect(event.defaultPrevented).toBe(true);
  });

  it('leaves Ctrl+F alone while a field of another panel has focus and the drawer is closed', () => {
    const field = document.createElement('input');
    document.body.appendChild(field);
    const { result } = renderHook(() => useDrawerState());
    const event = new KeyboardEvent('keydown', { key: 'f', ctrlKey: true, bubbles: true, cancelable: true });
    act(() => { field.dispatchEvent(event); });
    expect(result.current.open).toBe(false);
    expect(result.current.searchToken).toBe(0);
    expect(event.defaultPrevented).toBe(false);
    field.remove();
  });

  it('closes on Escape only while open', () => {
    const { result } = renderHook(() => useDrawerState());
    act(() => { window.dispatchEvent(new KeyboardEvent('keydown', { key: 'Escape' })); });
    expect(result.current.open).toBe(false);
    act(() => result.current.openDrawer());
    act(() => { window.dispatchEvent(new KeyboardEvent('keydown', { key: 'Escape' })); });
    expect(result.current.open).toBe(false);
  });

  it('restores the saved prefs on mount', () => {
    localStorage.setItem(DRAWER_PREFS_KEY, JSON.stringify({ height: 420, pinned: true, tileSize: 200, view: 'list', treeOpen: false, detailOpen: true }));
    const { result } = renderHook(() => useDrawerState());
    expect(result.current.prefs).toMatchObject({ height: 420, pinned: true, tileSize: 200, view: 'list', treeOpen: false });
    expect(result.current.open).toBe(false);
  });
});

import { useCallback, useEffect, useReducer } from 'react';
import {
  clampDrawerHeight, clampTileSize, drawerHotkeyAction, isEditableTarget, loadDrawerPrefs, saveDrawerPrefs,
  type BrowserView, type DrawerPrefs,
} from './drawerPrefs';

export interface DrawerState {
  open: boolean;
  prefs: DrawerPrefs;
  searchToken: number;
}

export type DrawerAction =
  | { type: 'open' }
  | { type: 'close' }
  | { type: 'toggle' }
  | { type: 'search' }
  | { type: 'dragEnd'; dropEffect: string }
  | { type: 'height'; height: number; panelHeight: number }
  | { type: 'pinned'; pinned: boolean }
  | { type: 'tileSize'; size: number }
  | { type: 'view'; view: BrowserView }
  | { type: 'treeOpen'; open: boolean }
  | { type: 'detailOpen'; open: boolean };

export function reduceDrawer(state: DrawerState, action: DrawerAction): DrawerState {
  switch (action.type) {
    case 'open': return state.open ? state : { ...state, open: true };
    case 'close': return state.open ? { ...state, open: false } : state;
    case 'toggle': return { ...state, open: !state.open };
    case 'search': return { ...state, open: true, searchToken: state.searchToken + 1 };
    case 'dragEnd':
      if (action.dropEffect === 'none' || state.prefs.pinned || !state.open) return state;
      return { ...state, open: false };
    case 'height': {
      const height = clampDrawerHeight(action.height, action.panelHeight);
      return height === state.prefs.height ? state : { ...state, prefs: { ...state.prefs, height } };
    }
    case 'pinned': return { ...state, prefs: { ...state.prefs, pinned: action.pinned } };
    case 'tileSize': return { ...state, prefs: { ...state.prefs, tileSize: clampTileSize(action.size) } };
    case 'view': return { ...state, prefs: { ...state.prefs, view: action.view } };
    case 'treeOpen': return { ...state, prefs: { ...state.prefs, treeOpen: action.open } };
    case 'detailOpen': return { ...state, prefs: { ...state.prefs, detailOpen: action.open } };
  }
}

export interface DrawerController {
  open: boolean;
  prefs: DrawerPrefs;
  searchToken: number;
  toggle: () => void;
  close: () => void;
  openDrawer: () => void;
  search: () => void;
  dragEnd: (dropEffect: string) => void;
  setHeight: (height: number, panelHeight: number) => void;
  setPinned: (pinned: boolean) => void;
  setTileSize: (size: number) => void;
  setView: (view: BrowserView) => void;
  setTreeOpen: (open: boolean) => void;
  setDetailOpen: (open: boolean) => void;
}

function initialState(): DrawerState {
  return { open: false, prefs: loadDrawerPrefs(), searchToken: 0 };
}

export function useDrawerState(): DrawerController {
  const [state, dispatch] = useReducer(reduceDrawer, undefined, initialState);
  const { open } = state;

  useEffect(() => { saveDrawerPrefs(state.prefs); }, [state.prefs]);

  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      const target = e.target instanceof HTMLElement ? e.target : null;
      const editable = isEditableTarget(target?.tagName ?? '', target?.isContentEditable ?? false);
      const action = drawerHotkeyAction({
        key: e.key,
        ctrlKey: e.ctrlKey,
        metaKey: e.metaKey,
        altKey: e.altKey,
        shiftKey: e.shiftKey,
        editable,
      });
      if (action === 'toggle') {
        e.preventDefault();
        dispatch({ type: 'toggle' });
      } else if (action === 'search' && (open || !editable)) {
        e.preventDefault();
        dispatch({ type: 'search' });
      } else if (e.key === 'Escape' && open && !editable && !e.defaultPrevented) {
        dispatch({ type: 'close' });
      }
    };
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
  }, [open]);

  const toggle = useCallback(() => dispatch({ type: 'toggle' }), []);
  const close = useCallback(() => dispatch({ type: 'close' }), []);
  const openDrawer = useCallback(() => dispatch({ type: 'open' }), []);
  const search = useCallback(() => dispatch({ type: 'search' }), []);
  const dragEnd = useCallback((dropEffect: string) => dispatch({ type: 'dragEnd', dropEffect }), []);
  const setHeight = useCallback((height: number, panelHeight: number) => dispatch({ type: 'height', height, panelHeight }), []);
  const setPinned = useCallback((pinned: boolean) => dispatch({ type: 'pinned', pinned }), []);
  const setTileSize = useCallback((size: number) => dispatch({ type: 'tileSize', size }), []);
  const setView = useCallback((view: BrowserView) => dispatch({ type: 'view', view }), []);
  const setTreeOpen = useCallback((next: boolean) => dispatch({ type: 'treeOpen', open: next }), []);
  const setDetailOpen = useCallback((next: boolean) => dispatch({ type: 'detailOpen', open: next }), []);

  return {
    open: state.open,
    prefs: state.prefs,
    searchToken: state.searchToken,
    toggle,
    close,
    openDrawer,
    search,
    dragEnd,
    setHeight,
    setPinned,
    setTileSize,
    setView,
    setTreeOpen,
    setDetailOpen,
  };
}

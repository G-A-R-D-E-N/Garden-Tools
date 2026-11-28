import { describe, expect, it, vi } from 'vitest';
import {
  buildCellPopoutUrl,
  emitPopoutWindow,
  isCellMainMessage,
  isCellPopoutIntent,
  isCellPopoutRoute,
  parsePopoutWindowEvent,
  subscribePopoutWindow,
} from './cellPopoutProtocol';

const reference = {
  formKey: '00012345:Fallout4.esm',
  position: { x: 1, y: 2, z: 3 },
  rotation: { x: 0, y: 0, z: 0 },
};

describe('isCellPopoutIntent', () => {
  it('accepts every intent shape the popout sends', () => {
    expect(isCellPopoutIntent({ kind: 'hello' })).toBe(true);
    expect(isCellPopoutIntent({ kind: 'bye' })).toBe(true);
    expect(isCellPopoutIntent({ kind: 'select', formKey: null })).toBe(true);
    expect(isCellPopoutIntent({ kind: 'select', formKey: 'A:B' })).toBe(true);
    expect(isCellPopoutIntent({ kind: 'hide', formKey: 'A:B' })).toBe(true);
    expect(isCellPopoutIntent({ kind: 'unhideAll' })).toBe(true);
    expect(isCellPopoutIntent({ kind: 'toggleMarkers' })).toBe(true);
    expect(isCellPopoutIntent({ kind: 'delete', formKey: 'A:B' })).toBe(true);
    expect(isCellPopoutIntent({ kind: 'duplicate', formKey: 'A:B' })).toBe(true);
    expect(isCellPopoutIntent({ kind: 'details', formKey: 'A:B' })).toBe(true);
    expect(isCellPopoutIntent({ kind: 'move', reference })).toBe(true);
    expect(isCellPopoutIntent({ kind: 'drop', payload: { items: [] }, point: { x: 0, y: 0, z: 0 } })).toBe(true);
    expect(isCellPopoutIntent({ kind: 'log', line: 'hi' })).toBe(true);
  });

  it('rejects unknown kinds and malformed fields', () => {
    expect(isCellPopoutIntent(null)).toBe(false);
    expect(isCellPopoutIntent('hello')).toBe(false);
    expect(isCellPopoutIntent({ kind: 'explode' })).toBe(false);
    expect(isCellPopoutIntent({ kind: 'select' })).toBe(false);
    expect(isCellPopoutIntent({ kind: 'hide', formKey: 7 })).toBe(false);
    expect(isCellPopoutIntent({ kind: 'move', reference: { formKey: 'A:B' } })).toBe(false);
    expect(isCellPopoutIntent({ kind: 'drop', payload: { items: [] }, point: { x: 0 } })).toBe(false);
    expect(isCellPopoutIntent({ kind: 'log' })).toBe(false);
  });
});

describe('isCellMainMessage', () => {
  it('accepts references, sync and close messages', () => {
    expect(isCellMainMessage({ kind: 'references', references: [reference], lighting: null })).toBe(true);
    expect(isCellMainMessage({ kind: 'sync', hiddenTypes: ['Marker'], hiddenRefs: [], selectedKey: null })).toBe(true);
    expect(isCellMainMessage({ kind: 'close' })).toBe(true);
  });

  it('rejects malformed main messages', () => {
    expect(isCellMainMessage({ kind: 'references', references: [{ formKey: 'A:B' }], lighting: null })).toBe(false);
    expect(isCellMainMessage({ kind: 'sync', hiddenTypes: 'Marker', hiddenRefs: [], selectedKey: null })).toBe(false);
    expect(isCellMainMessage({ kind: 'sync', hiddenTypes: [], hiddenRefs: [], selectedKey: 3 })).toBe(false);
    expect(isCellMainMessage({ kind: 'hello' })).toBe(false);
    expect(isCellMainMessage(undefined)).toBe(false);
  });
});

describe('parsePopoutWindowEvent', () => {
  it('reads the native PopoutWindow event', () => {
    expect(parsePopoutWindowEvent({ Type: 'PopoutWindow', Route: 'cell-viewport', Open: true }))
      .toEqual({ Type: 'PopoutWindow', Route: 'cell-viewport', Open: true });
  });

  it('ignores other events and malformed payloads', () => {
    expect(parsePopoutWindowEvent({ Type: 'PluginChanged', Route: 'cell-viewport', Open: true })).toBeNull();
    expect(parsePopoutWindowEvent({ Type: 'PopoutWindow', Route: 'cell-viewport' })).toBeNull();
    expect(parsePopoutWindowEvent('PopoutWindow')).toBeNull();
  });
});

describe('buildCellPopoutUrl', () => {
  it('points at the popout hash route and carries the access token', () => {
    const url = buildCellPopoutUrl({ origin: 'http://127.0.0.1:49735', pathname: '/' }, 'abc123');
    expect(url).toBe('http://127.0.0.1:49735/#/popout/cell-viewport?t=abc123');
  });

  it('omits the token query when none is known', () => {
    expect(buildCellPopoutUrl({ origin: 'http://localhost:5173', pathname: '/' }, ''))
      .toBe('http://localhost:5173/#/popout/cell-viewport');
  });
});

describe('isCellPopoutRoute', () => {
  it('matches only the cell viewport popout route', () => {
    expect(isCellPopoutRoute('#/popout/cell-viewport')).toBe(true);
    expect(isCellPopoutRoute('#/popout/cell-viewport?t=abc')).toBe(true);
    expect(isCellPopoutRoute('#/main?t=abc')).toBe(false);
    expect(isCellPopoutRoute('')).toBe(false);
  });
});

describe('popout window bus', () => {
  it('delivers events to subscribers until they unsubscribe', () => {
    const listener = vi.fn();
    const off = subscribePopoutWindow(listener);
    const event = { Type: 'PopoutWindow' as const, Route: 'cell-viewport', Open: false };
    emitPopoutWindow(event);
    off();
    emitPopoutWindow(event);
    expect(listener).toHaveBeenCalledTimes(1);
    expect(listener).toHaveBeenCalledWith(event);
  });
});

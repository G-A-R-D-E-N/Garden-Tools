import { afterEach, beforeEach, describe, expect, it } from 'vitest';
import {
  clampInspectorWidth,
  hasReferenceScripts,
  INSPECTOR_DEFAULT,
  INSPECTOR_MIN,
  loadInspectorWidth,
  maxInspectorWidth,
  saveInspectorWidth,
} from './referenceInspector';
import type { ReferenceDetails, ReferenceScriptSet } from '../referenceDetails';

const scriptSet = (overrides: Partial<ReferenceScriptSet>): ReferenceScriptSet => ({
  version: 6,
  objectFormat: 2,
  unsupported: false,
  fragmentBytes: 0,
  scripts: [],
  ...overrides,
});

const detailsWith = (scripts: ReferenceDetails['scripts'], baseScripts: ReferenceDetails['baseScripts'] = null) =>
  ({ scripts, baseScripts } as ReferenceDetails);

const WIN = 1600;

beforeEach(() => { localStorage.clear(); });
afterEach(() => { localStorage.clear(); });

describe('reference inspector width', () => {
  it('clamps to [320, 60% of the window]', () => {
    expect(clampInspectorWidth(100, WIN)).toBe(320);
    expect(clampInspectorWidth(5000, WIN)).toBe(960);
    expect(clampInspectorWidth(500.4, WIN)).toBe(500);
    expect(clampInspectorWidth(Number.NaN, WIN)).toBe(INSPECTOR_DEFAULT);
  });

  it('keeps the minimum width when the window is narrower than the minimum', () => {
    expect(maxInspectorWidth(400)).toBe(INSPECTOR_MIN);
    expect(clampInspectorWidth(900, 400)).toBe(INSPECTOR_MIN);
  });

  it('defaults to 420px and persists the width across loads', () => {
    expect(loadInspectorWidth(WIN)).toBe(420);
    saveInspectorWidth(500);
    expect(localStorage.getItem('cell.inspectorWidth')).toBe('500');
    expect(loadInspectorWidth(WIN)).toBe(500);
  });

  it('re-clamps a stored width that no longer fits the window', () => {
    localStorage.setItem('cell.inspectorWidth', '5000');
    expect(loadInspectorWidth(WIN)).toBe(960);
  });
});

describe('reference scripts detection', () => {
  const script = { name: 'MyScript', flags: 0, properties: [], parent: null, declared: [], declaredError: null };

  it('is false without details or script sets', () => {
    expect(hasReferenceScripts(null)).toBe(false);
    expect(hasReferenceScripts(detailsWith(null, null))).toBe(false);
    expect(hasReferenceScripts(detailsWith(scriptSet({}), null))).toBe(false);
  });

  it('is true for scripts on the reference or on its base', () => {
    expect(hasReferenceScripts(detailsWith(scriptSet({ scripts: [script] })))).toBe(true);
    expect(hasReferenceScripts(detailsWith(null, scriptSet({ scripts: [script] })))).toBe(true);
  });

  it('is true for fragment bytes and false for an error reply', () => {
    expect(hasReferenceScripts(detailsWith(scriptSet({ fragmentBytes: 12 })))).toBe(true);
    expect(hasReferenceScripts(detailsWith(null, { error: 'Record not found' }))).toBe(false);
  });
});

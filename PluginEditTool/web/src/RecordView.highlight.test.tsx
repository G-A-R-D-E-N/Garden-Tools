import { act, cleanup, render, screen } from '@testing-library/react';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import type { ConflictMatrix } from './backend';
import { DialogProvider } from './dialogs';
import MainShell from './MainShell';

const row = {
  Field: 'FULL', DisplayLabel: 'Name', Level: 0, Values: ['Old', 'New'], Differs: true,
  Statuses: [], Severity: 'conflict', Kind: 'Value' as const, Group: '', GroupLabel: '', IsSummary: false,
  HasChildren: false, EditKind: 'Text' as const, EnumOptions: null, RefType: null, RefTypes: null,
};
const matrix: ConflictMatrix = {
  FormKey: '000800:Master.esm', EditorID: 'TestWeapon', Type: 'WEAP',
  Winner: 'Later.esp', Plugins: ['Master.esm', 'Later.esp'], Rows: [row], Level: 'conflict',
};
let onMessage: ((e: { data: unknown }) => void) | undefined;

beforeEach(() => {
  onMessage = undefined;
  localStorage.clear();
  vi.stubGlobal('chrome', { webview: {
    addEventListener: vi.fn((type: string, fn: (e: { data: unknown }) => void) => { if (type === 'message') onMessage = fn; }),
    removeEventListener: vi.fn(),
    hostObjects: { backend: {
      GetConflictMatrix: vi.fn(async () => JSON.stringify(matrix)),
      GetProblems: vi.fn(async () => '[]'),
      GetReferencedBy: vi.fn(async () => '[]'),
      GetDependencies: vi.fn(async () => '[]'),
      GetHistory: vi.fn(async () => '[]'),
      GetContainmentPath: vi.fn(async () => '[]'),
    } },
  } });
});

afterEach(() => { cleanup(); vi.unstubAllGlobals(); });

const highlight = () => act(async () => onMessage?.({ data: {
  Type: 'McpLive', Tool: 'set_field', Plugin: 'Later.esp', Record: matrix.FormKey, Field: 'FULL', Summary: '', IsWrite: true,
} }));
const flashedRow = () => screen.getByText('New').closest('tr')!;

describe('MCP field highlight', () => {
  it('re-flashes when the same field is highlighted twice in a row', async () => {
    render(<DialogProvider><MainShell /></DialogProvider>);
    await highlight();
    await screen.findByText('New');
    const first = flashedRow();
    expect(first.classList.contains('rv-row-flash')).toBe(true);
    const firstSeq = first.getAttribute('data-flash-seq');
    expect(firstSeq).not.toBeNull();

    await highlight();
    const second = flashedRow();
    expect(second.classList.contains('rv-row-flash')).toBe(true);
    expect(second.getAttribute('data-flash-seq')).not.toBe(firstSeq);
    expect(second).not.toBe(first);
  });

  it('keeps the same row node when the highlight clears', async () => {
    vi.useFakeTimers({ shouldAdvanceTime: true });
    try {
      render(<DialogProvider><MainShell /></DialogProvider>);
      await highlight();
      await screen.findByText('New');
      const before = flashedRow();
      expect(before.classList.contains('rv-row-flash')).toBe(true);

      await act(async () => { vi.advanceTimersByTime(3000); });
      const after = flashedRow();
      expect(after.classList.contains('rv-row-flash')).toBe(false);
      expect(after).toBe(before);
    } finally {
      vi.useRealTimers();
    }
  });

  it('does not hold the flash end state over the row colours', async () => {
    const fs = await vi.importActual<{ readFileSync: (path: string, enc: 'utf8') => string }>('node:fs');
    const css = fs.readFileSync('src/RecordView.css', 'utf8');
    const rules = [...css.matchAll(/\.rv-row-flash td\s*\{([^}]*)\}/g)].map(m => m[1]);
    expect(rules).toHaveLength(2);
    for (const rule of rules) expect(rule).not.toMatch(/forwards|both/);
  });
});

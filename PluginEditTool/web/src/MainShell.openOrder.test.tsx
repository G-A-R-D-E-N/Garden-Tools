import { act, cleanup, fireEvent, render, screen } from '@testing-library/react';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import type { ConflictMatrix } from './backend';
import { DialogProvider } from './dialogs';
import MainShell from './MainShell';

const ALPHA = '000800:Master.esm';
const BETA = '000801:Master.esm';

const row = {
  Field: 'FULL', DisplayLabel: 'Name', Level: 0, Values: ['Old', 'New'], Differs: true,
  Statuses: [], Severity: 'conflict', Kind: 'Value' as const, Group: '', GroupLabel: '', IsSummary: false,
  HasChildren: false, EditKind: 'Text' as const, EnumOptions: null, RefType: null, RefTypes: null,
};
const matrixFor = (formKey: string, editorId: string, values: [string, string]): ConflictMatrix => ({
  FormKey: formKey, EditorID: editorId, Type: 'WEAP', Winner: 'Later.esp',
  Plugins: ['Master.esm', 'Later.esp'], Rows: [{ ...row, Values: values }], Level: 'conflict',
});
const entry = (formKey: string, editorId: string) => ({
  FormKey: formKey, EditorID: editorId, Type: 'WEAP', Winner: 'Later.esp',
  Plugins: ['Master.esm', 'Later.esp'], InvolvesMod: false,
});
const liveWrite = { Type: 'McpLive', Tool: 'set_field', Plugin: 'Later.esp', Record: ALPHA, Field: '', Summary: '', IsWrite: true };

const calls: Array<{ formKey: string; resolve: (raw: string) => void }> = [];
let onMessage: ((e: { data: unknown }) => void) | undefined;

beforeEach(() => {
  calls.length = 0;
  onMessage = undefined;
  localStorage.clear();
  localStorage.setItem('treeMode', 'conflicts');
  vi.stubGlobal('chrome', { webview: {
    addEventListener: vi.fn((type: string, fn: (e: { data: unknown }) => void) => { if (type === 'message') onMessage = fn; }),
    removeEventListener: vi.fn(),
    hostObjects: { backend: {
      GetConflicts: vi.fn(async () => JSON.stringify([entry(ALPHA, 'AlphaWeapon'), entry(BETA, 'BetaWeapon')])),
      GetConflictMatrix: vi.fn((formKey: string) => new Promise<string>(resolve => { calls.push({ formKey, resolve }); })),
      GetEditablePlugins: vi.fn(async () => '[]'),
      GetProblems: vi.fn(async () => '[]'),
      GetReferencedBy: vi.fn(async () => '[]'),
      GetDependencies: vi.fn(async () => '[]'),
      GetHistory: vi.fn(async () => '[]'),
      GetContainmentPath: vi.fn(async () => '[]'),
    } },
  } });
});

afterEach(() => { cleanup(); vi.unstubAllGlobals(); });

describe('MainShell record open ordering', () => {
  it('shows the newer conflict record when an earlier open resolves last', async () => {
    render(<DialogProvider><MainShell /></DialogProvider>);
    fireEvent.click(await screen.findByText('AlphaWeapon'));
    fireEvent.click(screen.getByText('BetaWeapon'));
    expect(calls.map(c => c.formKey)).toEqual([ALPHA, BETA]);
    await act(async () => { calls[1].resolve(JSON.stringify(matrixFor(BETA, 'BetaWeapon', ['BetaOld', 'BetaNew']))); });
    expect(await screen.findByText('BetaNew')).toBeTruthy();
    await act(async () => { calls[0].resolve(JSON.stringify(matrixFor(ALPHA, 'AlphaWeapon', ['AlphaOld', 'AlphaNew']))); });
    expect(screen.getByText('BetaNew')).toBeTruthy();
    expect(screen.queryByText('AlphaNew')).toBeNull();
  });

  it('keeps the newest record reload when an earlier reload resolves last', async () => {
    render(<DialogProvider><MainShell /></DialogProvider>);
    fireEvent.click(await screen.findByText('AlphaWeapon'));
    await act(async () => { calls[0].resolve(JSON.stringify(matrixFor(ALPHA, 'AlphaWeapon', ['AlphaOld', 'AlphaNew']))); });
    expect(await screen.findByText('AlphaNew')).toBeTruthy();
    await act(async () => { onMessage?.({ data: liveWrite }); });
    await act(async () => { onMessage?.({ data: liveWrite }); });
    expect(calls).toHaveLength(3);
    await act(async () => { calls[2].resolve(JSON.stringify(matrixFor(ALPHA, 'AlphaWeapon', ['FreshOld', 'FreshNew']))); });
    expect(await screen.findByText('FreshNew')).toBeTruthy();
    await act(async () => { calls[1].resolve(JSON.stringify(matrixFor(ALPHA, 'AlphaWeapon', ['StaleOld', 'StaleNew']))); });
    expect(screen.getByText('FreshNew')).toBeTruthy();
    expect(screen.queryByText('StaleNew')).toBeNull();
  });
});

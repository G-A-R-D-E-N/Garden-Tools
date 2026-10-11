import { act, cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import type { ConflictMatrix } from './backend';
import { DialogProvider } from './dialogs';
import RecordView from './RecordView';

const row = {
  Field: 'FULL', DisplayLabel: 'Name', Level: 0, Values: ['Old', 'New'], Differs: true,
  Statuses: [], Severity: 'conflict', Kind: 'Value' as const, Group: '', GroupLabel: '', IsSummary: false,
  HasChildren: false, EditKind: 'Text' as const, EnumOptions: null, RefType: null, RefTypes: null,
};
const descRow = { ...row, Field: 'DESC', DisplayLabel: 'Description', Values: ['OldDesc', 'NewDesc'] };
const matrixA: ConflictMatrix = {
  FormKey: '000800:Master.esm', EditorID: 'TestWeapon', Type: 'WEAP',
  Winner: 'Later.esp', Plugins: ['Master.esm', 'Later.esp'], Rows: [row], Level: 'conflict',
};
const matrixB: ConflictMatrix = { ...matrixA, FormKey: '000801:Master.esm', EditorID: 'OtherWeapon' };
const twoRowMatrix: ConflictMatrix = { ...matrixA, Rows: [row, descRow] };
const elementActions = (count: number) => JSON.stringify({
  canAdd: false, templates: [], elementType: 'Text', canRemove: false, canMoveUp: false, canMoveDown: false,
  canClear: true, count,
});

let resolveA: (raw: string) => void = () => {};
let resolveB: (raw: string) => void = () => {};
const describeResolvers: Record<string, (raw: string) => void> = {};
const onReload = vi.fn();

beforeEach(() => {
  vi.stubGlobal('chrome', { webview: {
    addEventListener: vi.fn(),
    removeEventListener: vi.fn(),
    hostObjects: { backend: {
      GetProblems: vi.fn((formKey: string) => new Promise<string>(resolve => {
        if (formKey === matrixA.FormKey) resolveA = resolve; else resolveB = resolve;
      })),
      DescribeElement: vi.fn((_plugin: string, _formKey: string, field: string) => new Promise<string>(resolve => {
        describeResolvers[field] = resolve;
      })),
      GetReferencedBy: vi.fn(async () => '[]'),
      GetDependencies: vi.fn(async () => '[]'),
      GetHistory: vi.fn(async () => '[]'),
      GetContainmentPath: vi.fn(async () => '[]'),
      GetEditablePlugins: vi.fn(async () => '[]'),
    } },
  } });
});

afterEach(() => { cleanup(); vi.unstubAllGlobals(); });

describe('record load ordering', () => {
  it('keeps the newest record problems when an older load resolves last', async () => {
    const view = (matrix: ConflictMatrix) => (
      <DialogProvider><RecordView matrix={matrix} plugin="Later.esp" onReload={onReload} /></DialogProvider>
    );
    const { rerender } = render(view(matrixA));
    rerender(view(matrixB));
    await act(async () => { resolveB(JSON.stringify([{ Severity: 'warn', Description: 'Newer problem' }])); });
    await act(async () => { resolveA(JSON.stringify([{ Severity: 'warn', Description: 'Stale problem' }])); });
    expect(await screen.findByText('Newer problem')).toBeTruthy();
    expect(screen.queryByText('Stale problem')).toBeNull();
  });

  it('keeps the actions for the latest right-clicked element when an older lookup resolves last', async () => {
    render(<DialogProvider><RecordView matrix={twoRowMatrix} plugin="Later.esp" onReload={onReload} /></DialogProvider>);
    fireEvent.contextMenu(await screen.findByText('New'));
    fireEvent.contextMenu(screen.getByText('NewDesc'));
    await waitFor(() => expect(describeResolvers.DESC).toBeDefined());
    await act(async () => { describeResolvers.DESC(elementActions(2)); });
    expect(await screen.findByRole('button', { name: 'Clear (2)' })).toBeTruthy();
    await act(async () => { describeResolvers.FULL(elementActions(9)); });
    expect(screen.queryByRole('button', { name: 'Clear (9)' })).toBeNull();
    expect(screen.getByRole('button', { name: 'Clear (2)' })).toBeTruthy();
  });
});

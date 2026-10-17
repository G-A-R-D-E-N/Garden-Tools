import { act, cleanup, fireEvent, render, screen } from '@testing-library/react';
import { afterEach, beforeEach, expect, it, vi } from 'vitest';
import type { ConflictFieldRow, ConflictMatrix } from './backend';
import { DialogProvider } from './dialogs';
import RecordView from './RecordView';

const field = (Field: string, Group: string, value: string): ConflictFieldRow => ({
  Field, Group, DisplayLabel: Field, GroupLabel: Group, Level: 0, Values: [value],
  Differs: false, Statuses: ['only'], Severity: 'none', Kind: 'Value',
  IsSummary: false, HasChildren: false, EditKind: 'Text',
  EnumOptions: null, RefType: null, RefTypes: null,
});
const matrix: ConflictMatrix = {
  FormKey: '000800:W0.esp', EditorID: 'TestRace', Type: 'RACE', Winner: 'W0.esp',
  Plugins: ['W0.esp'], Level: 'onlyone', Rows: [
    field('RACE\\BipedObjectNames[1]', 'Semantic', 'second'),
    field('Subrecords\\ZZZZ[0]', 'Raw', 'AA BB'),
  ],
};

beforeEach(() => localStorage.clear());
afterEach(() => { cleanup(); vi.unstubAllGlobals(); });

it('shows semantic fields by default and preserves an explicit raw view', () => {
  render(<DialogProvider><RecordView matrix={matrix} plugin="W0.esp" onReload={async () => {}} /></DialogProvider>);
  expect(screen.getByText('second')).toBeTruthy();
  expect(screen.queryByText('AA BB')).toBeNull();
  fireEvent.click(screen.getByRole('button', { name: 'Raw / Advanced' }));
  expect(screen.getByText('AA BB')).toBeTruthy();
  expect(screen.queryByText('second')).toBeNull();
});

it('does not open an editor for a backend-refused cell', () => {
  const opaque = { ...matrix, Rows: [{ ...field('RACE\\BipedObjectNames[1]', 'Semantic', '<opaque>'), Editable: [false] }] };
  render(<DialogProvider><RecordView matrix={opaque} plugin="W0.esp" onReload={async () => {}} /></DialogProvider>);
  fireEvent.doubleClick(screen.getByText('<opaque>'));
  fireEvent.keyDown(screen.getByText('<opaque>'), { key: 'F2' });
  expect(screen.queryByDisplayValue('<opaque>')).toBeNull();
});

it('starts the same inline edit from F2 on a focused value cell', () => {
  render(<DialogProvider><RecordView matrix={matrix} plugin="W0.esp" onReload={async () => {}} /></DialogProvider>);
  fireEvent.keyDown(screen.getByText('second'), { key: 'F2' });
  expect(screen.getByDisplayValue('second')).toBeTruthy();
});

it('reports a failed context-menu lookup in the toolbar status', async () => {
  vi.stubGlobal('chrome', { webview: { hostObjects: { backend: {
    GetEditablePlugins: vi.fn(async () => { throw new Error('plugin list offline'); }),
    GetContainmentPath: vi.fn(async () => '[]'),
    DescribeElement: vi.fn(async () => '{}'),
  } } } });
  render(<DialogProvider><RecordView matrix={matrix} plugin="W0.esp" onReload={async () => {}} /></DialogProvider>);
  fireEvent.contextMenu(screen.getByText('second'));
  expect(await screen.findByText('Could not list editable plugins: plugin list offline')).toBeTruthy();
});

it('offers schema actions only when permitted and submits schema creation', async () => {
  const CreateRecord = vi.fn(async () => JSON.stringify({ ok: true, formKey: '000801:W0.esp' }));
  const onReload = vi.fn(async () => {});
  vi.stubGlobal('chrome', { webview: { hostObjects: { backend: {
    GetEditablePlugins: vi.fn(async () => '["W0.esp"]'),
    GetContainmentPath: vi.fn(async () => '[]'),
    DescribeElement: vi.fn(async () => JSON.stringify({
      canAdd: false, canRemove: false, canCreate: true, templates: [],
      elementType: 'Subrecord', canMoveUp: false, canMoveDown: false, canClear: false, count: 1,
    })), CreateRecord,
  } } } });
  const authoring = { ...matrix, Type: 'ARMO', Rows: [
    { ...field('Authoring\\DATA[0]', 'Semantic', 'present'), Editable: [false] },
  ] };
  render(<DialogProvider><RecordView matrix={authoring} plugin="W0.esp" onReload={onReload} /></DialogProvider>);
  fireEvent.contextMenu(screen.getByText('present'));
  const create = await screen.findByRole('button', { name: 'Create ARMO record…' });
  expect(screen.queryByRole('button', { name: 'Edit value F2' })).toBeNull();
  expect(screen.queryByRole('button', { name: 'Add "DATA"' })).toBeNull();
  fireEvent.click(create);
  fireEvent.change(screen.getByLabelText('Editor ID'), { target: { value: 'NewArmor' } });
  fireEvent.click(screen.getByRole('button', { name: 'Create' }));
  await vi.waitFor(() => expect(CreateRecord).toHaveBeenCalledWith('W0.esp', 'ARMO', {
    schema: true, editorId: 'NewArmor',
  }));
  expect(onReload).toHaveBeenCalledOnce();
});

it('discards authoring permissions returned for a previous context menu', async () => {
  let completeFirst!: (value: string) => void;
  const first = new Promise<string>(resolve => { completeFirst = resolve; });
  const denied = JSON.stringify({ canAdd: false, canRemove: false, canCreate: false, templates: [] });
  const DescribeElement = vi.fn().mockReturnValueOnce(first).mockResolvedValue(denied);
  vi.stubGlobal('chrome', { webview: { hostObjects: { backend: {
    GetEditablePlugins: vi.fn(async () => '[]'), GetContainmentPath: vi.fn(async () => '[]'), DescribeElement,
  } } } });
  const authoring = { ...matrix, Type: 'ARMO', Rows: [
    field('Authoring\\ICON', 'Semantic', 'icons'), field('Authoring\\DATA', 'Semantic', 'data'),
  ] };
  render(<DialogProvider><RecordView matrix={authoring} plugin="W0.esp" onReload={async () => {}} /></DialogProvider>);
  fireEvent.contextMenu(screen.getByText('icons'));
  await vi.waitFor(() => expect(DescribeElement).toHaveBeenCalledTimes(1));
  fireEvent.contextMenu(screen.getByText('data'));
  await vi.waitFor(() => expect(DescribeElement).toHaveBeenCalledTimes(2));
  await act(async () => completeFirst(JSON.stringify({ canCreate: true, templates: [] })));
  expect(screen.queryByRole('button', { name: 'Create ARMO record…' })).toBeNull();
});

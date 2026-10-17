import { cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { afterEach, beforeEach, expect, it, vi } from 'vitest';
import type { ConflictFieldRow, ConflictMatrix, ScalarPresentation } from './backend';
import { DialogProvider } from './dialogs';
import RecordView from './RecordView';

const SetField = vi.fn(async () => 'Saved');

const crimeArrest: ScalarPresentation = {
  kind: 'enum', names: [{ value: 0, name: 'False' }, { value: 1, name: 'True' }],
};
const flags: ScalarPresentation = {
  kind: 'flags', names: [{ bit: 0, name: 'Hidden From NPC' }, { bit: 1, name: 'Special Combat' }, { bit: 6, name: 'Track Crime' }],
};

const scalarRow = (Field: string, value: string, Presentation: ScalarPresentation): ConflictFieldRow => ({
  Field, Group: 'Semantic', DisplayLabel: Field, GroupLabel: 'Semantic fields', Level: 0, Values: [value],
  Differs: false, Statuses: ['only'], Severity: 'none', Kind: 'Value',
  IsSummary: false, HasChildren: false, EditKind: 'Text',
  EnumOptions: null, RefType: null, RefTypes: null, Presentation,
});

const matrix: ConflictMatrix = {
  FormKey: '000800:W0.esp', EditorID: 'TestFaction', Type: 'FACT', Winner: 'W0.esp',
  Plugins: ['W0.esp'], Level: 'onlyone', Rows: [
    scalarRow('FACT\\CrimeArrest', '1', crimeArrest),
    scalarRow('FACT\\Flags', '131', flags),
  ],
};

beforeEach(() => {
  SetField.mockClear();
  vi.stubGlobal('chrome', { webview: { hostObjects: { backend: {
    SetField,
    GetProblems: vi.fn(async () => '[]'),
    GetReferencedBy: vi.fn(async () => '[]'),
    GetDependencies: vi.fn(async () => '[]'),
    GetHistory: vi.fn(async () => '[]'),
    GetContainmentPath: vi.fn(async () => '[]'),
    GetEditablePlugins: vi.fn(async () => '[]'),
  } } } });
});

afterEach(() => {
  cleanup();
  vi.unstubAllGlobals();
});

const renderView = () => render(
  <DialogProvider><RecordView matrix={matrix} plugin="W0.esp" onReload={async () => {}} /></DialogProvider>,
);

it('shows enum and flag names with the raw number in the tooltip', () => {
  renderView();
  expect(screen.getByText('True').getAttribute('title')).toBe('1');
  expect(screen.getByText('Hidden From NPC, Special Combat, <Unknown: 7 $7>').getAttribute('title')).toBe('131');
});

it('writes the picked enum integer through SetField', async () => {
  renderView();
  fireEvent.doubleClick(screen.getByText('True'));
  fireEvent.change(screen.getByDisplayValue('True'), { target: { value: '0' } });
  await waitFor(() => expect(SetField).toHaveBeenCalledWith('W0.esp', '000800:W0.esp', 'FACT\\CrimeArrest', '0'));
});

it('edits flags with a checkbox list and writes the combined integer', async () => {
  renderView();
  fireEvent.doubleClick(screen.getByText('Hidden From NPC, Special Combat, <Unknown: 7 $7>'));
  fireEvent.click(screen.getByLabelText('Track Crime'));
  fireEvent.click(screen.getByRole('button', { name: 'Save' }));
  await waitFor(() => expect(SetField).toHaveBeenCalledWith('W0.esp', '000800:W0.esp', 'FACT\\Flags', '195'));
});

it('keeps a toggled flag when the flag editor is double-clicked', () => {
  const view = renderView();
  fireEvent.doubleClick(screen.getByText('Hidden From NPC, Special Combat, <Unknown: 7 $7>'));
  fireEvent.click(screen.getByLabelText('Track Crime'));
  fireEvent.doubleClick(view.container.querySelector('.scalar-flags-edit') as HTMLElement);
  expect((screen.getByLabelText('Track Crime') as HTMLInputElement).checked).toBe(true);
  fireEvent.click(screen.getByRole('button', { name: 'Save' }));
  expect(SetField).toHaveBeenCalledWith('W0.esp', '000800:W0.esp', 'FACT\\Flags', '195');
});

it('opens no editor for an enum cell whose raw value is not an integer', () => {
  const opaque = { ...matrix, Rows: [scalarRow('FACT\\CrimeArrest', '<opaque>', crimeArrest)] };
  const view = render(
    <DialogProvider><RecordView matrix={opaque} plugin="W0.esp" onReload={async () => {}} /></DialogProvider>,
  );
  fireEvent.doubleClick(screen.getByText('<opaque>'));
  expect(view.container.querySelector('select.rv-edit-input')).toBeNull();
});

it('closes an open flag editor when the record changes and writes nothing', () => {
  const view = renderView();
  fireEvent.doubleClick(screen.getByText('Hidden From NPC, Special Combat, <Unknown: 7 $7>'));
  expect(view.container.querySelector('.scalar-flags-edit')).not.toBeNull();
  view.rerender(
    <DialogProvider><RecordView matrix={{ ...matrix, FormKey: '000801:W0.esp' }} plugin="W0.esp" onReload={async () => {}} /></DialogProvider>,
  );
  expect(view.container.querySelector('.scalar-flags-edit')).toBeNull();
  expect(SetField).not.toHaveBeenCalled();
});

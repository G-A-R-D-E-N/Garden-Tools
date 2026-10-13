import { cleanup, render, screen } from '@testing-library/react';
import { afterEach, expect, it } from 'vitest';
import type { ConflictFieldRow, ConflictMatrix } from './backend';
import type { useRecordActions } from './recordActions';
import RecordContextMenu from './RecordContextMenu';

afterEach(() => cleanup());

const row: ConflictFieldRow = {
  Field: 'Name', DisplayLabel: 'Name', Level: 0, Values: ['x'], Differs: false, Statuses: [],
  Severity: '', Kind: 'Value', Group: '', GroupLabel: '', IsSummary: false, HasChildren: false,
  EditKind: 'Text', EnumOptions: null, RefType: null, RefTypes: null,
};

function renderMenuFor(type: string) {
  const matrix: ConflictMatrix = {
    FormKey: '000800:Patch.esp', EditorID: 'Test', Type: type, Winner: 'Patch.esp',
    Plugins: ['Patch.esp'], Rows: [row], Level: '',
  };
  return render(<RecordContextMenu
    menu={{ x: 0, y: 0, row, col: 0 }} matrix={matrix} elementActions={null}
    actions={{} as ReturnType<typeof useRecordActions>} onEdit={() => {}}
    onEditConditions={() => {}} onShowTab={() => {}} />);
}

it('offers Copy as disabled override only for an Initially Disabled capable type', () => {
  const { unmount } = renderMenuFor('REFR');
  expect(screen.getByText('Copy as disabled override into...')).toBeTruthy();
  unmount();
  renderMenuFor('WEAP');
  expect(screen.queryByText('Copy as disabled override into...')).toBeNull();
  expect(screen.getByText('Copy as override into...')).toBeTruthy();
});

it('offers Copy as wrapper only for a type a leveled list can reference', () => {
  const { unmount } = renderMenuFor('NPC_');
  expect(screen.getByText('Copy as wrapper into...')).toBeTruthy();
  unmount();
  renderMenuFor('CELL');
  expect(screen.queryByText('Copy as wrapper into...')).toBeNull();
});

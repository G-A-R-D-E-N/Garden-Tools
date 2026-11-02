import { cleanup, fireEvent, render, screen } from '@testing-library/react';
import { afterEach, expect, it, vi } from 'vitest';
import type { BulkTarget, ConflictFieldRow, ConflictMatrix } from './backend';
import type { useRecordActions } from './recordActions';
import RecordContextMenu from './RecordContextMenu';

afterEach(() => cleanup());

const row: ConflictFieldRow = {
  Field: 'Name', DisplayLabel: 'Name', Level: 0, Values: ['x'], Differs: false, Statuses: [],
  Severity: '', Kind: 'Value', Group: '', GroupLabel: '', IsSummary: false, HasChildren: false,
  EditKind: 'Text', EnumOptions: null, RefType: null, RefTypes: null,
};

const bulkStub = { bulkTargetsFor: (): BulkTarget[] => [], selectedCount: 0, copyElementToSelected: () => {}, removeElementFromSelected: () => {} };

function renderMenuFor(type: string, actions: Partial<typeof bulkStub> = {}, menuRow: ConflictFieldRow = row) {
  const matrix: ConflictMatrix = {
    FormKey: '000800:Patch.esp', EditorID: 'Test', Type: type, Winner: 'Patch.esp',
    Plugins: ['Patch.esp'], Rows: [menuRow], Level: '',
  };
  return render(<RecordContextMenu
    menu={{ x: 0, y: 0, row: menuRow, col: 0 }} matrix={matrix} elementActions={null}
    actions={{ ...bulkStub, ...actions } as unknown as ReturnType<typeof useRecordActions>} onEdit={() => {}}
    onEditConditions={() => {}} onShowTab={() => {}} />);
}

const subrecordRow: ConflictFieldRow = { ...row, Field: 'Authoring\\ICON[0]', DisplayLabel: 'Icon' };
const oneOtherTarget = [{ plugin: 'Other.esp', formKey: '000801:Other.esp' }];

it('offers Copy as disabled override only for an Initially Disabled capable type', () => {
  const { unmount } = renderMenuFor('REFR');
  expect(screen.getByText('Copy as disabled override into...')).toBeTruthy();
  unmount();
  renderMenuFor('WEAP');
  expect(screen.queryByText('Copy as disabled override into...')).toBeNull();
  expect(screen.getByText('Copy as override into...')).toBeTruthy();
});

it('offers Copy as wrapper only for a leveled list type', () => {
  const { unmount } = renderMenuFor('LVLI');
  expect(screen.getByText('Copy as wrapper into...')).toBeTruthy();
  unmount();
  renderMenuFor('NPC_');
  expect(screen.queryByText('Copy as wrapper into...')).toBeNull();
});

it('enables copy and remove to selected only for a subrecord element with another record picked', () => {
  const { unmount } = renderMenuFor('ARMO', { bulkTargetsFor: () => oneOtherTarget, selectedCount: 2 }, subrecordRow);
  expect((screen.getByText('Copy to selected records') as HTMLButtonElement).closest('button')!.disabled).toBe(false);
  expect((screen.getByText('Remove from selected records') as HTMLButtonElement).closest('button')!.disabled).toBe(false);
  unmount();
  renderMenuFor('ARMO', { bulkTargetsFor: () => oneOtherTarget, selectedCount: 1 }, subrecordRow);
  expect((screen.getByText('Copy to selected records') as HTMLButtonElement).closest('button')!.disabled).toBe(true);
});

it('keeps copy and remove to selected disabled for a non-subrecord element', () => {
  renderMenuFor('ARMO', { bulkTargetsFor: () => oneOtherTarget, selectedCount: 2 }, row);
  expect((screen.getByText('Copy to selected records') as HTMLButtonElement).closest('button')!.disabled).toBe(true);
});

it('passes the element row to copyElementToSelected with the column when enabled', () => {
  const copy = vi.fn();
  renderMenuFor('ARMO', { bulkTargetsFor: () => oneOtherTarget, selectedCount: 2, copyElementToSelected: copy }, subrecordRow);
  fireEvent.click(screen.getByText('Copy to selected records'));
  expect(copy).toHaveBeenCalledWith(0, subrecordRow);
});

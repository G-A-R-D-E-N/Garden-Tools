import { cleanup, render, screen } from '@testing-library/react';
import { afterEach, expect, it } from 'vitest';
import type { ConflictFieldRow, ConflictMatrix, ScalarPresentation } from '../backend';
import ConflictsView from './ConflictsView';

const crimeArrest: ScalarPresentation = {
  kind: 'enum', names: [{ value: 0, name: 'False' }, { value: 1, name: 'True' }],
};

afterEach(() => cleanup());

it('shows base and winner enum values by name with the raw number in the tooltip', () => {
  const row: ConflictFieldRow = {
    Field: 'FACT\\CrimeArrest', Group: 'Semantic', DisplayLabel: 'Arrest', GroupLabel: 'Semantic fields', Level: 0,
    Values: ['0', '1'], Differs: true, Statuses: ['only'], Severity: 'none', Kind: 'Value',
    IsSummary: false, HasChildren: false, EditKind: 'Enum', EnumOptions: null, RefType: null, RefTypes: null,
    Presentation: crimeArrest,
  };
  const matrix: ConflictMatrix = {
    FormKey: '000800:A.esp', EditorID: 'TestFaction', Type: 'FACT', Winner: 'B.esp',
    Plugins: ['A.esp', 'B.esp'], Level: 'conflict', Rows: [row],
  };
  render(
    <ConflictsView matrix={matrix} rows={[row]} layout="cards" onLayoutChange={() => {}} matrixView={null} anchors={null} />,
  );
  expect(screen.getByText('False').getAttribute('title')).toBe('0');
  expect(screen.getByText('True').getAttribute('title')).toBe('1');
});

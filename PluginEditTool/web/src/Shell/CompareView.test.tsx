import { cleanup, fireEvent, render, screen } from '@testing-library/react';
import { afterEach, expect, it, vi } from 'vitest';
import CompareView from './CompareView';
import type { CompareView as CompareViewData } from './useConflictBatch';

afterEach(() => cleanup());

const view: CompareViewData = {
  columns: [{ formKey: '000800:Master.esm', plugin: 'Master.esm' }, { formKey: '000800:Master.esm', plugin: 'Later.esp' }],
  rows: [
    { field: 'Name', label: 'Name', differs: false, cells: [{ value: 'Shared', differs: false }, { value: 'Shared', differs: false }] },
    { field: 'Authoring\\ICON[0]', label: 'Icon', differs: true, cells: [{ value: 'a.dds', differs: true }, { value: 'b.dds', differs: true }] },
  ],
};

it('renders one value column per picked record and marks only the differing row', () => {
  render(<CompareView view={view} onClose={() => {}} />);
  expect(screen.getAllByRole('columnheader')).toHaveLength(3);
  expect(screen.getByText('Later.esp')).toBeTruthy();
  expect(screen.getAllByText('Shared')[0].closest('tr')!.className).toBe('');
  expect(screen.getByText('b.dds').closest('tr')!.className).toBe('differs');
  expect(screen.getByText('a.dds').className).toBe('differs');
});

it('calls onClose from the close button', () => {
  const onClose = vi.fn();
  render(<CompareView view={view} onClose={onClose} />);
  fireEvent.click(screen.getByLabelText('Close comparison'));
  expect(onClose).toHaveBeenCalledTimes(1);
});

import { cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';
import type { CellEditChanges } from './backend';
import ReferenceFlagsEditor from './ReferenceFlagsEditor';
import type { ReferenceFlags } from './referenceDetails';

afterEach(() => cleanup());

const flags: ReferenceFlags = {
  raw: 0x400,
  deleted: false,
  persistent: true,
  initiallyDisabled: false,
  respawns: null,
  names: [{ name: 'Persistent', bit: 10 }],
  options: [
    { name: 'Persistent', bit: 10, editable: false },
    { name: 'Initially disabled', bit: 11, editable: true },
    { name: 'Open by default', bit: 9, editable: true },
  ],
};

function editWith(patchChosen: boolean, result = true) {
  return {
    patchChosen,
    onEdit: vi.fn<(changes: CellEditChanges) => Promise<boolean>>(async () => result),
  };
}

describe('ReferenceFlagsEditor', () => {
  it('shows the names as read-only badges without a patch plugin', () => {
    render(<ReferenceFlagsEditor flags={flags} edit={editWith(false)} />);
    expect(screen.getByText('Persistent')).toBeTruthy();
    expect(screen.queryByRole('checkbox')).toBeNull();
    expect(screen.queryByRole('button', { name: 'Apply flags' })).toBeNull();
  });

  it('lists each option with its stored state and locks the fixed bits', () => {
    render(<ReferenceFlagsEditor flags={flags} edit={editWith(true)} />);
    expect(screen.getAllByRole('checkbox')).toHaveLength(3);
    const persistent = screen.getByLabelText('Persistent') as HTMLInputElement;
    expect(persistent.checked).toBe(true);
    expect(persistent.disabled).toBe(true);
    const disabled = screen.getByLabelText('Initially disabled') as HTMLInputElement;
    expect(disabled.checked).toBe(false);
    expect(disabled.disabled).toBe(false);
  });

  it('applies a toggled flag as the full raw value through onEdit', () => {
    const edit = editWith(true);
    render(<ReferenceFlagsEditor flags={flags} edit={edit} />);
    expect((screen.getByRole('button', { name: 'Apply flags' }) as HTMLButtonElement).disabled).toBe(true);
    fireEvent.click(screen.getByLabelText('Initially disabled'));
    fireEvent.click(screen.getByRole('button', { name: 'Apply flags' }));
    expect(edit.onEdit).toHaveBeenCalledTimes(1);
    expect(edit.onEdit).toHaveBeenCalledWith({ flags: 0xc00 });
  });

  it('accepts a raw hex value for the editable bits', () => {
    const edit = editWith(true);
    render(<ReferenceFlagsEditor flags={flags} edit={edit} />);
    fireEvent.change(screen.getByRole('textbox'), { target: { value: '0x600' } });
    expect((screen.getByLabelText('Open by default') as HTMLInputElement).checked).toBe(true);
    fireEvent.click(screen.getByRole('button', { name: 'Apply flags' }));
    expect(edit.onEdit).toHaveBeenCalledWith({ flags: 0x600 });
  });

  it('refuses a raw value that changes a fixed bit', () => {
    const edit = editWith(true);
    render(<ReferenceFlagsEditor flags={flags} edit={edit} />);
    fireEvent.change(screen.getByRole('textbox'), { target: { value: '0' } });
    expect(screen.getByText('Only the editable flags can change; the other bits are fixed.')).toBeTruthy();
    expect((screen.getByRole('button', { name: 'Apply flags' }) as HTMLButtonElement).disabled).toBe(true);
  });

  it('rejects text that is not hex and disables Apply', () => {
    const edit = editWith(true);
    render(<ReferenceFlagsEditor flags={flags} edit={edit} />);
    fireEvent.change(screen.getByRole('textbox'), { target: { value: 'zz' } });
    expect(screen.getByText('Raw flags must be hex, up to 8 digits.')).toBeTruthy();
    expect((screen.getByRole('button', { name: 'Apply flags' }) as HTMLButtonElement).disabled).toBe(true);
  });

  it('restores the stored checkbox when the edit is refused', async () => {
    const edit = editWith(true, false);
    render(<ReferenceFlagsEditor flags={flags} edit={edit} />);
    fireEvent.click(screen.getByLabelText('Initially disabled'));
    fireEvent.click(screen.getByRole('button', { name: 'Apply flags' }));
    await waitFor(() => expect((screen.getByLabelText('Initially disabled') as HTMLInputElement).checked).toBe(false));
    expect(edit.onEdit).toHaveBeenCalledWith({ flags: 0xc00 });
  });
});

describe('ReferenceFlagsEditor markers', () => {
  const plainFlags: ReferenceFlags = { ...flags, options: (flags.options ?? []).filter((option) => option.name !== 'Open by default') };
  it('shows the markers as read-only rows without a patch plugin', () => {
    render(<ReferenceFlagsEditor flags={flags} edit={editWith(false)} markers={{ openByDefault: true, ignoredBySandbox: false }} />);
    expect(screen.getByText('Open by default')).toBeTruthy();
    expect(screen.getByText('Yes')).toBeTruthy();
    expect(screen.queryByRole('checkbox')).toBeNull();
    expect(screen.getByText('Choose a patch plugin to edit')).toBeTruthy();
  });

  it('applies a marker change alone as its own key in the same edit', () => {
    const edit = editWith(true);
    render(<ReferenceFlagsEditor flags={plainFlags} edit={edit} markers={{ openByDefault: false, ignoredBySandbox: false }} />);
    fireEvent.click(screen.getByLabelText('Ignored by sandbox'));
    fireEvent.click(screen.getByRole('button', { name: 'Apply flags' }));
    expect(edit.onEdit).toHaveBeenCalledWith({ ignoredBySandbox: true });
  });

  it('sends the flag bits and a marker together when both change', () => {
    const edit = editWith(true);
    render(<ReferenceFlagsEditor flags={plainFlags} edit={edit} markers={{ openByDefault: false, ignoredBySandbox: false }} />);
    fireEvent.click(screen.getByLabelText('Initially disabled'));
    fireEvent.click(screen.getByLabelText('Open by default'));
    fireEvent.click(screen.getByRole('button', { name: 'Apply flags' }));
    expect(edit.onEdit).toHaveBeenCalledWith({ flags: 0xc00, openByDefault: true });
  });
});
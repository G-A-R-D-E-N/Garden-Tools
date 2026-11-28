import { cleanup, fireEvent, render, screen } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';
import type { CellEditChanges } from './backend';
import type { ReferenceDetails } from './referenceDetails';
import ReferenceLockEditor from './ReferenceLockEditor';

vi.mock('./RecordPicker', async () => {
  const { createElement } = await import('react');
  return {
    default: (props: { onPick: (formKey: string, plugin: string) => void }) =>
      createElement('button', { onClick: () => props.onPick('000820:Base.esm', 'Base.esm') }, 'Stub key picker'),
  };
});

afterEach(() => cleanup());

type Lock = ReferenceDetails['lock'];

function lockOf(level: number, key: string | null = null, flags = 0): Lock {
  return {
    level,
    levelName: null,
    key: key === null ? null : { formKey: key, editorId: 'Key', signature: 'KEYM' },
    flags,
  };
}

function editWith() {
  return {
    patchChosen: true,
    onEdit: vi.fn<(changes: CellEditChanges) => Promise<boolean>>(async () => true),
  };
}

const levelSelect = () => screen.getByRole('combobox') as HTMLSelectElement;
const applyButton = () => screen.getByRole('button', { name: 'Apply lock' }) as HTMLButtonElement;
const keyText = (container: HTMLElement) => container.querySelector('.ref-lock-key')?.textContent;

describe('ReferenceLockEditor', () => {
  it('starts at None with no lock and keeps Apply disabled', () => {
    const { container } = render(<ReferenceLockEditor lock={null} edit={editWith()} />);
    expect(levelSelect().value).toBe('0');
    expect(keyText(container)).toBe('None');
    expect(applyButton().disabled).toBe(true);
  });

  it('keeps a stored level that has no name selectable', () => {
    render(<ReferenceLockEditor lock={lockOf(7)} edit={editWith()} />);
    expect(levelSelect().value).toBe('7');
    expect(screen.getByRole('option', { name: 'Level 7' })).toBeTruthy();
  });

  it('sets a named level with no key', () => {
    const edit = editWith();
    render(<ReferenceLockEditor lock={null} edit={edit} />);
    fireEvent.change(levelSelect(), { target: { value: '254' } });
    fireEvent.click(applyButton());
    expect(edit.onEdit).toHaveBeenCalledWith({ lock: { level: 254, key: null, flags: 0 } });
  });

  it('removes the lock when None is chosen on a locked reference', () => {
    const edit = editWith();
    render(<ReferenceLockEditor lock={lockOf(50)} edit={edit} />);
    fireEvent.change(levelSelect(), { target: { value: '0' } });
    fireEvent.click(applyButton());
    expect(edit.onEdit).toHaveBeenCalledWith({ lock: null });
  });

  it('picks a key from the KEYM picker and applies it with the level', () => {
    const edit = editWith();
    const { container } = render(<ReferenceLockEditor lock={null} edit={edit} />);
    fireEvent.change(levelSelect(), { target: { value: '255' } });
    fireEvent.click(screen.getByRole('button', { name: 'Pick key' }));
    fireEvent.click(screen.getByRole('button', { name: 'Stub key picker' }));
    expect(keyText(container)).toBe('000820:Base.esm');
    fireEvent.click(applyButton());
    expect(edit.onEdit).toHaveBeenCalledWith({ lock: { level: 255, key: '000820:Base.esm', flags: 0 } });
  });

  it('clears a stored key', () => {
    const edit = editWith();
    render(<ReferenceLockEditor lock={lockOf(255, '000820:Base.esm')} edit={edit} />);
    fireEvent.click(screen.getByRole('button', { name: 'Clear key' }));
    fireEvent.click(applyButton());
    expect(edit.onEdit).toHaveBeenCalledWith({ lock: { level: 255, key: null, flags: 0 } });
  });

  it('applies the raw flags byte', () => {
    const edit = editWith();
    render(<ReferenceLockEditor lock={lockOf(50, null, 1)} edit={edit} />);
    fireEvent.change(screen.getByRole('textbox'), { target: { value: '4' } });
    fireEvent.click(applyButton());
    expect(edit.onEdit).toHaveBeenCalledWith({ lock: { level: 50, key: null, flags: 4 } });
  });

  it('rejects a flags byte above 255 and disables Apply', () => {
    render(<ReferenceLockEditor lock={lockOf(50, null, 1)} edit={editWith()} />);
    fireEvent.change(screen.getByRole('textbox'), { target: { value: '256' } });
    expect(screen.getByText('Lock flags must be a whole number from 0 to 255.')).toBeTruthy();
    expect(applyButton().disabled).toBe(true);
  });
});

import { cleanup, fireEvent, render, screen } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';
import type { CellEditChanges } from './backend';
import type { ReferenceDetails } from './referenceDetails';
import ReferencePrimitiveEditor from './ReferencePrimitiveEditor';

afterEach(() => cleanup());

const primitive: NonNullable<ReferenceDetails['primitive']> = {
  bounds: { x: 1, y: 2, z: 3 },
  color: [0, 0.5, 1, 0.4],
  type: 1,
};

function editWith(result = true) {
  return { patchChosen: true, onEdit: vi.fn<(changes: CellEditChanges) => Promise<boolean>>(async () => result) };
}

describe('ReferencePrimitiveEditor', () => {
  it('keeps the shape read-only and disables Apply until a value changes', () => {
    const edit = editWith();
    render(<ReferencePrimitiveEditor primitive={primitive} edit={edit} />);
    expect((screen.getByRole('button', { name: 'Apply primitive' }) as HTMLButtonElement).disabled).toBe(true);
    fireEvent.change(screen.getByLabelText('Half-extent X'), { target: { value: '5' } });
    fireEvent.click(screen.getByRole('button', { name: 'Apply primitive' }));
    expect(edit.onEdit).toHaveBeenCalledWith({ primitive: { bounds: { x: 5, y: 2, z: 3 } } });
  });

  it('sends a changed colour only and refuses a negative half-extent', () => {
    const edit = editWith();
    render(<ReferencePrimitiveEditor primitive={primitive} edit={edit} />);
    fireEvent.change(screen.getByLabelText('Colour R'), { target: { value: '1' } });
    fireEvent.click(screen.getByRole('button', { name: 'Apply primitive' }));
    expect(edit.onEdit).toHaveBeenCalledWith({ primitive: { color: [1, 0.5, 1, 0.4] } });
    fireEvent.change(screen.getByLabelText('Half-extent Y'), { target: { value: '-1' } });
    expect(screen.getByText('Half-extents must be non-negative numbers.')).toBeTruthy();
    expect((screen.getByRole('button', { name: 'Apply primitive' }) as HTMLButtonElement).disabled).toBe(true);
  });
});

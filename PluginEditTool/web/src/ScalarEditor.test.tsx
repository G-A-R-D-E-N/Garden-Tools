import { cleanup, fireEvent, render, screen } from '@testing-library/react';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import type { ScalarPresentation } from './backend';
import { ScalarCell, ScalarEditor, ScalarText } from './ScalarEditor';
import { flagIsSet, rawEditable, scalarLabel, setFlag } from './scalarPresentation';

const crimeArrest: ScalarPresentation = {
  kind: 'enum', names: [{ value: 0, name: 'False' }, { value: 1, name: 'True' }],
};
const flags: ScalarPresentation = {
  kind: 'flags', names: [{ bit: 0, name: 'Hidden From NPC' }, { bit: 1, name: 'Special Combat' }, { bit: 6, name: 'Track Crime' }],
};

const onValue = vi.fn<(next: string) => void>();
const onCommit = vi.fn<(next: string) => void>();
const onCancel = vi.fn<() => void>();

beforeEach(() => {
  onValue.mockClear();
  onCommit.mockClear();
  onCancel.mockClear();
});

afterEach(() => cleanup());

describe('scalarLabel', () => {
  it('shows the enum name, or <Unknown: N> for an unnamed value', () => {
    expect(scalarLabel('1', crimeArrest)).toBe('True');
    expect(scalarLabel('7', crimeArrest)).toBe('<Unknown: 7 $7>');
    expect(scalarLabel('-1', crimeArrest)).toBe('<Unknown: -1 $FFFFFFFFFFFFFFFF>');
    expect(scalarLabel('0', { kind: 'enum', names: [] })).toBe('<Unknown: 0>');
    expect(scalarLabel('1380273732', crimeArrest)).toBe('<Unknown: 1380273732 $52454E44 DNER>');
    expect(scalarLabel('1073741921', crimeArrest)).toBe('<Unknown: 1073741921 $40000061>');
  });

  it('joins the set flag names and gives each unknown bit its own <Unknown: bit>', () => {
    expect(scalarLabel('3', flags)).toBe('Hidden From NPC, Special Combat');
    expect(scalarLabel('131', flags)).toBe('Hidden From NPC, Special Combat, <Unknown: 7 $7>');
    expect(scalarLabel('168', flags)).toBe('<Unknown: 3 $3>, <Unknown: 5 $5>, <Unknown: 7 $7>');
    expect(scalarLabel('1', { kind: 'flags', names: [] })).toBe('<Unknown: 0>');
    expect(scalarLabel('0', flags)).toBe('none');
  });

  it('passes through raw text that is not an integer', () => {
    expect(scalarLabel('<opaque>', crimeArrest)).toBe('<opaque>');
    expect(scalarLabel('', flags)).toBe('');
  });

  it('names negative enum values and passes negative flag values through', () => {
    const signed: ScalarPresentation = { kind: 'enum', names: [{ value: -1, name: 'Minus' }] };
    expect(scalarLabel('-1', signed)).toBe('Minus');
    expect(scalarLabel('-1', flags)).toBe('-1');
  });
});

describe('flag bits', () => {
  it('sets and reads bit 31 without going negative', () => {
    expect(setFlag(0, 31, true)).toBe(2147483648);
    expect(flagIsSet(2147483648, 31)).toBe(true);
  });

  it('clears a flag bit, ignores a no-op change and reads bits above 31', () => {
    expect(setFlag(3, 0, false)).toBe(2);
    expect(setFlag(2, 0, false)).toBe(2);
    expect(setFlag(2, 0, true)).toBe(3);
    expect(setFlag(2147483649, 31, false)).toBe(1);
    expect(scalarLabel(String(2 ** 40), { kind: 'flags', names: [{ bit: 40, name: 'Big' }] })).toBe('Big');
  });
});

describe('rawEditable', () => {
  it('accepts signed integers for enums and unsigned for flags', () => {
    expect(rawEditable('5', crimeArrest)).toBe(true);
    expect(rawEditable('-5', crimeArrest)).toBe(true);
    expect(rawEditable('1.5', crimeArrest)).toBe(false);
    expect(rawEditable('', crimeArrest)).toBe(false);
    expect(rawEditable('0x10', crimeArrest)).toBe(false);
    expect(rawEditable(' 1', crimeArrest)).toBe(false);
    expect(rawEditable('5', flags)).toBe(true);
    expect(rawEditable('-5', flags)).toBe(false);
  });
});

describe('ScalarText', () => {
  it('shows the name with the raw number in the tooltip', () => {
    render(<ScalarText raw="1" presentation={crimeArrest} />);
    expect(screen.getByText('True').getAttribute('title')).toBe('1');
  });

  it('renders the raw text unchanged without a presentation', () => {
    render(<ScalarText raw="42" />);
    expect(screen.getByText('42').getAttribute('title')).toBeNull();
  });
});

describe('ScalarCell', () => {
  it('reports the integer value picked from an enum select', () => {
    render(<ScalarCell presentation={crimeArrest} value="1" disabled={false} onValue={onValue} />);
    fireEvent.change(screen.getByDisplayValue('True'), { target: { value: '0' } });
    expect(onValue).toHaveBeenCalledWith('0');
  });

  it('reports the new integer when a flag checkbox is toggled', () => {
    render(<ScalarCell presentation={flags} value="1" disabled={false} onValue={onValue} />);
    fireEvent.click(screen.getByLabelText('Special Combat'));
    expect(onValue).toHaveBeenCalledWith('3');
  });
});

describe('ScalarEditor', () => {
  it('commits the picked enum integer at once', () => {
    render(<ScalarEditor presentation={crimeArrest} value="1" onValue={onValue} onCommit={onCommit} onCancel={onCancel} />);
    fireEvent.change(screen.getByDisplayValue('True'), { target: { value: '0' } });
    expect(onCommit).toHaveBeenCalledWith('0');
  });

  it('saves the flag integer on Save and keeps unknown bits', () => {
    render(<ScalarEditor presentation={flags} value="131" onValue={onValue} onCommit={onCommit} onCancel={onCancel} />);
    fireEvent.click(screen.getByLabelText('Special Combat'));
    expect(onValue).toHaveBeenCalledWith('129');
    fireEvent.click(screen.getByRole('button', { name: 'Save' }));
    expect(onCommit).toHaveBeenCalledWith('131');
  });

  it('cancels a flag edit without committing', () => {
    render(<ScalarEditor presentation={flags} value="3" onValue={onValue} onCommit={onCommit} onCancel={onCancel} />);
    fireEvent.click(screen.getByRole('button', { name: 'Cancel' }));
    expect(onCancel).toHaveBeenCalledOnce();
    expect(onCommit).not.toHaveBeenCalled();
  });

  it.each([
    ['flag', flags, '3', () => screen.getByLabelText('Special Combat')],
    ['enum', crimeArrest, '1', () => screen.getByDisplayValue('True')],
  ])('cancels a %s edit on Escape once', (_kind, presentation, value, target) => {
    render(<ScalarEditor presentation={presentation} value={value} onValue={onValue} onCommit={onCommit} onCancel={onCancel} />);
    fireEvent.keyDown(target(), { key: 'Escape' });
    expect(onCancel).toHaveBeenCalledOnce();
  });

  it('disables flag checkboxes when the raw value is not an integer', () => {
    render(<ScalarEditor presentation={flags} value="<opaque>" onValue={onValue} onCommit={onCommit} onCancel={onCancel} />);
    expect((screen.getByLabelText('Special Combat') as HTMLInputElement).disabled).toBe(true);
  });
});

import { act, cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';
import CellWorldPanel from './CellWorldPanel';
import type { WorldPropertiesResult, WorldPropertyDescriptor } from './backend';

const WORLD = '00003C:Fallout4.esm';

const SCALAR: WorldPropertyDescriptor = {
  name: 'WaterHeight', label: 'Water height', kind: 'scalar', minimum: -3.4e38, maximum: 3.4e38, targets: [],
};
const COLOUR: WorldPropertyDescriptor = {
  name: 'LightingAmbient', label: 'Ambient color', kind: 'colour', minimum: 0, maximum: 255, targets: [],
};
const CLIMATE: WorldPropertyDescriptor = {
  name: 'Climate', label: 'Climate', kind: 'link', minimum: 0, maximum: 0, targets: ['CLMT'],
};

const properties = (overrides: Partial<WorldPropertiesResult> = {}): WorldPropertiesResult => ({
  formKey: WORLD,
  descriptors: [SCALAR, COLOUR, CLIMATE],
  values: { WaterHeight: 12.5, LightingAmbient: [10, 20, 30], Climate: '000A11:Fallout4.esm' },
  ...overrides,
});

const OTHER_WORLD = properties({
  formKey: '00003D:Fallout4.esm',
  values: { WaterHeight: 40, LightingAmbient: [1, 2, 3], Climate: null },
});

const field = (label: string) => screen.getByLabelText(label) as HTMLInputElement;
const waterHeight = () => field('World Water height');
const climate = () => field('World Climate');
const ambient = () => field('World Ambient color');
const warning = () => document.querySelector('.papyrus-warn');

function type(label: string, value: string) {
  fireEvent.change(field(label), { target: { value } });
}

function pressEnter(label: string) {
  fireEvent.keyDown(field(label), { key: 'Enter' });
}

type Waiting = { resolve: (failure: string | null) => void; reject: (reason: unknown) => void };

function gatedEdit() {
  const waiting: Waiting[] = [];
  const onEdit = vi.fn<(changes: Record<string, unknown>) => Promise<string | null>>(() => new Promise<string | null>((resolve, reject) => {
    waiting.push({ resolve, reject });
  }));
  const settle = async (failure: string | null = null) => {
    const call = waiting.shift();
    if (!call) throw new Error('no edit is waiting');
    await act(async () => { call.resolve(failure); });
  };
  const fail = async (reason: unknown) => {
    const call = waiting.shift();
    if (!call) throw new Error('no edit is waiting');
    await act(async () => { call.reject(reason); });
  };
  return { onEdit, settle, fail };
}

afterEach(() => { cleanup(); vi.restoreAllMocks(); });

describe('Cell Worldspace property panel', () => {
  it('renders a numeric scalar and flags as number fields, everything else as text', () => {
    render(<CellWorldPanel properties={properties()} onEdit={vi.fn(async () => null)} />);
    expect(waterHeight().type).toBe('number');
    expect(waterHeight().value).toBe('12.5');
    expect(waterHeight().getAttribute('step')).toBe('any');
    expect(climate().type).toBe('text');
    expect(climate().value).toBe('000A11:Fallout4.esm');
    expect(ambient().type).toBe('text');
    expect(ambient().value).toBe('10 20 30');
  });

  it('shows the record types a link field accepts in its title and a plain field keeps none', () => {
    render(<CellWorldPanel properties={properties()} onEdit={vi.fn(async () => null)} />);
    expect(climate().parentElement?.getAttribute('title')).toBe('Accepts CLMT');
    expect(waterHeight().parentElement?.getAttribute('title')).toBeNull();
    expect(screen.getByLabelText('World Water height')).toBeTruthy();
    expect(warning()).toBeNull();
  });

  it('sends one change on Enter and another on blur', async () => {
    const onEdit = vi.fn(async () => null);
    render(<CellWorldPanel properties={properties()} onEdit={onEdit} />);
    type('World Water height', '20');
    pressEnter('World Water height');
    await waitFor(() => expect(onEdit).toHaveBeenCalledWith({ WaterHeight: 20 }));
    type('World Climate', '000A22:Fallout4.esm');
    fireEvent.blur(climate());
    await waitFor(() => expect(onEdit).toHaveBeenLastCalledWith({ Climate: '000A22:Fallout4.esm' }));
    expect(onEdit).toHaveBeenCalledTimes(2);
  });

  it('an untouched field commits nothing on Enter or blur', () => {
    const onEdit = vi.fn(async () => null);
    render(<CellWorldPanel properties={properties()} onEdit={onEdit} />);
    pressEnter('World Water height');
    fireEvent.blur(waterHeight());
    pressEnter('World Ambient color');
    fireEvent.blur(ambient());
    pressEnter('World Climate');
    fireEvent.blur(climate());
    expect(onEdit).not.toHaveBeenCalled();
    expect(warning()).toBeNull();
  });

  it('refuses a non-numeric field without calling onEdit', () => {
    const onEdit = vi.fn(async () => null);
    render(<CellWorldPanel properties={properties()} onEdit={onEdit} />);
    type('World Ambient color', '10 20 x');
    pressEnter('World Ambient color');
    expect(screen.getByText('Enter numbers only')).toBeTruthy();
    expect(onEdit).not.toHaveBeenCalled();
  });

  it('a returned failure message reverts the field and is displayed', async () => {
    const onEdit = vi.fn(async () => 'No patch plugin chosen.');
    render(<CellWorldPanel properties={properties()} onEdit={onEdit} />);
    type('World Water height', '20');
    pressEnter('World Water height');
    expect(await screen.findByText('No patch plugin chosen.')).toBeTruthy();
    expect(waterHeight().value).toBe('12.5');
    expect(onEdit).toHaveBeenCalledWith({ WaterHeight: 20 });
  });

  it('a rejected edit reverts the field and shows the error message', async () => {
    const onEdit = vi.fn(() => Promise.reject(new Error('SetWorldProperties failed')));
    render(<CellWorldPanel properties={properties()} onEdit={onEdit} />);
    type('World Climate', '000A22:Fallout4.esm');
    fireEvent.blur(climate());
    expect(await screen.findByText('SetWorldProperties failed')).toBeTruthy();
    expect(climate().value).toBe('000A11:Fallout4.esm');
  });

  it('a rejection that is not an Error is shown as text and reverts the field', async () => {
    const onEdit = vi.fn(() => Promise.reject('backend unavailable'));
    render(<CellWorldPanel properties={properties()} onEdit={onEdit} />);
    type('World Ambient color', '1 2 3');
    pressEnter('World Ambient color');
    expect(await screen.findByText('backend unavailable')).toBeTruthy();
    expect(ambient().value).toBe('10 20 30');
  });

  it('a second commit of the same field is dropped while the first is in flight', async () => {
    const edit = gatedEdit();
    render(<CellWorldPanel properties={properties()} onEdit={edit.onEdit} />);
    type('World Water height', '20');
    pressEnter('World Water height');
    type('World Water height', '30');
    pressEnter('World Water height');
    await waitFor(() => expect(edit.onEdit).toHaveBeenCalledTimes(1));
    await edit.settle();
    expect(edit.onEdit).toHaveBeenCalledWith({ WaterHeight: 20 });
  });

  it('switching worldspaces ignores a late failure from the old one', async () => {
    const edit = gatedEdit();
    const { rerender } = render(<CellWorldPanel properties={properties()} onEdit={edit.onEdit} />);
    type('World Water height', '20');
    pressEnter('World Water height');
    await waitFor(() => expect(edit.onEdit).toHaveBeenCalledTimes(1));
    rerender(<CellWorldPanel properties={OTHER_WORLD} onEdit={edit.onEdit} />);
    await edit.settle('the previous worldspace refused the edit');
    expect(screen.queryByText('the previous worldspace refused the edit')).toBeNull();
    expect(warning()).toBeNull();
    expect(field('World Water height').value).toBe('40');
  });

  it('switching worldspaces ignores a late rejection from the old one and the new one still reports', async () => {
    const edit = gatedEdit();
    const { rerender } = render(<CellWorldPanel properties={properties()} onEdit={edit.onEdit} />);
    type('World Climate', '000A22:Fallout4.esm');
    fireEvent.blur(climate());
    await waitFor(() => expect(edit.onEdit).toHaveBeenCalledTimes(1));
    rerender(<CellWorldPanel properties={OTHER_WORLD} onEdit={edit.onEdit} />);
    await edit.fail(new Error('old worldspace failed'));
    expect(screen.queryByText('old worldspace failed')).toBeNull();
    type('World Water height', '41');
    pressEnter('World Water height');
    await waitFor(() => expect(edit.onEdit).toHaveBeenCalledTimes(2));
    await edit.settle('SetWorldProperties failed');
    expect(await screen.findByText('SetWorldProperties failed')).toBeTruthy();
    expect(field('World Water height').value).toBe('40');
  });

  it('a worldspace reload clears the error left by the previous worldspace', async () => {
    const onEdit = vi.fn(async () => 'Nothing was saved.');
    const { rerender } = render(<CellWorldPanel properties={properties()} onEdit={onEdit} />);
    type('World Water height', '20');
    pressEnter('World Water height');
    expect(await screen.findByText('Nothing was saved.')).toBeTruthy();
    rerender(<CellWorldPanel properties={OTHER_WORLD} onEdit={onEdit} />);
    expect(screen.queryByText('Nothing was saved.')).toBeNull();
    expect(warning()).toBeNull();
    expect(field('World Water height').value).toBe('40');
  });

  it('a link field cleared to an empty text sends null rather than an empty string', async () => {
    const onEdit = vi.fn(async () => null);
    render(<CellWorldPanel properties={properties()} onEdit={onEdit} />);
    type('World Climate', '');
    fireEvent.blur(climate());
    await waitFor(() => expect(onEdit).toHaveBeenCalledWith({ Climate: null }));
  });

  it('switching A -> B -> A with identical empty field resets dirty text and does not resurrect old errors', async () => {
    const onEdit = vi.fn(async () => 'Nothing was saved.');
    const worldA = properties({ values: { WaterHeight: 12.5, LightingAmbient: [10, 20, 30], Climate: null } });
    const worldB = properties({ formKey: '00003D:Fallout4.esm', values: { WaterHeight: 40, LightingAmbient: [1, 2, 3], Climate: null } });
    const { rerender } = render(<CellWorldPanel properties={worldA} onEdit={onEdit} />);
    type('World Water height', '20');
    pressEnter('World Water height');
    expect(await screen.findByText('Nothing was saved.')).toBeTruthy();
    type('World Climate', 'bad-link');

    rerender(<CellWorldPanel properties={worldB} onEdit={onEdit} />);
    expect(climate().value).toBe('');
    expect(warning()).toBeNull();

    rerender(<CellWorldPanel properties={worldA} onEdit={onEdit} />);
    expect(warning()).toBeNull();
    expect(climate().value).toBe('');
  });

  it('a same-worldspace object refresh during an in-flight failing edit still reverts and reports the error', async () => {
    const edit = gatedEdit();
    const initial = properties();
    const { rerender } = render(<CellWorldPanel properties={initial} onEdit={edit.onEdit} />);
    type('World Water height', '20');
    pressEnter('World Water height');
    await waitFor(() => expect(edit.onEdit).toHaveBeenCalledTimes(1));

    rerender(<CellWorldPanel properties={{ ...initial }} onEdit={edit.onEdit} />);
    await edit.settle('No patch plugin chosen.');
    expect(await screen.findByText('No patch plugin chosen.')).toBeTruthy();
    expect(waterHeight().value).toBe('12.5');
  });
});

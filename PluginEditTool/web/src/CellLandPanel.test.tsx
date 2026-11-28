import { act, cleanup, fireEvent, render, screen } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';
import CellLandPanel from './CellLandPanel';
import type { CellLandResult } from './backend';

const LAND: CellLandResult = {
  cellFormKey: '00ABCD:Fallout4.esm',
  landFormKey: '000800:Patch.esp',
  width: 3,
  height: 2,
  offset: 512,
  heights: [0, 1.5, 2, -3, 4.25, 5],
};

const land = (overrides: Partial<CellLandResult> = {}): CellLandResult => ({ ...LAND, ...overrides });

const OTHER: CellLandResult = land({
  cellFormKey: '00ABCE:Fallout4.esm',
  landFormKey: '000801:Patch.esp',
  heights: [1, 2, 3, 4, 5, 6],
});

const heightsBox = () => screen.getByLabelText('Heights') as HTMLTextAreaElement;
const saveButton = () => screen.getByRole('button', { name: 'Save terrain' }) as HTMLButtonElement;
const offsetBox = () => screen.getByLabelText('Height offset') as HTMLInputElement;
const gridBox = () => screen.getByLabelText('Grid') as HTMLInputElement;
const warning = () => document.querySelector('.papyrus-warn');

async function commitHeights(text: string) {
  fireEvent.change(heightsBox(), { target: { value: text } });
  await act(async () => { fireEvent.click(saveButton()); });
}

type Waiting = { resolve: (failure: string | null) => void; reject: (reason: unknown) => void };

function gatedSave() {
  const waiting: Waiting[] = [];
  const onSave = vi.fn<(heights: number[]) => Promise<string | null>>(() => new Promise<string | null>((resolve, reject) => {
    waiting.push({ resolve, reject });
  }));
  const settle = async (failure: string | null = null) => {
    const call = waiting.shift();
    if (!call) throw new Error('no save is waiting');
    await act(async () => { call.resolve(failure); });
  };
  const fail = async (reason: unknown) => {
    const call = waiting.shift();
    if (!call) throw new Error('no save is waiting');
    await act(async () => { call.reject(reason); });
  };
  return { onSave, settle, fail };
}

afterEach(() => { cleanup(); vi.restoreAllMocks(); });

describe('Cell Land terrain editing', () => {
  it('shows the stored offset, grid and heights one row per line', () => {
    render(<CellLandPanel land={land()} onSave={vi.fn(async () => null)} />);
    expect(offsetBox().value).toBe('512');
    expect(gridBox().value).toBe('3 x 2');
    expect(heightsBox().value).toBe('0 1.5 2\n-3 4.25 5');
  });

  it('formats heights to three decimals without trailing zeros and keeps one row per line', () => {
    render(<CellLandPanel land={land({ width: 1, height: 2, heights: [0.123456, 2.0004] })} onSave={vi.fn(async () => null)} />);
    expect(gridBox().value).toBe('1 x 2');
    expect(heightsBox().value).toBe('0.123\n2');
  });

  it('sends the parsed heights, accepting any whitespace or comma between them', async () => {
    const onSave = vi.fn(async () => null);
    render(<CellLandPanel land={land()} onSave={onSave} />);
    await commitHeights('10 11 12\n13,14, 15');
    expect(onSave).toHaveBeenCalledTimes(1);
    expect(onSave).toHaveBeenCalledWith([10, 11, 12, 13, 14, 15]);
    expect(saveButton().disabled).toBe(false);
    expect(warning()).toBeNull();
  });

  it('shows a returned failure message and keeps the heights the user typed', async () => {
    const onSave = vi.fn(async () => 'TerrainInvalid: heights out of range');
    render(<CellLandPanel land={land()} onSave={onSave} />);
    await commitHeights('10 11 12 13 14 15');
    expect(await screen.findByText('TerrainInvalid: heights out of range')).toBeTruthy();
    expect(heightsBox().value).toBe('10 11 12 13 14 15');
    expect(saveButton().disabled).toBe(false);
  });

  it('refuses non-numeric heights without calling onSave', async () => {
    const onSave = vi.fn(async () => null);
    render(<CellLandPanel land={land()} onSave={onSave} />);
    await commitHeights('0 1 2\n3 4 five');
    expect(screen.getByText('Terrain heights must be numbers, one row of the grid per line')).toBeTruthy();
    expect(onSave).not.toHaveBeenCalled();
    expect(saveButton().disabled).toBe(false);
  });

  it('refuses an empty grid and the wrong number of heights, naming both counts', async () => {
    const onSave = vi.fn(async () => null);
    render(<CellLandPanel land={land()} onSave={onSave} />);
    await commitHeights('   ');
    expect(screen.getByText('Terrain heights must be numbers, one row of the grid per line')).toBeTruthy();
    await commitHeights('0 1 2 3 4');
    expect(screen.getByText('Terrain needs 6 heights, not 5')).toBeTruthy();
    expect(onSave).not.toHaveBeenCalled();
  });

  it('surfaces a rejected save as an error and clears saving', async () => {
    const onSave = vi.fn(() => Promise.reject(new Error('SetCellLand failed')));
    render(<CellLandPanel land={land()} onSave={onSave} />);
    await commitHeights('10 11 12 13 14 15');
    expect(await screen.findByText('SetCellLand failed')).toBeTruthy();
    expect(saveButton().disabled).toBe(false);
  });

  it('surfaces a non-Error rejection as text and clears saving', async () => {
    const onSave = vi.fn(() => Promise.reject('backend unavailable'));
    render(<CellLandPanel land={land()} onSave={onSave} />);
    await commitHeights('10 11 12 13 14 15');
    expect(await screen.findByText('backend unavailable')).toBeTruthy();
    expect(saveButton().disabled).toBe(false);
  });

  it('switching cells ignores a save that is still in flight and re-enables Save', async () => {
    const save = gatedSave();
    const { rerender } = render(<CellLandPanel land={land()} onSave={save.onSave} />);
    await commitHeights('10 11 12 13 14 15');
    expect(saveButton().disabled).toBe(true);
    rerender(<CellLandPanel land={OTHER} onSave={save.onSave} />);
    expect(saveButton().disabled).toBe(false);
    expect(heightsBox().value).toBe('1 2 3\n4 5 6');
    await save.settle('Terrain for the old cell was refused');
    expect(screen.queryByText('Terrain for the old cell was refused')).toBeNull();
    expect(saveButton().disabled).toBe(false);
    expect(heightsBox().value).toBe('1 2 3\n4 5 6');
  });

  it('a stale rejection after switching cells is not shown and does not escape', async () => {
    const save = gatedSave();
    const { rerender } = render(<CellLandPanel land={land()} onSave={save.onSave} />);
    await commitHeights('10 11 12 13 14 15');
    rerender(<CellLandPanel land={OTHER} onSave={save.onSave} />);
    await save.fail(new Error('old cell failed'));
    expect(screen.queryByText('old cell failed')).toBeNull();
    expect(saveButton().disabled).toBe(false);
  });

  it('switching cells clears a validation error left by the previous cell', async () => {
    const onSave = vi.fn(async () => null);
    const { rerender } = render(<CellLandPanel land={land()} onSave={onSave} />);
    await commitHeights('0 1 2');
    expect(screen.getByText('Terrain needs 6 heights, not 3')).toBeTruthy();
    rerender(<CellLandPanel land={OTHER} onSave={onSave} />);
    expect(screen.queryByText('Terrain needs 6 heights, not 3')).toBeNull();
    expect(warning()).toBeNull();
    expect(heightsBox().value).toBe('1 2 3\n4 5 6');
  });

  it('a save started on the new cell still reports its own failure after an older save settles', async () => {
    const save = gatedSave();
    const { rerender } = render(<CellLandPanel land={land()} onSave={save.onSave} />);
    await commitHeights('10 11 12 13 14 15');
    rerender(<CellLandPanel land={OTHER} onSave={save.onSave} />);
    await commitHeights('6 5 4 3 2 1');
    await save.settle('stale failure');
    expect(screen.queryByText('stale failure')).toBeNull();
    await save.settle('current failure');
    expect(await screen.findByText('current failure')).toBeTruthy();
    expect(saveButton().disabled).toBe(false);
  });

  it('switching A -> B -> A mid-save does not deadlock Save or resurrect old errors, and resets identical heights across cells', async () => {
    const save = gatedSave();
    const cellA = land();
    const sameHeightsOther = land({ cellFormKey: '00ABCE:Fallout4.esm', landFormKey: '000801:Patch.esp' });
    const { rerender } = render(<CellLandPanel land={cellA} onSave={save.onSave} />);
    await commitHeights('9 9 9 9 9 9');
    expect(saveButton().disabled).toBe(true);

    rerender(<CellLandPanel land={sameHeightsOther} onSave={save.onSave} />);
    expect(saveButton().disabled).toBe(false);
    expect(heightsBox().value).toBe('0 1.5 2\n-3 4.25 5');

    rerender(<CellLandPanel land={cellA} onSave={save.onSave} />);
    expect(saveButton().disabled).toBe(false);
    expect(heightsBox().value).toBe('0 1.5 2\n-3 4.25 5');

    await save.settle('old cell failure');
    expect(screen.queryByText('old cell failure')).toBeNull();
    expect(saveButton().disabled).toBe(false);

    rerender(<CellLandPanel land={{ ...cellA }} onSave={save.onSave} />);
    await commitHeights('8 8 8 8 8 8');
    expect(save.onSave).toHaveBeenCalledTimes(2);
    await save.settle('refreshed cell failure');
    expect(await screen.findByText('refreshed cell failure')).toBeTruthy();
  });

  it('rerendering a fresh land object for the same cell mid-save keeps Save disabled and shows the failure', async () => {
    const save = gatedSave();
    const cellA = land();
    const { rerender } = render(<CellLandPanel land={cellA} onSave={save.onSave} />);
    await commitHeights('9 9 9 9 9 9');
    expect(saveButton().disabled).toBe(true);

    rerender(<CellLandPanel land={{ ...cellA }} onSave={save.onSave} />);
    expect(saveButton().disabled).toBe(true);

    await save.settle('Terrain for cell refused');
    expect(await screen.findByText('Terrain for cell refused')).toBeTruthy();
    expect(saveButton().disabled).toBe(false);
  });
});

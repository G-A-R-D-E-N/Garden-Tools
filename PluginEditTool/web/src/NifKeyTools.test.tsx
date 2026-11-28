import { cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';
import type { NifHost } from './backend';
import NifKeyTools from './NifKeyTools';
import {
  channelsFor, keyCount, keyValueLabels, parseKeys, parseSetKeysReport, resizeKeys,
} from './keyToolsClient';

const NIF = 'C:\\mods\\anim.nif';

const savedText = `set_keys OK (in-process)\nSaved: ${NIF}\nChannel: translation\nKeys: 2`;

const makeHost = (overrides: Partial<NifHost> = {}) => ({
  BlockOp: vi.fn(async () => savedText),
  ...overrides,
} as unknown as NifHost);

const blocks = [
  { id: 4, type: 'NiTransformData', name: '' },
  { id: 6, type: 'NiFloatData', name: 'Alpha' },
];

const mount = (host: NifHost, onSaved = vi.fn(), appendLog = vi.fn()) => {
  render(
    <NifKeyTools nif={host} nifPath={NIF} blocks={blocks} disabled={false} expectedSource="stamp-2"
                 appendLog={appendLog} onSaved={onSaved} />,
  );
  return { onSaved, appendLog };
};

const type = (label: string, value: string) =>
  fireEvent.change(screen.getByLabelText(label), { target: { value } });
const button = (label: string) => screen.getByText(label) as HTMLButtonElement;

afterEach(cleanup);

describe('keyToolsClient', () => {
  // Invariant: fails when a key size drifts from NifService.cpp:keyNumbers, which makes every Apply refused by the engine.
  it('counts the numbers of one key like the native op', () => {
    expect(keyCount(1, 3, false)).toBe(3);
    expect(keyCount(5, 1, false)).toBe(1);
    expect(keyCount(2, 3, false)).toBe(9);
    expect(keyCount(2, 1, false)).toBe(3);
    expect(keyCount(2, 4, true)).toBe(4);
    expect(keyCount(3, 3, false)).toBe(6);
    expect(keyCount(3, 4, true)).toBe(7);
    const [translation, quaternion] = channelsFor('NiTransformData');
    expect(keyValueLabels(translation, 2)).toHaveLength(9);
    expect(keyValueLabels(quaternion, 3)).toEqual(['w', 'x', 'y', 'z', 'tension', 'continuity', 'bias']);
    expect(channelsFor('NiBoolData')).toEqual([]);
    expect(channelsFor('NiPosData').map(c => c.id)).toEqual(['data']);
  });

  // Invariant: fails when a blank, non-finite or decreasing-time key reaches the host as a valid key.
  it('accepts only complete keys with finite numbers and non-decreasing times', () => {
    expect(parseKeys([{ time: '0', value: ['1', '-2'] }, { time: '0.5', value: ['3', '4'] }])).toEqual([
      { time: 0, value: [1, -2] }, { time: 0.5, value: [3, 4] }]);
    expect(parseKeys([])).toBeNull();
    expect(parseKeys([{ time: '0', value: [''] }])).toBeNull();
    expect(parseKeys([{ time: '0', value: ['Infinity'] }])).toBeNull();
    expect(parseKeys([{ time: '1', value: ['0'] }, { time: '0.5', value: ['0'] }])).toBeNull();
    expect(resizeKeys([{ time: '2', value: ['7'] }], 3)).toEqual([{ time: '2', value: ['7', '0', '0'] }]);
  });

  it('reads a set_keys report and a refusal', () => {
    const report = parseSetKeysReport(savedText);
    expect(report).toMatchObject({ ok: true, saved: NIF, channel: 'translation', keys: 2 });
    const refused = parseSetKeysReport('Error: key 0 needs 4 numbers for interpolation 1 on rotation_quat');
    expect(refused.ok).toBe(false);
    expect(refused.refusal).toContain('needs 4 numbers');
  });
});

describe('NifKeyTools panel', () => {
  // Invariant: fails when the panel sends the wrong channel, key size or source stamp, or saves without a reload.
  it('edits a translation channel and writes it in place with the source stamp', async () => {
    const host = makeHost();
    const { onSaved } = mount(host);

    expect(button('Apply keys').disabled).toBe(true);
    fireEvent.change(screen.getByLabelText('Key tools block'), { target: { value: '4' } });
    expect((screen.getByLabelText('Key tools channel') as HTMLSelectElement).value).toBe('translation');
    type('Key 0 time', '0');
    type('Key 0 x', '1');
    type('Key 0 y', '2');
    type('Key 0 z', '3');
    fireEvent.click(button('Add key'));
    type('Key 1 time', '0.5');
    type('Key 1 x', '4');
    type('Key 1 y', '5');
    type('Key 1 z', '-6');
    fireEvent.click(button('Apply keys'));

    await waitFor(() => expect(onSaved).toHaveBeenCalledWith(NIF));
    expect(host.BlockOp).toHaveBeenCalledWith(NIF, 'set_keys', 4, null, '', null, '', 'stamp-2', false, null,
      '', '', 'translation', 1, [{ time: 0, value: [1, 2, 3] }, { time: 0.5, value: [4, 5, -6] }]);
  });

  // Invariant: fails when changing the interpolation leaves keys with the old size or Apply stays enabled on a decreasing time.
  it('resizes the keys for the interpolation and blocks decreasing times', () => {
    mount(makeHost());
    fireEvent.change(screen.getByLabelText('Key tools block'), { target: { value: '4' } });
    fireEvent.change(screen.getByLabelText('Key tools channel'), { target: { value: 'rotation_quat' } });
    expect(screen.getByLabelText('Key 0 w')).toBeTruthy();
    fireEvent.change(screen.getByLabelText('Key tools interpolation'), { target: { value: '3' } });
    expect(screen.getByLabelText('Key 0 tension')).toBeTruthy();
    expect(screen.getByLabelText('Key 0 continuity')).toBeTruthy();
    fireEvent.change(screen.getByLabelText('Key tools interpolation'), { target: { value: '2' } });
    expect(screen.queryByLabelText('Key 0 tension')).toBeNull();
    expect(screen.queryByLabelText('Key 0 fwd w')).toBeNull();

    fireEvent.change(screen.getByLabelText('Key tools channel'), { target: { value: 'translation' } });
    expect(screen.getByLabelText('Key 0 bwd z')).toBeTruthy();
    fireEvent.click(button('Add key'));
    type('Key 0 time', '1');
    type('Key 1 time', '0.5');
    expect(button('Apply keys').disabled).toBe(true);
    type('Key 1 time', '1');
    expect(button('Apply keys').disabled).toBe(false);
  });

  // Invariant: fails when an engine refusal is shown as a save or reloads the editor.
  it('logs the engine refusal without reporting a save', async () => {
    const host = makeHost({
      BlockOp: vi.fn(async () => 'Error: block 6 uses a channel that does not exist') } as Partial<NifHost>);
    const { onSaved, appendLog } = mount(host);
    fireEvent.change(screen.getByLabelText('Key tools block'), { target: { value: '6' } });
    expect(screen.getByLabelText('Key 0 value')).toBeTruthy();
    fireEvent.click(button('Apply keys'));
    await waitFor(() => expect(appendLog.mock.calls.some(c => String(c[0]).includes('set keys refused'))).toBe(true));
    expect(appendLog.mock.calls.some(c => String(c[0]).includes('does not exist'))).toBe(true);
    expect(onSaved).not.toHaveBeenCalled();
    expect(host.BlockOp).toHaveBeenCalledWith(NIF, 'set_keys', 6, null, '', null, '', 'stamp-2', false, null,
      '', '', 'data', 1, [{ time: 0, value: [0] }]);
  });
});

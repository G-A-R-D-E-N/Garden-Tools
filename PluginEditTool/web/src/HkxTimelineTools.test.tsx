import { cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';
import type { HkxHost } from './backend';
import HkxTimelineTools from './HkxTimelineTools';
import { OP_FIELDS } from './hkxTimelineClient';

const reply = (saved: boolean) => ({
  source: 'abc123', ops: 1, bytes: 100, saved, frame_count: 3, duration: 2, track_count: 2, motion: 'default',
  annotation_tracks: [{ name: 'Root', annotations: 1 }],
  report: { annotations_dropped: 2, resampled: false, position_error: 0, rotation_error: 0, scale_error: 0 },
});

afterEach(() => cleanup());

describe('HkxTimelineTools', () => {
  it('previews first, then saves against the previewed source hash', async () => {
    // Constraint: detects a save that skips the preview or ignores the file changing after it.
    const ClipEdit = vi.fn(async (_p: string, _o: unknown[], out: string) => JSON.stringify(reply(out !== '')));
    render(<HkxTimelineTools host={{ ClipEdit } as unknown as HkxHost} initialPath="C:/clip.hkx" disabled={false} />);
    expect((screen.getByText('Save edit') as HTMLButtonElement).disabled).toBe(true);
    fireEvent.change(screen.getByLabelText('Output path'), { target: { value: 'C:/out.hkx' } });
    fireEvent.click(screen.getByText('Preview edit'));
    await waitFor(() => expect(screen.getByRole('status').textContent).toContain('Preview: 3 frames'));
    expect(screen.getByRole('status').textContent).toContain('2 dropped');
    expect(ClipEdit.mock.calls[0][2]).toBe('');
    fireEvent.click(screen.getByText('Save edit'));
    await waitFor(() => expect(screen.getByRole('status').textContent).toContain('Saved: 3 frames'));
    expect(ClipEdit.mock.calls[1]).toEqual(['C:/clip.hkx', '[{"op":"trim","first":0,"last":1}]', 'C:/out.hkx',
      'abc123', false, null]);
    fireEvent.click(screen.getByText('Preview edit'));
    const saveButton = () => screen.getByText('Save edit') as HTMLButtonElement;
    await waitFor(() => expect(saveButton().disabled).toBe(false));
    fireEvent.click(screen.getByLabelText('Replace an existing file'));
    expect(saveButton().disabled).toBe(true);
    fireEvent.click(screen.getByText('Preview edit'));
    await waitFor(() => expect(saveButton().disabled).toBe(false));
    fireEvent.change(screen.getByLabelText('Clip storage'), { target: { value: 'spline' } });
    expect(saveButton().disabled).toBe(true);
  });

  it('shows the host refusal and refuses malformed ops before calling the host', async () => {
    // Constraint: detects a refusal shown as a successful edit and a typo sent to the host.
    const ClipEdit = vi.fn(async () => JSON.stringify({ error: 'op 0 refused: BadRange' }));
    render(<HkxTimelineTools host={{ ClipEdit } as unknown as HkxHost} initialPath="C:/clip.hkx" disabled={false} />);
    fireEvent.click(screen.getByText('Preview edit'));
    await waitFor(() => expect(screen.getByRole('alert').textContent).toContain('BadRange'));
    fireEvent.change(screen.getByLabelText('Edit ops'), { target: { value: '[oops' } });
    fireEvent.click(screen.getByText('Preview edit'));
    await waitFor(() => expect(screen.getByRole('alert').textContent).toContain('not valid JSON'));
    expect(ClipEdit).toHaveBeenCalledTimes(1);
  });

  it('says the host build cannot edit a clip instead of showing an unchanged one', async () => {
    // Constraint: detects an older host shown as a successful no-op edit.
    render(<HkxTimelineTools host={{} as HkxHost} initialPath="C:/clip.hkx" disabled={false} />);
    fireEvent.click(screen.getByText('Preview edit'));
    await waitFor(() => expect(screen.getByRole('alert').textContent).toContain('cannot edit an animation clip'));
  });
});

describe('HkxTimelineTools op form', () => {
  const opsText = () => (screen.getByLabelText('Edit ops') as HTMLTextAreaElement).value;
  const pick = (op: string) => fireEvent.change(screen.getByLabelText('Op to add'), { target: { value: op } });
  const fill = (label: string, value: string) => fireEvent.change(screen.getByLabelText(label), { target: { value } });
  const add = () => fireEvent.click(screen.getByText('Add op'));
  const show = (ClipEdit = vi.fn<(path: string, ops: string) => Promise<string>>(async () => JSON.stringify(reply(false)))) => {
    render(<HkxTimelineTools host={{ ClipEdit } as unknown as HkxHost} initialPath="C:/clip.hkx" disabled={false} />);
    return ClipEdit;
  };

  it('appends the ops it builds to the op list the preview sends', async () => {
    // Constraint: detects a form that sends a different op shape than the host parses, so a retime loses its flag or a key becomes text.
    const ClipEdit = show();
    pick('retime');
    fill('Time scale', '2');
    fireEvent.click(screen.getByLabelText('Keep the frame rate (resample)'));
    add();
    expect(JSON.parse(opsText())).toEqual([{ op: 'trim', first: 0, last: 1 }, { op: 'retime', scale: 2, keep_frame_rate: true }]);
    pick('set_key');
    fill('Track', '1');
    fill('Frame', '3');
    fill('Translation x, y, z', '0.5, 0 -2');
    add();
    expect(JSON.parse(opsText())[2]).toEqual({ op: 'set_key', track: 1, frame: 3, translation: [0.5, 0, -2] });
    fireEvent.click(screen.getByText('Preview edit'));
    await waitFor(() => expect(ClipEdit).toHaveBeenCalledTimes(1));
    expect(ClipEdit.mock.calls[0][1]).toBe(opsText());
  });

  it('names the field of a bad value and leaves the op list alone', () => {
    // Constraint: detects an empty or fractional frame turned into 0 or 1 and sent, or an op added to text that is not a list.
    const ClipEdit = show();
    const before = opsText();
    add();
    expect(screen.getByRole('alert').textContent).toContain('First frame is required');
    fill('First frame', '1.5');
    fill('Last frame', '3');
    add();
    expect(screen.getByRole('alert').textContent).toContain('First frame must be a whole number');
    pick('set_key');
    fill('Track', '0');
    fill('Frame', '0');
    add();
    expect(screen.getByRole('alert').textContent).toContain('set_key needs a translation');
    fill('Rotation x, y, z, w', '0, 0, 1');
    add();
    expect(screen.getByRole('alert').textContent).toContain('Rotation x, y, z, w must be 4 numbers');
    expect(opsText()).toBe(before);
    fireEvent.change(screen.getByLabelText('Edit ops'), { target: { value: '[oops' } });
    pick('trim');
    fill('First frame', '0');
    fill('Last frame', '3');
    add();
    expect(screen.getByRole('alert').textContent).toContain('not valid JSON');
    expect(ClipEdit).not.toHaveBeenCalled();
  });

  it('starts a new list after Clear ops', () => {
    // Constraint: detects the example op staying in the list, so a new edit would run after a trim the user never asked for.
    show();
    fireEvent.click(screen.getByText('Clear ops'));
    fill('First frame', '2');
    fill('Last frame', '5');
    add();
    expect(opsText()).toBe('[{"op":"trim","first":2,"last":5}]');
  });

  it('offers exactly the fields the host accepts for every op', async () => {
    // Constraint: detects a form field renamed or added without the host, which the host would refuse as an unknown field.
    const fs = await vi.importActual<{ readFileSync: (path: string, enc: 'utf8') => string }>('node:fs');
    const source = fs.readFileSync('../native/src/hkx/HkxClipEdit.cpp', 'utf8');
    const host: Record<string, string[]> = {};
    for (const match of source.matchAll(/name == "(\w+)"\) \{\s*allow\(op, \{([^}]*)\}\)/g)) {
      host[match[1]] = [...match[2].matchAll(/"(\w+)"/g)].map(field => field[1]).sort();
    }
    const form = Object.fromEntries(Object.entries(OP_FIELDS).map(([op, fields]) => [op, fields.map(f => f.name).sort()]));
    expect(Object.keys(host).length).toBeGreaterThan(0);
    expect(form).toEqual(host);
  });
});

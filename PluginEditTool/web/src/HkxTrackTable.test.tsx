import { cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';
import type { HkxAnimationInspect, HkxHost } from './backend';
import HkxTrackTable from './HkxTrackTable';
import { trackRows } from './hkxTrackRows';

const inspect = (scales: number[], truncated = false): HkxAnimationInspect => ({
  class_name: 'hkaSplineCompressedAnimation', kind: 'spline', duration: 1, frame_count: 3, track_count: 2,
  annotation_track_count: 0, annotation_count: 0, object_count: 5, annotations: [], frame_duration: 0.5,
  tracks: [{ index: 0, samples: 3, translation_animated: false, rotation_animated: false, scale_animated: false },
    { index: 1, samples: 3, translation_animated: true, rotation_animated: true, scale_animated: scales.some(v => v !== 1) }],
  track: {
    index: 1, frames: truncated ? 9 : 2, shown: 2, truncated,
    translations: [1, 2, 3, 4, 5, 6], rotations: [0, 0, 0, 1, 0, 0, 1, 0], scales,
  },
});

afterEach(() => cleanup());

describe('trackRows', () => {
  it('lists each frame with its time and leaves the scale empty for a track that is never scaled', () => {
    // Constraint: detects a track table that prints 1, 1, 1 on every row of an unscaled track or drops a frame's time.
    const plain = trackRows(inspect([1, 1, 1, 1, 1, 1]));
    expect(plain.scaled).toBe(false);
    expect(plain.rows).toEqual([
      { frame: 0, time: 0, position: '1.000, 2.000, 3.000', rotation: '0.0000, 0.0000, 0.0000, 1.0000', scale: '' },
      { frame: 1, time: 0.5, position: '4.000, 5.000, 6.000', rotation: '0.0000, 0.0000, 1.0000, 0.0000', scale: '' },
    ]);
    const scaled = trackRows(inspect([1, 1, 1, 2, 2, 2]));
    expect(scaled.scaled).toBe(true);
    expect(scaled.rows[1].scale).toBe('2.0000, 2.0000, 2.0000');
  });

  it('refuses a reply that carries no track page', () => {
    // Constraint: detects an older host shown as an empty table.
    expect(() => trackRows({ ...inspect([1, 1, 1, 1, 1, 1]), track: undefined })).toThrow('no track page');
  });
});

describe('HkxTrackTable', () => {
  const show = (InspectAnimation: ReturnType<typeof vi.fn>) =>
    render(<HkxTrackTable host={{ InspectAnimation } as unknown as HkxHost} clipPath="C:/clip.hkx" disabled={false} />);

  it('asks for the chosen track and shows its frames, flags and truncation', async () => {
    // Constraint: detects a table that always shows track 0, hides that the page stopped early or loses which channels the track animates.
    const InspectAnimation = vi.fn(async () => JSON.stringify(inspect([1, 1, 1, 1, 1, 1], true)));
    show(InspectAnimation);
    fireEvent.change(screen.getByLabelText('Track index'), { target: { value: '1' } });
    fireEvent.click(screen.getByText('Show track'));
    await waitFor(() => expect(screen.getByRole('status').textContent).toContain('Track 1 of 2: 9 frames, first 2 shown'));
    expect(screen.getByRole('status').textContent).toContain('animated: translation, rotation');
    expect(InspectAnimation).toHaveBeenCalledWith('C:/clip.hkx', 1, 500);
    expect(screen.getByLabelText('Track frames').querySelectorAll('tbody tr')).toHaveLength(2);
  });

  it('shows the host refusal and refuses a bad track index before calling the host', async () => {
    // Constraint: detects a refusal shown as an empty table and a typed index sent as 0.
    const InspectAnimation = vi.fn(async () => JSON.stringify({ error: 'track index is out of range' }));
    show(InspectAnimation);
    fireEvent.click(screen.getByText('Show track'));
    await waitFor(() => expect(screen.getByRole('alert').textContent).toContain('out of range'));
    fireEvent.change(screen.getByLabelText('Track index'), { target: { value: '1.5' } });
    fireEvent.click(screen.getByText('Show track'));
    await waitFor(() => expect(screen.getByRole('alert').textContent).toContain('whole number'));
    expect(InspectAnimation).toHaveBeenCalledTimes(1);
  });
});

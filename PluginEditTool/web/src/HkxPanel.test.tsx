import { cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import type { HkxHost } from './backend';
import HkxPanel from './HkxPanel';
import { advanceTime } from './util/advanceTime';

vi.mock('./HkxViewport', () => ({ default: () => null }));

const SK = 'C:\\mods\\rig.hkx';
const CLIP = 'C:\\mods\\idle.hkx';

const skeleton = vi.fn<(skeletonPath: string) => Promise<unknown>>();
const loadClipCall = vi.fn<(skeletonPath: string, animationPath: string) => Promise<unknown>>();
const poseBoth = vi.fn<(skeletonPath: string, animationPath: string, time: number) => Promise<unknown>>();
const inspectAnimation = vi.fn<(animationPath: string, track?: number) => Promise<unknown>>();

const matchingClip = {
  duration: 2,
  frame_count: 61,
  skeleton_name: 'Rig',
  original_skeleton_name: 'Rig',
  skeleton_match: 'match',
};

// Requires the fixture to match the reply the host returns, extras included, so one text matcher serves every case.
const RIG_INFO = 'Rig · 2 bones · 1 partition · 2 float slots · 1 locked bone';

const skeletonData = {
  name: 'Rig',
  bones: [
    { name: 'Root', parent: -1, translation: [0, 0, 0], rotation: [0, 0, 0, 1], scale: [1, 1, 1], lock: false },
    { name: 'Spine', parent: 0, translation: [0, 0, 1], rotation: [0, 0, 0, 1], scale: [1, 1, 1], lock: true },
  ],
  reference_floats: [2],
  float_slots: [{ name: 'speed', named: true }, { name: '', named: false }],
  local_frames: [{ bone: 1, has_frame: true }],
  partitions: [{ name: 'upper', named: true, start_bone: 0, num_bones: 1 }],
};

function bones(spine: number[]) {
  return [
    { name: 'Root', parent: -1, translation: [0, 0, 0], rotation: [0, 0, 0, 1], scale: [1, 1, 1] },
    { name: 'Spine', parent: 0, translation: spine, rotation: [0, 0, 0, 1], scale: [1, 1, 1] },
  ];
}

function poseData(time: number, duration = 2, frameCount = 61) {
  return {
    duration,
    frame_count: frameCount,
    local: bones([time, 0, 0]),
    model: bones([time, 0, 1]),
  };
}

function gate() {
  let release!: () => void;
  const promise = new Promise<void>(resolve => { release = () => resolve(); });
  return { promise, open: () => release() };
}

function stubBridge() {
  vi.stubGlobal('chrome', {
    webview: {
      addEventListener: vi.fn(),
      removeEventListener: vi.fn(),
      hostObjects: {
        hkx: { Skeleton: skeleton, LoadClip: loadClipCall, PoseBoth: poseBoth, InspectAnimation: inspectAnimation } as unknown as HkxHost,
      },
    },
  });
}

async function loadClip() {
  fireEvent.change(screen.getByPlaceholderText(/skeleton \.hkx/), { target: { value: SK } });
  fireEvent.change(screen.getByPlaceholderText(/animation \.hkx/), { target: { value: CLIP } });
  fireEvent.click(screen.getByRole('button', { name: 'Load clip' }));
  await screen.findByText('2.000 s · 61 frames');
}

beforeEach(() => {
  vi.clearAllMocks();
  localStorage.clear();
  skeleton.mockResolvedValue(skeletonData);
  loadClipCall.mockResolvedValue(matchingClip);
  poseBoth.mockImplementation(async (_sk, _an, time) => poseData(time));
  inspectAnimation.mockResolvedValue({
    kind: 'interleaved',
    track_count: 2,
    annotation_count: 1,
    annotations: [{ name: 'Events', entries: [{ time: 0.5, text: 'Footstep' }] }],
  });
  stubBridge();
});

afterEach(() => {
  cleanup();
  vi.unstubAllGlobals();
});

describe('HKX timeline panel', () => {
  it('loads a skeleton and a clip, then shows the duration, frame count and bones', async () => {
    render(<HkxPanel onClose={() => {}} />);
    await loadClip();

    expect(skeleton).toHaveBeenCalledWith(SK);
    expect(poseBoth).toHaveBeenCalledWith(SK, CLIP, 0);
    // Regression: the info line must count partitions, float slots and locked bones, not bones alone.
    expect(screen.getByText(RIG_INFO)).toBeTruthy();
    expect(screen.getByRole('button', { name: 'Spine' })).toBeTruthy();
    expect(screen.getByText('upper · bones 0–0')).toBeTruthy();
    expect(screen.getByText('speed')).toBeTruthy();
    expect(screen.getByText('slot 1')).toBeTruthy();
  });

  it('shows the clip kind, track and annotation counts the host inspection reports', async () => {
    render(<HkxPanel onClose={() => {}} />);
    await loadClip();

    // Regression: the panel dropped the clip inspection, so a lossless or annotated clip read like a bare timeline.
    expect(inspectAnimation).toHaveBeenCalledWith(CLIP);
    await screen.findByText(/interleaved · 2 tracks · annotations 1/);
    expect(screen.getByText('Events: 0.500s Footstep')).toBeTruthy();
  });

  it('reports a refused clip inspection without hiding the clip that loaded', async () => {
    inspectAnimation.mockRejectedValue(new Error('the animation could not be decoded, HkxStatus 3'));
    render(<HkxPanel onClose={() => {}} />);
    await loadClip();

    expect(await screen.findByText(/Clip inspection failed: the animation could not be decoded/)).toBeTruthy();
    expect(screen.getByText('2.000 s · 61 frames')).toBeTruthy();
  });

  it('asks the host for both spaces in one request per sampled time', async () => {
    render(<HkxPanel onClose={() => {}} />);
    await loadClip();
    expect(poseBoth).toHaveBeenCalledTimes(1);

    fireEvent.change(screen.getByRole('slider'), { target: { value: '0.5' } });
    await waitFor(() => expect(poseBoth).toHaveBeenCalledTimes(2));
    expect(poseBoth).toHaveBeenLastCalledWith(SK, CLIP, 0.5);
  });

  it('moving the slider requests the pose at the new time', async () => {
    render(<HkxPanel onClose={() => {}} />);
    await loadClip();

    fireEvent.change(screen.getByRole('slider'), { target: { value: '0.5' } });
    await waitFor(() => expect(poseBoth).toHaveBeenCalledWith(SK, CLIP, 0.5));
  });

  it('shows the selected bone local translation at the current time', async () => {
    render(<HkxPanel onClose={() => {}} />);
    await loadClip();

    fireEvent.click(screen.getByRole('button', { name: 'Spine' }));
    fireEvent.change(screen.getByRole('slider'), { target: { value: '0.5' } });
    expect(await screen.findByText('translation: 0.500, 0.000, 0.000')).toBeTruthy();
    expect(poseBoth).toHaveBeenCalledWith(SK, CLIP, 0.5);
  });

  it('steps forward one frame and requests that frame', async () => {
    render(<HkxPanel onClose={() => {}} />);
    await loadClip();

    fireEvent.click(screen.getByRole('button', { name: 'Step forward one frame' }));
    await waitFor(() => expect(poseBoth).toHaveBeenCalledWith(SK, CLIP, 2 / 60));
  });

  it('keeps only the newest scrub position while a pose request is pending', async () => {
    render(<HkxPanel onClose={() => {}} />);
    await loadClip();

    const held = gate();
    poseBoth.mockImplementation(async (_sk, _an, time) => {
      if (time === 0.5) await held.promise;
      return poseData(time);
    });

    fireEvent.change(screen.getByRole('slider'), { target: { value: '0.5' } });
    fireEvent.change(screen.getByRole('slider'), { target: { value: '1' } });
    fireEvent.change(screen.getByRole('slider'), { target: { value: '1.5' } });
    held.open();

    await waitFor(() => expect(poseBoth).toHaveBeenCalledWith(SK, CLIP, 1.5));
    const times = poseBoth.mock.calls.map(call => call[2]);
    expect(times).toEqual([0, 0.5, 1.5]);
  });

  it('ignores a pose reply that arrives after the clip was reloaded', async () => {
    render(<HkxPanel onClose={() => {}} />);
    await loadClip();

    const stale = gate();
    const fresh = gate();
    let reloaded = false;
    poseBoth.mockImplementation(async (_sk, _an, time) => {
      if (time === 0.5) {
        await stale.promise;
        return poseData(time, 5, 11);
      }
      if (reloaded) await fresh.promise;
      return poseData(time);
    });

    fireEvent.change(screen.getByRole('slider'), { target: { value: '0.5' } });
    await waitFor(() => expect(poseBoth).toHaveBeenCalledWith(SK, CLIP, 0.5));

    reloaded = true;
    fireEvent.click(screen.getByRole('button', { name: 'Load clip' }));
    await screen.findByText(RIG_INFO);
    stale.open();

    await waitFor(() => expect(poseBoth).toHaveBeenCalledTimes(3));
    expect(screen.queryByText('5.000 s · 11 frames')).toBeNull();

    fresh.open();
    expect(await screen.findByText('2.000 s · 61 frames')).toBeTruthy();
  });

  it('shows no error for a pose request that fails after the clip was reloaded', async () => {
    render(<HkxPanel onClose={() => {}} />);
    await loadClip();

    const stale = gate();
    poseBoth.mockImplementation(async (_sk, _an, time) => {
      if (time === 0.5) {
        await stale.promise;
        throw new Error('stale pose failed');
      }
      return poseData(time);
    });

    fireEvent.change(screen.getByRole('slider'), { target: { value: '0.5' } });
    await waitFor(() => expect(poseBoth).toHaveBeenCalledWith(SK, CLIP, 0.5));

    fireEvent.click(screen.getByRole('button', { name: 'Load clip' }));
    await screen.findByText(RIG_INFO);
    stale.open();
    await new Promise(resolve => setTimeout(resolve, 0));

    expect(screen.queryByText('stale pose failed')).toBeNull();
  });

  it('requests the new clip pose when a stale pose request fails after a reload', async () => {
    render(<HkxPanel onClose={() => {}} />);
    await loadClip();

    const stale = gate();
    poseBoth.mockImplementation(async (_sk, _an, time) => {
      if (time === 0.5) {
        await stale.promise;
        throw new Error('stale pose failed');
      }
      return poseData(time);
    });

    fireEvent.change(screen.getByRole('slider'), { target: { value: '0.5' } });
    await waitFor(() => expect(poseBoth).toHaveBeenCalledWith(SK, CLIP, 0.5));

    fireEvent.click(screen.getByRole('button', { name: 'Load clip' }));
    await screen.findByText(RIG_INFO);
    stale.open();

    expect(await screen.findByText('2.000 s · 61 frames')).toBeTruthy();
    expect(poseBoth).toHaveBeenCalledTimes(3);
    expect(screen.queryByText('stale pose failed')).toBeNull();
  });

  it('shows a synchronous throw from the Skeleton host call in the panel', async () => {
    skeleton.mockImplementation(() => { throw new Error('host refused the file'); });
    render(<HkxPanel onClose={() => {}} />);
    fireEvent.change(screen.getByPlaceholderText(/skeleton \.hkx/), { target: { value: SK } });
    fireEvent.change(screen.getByPlaceholderText(/animation \.hkx/), { target: { value: CLIP } });
    fireEvent.click(screen.getByRole('button', { name: 'Load clip' }));

    expect(await screen.findByText('host refused the file')).toBeTruthy();
  });

  it('shows an error returned by the PoseBoth call as a JSON string in the panel', async () => {
    poseBoth.mockImplementation(async () => JSON.stringify({ error: 'clip is corrupt' }));
    render(<HkxPanel onClose={() => {}} />);
    fireEvent.change(screen.getByPlaceholderText(/skeleton \.hkx/), { target: { value: SK } });
    fireEvent.change(screen.getByPlaceholderText(/animation \.hkx/), { target: { value: CLIP } });
    fireEvent.click(screen.getByRole('button', { name: 'Load clip' }));

    expect(await screen.findByText('clip is corrupt')).toBeTruthy();
  });

  it('shows a skeleton mismatch from LoadClip beside the clip before playback', async () => {
    loadClipCall.mockResolvedValue({ ...matchingClip, original_skeleton_name: 'Biped', skeleton_match: 'mismatch' });
    render(<HkxPanel onClose={() => {}} />);
    await loadClip();

    expect(loadClipCall).toHaveBeenCalledWith(SK, CLIP);
    expect(await screen.findByText(/authored for skeleton "Biped", not "Rig"/)).toBeTruthy();
    expect((screen.getByRole('button', { name: 'Play' }) as HTMLButtonElement).disabled).toBe(false);
  });

  it('shows an unverified skeleton compatibility when the clip names no skeleton', async () => {
    loadClipCall.mockResolvedValue({ ...matchingClip, original_skeleton_name: '', skeleton_match: 'unverified' });
    render(<HkxPanel onClose={() => {}} />);
    await loadClip();

    expect(await screen.findByText(/Skeleton compatibility unverified/)).toBeTruthy();
  });

  it('processes the newest scrub when an older pose request fails after it was superseded', async () => {
    render(<HkxPanel onClose={() => {}} />);
    await loadClip();
    fireEvent.click(screen.getByRole('button', { name: 'Spine' }));

    const held = gate();
    poseBoth.mockImplementation(async (_sk, _an, time) => {
      if (time === 0.5) {
        await held.promise;
        throw new Error('old pose failed');
      }
      return poseData(time);
    });

    fireEvent.change(screen.getByRole('slider'), { target: { value: '0.5' } });
    await waitFor(() => expect(poseBoth).toHaveBeenCalledWith(SK, CLIP, 0.5));
    fireEvent.change(screen.getByRole('slider'), { target: { value: '1.5' } });
    held.open();

    expect(await screen.findByText('translation: 1.500, 0.000, 0.000')).toBeTruthy();
    expect(poseBoth).toHaveBeenLastCalledWith(SK, CLIP, 1.5);
    expect(screen.queryByText('old pose failed')).toBeNull();
  });

  it('stops playback and marks the preview stale when the animation path is edited', async () => {
    render(<HkxPanel onClose={() => {}} />);
    await loadClip();

    fireEvent.click(screen.getByRole('button', { name: 'Play' }));
    expect(screen.getByRole('button', { name: 'Pause' })).toBeTruthy();

    fireEvent.change(screen.getByPlaceholderText(/animation \.hkx/), { target: { value: 'C:\\mods\\other.hkx' } });
    expect(await screen.findByText(/Paths changed since load/)).toBeTruthy();
    expect((screen.getByRole('button', { name: 'Play' }) as HTMLButtonElement).disabled).toBe(true);
  });

  it('shows the pair actually loaded and marks it stale when a path is edited during a delayed load', async () => {
    const OTHER = 'C:\\mods\\other.hkx';
    render(<HkxPanel onClose={() => {}} />);
    await loadClip();

    const held = gate();
    skeleton.mockImplementationOnce(async () => {
      await held.promise;
      return skeletonData;
    });
    fireEvent.change(screen.getByPlaceholderText(/animation \.hkx/), { target: { value: OTHER } });
    fireEvent.click(screen.getByRole('button', { name: 'Load clip' }));
    fireEvent.change(screen.getByPlaceholderText(/animation \.hkx/), { target: { value: 'C:\\mods\\third.hkx' } });
    held.open();

    expect(await screen.findByText(`Loaded ${SK} + ${OTHER}`)).toBeTruthy();
    expect(await screen.findByText(/Paths changed since load/)).toBeTruthy();
  });

  it('shows a clear error and no timeline when PoseBoth reports a null duration', async () => {
    poseBoth.mockImplementation(async (_sk, _an, time) => ({ ...poseData(time), duration: null }));
    render(<HkxPanel onClose={() => {}} />);
    fireEvent.change(screen.getByPlaceholderText(/skeleton \.hkx/), { target: { value: SK } });
    fireEvent.change(screen.getByPlaceholderText(/animation \.hkx/), { target: { value: CLIP } });
    fireEvent.click(screen.getByRole('button', { name: 'Load clip' }));

    expect(await screen.findByText('clip duration is missing or not finite')).toBeTruthy();
    expect(screen.queryByRole('slider')).toBeNull();
  });

  it('shows a clear error for a scrub reply with a null bone translation and keeps the loaded timeline', async () => {
    render(<HkxPanel onClose={() => {}} />);
    await loadClip();

    poseBoth.mockImplementation(async (_sk, _an, time) => ({
      ...poseData(time),
      local: [
        { name: 'Root', parent: -1, translation: [0, 0, 0], rotation: [0, 0, 0, 1], scale: [1, 1, 1] },
        { name: 'Spine', parent: 0, translation: [null, 0, 0], rotation: [0, 0, 0, 1], scale: [1, 1, 1] },
      ],
    }));
    fireEvent.change(screen.getByRole('slider'), { target: { value: '0.5' } });

    expect(await screen.findByText('no finite transform for bone "Spine"')).toBeTruthy();
    expect(screen.getByText('2.000 s · 61 frames')).toBeTruthy();
  });
});

describe('advanceTime', () => {
  it('wraps a negative step into [0, duration) and returns 0 for a non-positive duration', () => {
    expect(advanceTime(0, -0.5, 2)).toBeCloseTo(1.5);
    expect(advanceTime(0.25, -0.5, 2)).toBeCloseTo(1.75);
    expect(advanceTime(1, 0.5, 0)).toBe(0);
  });
});

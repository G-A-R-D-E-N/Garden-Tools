import { cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import type { HkxHost } from './backend';
import HkxPanel from './HkxPanel';
import { advanceTime } from './util/advanceTime';

vi.mock('./HkxViewport', () => ({ default: () => null }));

const SK = 'C:\\mods\\rig.hkx';
const CLIP = 'C:\\mods\\idle.hkx';

const skeleton = vi.fn<(skeletonPath: string) => Promise<unknown>>();
const pose = vi.fn<(skeletonPath: string, animationPath: string, time: number, space: string) => Promise<unknown>>();

const skeletonData = {
  name: 'Rig',
  bones: [
    { name: 'Root', parent: -1, translation: [0, 0, 0], rotation: [0, 0, 0, 1], scale: [1, 1, 1] },
    { name: 'Spine', parent: 0, translation: [0, 0, 1], rotation: [0, 0, 0, 1], scale: [1, 1, 1] },
  ],
};

function poseData(time: number, space: string, duration = 2, frameCount = 61) {
  const spine = space === 'local' ? [time, 0, 0] : [time, 0, 1];
  return {
    duration,
    frame_count: frameCount,
    bones: [
      { name: 'Root', parent: -1, translation: [0, 0, 0], rotation: [0, 0, 0, 1], scale: [1, 1, 1] },
      { name: 'Spine', parent: 0, translation: spine, rotation: [0, 0, 0, 1], scale: [1, 1, 1] },
    ],
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
      hostObjects: { hkx: { Skeleton: skeleton, Pose: pose } as unknown as HkxHost },
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
  pose.mockImplementation(async (_sk, _an, time, space) => poseData(time, space));
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
    expect(pose).toHaveBeenCalledWith(SK, CLIP, 0, 'model');
    expect(pose).toHaveBeenCalledWith(SK, CLIP, 0, 'local');
    expect(screen.getByText('Rig · 2 bones')).toBeTruthy();
    expect(screen.getByRole('button', { name: 'Spine' })).toBeTruthy();
  });

  it('moving the slider requests the model-space pose at the new time', async () => {
    render(<HkxPanel onClose={() => {}} />);
    await loadClip();

    fireEvent.change(screen.getByRole('slider'), { target: { value: '0.5' } });
    await waitFor(() => expect(pose).toHaveBeenCalledWith(SK, CLIP, 0.5, 'model'));
  });

  it('shows the selected bone local translation at the current time', async () => {
    render(<HkxPanel onClose={() => {}} />);
    await loadClip();

    fireEvent.click(screen.getByRole('button', { name: 'Spine' }));
    fireEvent.change(screen.getByRole('slider'), { target: { value: '0.5' } });
    expect(await screen.findByText('translation: 0.500, 0.000, 0.000')).toBeTruthy();
    expect(pose).toHaveBeenCalledWith(SK, CLIP, 0.5, 'local');
  });

  it('steps forward one frame and requests that frame in model space', async () => {
    render(<HkxPanel onClose={() => {}} />);
    await loadClip();

    fireEvent.click(screen.getByRole('button', { name: 'Step forward one frame' }));
    await waitFor(() => expect(pose).toHaveBeenCalledWith(SK, CLIP, 2 / 60, 'model'));
  });

  it('keeps only the newest scrub position while a pose request is pending', async () => {
    render(<HkxPanel onClose={() => {}} />);
    await loadClip();

    const held = gate();
    pose.mockImplementation(async (_sk, _an, time, space) => {
      if (time === 0.5) await held.promise;
      return poseData(time, space);
    });

    fireEvent.change(screen.getByRole('slider'), { target: { value: '0.5' } });
    fireEvent.change(screen.getByRole('slider'), { target: { value: '1' } });
    fireEvent.change(screen.getByRole('slider'), { target: { value: '1.5' } });
    held.open();

    await waitFor(() => expect(pose).toHaveBeenCalledWith(SK, CLIP, 1.5, 'model'));
    const modelTimes = pose.mock.calls.filter(call => call[3] === 'model').map(call => call[2]);
    expect(modelTimes).toEqual([0, 0.5, 1.5]);
  });

  it('ignores a pose reply that arrives after the clip was reloaded', async () => {
    render(<HkxPanel onClose={() => {}} />);
    await loadClip();

    const stale = gate();
    const fresh = gate();
    let reloaded = false;
    pose.mockImplementation(async (_sk, _an, time, space) => {
      if (time === 0.5) {
        await stale.promise;
        return poseData(time, space, 5, 11);
      }
      if (reloaded) await fresh.promise;
      return poseData(time, space);
    });

    fireEvent.change(screen.getByRole('slider'), { target: { value: '0.5' } });
    await waitFor(() => expect(pose).toHaveBeenCalledWith(SK, CLIP, 0.5, 'model'));

    reloaded = true;
    fireEvent.click(screen.getByRole('button', { name: 'Load clip' }));
    await screen.findByText('Rig · 2 bones');
    stale.open();

    await waitFor(() => expect(pose).toHaveBeenCalledTimes(6));
    expect(screen.queryByText('5.000 s · 11 frames')).toBeNull();

    fresh.open();
    expect(await screen.findByText('2.000 s · 61 frames')).toBeTruthy();
  });

  it('shows no error for a pose request that fails after the clip was reloaded', async () => {
    render(<HkxPanel onClose={() => {}} />);
    await loadClip();

    const stale = gate();
    pose.mockImplementation(async (_sk, _an, time, space) => {
      if (time === 0.5) {
        await stale.promise;
        throw new Error('stale pose failed');
      }
      return poseData(time, space);
    });

    fireEvent.change(screen.getByRole('slider'), { target: { value: '0.5' } });
    await waitFor(() => expect(pose).toHaveBeenCalledWith(SK, CLIP, 0.5, 'model'));

    fireEvent.click(screen.getByRole('button', { name: 'Load clip' }));
    await screen.findByText('Rig · 2 bones');
    stale.open();
    await new Promise(resolve => setTimeout(resolve, 0));

    expect(screen.queryByText('stale pose failed')).toBeNull();
  });

  it('requests the new clip pose when a stale pose request fails after a reload', async () => {
    render(<HkxPanel onClose={() => {}} />);
    await loadClip();

    const stale = gate();
    pose.mockImplementation(async (_sk, _an, time, space) => {
      if (time === 0.5) {
        await stale.promise;
        throw new Error('stale pose failed');
      }
      return poseData(time, space);
    });

    fireEvent.change(screen.getByRole('slider'), { target: { value: '0.5' } });
    await waitFor(() => expect(pose).toHaveBeenCalledWith(SK, CLIP, 0.5, 'model'));

    fireEvent.click(screen.getByRole('button', { name: 'Load clip' }));
    await screen.findByText('Rig · 2 bones');
    stale.open();

    expect(await screen.findByText('2.000 s · 61 frames')).toBeTruthy();
    expect(pose).toHaveBeenCalledTimes(6);
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

  it('shows an error returned by the Pose call as a JSON string in the panel', async () => {
    pose.mockImplementation(async () => JSON.stringify({ error: 'clip is corrupt' }));
    render(<HkxPanel onClose={() => {}} />);
    fireEvent.change(screen.getByPlaceholderText(/skeleton \.hkx/), { target: { value: SK } });
    fireEvent.change(screen.getByPlaceholderText(/animation \.hkx/), { target: { value: CLIP } });
    fireEvent.click(screen.getByRole('button', { name: 'Load clip' }));

    expect(await screen.findByText('clip is corrupt')).toBeTruthy();
  });
});

describe('advanceTime', () => {
  it('wraps a negative step into [0, duration) and returns 0 for a non-positive duration', () => {
    expect(advanceTime(0, -0.5, 2)).toBeCloseTo(1.5);
    expect(advanceTime(0.25, -0.5, 2)).toBeCloseTo(1.75);
    expect(advanceTime(1, 0.5, 0)).toBe(0);
  });
});

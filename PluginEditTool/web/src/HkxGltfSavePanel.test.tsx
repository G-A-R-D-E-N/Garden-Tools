import { cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';
import type { HkxHost } from './backend';
import HkxGltfSavePanel from './HkxGltfSavePanel';
import { describeGltfSave, saveGltfAnimation, type GltfSaveResult } from './hkxGltfSaveClient';

const zero = { position: 0, rotation: 0, scale: 0 };
const saved: GltfSaveResult = {
  saved: true, output: 'C:\\out\\walk.hkx', overwrote: false, bytes: 4096,
  report: {
    tracks: 3, frames: 31, frame_rate: 30, duration: 1, animated: ['COM', 'Root'], reference_tracks: [],
    untracked_bones: ['Head'], unmapped_joints: ['Prop'], rest_mismatch: [], weight_channels: 0, non_bone_channels: 0,
    annotations_replaced: 1, annotations_written: 2, motion_replaced: true, motion_samples: 31, binding_linked: true,
    binding_skeleton_name: 'Root', skeleton_match: 'match', sampling_error: zero,
    readback_error: { position: 0.0012, rotation: 0.0003, scale: 0 },
  },
};

const hostWith = (reply: unknown) =>
  ({ SaveGltfAnimation: vi.fn(async () => (typeof reply === 'string' ? reply : JSON.stringify(reply))) } as unknown as HkxHost);

const fill = (label: string, value: string) => fireEvent.change(screen.getByLabelText(label), { target: { value } });

const fillPaths = () => {
  fill('Template HKX path', 'C:\\t\\walk.hkx');
  fill('Skeleton HKX path', 'C:\\t\\skeleton.hkx');
  fill('glTF path', 'C:\\g\\walk.glb');
  fill('Output HKX path', 'C:\\out\\walk.hkx');
};

afterEach(() => cleanup());

describe('HkxGltfSavePanel', () => {
  it('sends the paths and flags in RPC order and names every bone the save left out', async () => {
    // Constraint: detects a panel that reports a partial save as complete, or shifts the positional RPC arguments.
    const host = hostWith(saved);
    render(<HkxGltfSavePanel host={host} skeletonPath="" templatePath="" disabled={false} />);
    fillPaths();
    fill('glTF animation name', 'Walk');
    fireEvent.click(screen.getByLabelText('replace the template\'s notes'));
    fireEvent.click(screen.getByText('Save animation'));
    await waitFor(() => expect(screen.getByRole('status')).toBeTruthy());
    expect(host.SaveGltfAnimation).toHaveBeenCalledWith(
      'C:\\t\\walk.hkx', 'C:\\t\\skeleton.hkx', 'C:\\g\\walk.glb', 'C:\\out\\walk.hkx', 'Walk', null, true, false, false);
    const text = screen.getByRole('status').textContent ?? '';
    expect(text).toContain('not in the clip: Head');
    expect(text).toContain('glTF joints not in the skeleton: Prop');
    expect(text).toContain('2 annotations written (1 replaced)');
    expect(text).toContain('31 samples');
  });

  it('shows the host refusal text unchanged so the user knows what to fix', async () => {
    // Constraint: detects a refusal swallowed into a generic failure, which hides the template choice the user must change.
    const refusal = 'AnnotationsReplaced: the template carries 2 notes. Set replace_annotations to true. Nothing was written.';
    render(<HkxGltfSavePanel host={hostWith({ error: refusal })} skeletonPath="" templatePath="" disabled={false} />);
    fillPaths();
    fireEvent.click(screen.getByText('Save animation'));
    await waitFor(() => expect(screen.getByRole('alert')).toBeTruthy());
    expect(screen.getByRole('alert').textContent).toBe(refusal);
    expect(screen.queryByRole('status')).toBeNull();
  });

  it('refuses before calling the host when a path is missing, and says which', async () => {
    // Constraint: detects a save sent with an empty output path, which the host would answer with a less direct error.
    const host = hostWith(saved);
    await expect(saveGltfAnimation(host, {
      templatePath: 'a', skeletonPath: 'b', gltfPath: 'c', outputPath: ' ', animation: '', skin: '',
      replaceAnnotations: false, replaceMotion: false, overwrite: false,
    })).rejects.toThrow('Pick the output .hkx first.');
    expect(host.SaveGltfAnimation).not.toHaveBeenCalled();
  });

  it('says an older host cannot save instead of doing nothing', async () => {
    // Constraint: detects an older host build without the RPC leaving the button silent.
    await expect(saveGltfAnimation({} as HkxHost, {
      templatePath: 'a', skeletonPath: 'b', gltfPath: 'c', outputPath: 'd', animation: '', skin: '',
      replaceAnnotations: false, replaceMotion: false, overwrite: false,
    })).rejects.toThrow('this host build cannot save a glTF animation');
  });

  it('follows the loaded skeleton and clip paths when they change', () => {
    // Constraint: detects a panel that keeps the first template path after another clip is loaded and saves over the wrong template.
    const props = { host: hostWith(saved), disabled: false };
    const view = render(<HkxGltfSavePanel {...props} skeletonPath={'C:\\a\\sk.hkx'} templatePath={'C:\\a\\one.hkx'} />);
    expect((screen.getByLabelText('Template HKX path') as HTMLInputElement).value).toBe('C:\\a\\one.hkx');
    view.rerender(<HkxGltfSavePanel {...props} skeletonPath={'C:\\b\\sk.hkx'} templatePath={'C:\\b\\two.hkx'} />);
    expect((screen.getByLabelText('Template HKX path') as HTMLInputElement).value).toBe('C:\\b\\two.hkx');
    expect((screen.getByLabelText('Skeleton HKX path') as HTMLInputElement).value).toBe('C:\\b\\sk.hkx');
  });

  it('states a replaced file and a still clip plainly', () => {
    // Constraint: detects a summary that hides an overwrite or reports a clip with no root motion as having some.
    const text = describeGltfSave({
      ...saved, overwrote: true,
      report: { ...saved.report, motion_samples: 0, untracked_bones: [], unmapped_joints: [] },
    });
    expect(text.startsWith('Replaced C:\\out\\walk.hkx')).toBe(true);
    expect(text).toContain('root motion none');
    expect(text).not.toContain('not in the clip');
  });
});

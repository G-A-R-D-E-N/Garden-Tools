import { cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';
import type { NifHost } from './backend';
import NifPoseTools from './NifPoseTools';
import { describePoseApplyReport, parsePoseApplyReport, parsePoseList } from './poseToolsClient';

const NIF = 'C:\\mods\\body.nif';
const LIBRARY = 'C:\\mods\\poses.txt';

const listJson = (dry: boolean) => JSON.stringify({
  poses: [
    dry
      ? { name: 'Sit', bones: 4, applied: 3, unknown_pose_bones: ['Tail'], unposed_bones: ['Spine'] }
      : { name: 'Sit', bones: 4 },
    dry ? { name: 'Stand', bones: 2, error: 'no bone of the pose is on this skin' } : { name: 'Stand', bones: 2 },
  ],
});

const appliedText =
  `apply_pose OK (in-process)\nSaved: ${NIF}\nApplied: 3\nUnknown pose bones: Tail\nUnposed skin bones: Spine`;

const makeHost = (overrides: Partial<NifHost> = {}) => ({
  BrowseForFile: vi.fn(async () => LIBRARY),
  PoseList: vi.fn(async (_library: string, _nif?: string | null, block?: number | null) => listJson(block != null)),
  BlockOp: vi.fn(async () => appliedText),
  ...overrides,
} as unknown as NifHost);

const mount = (host: NifHost, onSaved = vi.fn(), appendLog = vi.fn()) => {
  render(
    <NifPoseTools nif={host} nifPath={NIF} skins={[{ id: 5, type: 'BSSkin::Instance', name: '' }]} disabled={false}
                  expectedSource="stamp-1" appendLog={appendLog} onSaved={onSaved} />,
  );
  return { onSaved, appendLog };
};

afterEach(cleanup);

describe('poseToolsClient', () => {
  // Invariant: fails when a refusal is read as an empty pose list or an applied report loses its bone lists.
  it('reads a pose list, a refusal and an apply report', () => {
    const poses = parsePoseList(listJson(true));
    expect(poses.map(p => p.name)).toEqual(['Sit', 'Stand']);
    expect(poses[0].applied).toBe(3);
    expect(poses[0].unknownPoseBones).toEqual(['Tail']);
    expect(poses[1].error).toContain('no bone');
    expect(parsePoseList(listJson(false))[0].applied).toBeNull();
    expect(() => parsePoseList('Error: Pose library not found: x')).toThrow('Pose library not found: x');
    expect(() => parsePoseList(JSON.stringify({ error: 'no skin instance' }))).toThrow('no skin instance');

    const report = parsePoseApplyReport(appliedText);
    expect(report.ok).toBe(true);
    expect(report.applied).toBe(3);
    expect(describePoseApplyReport('Sit', report)).toBe('Sit: posed 3 bones, not on this skin: Tail, left unposed: Spine');
    const refused = parsePoseApplyReport('Error: Pose \'Nope\' is not in the library. Poses: Sit, Stand');
    expect(refused.ok).toBe(false);
    expect(refused.refusal).toContain('is not in the library');
  });
});

describe('NifPoseTools panel', () => {
  // Invariant: fails when the dry run is sent without the skin instance, or Apply runs before a pose is chosen.
  it('lists the poses against the picked skin instance and applies one in place with the source stamp', async () => {
    const host = makeHost();
    const { onSaved } = mount(host);
    const button = (label: string) => screen.getByText(label) as HTMLButtonElement;

    expect(button('List poses').disabled).toBe(true);
    fireEvent.click(button('Browse…'));
    await waitFor(() => expect((screen.getByLabelText('Pose library path') as HTMLInputElement).value).toBe(LIBRARY));
    fireEvent.change(screen.getByLabelText('Pose tools skin instance'), { target: { value: '5' } });
    expect(button('Apply pose').disabled).toBe(true);

    fireEvent.click(button('List poses'));
    await waitFor(() => expect((screen.getByLabelText('Pose') as HTMLSelectElement).value).toBe('Sit'));
    expect(host.PoseList).toHaveBeenCalledWith(LIBRARY, NIF, 5);
    expect(screen.getByText(/Sit: poses 3 of 4 bones, 1 not on this skin, 1 skin bones left unposed/)).toBeTruthy();

    fireEvent.click(button('Apply pose'));
    await waitFor(() => expect(onSaved).toHaveBeenCalledWith(NIF));
    expect(host.BlockOp).toHaveBeenCalledWith(NIF, 'apply_pose', 5, null, '', null, '', 'stamp-1', false, null,
      LIBRARY, 'Sit', '', null, null);
  });

  // Invariant: fails when a refused apply reloads the editor or a library with no skin picked sends a block.
  it('logs a refused apply without reporting a save, and lists with no skin instance as a plain read', async () => {
    const host = makeHost({ BlockOp: vi.fn(async () => 'Error: Nothing posed.\nApplied: 0') } as Partial<NifHost>);
    const { onSaved, appendLog } = mount(host);
    fireEvent.change(screen.getByLabelText('Pose library path'), { target: { value: LIBRARY } });
    fireEvent.click(screen.getByText('List poses'));
    await waitFor(() => expect((screen.getByLabelText('Pose') as HTMLSelectElement).value).toBe('Sit'));
    expect(host.PoseList).toHaveBeenCalledWith(LIBRARY, null, null);

    fireEvent.change(screen.getByLabelText('Pose tools skin instance'), { target: { value: '5' } });
    fireEvent.click(screen.getByText('List poses'));
    await waitFor(() => expect(host.PoseList).toHaveBeenCalledTimes(2));
    await waitFor(() => expect((screen.getByText('Apply pose') as HTMLButtonElement).disabled).toBe(false));
    fireEvent.click(screen.getByText('Apply pose'));
    await waitFor(() => expect(appendLog.mock.calls.some(call => String(call[0]).includes('apply pose refused'))).toBe(true));
    expect(appendLog.mock.calls.some(call => String(call[0]).includes('Nothing posed.'))).toBe(true);
    expect(onSaved).not.toHaveBeenCalled();
  });
});

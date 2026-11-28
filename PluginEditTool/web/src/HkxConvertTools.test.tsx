import { cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';
import type { HkxHost } from './backend';
import HkxConvertTools from './HkxConvertTools';

const reply = (saved: boolean, over: Record<string, unknown> = {}) => ({
  ok: true, status: 'ok', reason: '', source: 'abc123', source_version: 'hk_2014.1.0-r1',
  target_version: 'hk_2014.1.0-r1', same_version: true, evidence: 'fixtures', objects: 4, patched_objects: 0,
  changed_total: 0, changes: [], opaque_classes: ['hkaSkeletonMapper'], diagnostics: [], saved, ...over,
});

const changedReply = (saved: boolean) => reply(saved, {
  same_version: false, target_version: 'hk_2016.2.0-r1', patched_objects: 1, changed_total: 1,
  changes: [{ object: 8, class: 'hkaAnimationBinding', member: 'blendHint', before: '2', after: '7' }],
});

afterEach(() => cleanup());

describe('HkxConvertTools', () => {
  it('previews first, lists changed and opaque fields, then saves against the previewed source hash', async () => {
    // Constraint: detects a save that skips the preview, ignores the file changing after it, or hides what was changed and left opaque.
    const ConvertAsset = vi.fn(async (_p: string, _t: string, out: string) => JSON.stringify(changedReply(out !== '')));
    render(<HkxConvertTools host={{ ConvertAsset } as unknown as HkxHost} initialPath="C:/a.hkx" disabled={false} />);
    expect((screen.getByText('Save conversion') as HTMLButtonElement).disabled).toBe(true);
    fireEvent.change(screen.getByLabelText('Convert output path'), { target: { value: 'C:/b.hkx' } });
    fireEvent.click(screen.getByText('Preview conversion'));
    await waitFor(() => expect(screen.getByRole('status').textContent).toContain('Preview: hk_2014.1.0-r1 to hk_2016.2.0-r1'));
    expect(screen.getByRole('status').textContent).toContain('1 fields changed, 1 classes left opaque');
    expect(screen.getByLabelText('Changed fields').textContent).toContain('hkaAnimationBinding 8 blendHint: 2 to 7');
    expect(screen.getByText(/Left opaque: hkaSkeletonMapper/)).toBeTruthy();
    expect(ConvertAsset.mock.calls[0][2]).toBe('');
    fireEvent.click(screen.getByText('Save conversion'));
    await waitFor(() => expect(screen.getByRole('status').textContent).toContain('Saved:'));
    expect(ConvertAsset.mock.calls[1]).toEqual(['C:/a.hkx', '', 'C:/b.hkx', 'abc123', false]);
  });

  it('states the same-version limit and shows an identity preview with no changed fields', async () => {
    // Constraint: detects an identity pass shown as a conversion that changed fields, or a panel that hides the missing version pairs.
    const ConvertAsset = vi.fn(async () => JSON.stringify(reply(false)));
    render(<HkxConvertTools host={{ ConvertAsset } as unknown as HkxHost} initialPath="C:/a.hkx" disabled={false} />);
    expect(screen.getByText(/no field-changing version pair exists yet/)).toBeTruthy();
    fireEvent.click(screen.getByText('Preview conversion'));
    await waitFor(() => expect(screen.getByRole('status').textContent).toContain('0 fields changed'));
    expect(screen.getByRole('status').textContent).toContain('same version, nothing to convert');
    expect(screen.queryByLabelText('Changed fields')).toBeNull();
  });

  it('shows a refused conversion with its reason and keeps Save disabled', async () => {
    // Constraint: detects a refused pair shown as a successful conversion or one that can still be saved.
    const ConvertAsset = vi.fn<(p: string, t: string) => Promise<string>>(async () => JSON.stringify(reply(false, {
      ok: false, status: 'unsupported-pair', reason: 'no verified conversion from a to b', changed_total: 0,
      changes: [], opaque_classes: [],
    })));
    render(<HkxConvertTools host={{ ConvertAsset } as unknown as HkxHost} initialPath="C:/a.hkx" disabled={false} />);
    fireEvent.change(screen.getByLabelText('Convert output path'), { target: { value: 'C:/b.hkx' } });
    fireEvent.change(screen.getByLabelText('Target version'), { target: { value: 'hk_2012.2.1-r1' } });
    fireEvent.click(screen.getByText('Preview conversion'));
    await waitFor(() => expect(screen.getByRole('status').textContent).toContain('Refused (unsupported-pair): no verified conversion'));
    expect((screen.getByText('Save conversion') as HTMLButtonElement).disabled).toBe(true);
    expect(ConvertAsset.mock.calls[0][1]).toBe('hk_2012.2.1-r1');
  });

  it('shows the host error and says an older host build cannot convert', async () => {
    // Constraint: detects a host error or a missing RPC shown as an unchanged, successful conversion.
    const ConvertAsset = vi.fn(async () => JSON.stringify({ error: 'HKX not found' }));
    const { unmount } = render(<HkxConvertTools host={{ ConvertAsset } as unknown as HkxHost} initialPath="C:/a.hkx" disabled={false} />);
    fireEvent.click(screen.getByText('Preview conversion'));
    await waitFor(() => expect(screen.getByRole('alert').textContent).toContain('HKX not found'));
    unmount();
    render(<HkxConvertTools host={{} as HkxHost} initialPath="C:/a.hkx" disabled={false} />);
    fireEvent.click(screen.getByText('Preview conversion'));
    await waitFor(() => expect(screen.getByRole('alert').textContent).toContain('cannot convert a Havok packfile'));
  });
});

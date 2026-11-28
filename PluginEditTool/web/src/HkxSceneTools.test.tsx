import { cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';
import type { HkxHost } from './backend';
import HkxSceneTools from './HkxSceneTools';

const PATH = 'C:\\mods\\asset.hkx';

const scene = {
  object_count: 3,
  has_root: true,
  variants: [
    { name: 'Scene Data', declared_class: 'hkxScene', class_name: 'hkxScene', object: 1, signature: 1547108353, layout_known: false },
    { name: 'Animation', declared_class: 'hkaAnimationContainer', class_name: 'hkaAnimationContainer', object: 2, signature: 7, layout_known: true },
  ],
  scene_objects: [{ class_name: 'hkxScene', object: 1, signature: 1547108353, size: 48, layout_known: false }],
  unknown_classes: ['hkxScene'],
  diagnostics: ['scene class hkxScene: units, up axis and environment variables are not decoded'],
  metadata: { decoded: false, reason: 'units, up axis and environment variables have no verified layout, so they are not read' },
};

const hostWith = (reply: unknown) => ({ ReadScene: vi.fn(async () => JSON.stringify(reply)) } as unknown as HkxHost);

afterEach(() => cleanup());

describe('HkxSceneTools', () => {
  it('shows the scene wrapper and says unit and axis are not decoded instead of showing defaults', async () => {
    // Constraint: detects a panel that lists the scene class but implies a unit or axis, which an export would then trust.
    render(<HkxSceneTools host={hostWith(scene)} initialPath={PATH} disabled={false} />);
    fireEvent.click(screen.getByText('Read asset structure'));
    await waitFor(() => expect(screen.getByLabelText('Named variants')).toBeTruthy());
    expect(screen.getByLabelText('Named variants').textContent).toContain('Scene Data: hkxScene (object 1) no verified layout');
    expect(screen.getByLabelText('Unknown classes').textContent).toContain('hkxScene');
    expect(screen.getByLabelText('Scene metadata').textContent).toContain('no verified layout');
    expect(screen.getByRole('status').textContent).toContain('units, up axis and environment not decoded');
  });

  it('shows the engine refusal for a file that cannot be read', async () => {
    // Constraint: detects a refused file reported as an asset with no variants.
    const host = { ReadScene: vi.fn(async () => JSON.stringify({ error: 'HKX not found: C:\\mods\\asset.hkx' })) } as unknown as HkxHost;
    render(<HkxSceneTools host={host} initialPath={PATH} disabled={false} />);
    fireEvent.click(screen.getByText('Read asset structure'));
    await waitFor(() => expect(screen.getByRole('alert').textContent).toContain('HKX not found'));
  });

  it('says the host build cannot read an asset structure instead of showing none', async () => {
    // Constraint: detects an older host without the RPC shown as a file with no scene.
    render(<HkxSceneTools host={{} as HkxHost} initialPath={PATH} disabled={false} />);
    fireEvent.click(screen.getByText('Read asset structure'));
    await waitFor(() => expect(screen.getByRole('alert').textContent).toContain('cannot read an HKX asset structure'));
  });
});

import { act, cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { afterEach, expect, it, vi } from 'vitest';
import { stubBackend } from './quest/questTestHarness';
import ReadOnlyFieldset from './ReadOnlyFieldset';
import WritableScope from './WritableScope';

type WritableBackend = ReturnType<typeof stubBackend> & {
  CopyAsOverride: ReturnType<typeof vi.fn>;
};

afterEach(() => { cleanup(); vi.unstubAllGlobals(); });

const page = (onCopied?: (target: string) => void) => (
  <WritableScope plugin="Patch.esp" formKey="000800:Patch.esp" onCopied={onCopied}>
    <ReadOnlyFieldset><button type="button">Edit field</button></ReadOnlyFieldset>
    <button type="button">Browse</button>
  </WritableScope>
);

it('Detects: a read-only plugin shows the game-file banner and the fieldset controls are really disabled', async () => {
  stubBackend({ GetEditablePlugins: vi.fn(async () => '["Other.esp"]') });
  render(page());
  expect(await screen.findByText(/Patch\.esp is a game file and cannot be changed/)).toBeTruthy();
  expect(screen.getByRole('button', { name: 'Edit field' }).matches(':disabled')).toBe(true);
});

it('Detects: a writable plugin shows no banner and leaves the fieldset controls enabled once the plugin list has loaded', async () => {
  const backend = stubBackend({ GetEditablePlugins: vi.fn(async () => '["Patch.esp"]') });
  render(page());
  await waitFor(() => expect(backend.GetEditablePlugins).toHaveBeenCalledTimes(1));
  await act(async () => { await new Promise(resolve => setTimeout(resolve, 0)); });
  expect(screen.queryByText(/is a game file and cannot be changed/)).toBeNull();
  expect(screen.getByRole('button', { name: 'Edit field' }).matches(':disabled')).toBe(false);
});

it('Detects: a read-only plugin leaves controls outside the fieldset enabled, so the record can still be browsed', async () => {
  stubBackend({ GetEditablePlugins: vi.fn(async () => '["Other.esp"]') });
  render(page());
  await screen.findByText(/is a game file and cannot be changed/);
  expect(screen.getByRole('button', { name: 'Browse' }).matches(':disabled')).toBe(false);
});

it('Detects: Copy as override calls CopyAsOverride with the read-only plugin, the record and the chosen target, then reports the copy', async () => {
  const backend = stubBackend({
    GetEditablePlugins: vi.fn(async () => '["Other.esp"]'),
    CopyAsOverride: vi.fn(async () => 'Copied the record to Other.esp'),
  }) as WritableBackend;
  const onCopied = vi.fn();
  render(page(onCopied));
  fireEvent.click(await screen.findByRole('button', { name: 'Copy as override' }));
  await waitFor(() => expect(onCopied).toHaveBeenCalledWith('Other.esp'));
  expect(backend.CopyAsOverride).toHaveBeenCalledWith('Patch.esp', '000800:Patch.esp', 'Other.esp', false);
  expect(await screen.findByText('Copied the record to Other.esp')).toBeTruthy();
});

it('Detects: a failed Copy as override shows the friendly read-only sentence and does not report a copy', async () => {
  stubBackend({
    GetEditablePlugins: vi.fn(async () => '["Other.esp"]'),
    CopyAsOverride: vi.fn(async () => {
      throw new Error('backend.CopyAsOverride: PluginReadOnly: Refusing to write protected or game-Data plugin: Other.esp');
    }),
  });
  const onCopied = vi.fn();
  render(page(onCopied));
  fireEvent.click(await screen.findByRole('button', { name: 'Copy as override' }));
  expect(await screen.findByText(/Other\.esp is a game file and cannot be changed/)).toBeTruthy();
  expect(onCopied).not.toHaveBeenCalled();
});

import type { ComponentProps } from 'react';
import { cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';
import NifVertexColors from './NifVertexColors';
import type { NifHost } from './backend';

type SetArgs = Parameters<NifHost['VertexColorSet']>;

const path = 'C:\\mods\\a.nif';
const saved = 'vertex color OK (in-process)\nSaved: C:\\mods\\a.nif';

const info = (coloured: boolean) => JSON.stringify({
  file: 'a.nif',
  shape_count: 1,
  shapes: [{ block: 7, name: 'Body', vertices: 3, color: { present: coloured, values: coloured ? 12 : 0, channels: {} } }],
});

function hostFor(coloured: boolean, setResult: string = saved) {
  const get = vi.fn<(nifPath: string) => Promise<string>>(async () => info(coloured));
  const set = vi.fn<(...args: SetArgs) => Promise<string>>(async () => setResult);
  const nif = { VertexColorGet: get, VertexColorSet: set } as unknown as NifHost;
  return { nif, get, set };
}

function renderPanel(nif: NifHost, props: Partial<ComponentProps<typeof NifVertexColors>> = {}) {
  const onSaved = vi.fn<() => void>();
  const appendLog = vi.fn<(line: string) => void>();
  render(<NifVertexColors nif={nif} nifPath={path} selection={null} disabled={false}
    onSaved={onSaved} appendLog={appendLog} {...props} />);
  return { onSaved, appendLog };
}

const button = (name: string) => screen.getByRole('button', { name }) as HTMLButtonElement;

afterEach(() => {
  cleanup();
  vi.restoreAllMocks();
});

describe('NIF vertex colour panel', () => {
  it('fill all sends the colour with no vertex list, then refreshes the shape list and reports success', async () => {
    const { nif, get, set } = hostFor(true);
    const { onSaved, appendLog } = renderPanel(nif);
    await waitFor(() => expect(button('Fill all').disabled).toBe(false));

    fireEvent.click(button('Fill all'));

    await waitFor(() => expect(onSaved).toHaveBeenCalledTimes(1));
    expect(set).toHaveBeenCalledWith(path, 7, 'fill', null, null, null, [1, 1, 1, 1], null, null, '');
    expect(get).toHaveBeenCalledTimes(2);
    expect(appendLog).toHaveBeenCalledWith('✓ filled all vertices');
  });

  it('fill selected stays disabled for a selection on another shape', async () => {
    const { nif, set } = hostFor(true);
    renderPanel(nif, { selection: { block: 9, verts: [0] } });
    await waitFor(() => expect(button('Fill all').disabled).toBe(false));

    expect(button('Fill selected').disabled).toBe(true);
    expect(set).not.toHaveBeenCalled();
  });

  it('fill selected sends the selected vertex indices for the chosen shape', async () => {
    const { nif, set } = hostFor(true);
    const { onSaved } = renderPanel(nif, { selection: { block: 7, verts: [2, 0] } });
    await waitFor(() => expect(button('Fill selected').disabled).toBe(false));

    fireEvent.click(button('Fill selected'));

    await waitFor(() => expect(onSaved).toHaveBeenCalledTimes(1));
    expect(set).toHaveBeenCalledWith(path, 7, 'fill', null, null, null, [1, 1, 1, 1], null, [2, 0], '');
  });

  it('channel set sends the chosen channel and value on every vertex', async () => {
    const { nif, set } = hostFor(true);
    const { onSaved } = renderPanel(nif);
    await waitFor(() => expect(button('Set channel').disabled).toBe(false));

    fireEvent.change(screen.getByLabelText('Channel'), { target: { value: 'alpha' } });
    fireEvent.change(screen.getByLabelText('Channel value'), { target: { value: '0.25' } });
    fireEvent.click(button('Set channel'));

    await waitFor(() => expect(onSaved).toHaveBeenCalledTimes(1));
    expect(set).toHaveBeenCalledWith(path, 7, 'channel', 'alpha', null, 0.25, null, null, null, '');
  });

  it('gradient sends the axis and both colours', async () => {
    const { nif, set } = hostFor(true);
    const { onSaved } = renderPanel(nif);
    await waitFor(() => expect(button('Gradient').disabled).toBe(false));

    fireEvent.change(screen.getByLabelText('Gradient axis'), { target: { value: 'y' } });
    fireEvent.change(screen.getByLabelText('End colour'), { target: { value: '#00ff00' } });
    fireEvent.click(button('Gradient'));

    await waitFor(() => expect(onSaved).toHaveBeenCalledTimes(1));
    expect(set).toHaveBeenCalledWith(path, 7, 'gradient', null, 'y', null, [1, 1, 1, 1], [0, 1, 0, 1], null, '');
  });

  it('enable colours sends enable with no colour arguments on a shape without them', async () => {
    const { nif, set } = hostFor(false);
    const { onSaved } = renderPanel(nif);
    await waitFor(() => expect(button('Enable colours').disabled).toBe(false));

    fireEvent.click(button('Enable colours'));

    await waitFor(() => expect(onSaved).toHaveBeenCalledTimes(1));
    expect(set).toHaveBeenCalledWith(path, 7, 'enable', null, null, null, null, null, null, '');
  });

  it('paint buttons are disabled on a shape without colours, and enable is disabled on one with them', async () => {
    const bare = hostFor(false);
    renderPanel(bare.nif);
    await waitFor(() => expect(button('Enable colours').disabled).toBe(false));
    expect(button('Fill all').disabled).toBe(true);
    expect(button('Set channel').disabled).toBe(true);
    expect(button('Gradient').disabled).toBe(true);
    cleanup();

    const painted = hostFor(true);
    renderPanel(painted.nif);
    await waitFor(() => expect(button('Fill all').disabled).toBe(false));
    expect(button('Enable colours').disabled).toBe(true);
  });

  it('shows a refusal from the native side and does not refresh the viewport', async () => {
    const refusal = 'Error: vertex colour on block 7: vertex colour values must be finite numbers in [0, 1]';
    const { nif } = hostFor(true, refusal);
    const { onSaved, appendLog } = renderPanel(nif);
    await waitFor(() => expect(button('Fill all').disabled).toBe(false));

    fireEvent.click(button('Fill all'));

    expect(await screen.findByText(`Paint failed -- ${refusal}`)).toBeTruthy();
    expect(onSaved).not.toHaveBeenCalled();
    expect(appendLog).toHaveBeenCalledWith(`✗ filled all vertices failed -- ${refusal}`);
  });

  it('shows the error when the shapes cannot be read', async () => {
    const nif = {
      VertexColorGet: vi.fn(async () => JSON.stringify({ error: 'NIF not found' })),
      VertexColorSet: vi.fn(),
    } as unknown as NifHost;
    renderPanel(nif);

    expect(await screen.findByText('Paint failed -- NIF not found')).toBeTruthy();
  });

  it('shows the error when the shape read throws synchronously', async () => {
    const nif = {
      VertexColorGet: vi.fn((): Promise<string> => { throw new Error('bridge gone'); }),
      VertexColorSet: vi.fn(),
    } as unknown as NifHost;
    renderPanel(nif);

    expect(await screen.findByText('Paint failed -- bridge gone')).toBeTruthy();
  });

  it('locks every button while UV edits are unsaved', async () => {
    const { nif } = hostFor(false);
    renderPanel(nif, { disabled: true });
    await waitFor(() => expect(screen.getByLabelText('Colour shape')).toBeTruthy());

    for (const name of ['Fill all', 'Fill selected', 'Set channel', 'Gradient', 'Enable colours']) {
      expect(button(name).disabled).toBe(true);
    }
  });
});

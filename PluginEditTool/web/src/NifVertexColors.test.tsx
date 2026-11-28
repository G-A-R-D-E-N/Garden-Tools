import type { ComponentProps } from 'react';
import { act, cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';
import NifVertexColors from './NifVertexColors';
import type { NifHost } from './backend';

type SetArgs = Parameters<NifHost['VertexColorSet']>;

const path = 'C:\\mods\\a.nif';
const saved = 'vertex color OK (in-process)\nSaved: C:\\mods\\a.nif';
const token = 'sha-a';

const info = (coloured: boolean, source = token) => JSON.stringify({
  file: 'a.nif',
  source,
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
  const view = render(<NifVertexColors nif={nif} nifPath={path} selection={null} disabled={false}
    onSaved={onSaved} appendLog={appendLog} {...props} />);
  const rerender = (next: NifHost, nifPath: string) => view.rerender(<NifVertexColors nif={next} nifPath={nifPath}
    selection={null} disabled={false} onSaved={onSaved} appendLog={appendLog} />);
  return { onSaved, appendLog, rerender };
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
    expect(set).toHaveBeenCalledWith(path, 7, 'fill', null, null, null, [1, 1, 1, 1], null, null, '', token);
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
    expect(set).toHaveBeenCalledWith(path, 7, 'fill', null, null, null, [1, 1, 1, 1], null, [2, 0], '', token);
  });

  it('channel set sends the chosen channel and value on every vertex', async () => {
    const { nif, set } = hostFor(true);
    const { onSaved } = renderPanel(nif);
    await waitFor(() => expect(button('Set channel').disabled).toBe(false));

    fireEvent.change(screen.getByLabelText('Channel'), { target: { value: 'alpha' } });
    fireEvent.change(screen.getByLabelText('Channel value'), { target: { value: '0.25' } });
    fireEvent.click(button('Set channel'));

    await waitFor(() => expect(onSaved).toHaveBeenCalledTimes(1));
    expect(set).toHaveBeenCalledWith(path, 7, 'channel', 'alpha', null, 0.25, null, null, null, '', token);
  });

  it('gradient sends the axis and both colours', async () => {
    const { nif, set } = hostFor(true);
    const { onSaved } = renderPanel(nif);
    await waitFor(() => expect(button('Gradient').disabled).toBe(false));

    fireEvent.change(screen.getByLabelText('Gradient axis'), { target: { value: 'y' } });
    fireEvent.change(screen.getByLabelText('End colour'), { target: { value: '#00ff00' } });
    fireEvent.click(button('Gradient'));

    await waitFor(() => expect(onSaved).toHaveBeenCalledTimes(1));
    expect(set).toHaveBeenCalledWith(path, 7, 'gradient', null, 'y', null, [1, 1, 1, 1], [0, 1, 0, 1], null, '', token);
  });

  it('enable colours sends enable with no colour arguments on a shape without them', async () => {
    const { nif, set } = hostFor(false);
    const { onSaved } = renderPanel(nif);
    await waitFor(() => expect(button('Enable colours').disabled).toBe(false));

    fireEvent.click(button('Enable colours'));

    await waitFor(() => expect(onSaved).toHaveBeenCalledTimes(1));
    expect(set).toHaveBeenCalledWith(path, 7, 'enable', null, null, null, null, null, null, '', token);
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

  it('paint is locked and the old shape is gone while the next file loads, then binds to that file (detects painting the previous file through its block number)', async () => {
    const { rerender } = renderPanel(hostFor(true).nif);
    await waitFor(() => expect(button('Fill all').disabled).toBe(false));

    let finish!: (text: string) => void;
    const get = vi.fn(() => new Promise<string>(resolve => { finish = resolve; }));
    const set = vi.fn(async () => saved);
    const otherPath = 'C:\\mods\\b.nif';
    rerender({ VertexColorGet: get, VertexColorSet: set } as unknown as NifHost, otherPath);

    expect(button('Fill all').disabled).toBe(true);
    expect(button('Enable colours').disabled).toBe(true);
    expect((screen.getByLabelText('Colour shape') as HTMLSelectElement).options.length).toBe(0);
    fireEvent.click(button('Fill all'));
    expect(set).not.toHaveBeenCalled();

    finish(info(true));
    await waitFor(() => expect(button('Fill all').disabled).toBe(false));
    fireEvent.click(button('Fill all'));
    await waitFor(() => expect(set).toHaveBeenCalledWith(otherPath, 7, 'fill', null, null, null, [1, 1, 1, 1], null, null, '', token));
  });

  it('a failed load of the next file keeps paint locked and shows the error (detects the previous file staying paintable after a failed read)', async () => {
    const { rerender } = renderPanel(hostFor(true).nif);
    await waitFor(() => expect(button('Fill all').disabled).toBe(false));

    const set = vi.fn(async () => saved);
    const broken = { VertexColorGet: vi.fn(async () => JSON.stringify({ error: 'NIF not found' })), VertexColorSet: set } as unknown as NifHost;
    rerender(broken, 'C:\\mods\\missing.nif');

    expect(await screen.findByText('Paint failed -- NIF not found')).toBeTruthy();
    expect(button('Fill all').disabled).toBe(true);
    expect(button('Enable colours').disabled).toBe(true);
    fireEvent.click(button('Fill all'));
    expect(set).not.toHaveBeenCalled();
  });

  it('reports busy to the panel from the send until the reload after a delayed paint response', async () => {
    const { nif, set } = hostFor(true);
    let finish: (value: string) => void = () => {};
    set.mockImplementationOnce(() => new Promise<string>(resolve => { finish = resolve; }));
    const onBusyChange = vi.fn<(busy: boolean) => void>();
    const { onSaved } = renderPanel(nif, { onBusyChange });
    await waitFor(() => expect(button('Fill all').disabled).toBe(false));

    fireEvent.click(button('Fill all'));

    await waitFor(() => expect(onBusyChange).toHaveBeenLastCalledWith(true));
    await act(async () => { finish(saved); });
    await waitFor(() => expect(onSaved).toHaveBeenCalledTimes(1));
    await waitFor(() => expect(onBusyChange).toHaveBeenLastCalledWith(false));
  });

  it('a second paint after its own save sends the digest of the refreshed read, not the one from before the save', async () => {
    let current = 'sha-first';
    const get = vi.fn(async () => info(true, current));
    const set = vi.fn<(...args: SetArgs) => Promise<string>>(async () => {
      current = 'sha-second';
      return saved;
    });
    const nif = { VertexColorGet: get, VertexColorSet: set } as unknown as NifHost;
    const { onSaved } = renderPanel(nif);
    await waitFor(() => expect(button('Fill all').disabled).toBe(false));

    fireEvent.click(button('Fill all'));
    await waitFor(() => expect(onSaved).toHaveBeenCalledTimes(1));
    await waitFor(() => expect(button('Fill all').disabled).toBe(false));
    fireEvent.click(button('Fill all'));

    await waitFor(() => expect(set).toHaveBeenCalledTimes(2));
    expect(set.mock.calls[0][10]).toBe('sha-first');
    expect(set.mock.calls[1][10]).toBe('sha-second');
  });

  it('a save from another panel re-reads the digest so the next paint is not refused as changed on disk, and a file switch reads the new file once', async () => {
    let current = 'sha-before-uv';
    const get = vi.fn(async () => info(true, current));
    const set = vi.fn<(...args: SetArgs) => Promise<string>>(async () => saved);
    const nif = { VertexColorGet: get, VertexColorSet: set } as unknown as NifHost;
    const props = { nif, nifPath: path, selection: null, disabled: false, onSaved: vi.fn(), appendLog: vi.fn() };
    const view = render(<NifVertexColors {...props} reloadKey={1} />);
    await waitFor(() => expect(button('Fill all').disabled).toBe(false));

    current = 'sha-after-uv';
    view.rerender(<NifVertexColors {...props} reloadKey={2} />);
    await waitFor(() => expect(get).toHaveBeenCalledTimes(2));
    await act(async () => {});
    fireEvent.click(button('Fill all'));

    await waitFor(() => expect(set).toHaveBeenCalledTimes(1));
    expect(set.mock.calls[0][10]).toBe('sha-after-uv');
    expect((screen.getByLabelText('Colour shape') as HTMLSelectElement).value).toBe('7');

    await waitFor(() => expect(props.onSaved).toHaveBeenCalledTimes(1));
    expect(get).toHaveBeenCalledTimes(3);
    view.rerender(<NifVertexColors {...props} nifPath="C:\\mods\\b.nif" reloadKey={3} />);
    await waitFor(() => expect(get).toHaveBeenCalledTimes(4));
    await act(async () => {});
    expect(get).toHaveBeenCalledTimes(4);
  });
});

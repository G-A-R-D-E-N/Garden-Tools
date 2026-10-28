import { act, cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';
import SwfPanel from './SwfPanel';

afterEach(cleanup);

function stubBridge(overrides: Record<string, unknown> = {}) {
  const swf = {
    Open: vi.fn(async () => JSON.stringify({ revision: 0, undo: 0, redo: 0, dirty: false })),
    Characters: vi.fn(async () => JSON.stringify({ characters: [{ id: 11, kind: 'EditText', tagIndex: 0, name: '', imported: '' }] })),
    FrameLabels: vi.fn(async () => JSON.stringify({ labels: [{ labelIndex: 0, frame: 0, name: 'start' }] })),
    Frame: vi.fn(async () => JSON.stringify({ rows: [{ depth: 1, character: 11, name: '', x: 10, y: 20, scaleX: 1, scaleY: 1, rotation: 0, instanceId: 7, boundsX: 5, boundsY: 6, boundsWidth: 40, boundsHeight: 30, hasBounds: true, clipDepth: 0 }] })),
    Timeline: vi.fn(async (index: number) => JSON.stringify({ characterId: index === 1 ? 14 : 0, frameCount: 1 })),
    RenameLabel: vi.fn(async () => JSON.stringify({ changed: true, revision: 1, undo: 1, redo: 0, dirty: true })),
    Undo: vi.fn(async () => JSON.stringify({ changed: true, revision: 2, undo: 0, redo: 1, dirty: true })),
    Redo: vi.fn(async () => JSON.stringify({ changed: true, revision: 3, undo: 1, redo: 0, dirty: true })),
    State: vi.fn(async () => JSON.stringify({ revision: 0, undo: 0, redo: 0, dirty: false })),
    Save: vi.fn(async () => JSON.stringify({ saved: 'Interface/Test.swf' })),
    AddLabel: vi.fn(async () => JSON.stringify({ changed: true, revision: 5, undo: 3, redo: 0, dirty: true })),
    RemoveLabel: vi.fn(async () => JSON.stringify({ changed: true, revision: 9, undo: 7, redo: 0, dirty: true })),
    Placements: vi.fn(async () => JSON.stringify({ placements: [{ tagIndex: 2, depth: 1, character: 11, insideSprite: false }] })),
    SetDepth: vi.fn(async () => JSON.stringify({ changed: true, revision: 6, undo: 4, redo: 0, dirty: true })),
    PlacementMatrix: vi.fn(async () => JSON.stringify({ x: 0, y: 0, scaleX: 0, scaleY: 0, rotate0: 0, rotate1: 0 })),
    SetTransform: vi.fn(async () => JSON.stringify({ changed: true, revision: 7, undo: 5, redo: 0, dirty: true })),
    InsertKeyframe: vi.fn(async () => JSON.stringify({ changed: true, revision: 8, undo: 6, redo: 0, dirty: true })),
    ReadEditText: vi.fn(async () => JSON.stringify({ id: 11, text: 'hello', color: 'FF112233', x0: 0, x1: 400, y0: 0, y1: 200 })),
    EditEditText: vi.fn(async () => JSON.stringify({ changed: true, revision: 4, undo: 2, redo: 0, dirty: true })),
    AbcBlocks: vi.fn(async () => JSON.stringify({ blocks: [{ abc: 0, name: 'Interface', classes: ['MenuClass'], scripts: ['Menu'], methods: [{ index: 0, name: 'onLoad', returnType: 'void', hasBody: true }] }] })),
    Disassemble: vi.fn(async () => JSON.stringify({ text: '      0 getlocal0\n      1 returnvoid\n' })),
    ...overrides,
  };
  vi.stubGlobal('chrome', { webview: { hostObjects: { swf } } });
  return swf;
}

describe('SwfPanel', () => {
  it('selects, renames, undoes, redoes and saves through the history', async () => {
    const swf = stubBridge({ State: vi.fn(async () => JSON.stringify({ revision: 2, undo: 1, redo: 1, dirty: true })) });
    render(<SwfPanel onClose={() => {}} />);

    fireEvent.change(screen.getByPlaceholderText('Interface/Test.swf'), { target: { value: 'Interface/Test.swf' } });
    await act(async () => { fireEvent.click(screen.getByText('Open')); });
    await waitFor(() => expect(swf.Characters).toHaveBeenCalled());
    expect(screen.getByText(/EditText #11/)).toBeTruthy();
    expect(await screen.findByText(/root · start/)).toBeTruthy();

    fireEvent.click(screen.getByText(/root · start/));
    fireEvent.change(screen.getByDisplayValue('start'), { target: { value: 'menu' } });
    await act(async () => { fireEvent.click(screen.getByText('Rename')); });
    // Constraint: detects a rename sent by tag index instead of the timeline and the per-timeline label index.
    await waitFor(() => expect(swf.RenameLabel).toHaveBeenCalledWith(0, 0, 'menu'));

    await act(async () => { fireEvent.click(screen.getByTitle('Undo')); });
    await waitFor(() => expect(swf.Undo).toHaveBeenCalled());
    await act(async () => { fireEvent.click(screen.getByTitle('Redo')); });
    await waitFor(() => expect(swf.Redo).toHaveBeenCalled());
    await act(async () => { fireEvent.click(screen.getByTitle('Save')); });
    await waitFor(() => expect(swf.Save).toHaveBeenCalled());
  });

  it('shows a Radicle refusal verbatim', async () => {
    stubBridge({ RenameLabel: vi.fn(async () => JSON.stringify({ error: 'a frame label with that name already exists on this timeline' })) });
    render(<SwfPanel onClose={() => {}} />);
    fireEvent.change(screen.getByPlaceholderText('Interface/Test.swf'), { target: { value: 'Interface/Test.swf' } });
    await act(async () => { fireEvent.click(screen.getByText('Open')); });
    fireEvent.click(await screen.findByText(/root · start/));
    await act(async () => { fireEvent.click(screen.getByText('Rename')); });
    expect(await screen.findByText(/already exists on this timeline/)).toBeTruthy();
  });

  it('edits edit-text fields and keeps them in the history', async () => {
    const swf = stubBridge();
    render(<SwfPanel onClose={() => {}} />);
    fireEvent.change(screen.getByPlaceholderText('Interface/Test.swf'), { target: { value: 'Interface/Test.swf' } });
    await act(async () => { fireEvent.click(screen.getByText('Open')); });
    await act(async () => { fireEvent.click(await screen.findByText(/EditText #11/)); });
    await waitFor(() => expect(swf.ReadEditText).toHaveBeenCalledWith(0));

    const textField = await screen.findByDisplayValue('hello');
    fireEvent.change(textField, { target: { value: 'hello world' } });
    await act(async () => { fireEvent.click(screen.getByText('Apply')); });
    await waitFor(() => expect(swf.EditEditText).toHaveBeenCalledWith(0, 'hello world', 'FF112233', 0, 400, 0, 200));
  });

  it('changes a placement depth through the history', async () => {
    const swf = stubBridge();
    render(<SwfPanel onClose={() => {}} />);
    fireEvent.change(screen.getByPlaceholderText('Interface/Test.swf'), { target: { value: 'Interface/Test.swf' } });
    await act(async () => { fireEvent.click(screen.getByText('Open')); });
    await act(async () => { fireEvent.click(await screen.findByTestId('swf-row-7')); });
    await waitFor(() => expect(swf.PlacementMatrix).toHaveBeenCalledWith(2));
    const depthField = await screen.findByDisplayValue('1');
    fireEvent.change(depthField, { target: { value: '7' } });
    await act(async () => { fireEvent.click(screen.getByText('Apply depth')); });
    await waitFor(() => expect(swf.SetDepth).toHaveBeenCalledWith(2, 7));

    await act(async () => { fireEvent.click(screen.getByText('Apply transform')); });
    await waitFor(() => expect(swf.SetTransform).toHaveBeenCalledWith(2, 0, 0, 0, 0, 0, 0));
    expect(screen.getByTestId('swf-box-7')).toBeTruthy();
  });

  it('lists ABC classes and methods and shows the selected method instructions', async () => {
    // Constraint: detects the ABC listing or the Disassemble call being dropped from the panel.
    const swf = stubBridge();
    render(<SwfPanel onClose={() => {}} />);
    fireEvent.change(screen.getByPlaceholderText('Interface/Test.swf'), { target: { value: 'Interface/Test.swf' } });
    await act(async () => { fireEvent.click(screen.getByText('Open')); });
    expect(await screen.findByText(/class MenuClass/)).toBeTruthy();
    expect(screen.getByText(/script Menu/)).toBeTruthy();
    await act(async () => { fireEvent.click(screen.getByText(/onLoad : void/)); });
    await waitFor(() => expect(swf.Disassemble).toHaveBeenCalledWith(0, 0));
    await waitFor(() => expect(screen.getByLabelText('instructions').textContent).toContain('returnvoid'));
  });

  it('shows the empty state when the SWF has no ABC blocks', async () => {
    // Constraint: detects a SWF without DoABC tags rendering a blank or stale ABC list instead of the empty message.
    stubBridge({ AbcBlocks: vi.fn(async () => JSON.stringify({ blocks: [] })) });
    render(<SwfPanel onClose={() => {}} />);
    fireEvent.change(screen.getByPlaceholderText('Interface/Test.swf'), { target: { value: 'Interface/Test.swf' } });
    await act(async () => { fireEvent.click(screen.getByText('Open')); });
    expect(await screen.findByText('No ABC scripts in this SWF.')).toBeTruthy();
  });

  it('shows an ABC listing refusal instead of the empty state', async () => {
    // Constraint: detects an ABC refusal from Radicle being swallowed or shown as "no ABC scripts".
    stubBridge({ AbcBlocks: vi.fn(async () => JSON.stringify({ error: 'ABC block 0 is malformed' })) });
    render(<SwfPanel onClose={() => {}} />);
    fireEvent.change(screen.getByPlaceholderText('Interface/Test.swf'), { target: { value: 'Interface/Test.swf' } });
    await act(async () => { fireEvent.click(screen.getByText('Open')); });
    expect(await screen.findByText(/ABC block 0 is malformed/)).toBeTruthy();
    expect(screen.queryByText('No ABC scripts in this SWF.')).toBeNull();
  });

  it('drops a Disassemble reply for a method that is no longer selected', async () => {
    // Invariant: a late reply for an earlier method must not replace the listing of the selected method; detects out-of-order replies overwriting it.
    let resolveFirst: (text: string) => void = () => {};
    stubBridge({
      AbcBlocks: vi.fn(async () => JSON.stringify({ blocks: [{ abc: 0, name: 'Interface', classes: [], scripts: [], methods: [
        { index: 0, name: 'onLoad', returnType: 'void', hasBody: true },
        { index: 1, name: 'onUnload', returnType: 'void', hasBody: true },
      ] }] })),
      Disassemble: vi.fn()
        .mockImplementationOnce(() => new Promise<string>(resolve => { resolveFirst = resolve; }))
        .mockImplementationOnce(async () => JSON.stringify({ text: 'second listing' })),
    });
    render(<SwfPanel onClose={() => {}} />);
    fireEvent.change(screen.getByPlaceholderText('Interface/Test.swf'), { target: { value: 'Interface/Test.swf' } });
    await act(async () => { fireEvent.click(screen.getByText('Open')); });
    fireEvent.click(await screen.findByText(/onLoad : void/));
    fireEvent.click(screen.getByText(/onUnload : void/));
    await waitFor(() => expect(screen.getByLabelText('instructions').textContent).toBe('second listing'));
    await act(async () => {
      resolveFirst(JSON.stringify({ text: 'stale listing' }));
      await new Promise(done => setTimeout(done, 0));
    });
    expect(screen.getByLabelText('instructions').textContent).toBe('second listing');
    expect(screen.getByText(/onUnload : void/).getAttribute('aria-pressed')).toBe('true');
  });

  it('clears the ABC listing when a re-open is refused', async () => {
    // Constraint: detects a refused re-open leaving the previous document's ABC listing on screen.
    stubBridge({
      Open: vi.fn()
        .mockResolvedValueOnce(JSON.stringify({ revision: 0, undo: 0, redo: 0, dirty: false }))
        .mockResolvedValue(JSON.stringify({ error: 'Interface/Missing.swf was not found' })),
    });
    render(<SwfPanel onClose={() => {}} />);
    fireEvent.change(screen.getByPlaceholderText('Interface/Test.swf'), { target: { value: 'Interface/Test.swf' } });
    await act(async () => { fireEvent.click(screen.getByText('Open')); });
    expect(await screen.findByText(/class MenuClass/)).toBeTruthy();
    await act(async () => { fireEvent.click(screen.getByText('Open')); });
    expect(await screen.findByText(/was not found/)).toBeTruthy();
    expect(screen.queryByText(/class MenuClass/)).toBeNull();
    expect(screen.getByText('No ABC scripts in this SWF.')).toBeTruthy();
  });

  it('lists the labels of the selected timeline and renames the one picked from it', async () => {
    // Constraint: detects the label list staying on the root timeline, a sprite label renamed with the root's timeline, or the scrubber index sent to Radicle where the DefineSprite character id belongs.
    const swf = stubBridge({
      Frame: vi.fn(async (timeline: number) => timeline === 14
        ? JSON.stringify({ error: 'timeline 14 is not a frame index' })
        : JSON.stringify({ rows: [] })),
      FrameLabels: vi.fn(async (characterId: number) => JSON.stringify({
        labels: characterId === 14 ? [{ labelIndex: 0, frame: 0, name: 'inner' }] : [{ labelIndex: 0, frame: 0, name: 'start' }],
      })),
    });
    render(<SwfPanel onClose={() => {}} />);
    fireEvent.change(screen.getByPlaceholderText('Interface/Test.swf'), { target: { value: 'Interface/Test.swf' } });
    await act(async () => { fireEvent.click(screen.getByText('Open')); });
    expect(await screen.findByText(/root · start/)).toBeTruthy();

    fireEvent.change(screen.getByLabelText('Timeline'), { target: { value: '1' } });
    expect(await screen.findByText(/sprite 14 · inner/)).toBeTruthy();
    expect(swf.Timeline).toHaveBeenCalledWith(1);
    expect(swf.FrameLabels).toHaveBeenLastCalledWith(14);
    expect(swf.Frame).toHaveBeenLastCalledWith(1, 0);

    fireEvent.click(screen.getByText(/sprite 14 · inner/));
    fireEvent.change(screen.getByDisplayValue('inner'), { target: { value: 'outer' } });
    await act(async () => { fireEvent.click(screen.getByText('Rename')); });
    await waitFor(() => expect(swf.RenameLabel).toHaveBeenCalledWith(14, 0, 'outer'));
  });

  it('drops a label reply for a timeline the scrubber has already left', async () => {
    // Invariant: a late label reply for an earlier timeline must not replace the list of the timeline on screen; detects out-of-order replies overwriting the labels.
    let resolveSprite: (text: string) => void = () => {};
    const swf = stubBridge({
      FrameLabels: vi.fn((characterId: number) => characterId === 14
        ? new Promise<string>(resolve => { resolveSprite = resolve; })
        : Promise.resolve(JSON.stringify({ labels: [{ labelIndex: 0, frame: 0, name: 'start' }] }))),
    });
    render(<SwfPanel onClose={() => {}} />);
    fireEvent.change(screen.getByPlaceholderText('Interface/Test.swf'), { target: { value: 'Interface/Test.swf' } });
    await act(async () => { fireEvent.click(screen.getByText('Open')); });
    expect(await screen.findByText(/root · start/)).toBeTruthy();

    fireEvent.change(screen.getByLabelText('Timeline'), { target: { value: '1' } });
    await waitFor(() => expect(swf.FrameLabels).toHaveBeenLastCalledWith(14));
    fireEvent.change(screen.getByLabelText('Timeline'), { target: { value: '0' } });
    await waitFor(() => expect(swf.FrameLabels).toHaveBeenLastCalledWith(0));
    await act(async () => {
      resolveSprite(JSON.stringify({ labels: [{ labelIndex: 0, frame: 0, name: 'inner' }] }));
      await new Promise(done => setTimeout(done, 0));
    });
    expect(await screen.findByText(/root · start/)).toBeTruthy();
    expect(screen.queryByText(/inner/)).toBeNull();
  });

  it('keeps the picked label selected through a reload of its timeline', async () => {
    // Invariant: a reload of the same timeline keeps the picked label selected; detects Remove going disabled after a rename reloads the list.
    const swf = stubBridge();
    render(<SwfPanel onClose={() => {}} />);
    fireEvent.change(screen.getByPlaceholderText('Interface/Test.swf'), { target: { value: 'Interface/Test.swf' } });
    await act(async () => { fireEvent.click(screen.getByText('Open')); });
    fireEvent.click(await screen.findByText(/root · start/));
    fireEvent.change(screen.getByDisplayValue('start'), { target: { value: 'menu' } });
    await act(async () => { fireEvent.click(screen.getByText('Rename')); });
    await waitFor(() => expect(swf.FrameLabels).toHaveBeenCalledTimes(2));
    await act(async () => { fireEvent.click(screen.getByText('Remove')); });
    await waitFor(() => expect(swf.RemoveLabel).toHaveBeenCalledWith(0, 0));
  });

  it('clears the label selection after Remove so the label that moves into its index is not acted on', async () => {
    // Invariant: a label removal clears the selection, so Rename and Remove stay disabled rather than acting on the label that moved into the picked index; detects a selection kept by label index across a removal.
    const swf = stubBridge({
      FrameLabels: vi.fn()
        .mockResolvedValueOnce(JSON.stringify({ labels: [{ labelIndex: 0, frame: 0, name: 'start' }, { labelIndex: 1, frame: 2, name: 'a' }] }))
        .mockResolvedValue(JSON.stringify({ labels: [{ labelIndex: 0, frame: 2, name: 'a' }] })),
    });
    render(<SwfPanel onClose={() => {}} />);
    fireEvent.change(screen.getByPlaceholderText('Interface/Test.swf'), { target: { value: 'Interface/Test.swf' } });
    await act(async () => { fireEvent.click(screen.getByText('Open')); });
    fireEvent.click(await screen.findByText(/root · start/));
    await act(async () => { fireEvent.click(screen.getByText('Remove')); });
    await waitFor(() => expect(swf.RemoveLabel).toHaveBeenCalledWith(0, 0));
    await waitFor(() => expect(screen.queryByText(/root · start/)).toBeNull());
    expect(screen.getByText(/root · a/)).toBeTruthy();
    expect((screen.getByText('Remove') as HTMLButtonElement).disabled).toBe(true);
    expect((screen.getByText('Rename') as HTMLButtonElement).disabled).toBe(true);
  });

  it('removes the selected label by its timeline and label index', async () => {
    // Constraint: detects Remove dropping the timeline or sending a tag index, which would delete another label on a sprite timeline.
    const swf = stubBridge({ FrameLabels: vi.fn(async () => JSON.stringify({ labels: [{ labelIndex: 2, frame: 4, name: 'loop' }] })) });
    render(<SwfPanel onClose={() => {}} />);
    fireEvent.change(screen.getByPlaceholderText('Interface/Test.swf'), { target: { value: 'Interface/Test.swf' } });
    await act(async () => { fireEvent.click(screen.getByText('Open')); });
    fireEvent.click(await screen.findByText(/root · loop/));
    await act(async () => { fireEvent.click(screen.getByText('Remove')); });
    await waitFor(() => expect(swf.RemoveLabel).toHaveBeenCalledWith(0, 2));
  });

  it('reloads the label list from the history after an undo', async () => {
    // Invariant: the label and character lists are read again from Radicle after an undo; detects a label or character that the undo removed staying on screen.
    const swf = stubBridge({
      Open: vi.fn(async () => JSON.stringify({ revision: 1, undo: 1, redo: 0, dirty: true })),
      FrameLabels: vi.fn()
        .mockResolvedValueOnce(JSON.stringify({ labels: [{ labelIndex: 0, frame: 0, name: 'start' }] }))
        .mockResolvedValue(JSON.stringify({ labels: [] })),
    });
    render(<SwfPanel onClose={() => {}} />);
    fireEvent.change(screen.getByPlaceholderText('Interface/Test.swf'), { target: { value: 'Interface/Test.swf' } });
    await act(async () => { fireEvent.click(screen.getByText('Open')); });
    expect(await screen.findByText(/root · start/)).toBeTruthy();
    await act(async () => { fireEvent.click(screen.getByTitle('Undo')); });
    await waitFor(() => expect(screen.queryByText(/root · start/)).toBeNull());
    expect(swf.FrameLabels).toHaveBeenCalledTimes(2);
    expect(swf.Characters).toHaveBeenCalledTimes(2);
  });

  it('previews a selected shape as an SVG image and drops a stale ShapeSvg reply', async () => {
    // Invariant: a late ShapeSvg reply for an earlier shape must not replace the preview of the shape selected last, and selecting a non-shape clears the preview; detects out-of-order replies overwriting it and a stale preview surviving a non-shape selection.
    let resolveFirst: (text: string) => void = () => {};
    const second = '<svg xmlns="http://www.w3.org/2000/svg" id="second"></svg>';
    const swf = stubBridge({
      Characters: vi.fn(async () => JSON.stringify({ characters: [
        { id: 20, kind: 'Shape', tagIndex: 0, name: '', imported: '' },
        { id: 21, kind: 'Shape', tagIndex: 1, name: '', imported: '' },
        { id: 22, kind: 'EditText', tagIndex: 2, name: '', imported: '' },
      ] })),
      ShapeSvg: vi.fn()
        .mockImplementationOnce(() => new Promise<string>(resolve => { resolveFirst = resolve; }))
        .mockImplementationOnce(async () => JSON.stringify({ svg: second })),
    });
    render(<SwfPanel onClose={() => {}} />);
    fireEvent.change(screen.getByPlaceholderText('Interface/Test.swf'), { target: { value: 'Interface/Test.swf' } });
    await act(async () => { fireEvent.click(screen.getByText('Open')); });
    fireEvent.click(await screen.findByText(/Shape #20/));
    fireEvent.click(screen.getByText(/Shape #21/));
    expect((await screen.findByAltText('Shape preview')).getAttribute('src'))
      .toBe(`data:image/svg+xml;utf8,${encodeURIComponent(second)}`);
    await act(async () => {
      resolveFirst(JSON.stringify({ svg: '<svg id="stale"></svg>' }));
      await new Promise(done => setTimeout(done, 0));
    });
    expect(screen.getByAltText('Shape preview').getAttribute('src'))
      .toBe(`data:image/svg+xml;utf8,${encodeURIComponent(second)}`);
    expect(swf.ShapeSvg).toHaveBeenNthCalledWith(1, 20);
    expect(swf.ShapeSvg).toHaveBeenNthCalledWith(2, 21);

    fireEvent.click(screen.getByText(/EditText #22/));
    await waitFor(() => expect(screen.queryByAltText('Shape preview')).toBeNull());
    expect(swf.ShapeSvg).toHaveBeenCalledTimes(2);
  });
});
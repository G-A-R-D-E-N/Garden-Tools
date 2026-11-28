import { act, cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import SwfPanel from './SwfPanel';
import type { ConfirmRequest } from './useDialogs';

afterEach(cleanup);

const confirm = vi.hoisted(() => vi.fn<(req: ConfirmRequest) => Promise<boolean>>());
vi.mock('./useDialogs', () => ({ useDialogs: () => ({ confirm }) }));
beforeEach(() => { confirm.mockReset(); confirm.mockResolvedValue(true); });

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
    InspectText: vi.fn(async () => JSON.stringify({ text: '## Interface\\Test.swf\nStage 11000x8000\nRoot frame labels: start@1\n' })),
    ActionTags: vi.fn(async () => JSON.stringify({ tags: [{ tagIndex: 9, code: 12, insideSprite: false }] })),
    Avm1Disassemble: vi.fn(async () => JSON.stringify({ text: '0000  Play\n0001  End\n' })),
    FrameSvg: vi.fn(async () => JSON.stringify({ svg: '<svg></svg>' })),
    ShapeSvg: vi.fn(async () => JSON.stringify({ svg: '<svg></svg>' })),
    BitmapPng: vi.fn(async () => JSON.stringify({ png: '' })),
    BitmapJpeg: vi.fn(async () => JSON.stringify({ jpeg: '' })),
    AbcText: vi.fn(async () => JSON.stringify({ text: 'abc 46.16\n  string 1 "Foo"\n' })),
    ApplyAbcText: vi.fn(async () => JSON.stringify({ changed: true, revision: 1, undo: 1, redo: 0, dirty: true })),
    BrowseForFile: vi.fn(async () => ''),
    ...overrides,
  };
  vi.stubGlobal('chrome', { webview: { hostObjects: { swf } } });
  return swf;
}

async function openAbcTab(): Promise<HTMLTextAreaElement> {
  render(<SwfPanel onClose={() => {}} />);
  fireEvent.change(screen.getByPlaceholderText('Interface/Test.swf'), { target: { value: 'Interface/Test.swf' } });
  await act(async () => { fireEvent.click(screen.getByText('Open')); });
  await act(async () => { fireEvent.click(screen.getByRole('tab', { name: 'ABC scripts' })); });
  await waitFor(() => expect((screen.getByLabelText('ABC script') as HTMLTextAreaElement).value).toContain('"Foo"'));
  return screen.getByLabelText('ABC script') as HTMLTextAreaElement;
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

  it('shows the inspect text and the disassembly of a picked DoAction tag', async () => {
    const swf = stubBridge();
    render(<SwfPanel onClose={() => {}} />);
    fireEvent.change(screen.getByPlaceholderText('Interface/Test.swf'), { target: { value: 'Interface/Test.swf' } });
    await act(async () => { fireEvent.click(screen.getByText('Open')); });
    expect(await screen.findByText(/Root frame labels: start@1/)).toBeTruthy();
    await act(async () => { fireEvent.click(screen.getByText('DoAction @9')); });
    await waitFor(() => expect(swf.Avm1Disassemble).toHaveBeenCalledWith(9));
    await waitFor(() => expect(screen.getByLabelText('avm1 disassembly').textContent).toContain('0000  Play'));
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

  it('drops a stale ReadEditText reply that lands after a newer selection', async () => {
    // Invariant: a late ReadEditText reply for an edit-text picked earlier must not open its form over the shape picked last; detects an edit-text reply overwriting state after a newer selection.
    let resolveEditText: (text: string) => void = () => {};
    const shape = '<svg xmlns="http://www.w3.org/2000/svg" id="shape"></svg>';
    stubBridge({
      Characters: vi.fn(async () => JSON.stringify({ characters: [
        { id: 11, kind: 'EditText', tagIndex: 0, name: '', imported: '' },
        { id: 21, kind: 'Shape', tagIndex: 1, name: '', imported: '' },
      ] })),
      ReadEditText: vi.fn(() => new Promise<string>(resolve => { resolveEditText = resolve; })),
      ShapeSvg: vi.fn(async () => JSON.stringify({ svg: shape })),
    });
    render(<SwfPanel onClose={() => {}} />);
    fireEvent.change(screen.getByPlaceholderText('Interface/Test.swf'), { target: { value: 'Interface/Test.swf' } });
    await act(async () => { fireEvent.click(screen.getByText('Open')); });
    fireEvent.click(await screen.findByText(/EditText #11/));
    fireEvent.click(screen.getByText(/Shape #21/));
    expect((await screen.findByAltText('Shape preview')).getAttribute('src'))
      .toBe(`data:image/svg+xml;utf8,${encodeURIComponent(shape)}`);
    await act(async () => {
      resolveEditText(JSON.stringify({ id: 11, text: 'stale', color: 'FF000000', x0: 0, x1: 1, y0: 0, y1: 1 }));
      await new Promise(done => setTimeout(done, 0));
    });
    expect(screen.getByText('Select an edit-text character to edit its fields.')).toBeTruthy();
    expect(screen.queryByDisplayValue('stale')).toBeNull();
    expect(screen.getByAltText('Shape preview').getAttribute('src'))
      .toBe(`data:image/svg+xml;utf8,${encodeURIComponent(shape)}`);
  });

  it('previews a picked bitmap as a PNG data URL by its flat tag index and shows a refused BitmapPng in the error bar', async () => {
    // Invariant: a bitmap pick sends its tagIndex (not its character id) to BitmapPng and renders the reply as a base64 PNG; a refusal shows its message in the error bar and clears the previous preview; detects a bitmap read by character id, a PNG served with the wrong MIME type, and a refusal swallowed into a stale image.
    const png = 'iVBORw0KGgoAAAANSUhEUgAAAAIAAAACCAYAAABytg==';
    const swf = stubBridge({
      Characters: vi.fn(async () => JSON.stringify({ characters: [
        { id: 30, kind: 'Bitmap', tagIndex: 4, name: '', imported: '' },
        { id: 31, kind: 'Bitmap', tagIndex: 5, name: '', imported: '' },
      ] })),
      BitmapPng: vi.fn()
        .mockImplementationOnce(async () => JSON.stringify({ png }))
        .mockImplementationOnce(async () => JSON.stringify({ error: 'the tag carries no decoded bitmap' })),
    });
    render(<SwfPanel onClose={() => {}} />);
    fireEvent.change(screen.getByPlaceholderText('Interface/Test.swf'), { target: { value: 'Interface/Test.swf' } });
    await act(async () => { fireEvent.click(screen.getByText('Open')); });
    fireEvent.click(await screen.findByText(/Bitmap #30/));
    expect((await screen.findByAltText('Bitmap preview')).getAttribute('src'))
      .toBe(`data:image/png;base64,${png}`);
    expect(swf.BitmapPng).toHaveBeenNthCalledWith(1, 4);

    fireEvent.click(screen.getByText(/Bitmap #31/));
    expect(await screen.findByText('the tag carries no decoded bitmap')).toBeTruthy();
    expect(screen.queryByAltText('Bitmap preview')).toBeNull();
    expect(swf.BitmapPng).toHaveBeenNthCalledWith(2, 5);
  });

  it('falls back to BitmapJpeg when BitmapPng refuses a JPEG-defined bitmap as NotFound and shows the JPEG as an image/jpeg data URL', async () => {
    // Invariant: only a NotFound from BitmapPng falls back to BitmapJpeg for the same flat tag index, and its jpeg reply is shown as a base64 JPEG data URL; detects a JPEG bitmap left as an error, a fallback keyed by character id, or a JPEG served under the PNG MIME type.
    const jpeg = '/9j/2Q==';
    const swf = stubBridge({
      Characters: vi.fn(async () => JSON.stringify({ characters: [
        { id: 30, kind: 'Bitmap', tagIndex: 4, name: '', imported: '' },
      ] })),
      BitmapPng: vi.fn(async () => JSON.stringify({ error: 'the tag carries no decoded bitmap', status: 'NotFound' })),
      BitmapJpeg: vi.fn(async () => JSON.stringify({ jpeg })),
    });
    render(<SwfPanel onClose={() => {}} />);
    fireEvent.change(screen.getByPlaceholderText('Interface/Test.swf'), { target: { value: 'Interface/Test.swf' } });
    await act(async () => { fireEvent.click(screen.getByText('Open')); });
    fireEvent.click(await screen.findByText(/Bitmap #30/));
    expect((await screen.findByAltText('Bitmap preview')).getAttribute('src'))
      .toBe(`data:image/jpeg;base64,${jpeg}`);
    expect(swf.BitmapPng).toHaveBeenCalledWith(4);
    expect(swf.BitmapJpeg).toHaveBeenCalledWith(4);
    expect(screen.queryByText('the tag carries no decoded bitmap')).toBeNull();
  });

  it('shows a PNG-encodable bitmap from its PNG reply without asking BitmapJpeg', async () => {
    // Invariant: a bitmap that BitmapPng exports is previewed from that PNG alone; detects a PNG bitmap also fetched as a JPEG, or a JPEG reply replacing the PNG preview.
    const png = 'iVBORw0KGgoAAAANSUhEUgAAAAIAAAACCAYAAABytg==';
    const swf = stubBridge({
      Characters: vi.fn(async () => JSON.stringify({ characters: [
        { id: 30, kind: 'Bitmap', tagIndex: 4, name: '', imported: '' },
      ] })),
      BitmapPng: vi.fn(async () => JSON.stringify({ png })),
      BitmapJpeg: vi.fn(async () => JSON.stringify({ jpeg: '/9j/2Q==' })),
    });
    render(<SwfPanel onClose={() => {}} />);
    fireEvent.change(screen.getByPlaceholderText('Interface/Test.swf'), { target: { value: 'Interface/Test.swf' } });
    await act(async () => { fireEvent.click(screen.getByText('Open')); });
    fireEvent.click(await screen.findByText(/Bitmap #30/));
    expect((await screen.findByAltText('Bitmap preview')).getAttribute('src'))
      .toBe(`data:image/png;base64,${png}`);
    expect(swf.BitmapJpeg).not.toHaveBeenCalled();
  });

  it('shows the BitmapJpeg refusal in the error bar when BitmapPng and BitmapJpeg both refuse the bitmap', async () => {
    // Invariant: when the JPEG fallback is refused too, its message is the one shown and no preview is left; detects the PNG refusal shown in place of the JPEG one, or a refused fallback left as an empty image.
    const swf = stubBridge({
      Characters: vi.fn(async () => JSON.stringify({ characters: [
        { id: 30, kind: 'Bitmap', tagIndex: 4, name: '', imported: '' },
      ] })),
      BitmapPng: vi.fn(async () => JSON.stringify({ error: 'the tag carries no decoded bitmap', status: 'NotFound' })),
      BitmapJpeg: vi.fn(async () => JSON.stringify({ error: 'the tag carries no JPEG without alpha', status: 'NotFound' })),
    });
    render(<SwfPanel onClose={() => {}} />);
    fireEvent.change(screen.getByPlaceholderText('Interface/Test.swf'), { target: { value: 'Interface/Test.swf' } });
    await act(async () => { fireEvent.click(screen.getByText('Open')); });
    fireEvent.click(await screen.findByText(/Bitmap #30/));
    expect(await screen.findByText('the tag carries no JPEG without alpha')).toBeTruthy();
    expect(screen.queryByText('the tag carries no decoded bitmap')).toBeNull();
    expect(screen.queryByAltText('Bitmap preview')).toBeNull();
    expect(swf.BitmapJpeg).toHaveBeenCalledWith(4);
  });

  it('previews a chosen root frame through FrameSvg bounded to the root frame count and sends a refused frame to the error bar', async () => {
    // Invariant: a frame change sends the root frame clamped to 0..frameCount-1 to FrameSvg and renders its svg; a refusal shows its message in the error bar and leaves no frame image; detects a frame sent past the root timeline or below zero, a sprite frame count used for the bound, and a refused frame left on screen as a stale image.
    const swf = stubBridge({
      Timeline: vi.fn(async (index: number) => JSON.stringify({ characterId: index === 1 ? 14 : 0, frameCount: 3 })),
      FrameSvg: vi.fn(async (frame: number) => frame === 1
        ? JSON.stringify({ error: 'could not compose root frame 1' })
        : JSON.stringify({ svg: `<svg id="f${frame}"></svg>` })),
    });
    render(<SwfPanel onClose={() => {}} />);
    fireEvent.change(screen.getByPlaceholderText('Interface/Test.swf'), { target: { value: 'Interface/Test.swf' } });
    await act(async () => { fireEvent.click(screen.getByText('Open')); });
    await waitFor(() => expect(screen.getByLabelText('Preview frame').getAttribute('max')).toBe('2'));
    expect(swf.FrameSvg).toHaveBeenCalledTimes(1);
    expect(swf.FrameSvg).toHaveBeenCalledWith(0);

    fireEvent.change(screen.getByLabelText('Preview frame'), { target: { value: '2' } });
    expect((await screen.findByAltText('Frame preview')).getAttribute('src'))
      .toBe(`data:image/svg+xml;utf8,${encodeURIComponent('<svg id="f2"></svg>')}`);

    fireEvent.change(screen.getByLabelText('Preview frame'), { target: { value: '9' } });
    await waitFor(() => expect(swf.FrameSvg).toHaveBeenCalledTimes(3));

    fireEvent.change(screen.getByLabelText('Preview frame'), { target: { value: '-3' } });
    await waitFor(() => expect(swf.FrameSvg).toHaveBeenCalledTimes(4));
    expect((await screen.findByAltText('Frame preview')).getAttribute('src'))
      .toBe(`data:image/svg+xml;utf8,${encodeURIComponent('<svg id="f0"></svg>')}`);

    await act(async () => { fireEvent.click(screen.getByText('Next')); });
    expect(await screen.findByText('could not compose root frame 1')).toBeTruthy();
    expect(screen.queryByAltText('Frame preview')).toBeNull();
    expect(swf.FrameSvg).toHaveBeenNthCalledWith(1, 0);
    expect(swf.FrameSvg).toHaveBeenNthCalledWith(2, 2);
    expect(swf.FrameSvg).toHaveBeenNthCalledWith(3, 2);
    expect(swf.FrameSvg).toHaveBeenNthCalledWith(4, 0);
    expect(swf.FrameSvg).toHaveBeenNthCalledWith(5, 1);
  });

  it('drops a stale FrameSvg reply that lands after a newer frame', async () => {
    // Invariant: a late FrameSvg reply for an earlier frame must not replace the frame shown last; detects out-of-order replies overwriting the preview of a newer frame.
    let resolveFirst: (text: string) => void = () => {};
    const second = '<svg xmlns="http://www.w3.org/2000/svg" id="second"></svg>';
    const swf = stubBridge({
      Timeline: vi.fn(async (index: number) => JSON.stringify({ characterId: index === 1 ? 14 : 0, frameCount: 3 })),
      FrameSvg: vi.fn()
        .mockImplementationOnce(async () => JSON.stringify({ svg: '<svg id="open"></svg>' }))
        .mockImplementationOnce(() => new Promise<string>(resolve => { resolveFirst = resolve; }))
        .mockImplementationOnce(async () => JSON.stringify({ svg: second })),
    });
    render(<SwfPanel onClose={() => {}} />);
    fireEvent.change(screen.getByPlaceholderText('Interface/Test.swf'), { target: { value: 'Interface/Test.swf' } });
    await act(async () => { fireEvent.click(screen.getByText('Open')); });
    await waitFor(() => expect(screen.getByLabelText('Preview frame').getAttribute('max')).toBe('2'));

    fireEvent.change(screen.getByLabelText('Preview frame'), { target: { value: '1' } });
    fireEvent.change(screen.getByLabelText('Preview frame'), { target: { value: '2' } });
    expect((await screen.findByAltText('Frame preview')).getAttribute('src'))
      .toBe(`data:image/svg+xml;utf8,${encodeURIComponent(second)}`);
    await act(async () => {
      resolveFirst(JSON.stringify({ svg: '<svg id="stale"></svg>' }));
      await new Promise(done => setTimeout(done, 0));
    });
    expect(screen.getByAltText('Frame preview').getAttribute('src'))
      .toBe(`data:image/svg+xml;utf8,${encodeURIComponent(second)}`);
    expect(swf.FrameSvg).toHaveBeenNthCalledWith(2, 1);
    expect(swf.FrameSvg).toHaveBeenNthCalledWith(3, 2);
  });

  it('redraws the previewed frame after a document edit so the image shows the change', async () => {
    // Invariant: a document edit re-requests FrameSvg for the frame being previewed; detects the preview keeping the pre-edit image after an edit, or an edit redrawing frame 0 instead of the preview frame.
    const swf = stubBridge({
      Timeline: vi.fn(async (index: number) => JSON.stringify({ characterId: index === 1 ? 14 : 0, frameCount: 3 })),
    });
    render(<SwfPanel onClose={() => {}} />);
    fireEvent.change(screen.getByPlaceholderText('Interface/Test.swf'), { target: { value: 'Interface/Test.swf' } });
    await act(async () => { fireEvent.click(screen.getByText('Open')); });
    await waitFor(() => expect(screen.getByLabelText('Preview frame').getAttribute('max')).toBe('2'));
    fireEvent.change(screen.getByLabelText('Preview frame'), { target: { value: '2' } });
    await waitFor(() => expect(swf.FrameSvg).toHaveBeenCalledTimes(2));

    await act(async () => { fireEvent.click(await screen.findByTestId('swf-row-7')); });
    await waitFor(() => expect(swf.PlacementMatrix).toHaveBeenCalledWith(2));
    await act(async () => { fireEvent.click(screen.getByText('Apply depth')); });
    await waitFor(() => expect(swf.SetDepth).toHaveBeenCalledWith(2, 1));
    await waitFor(() => expect(swf.FrameSvg).toHaveBeenCalledTimes(3));
    expect(swf.FrameSvg).toHaveBeenLastCalledWith(2);
  });

  it('drops a stale BitmapPng reply that lands after a newer bitmap pick', async () => {
    // Invariant: a late BitmapPng reply for an earlier bitmap must not replace the preview of the bitmap picked last; detects out-of-order replies overwriting it on the bitmap branch.
    let resolveFirst: (text: string) => void = () => {};
    const second = 'iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mNkYAAAAAYAAjCB0C8AAAAASUVORK5CYII=';
    const swf = stubBridge({
      Characters: vi.fn(async () => JSON.stringify({ characters: [
        { id: 30, kind: 'Bitmap', tagIndex: 4, name: '', imported: '' },
        { id: 31, kind: 'Bitmap', tagIndex: 5, name: '', imported: '' },
      ] })),
      BitmapPng: vi.fn()
        .mockImplementationOnce(() => new Promise<string>(resolve => { resolveFirst = resolve; }))
        .mockImplementationOnce(async () => JSON.stringify({ png: second })),
    });
    render(<SwfPanel onClose={() => {}} />);
    fireEvent.change(screen.getByPlaceholderText('Interface/Test.swf'), { target: { value: 'Interface/Test.swf' } });
    await act(async () => { fireEvent.click(screen.getByText('Open')); });
    fireEvent.click(await screen.findByText(/Bitmap #30/));
    fireEvent.click(screen.getByText(/Bitmap #31/));
    expect((await screen.findByAltText('Bitmap preview')).getAttribute('src'))
      .toBe(`data:image/png;base64,${second}`);
    await act(async () => {
      resolveFirst(JSON.stringify({ png: 'iVBORw0KGgoAAAANSUhEUgAAAAIAAAACCAYAAABytg==' }));
      await new Promise(done => setTimeout(done, 0));
    });
    expect(screen.getByAltText('Bitmap preview').getAttribute('src'))
      .toBe(`data:image/png;base64,${second}`);
    expect(swf.BitmapPng).toHaveBeenNthCalledWith(1, 4);
    expect(swf.BitmapPng).toHaveBeenNthCalledWith(2, 5);
  });

  it('browses to a SWF anywhere on disk and opens the picked path at once', async () => {
    // Regression: detects Browse dropping the path the native dialog returned, which would leave a movie outside the roots unopened.
    const picked = 'D:\\Mods\\Menu.swf';
    const swf = stubBridge({ BrowseForFile: vi.fn(async () => picked) });
    render(<SwfPanel onClose={() => {}} />);
    await act(async () => { fireEvent.click(screen.getByText('Browse…')); });
    await waitFor(() => expect(swf.Open).toHaveBeenCalledWith(picked));
    expect(swf.BrowseForFile).toHaveBeenCalledWith('Open a SWF movie', 'SWF movie (*.swf)|*.swf|All files|*.*');
    expect((screen.getByPlaceholderText('Interface/Test.swf') as HTMLInputElement).value).toBe(picked);
  });

  it('calls no Open when the Browse dialog is cancelled', async () => {
    // Regression: detects a cancelled dialog's empty reply being sent to Open as a path.
    const swf = stubBridge({ BrowseForFile: vi.fn(async () => '') });
    render(<SwfPanel onClose={() => {}} />);
    await act(async () => { fireEvent.click(screen.getByText('Browse…')); });
    expect(swf.BrowseForFile).toHaveBeenCalledTimes(1);
    expect(swf.Open).not.toHaveBeenCalled();
  });

  it('shows the ABC page in place of the movie page through the tab row', async () => {
    // Regression: detects the ABC tab leaving the movie page visible or the script box missing from the ABC page.
    stubBridge();
    render(<SwfPanel onClose={() => {}} />);
    fireEvent.change(screen.getByPlaceholderText('Interface/Test.swf'), { target: { value: 'Interface/Test.swf' } });
    await act(async () => { fireEvent.click(screen.getByText('Open')); });
    expect(await screen.findByText(/root · start/)).toBeTruthy();
    expect(screen.getByText(/root · start/).closest('[hidden]')).toBeNull();

    await act(async () => { fireEvent.click(screen.getByRole('tab', { name: 'ABC scripts' })); });
    expect(screen.getByRole('tab', { name: 'ABC scripts' }).getAttribute('aria-selected')).toBe('true');
    expect(screen.getByRole('tab', { name: 'Movie' }).getAttribute('aria-selected')).toBe('false');
    expect(screen.getByText(/root · start/).closest('[hidden]')).not.toBeNull();
    expect(screen.getByLabelText('ABC script').closest('[hidden]')).toBeNull();
  });

  it('applies the edited ABC block text and redraws the preview without reading State', async () => {
    // Invariant: Apply sends the whole draft for the block and redraws the frame preview from the reply revision without a State read; detects an apply that reloads State or leaves the preview on the pre-apply image.
    const swf = stubBridge();
    const script = await openAbcTab();
    await waitFor(() => expect(swf.FrameSvg).toHaveBeenCalledTimes(1));
    fireEvent.change(script, { target: { value: 'abc 46.16\n  string 1 "Bar"\n' } });
    await act(async () => { fireEvent.click(screen.getByText('Apply script')); });
    await waitFor(() => expect(swf.ApplyAbcText).toHaveBeenCalledWith(0, 'abc 46.16\n  string 1 "Bar"\n'));
    await waitFor(() => expect(swf.FrameSvg).toHaveBeenCalledTimes(2));
    expect(swf.State).not.toHaveBeenCalled();
  });

  it('shows a refused ABC apply verbatim and keeps the edited draft', async () => {
    // Invariant: a refused apply shows the Radicle message verbatim and keeps the draft in the box; detects a refusal swallowed or the draft reset to the loaded text.
    stubBridge({ ApplyAbcText: vi.fn(async () => JSON.stringify({ error: 'line 2: unknown instruction frob', status: 'ParseError' })) });
    const script = await openAbcTab();
    fireEvent.change(script, { target: { value: 'frob' } });
    await act(async () => { fireEvent.click(screen.getByText('Apply script')); });
    expect(await screen.findByText('line 2: unknown instruction frob')).toBeTruthy();
    expect((screen.getByLabelText('ABC script') as HTMLTextAreaElement).value).toBe('frob');
  });

  it('keeps Ctrl+Z typed in the ABC script box out of the document Undo', async () => {
    // Regression: detects Ctrl+Z in the script box also calling the document Undo, which would undo a movie edit while the user types.
    const swf = stubBridge();
    const script = await openAbcTab();
    fireEvent.keyDown(script, { key: 'z', ctrlKey: true });
    expect(swf.Undo).not.toHaveBeenCalled();
    fireEvent.keyDown(window, { key: 'z', ctrlKey: true });
    await waitFor(() => expect(swf.Undo).toHaveBeenCalledTimes(1));
  });

  it('keeps an unapplied ABC draft when a document undo changes the revision', async () => {
    // Regression: detects a revision reload silently replacing the user's unapplied script with the document's text.
    const swf = stubBridge({
      State: vi.fn()
        .mockResolvedValue(JSON.stringify({ revision: 2, undo: 0, redo: 1, dirty: true })),
    });
    const script = await openAbcTab();
    fireEvent.change(script, { target: { value: 'draft text' } });
    const calls = swf.AbcText.mock.calls.length;
    fireEvent.keyDown(window, { key: 'z', ctrlKey: true });
    await waitFor(() => expect(swf.AbcText.mock.calls.length).toBeGreaterThan(calls));
    expect((screen.getByLabelText('ABC script') as HTMLTextAreaElement).value).toBe('draft text');
  });

  it('replaces the draft with the text Radicle reports after a successful apply', async () => {
    // Regression: detects an applied script staying marked unapplied when Radicle renders it differently from what was typed.
    const swf = stubBridge({
      ApplyAbcText: vi.fn(async () => JSON.stringify({ changed: true, revision: 12, undo: 1, redo: 0, dirty: true })),
    });
    const script = await openAbcTab();
    swf.AbcText.mockImplementation(async () => JSON.stringify({ text: 'canonical text' }));
    fireEvent.change(script, { target: { value: 'typed text' } });
    await act(async () => { fireEvent.click(screen.getByText('Apply script')); });
    await waitFor(() => expect((screen.getByLabelText('ABC script') as HTMLTextAreaElement).value).toBe('canonical text'));
    expect((screen.getByText('Apply script') as HTMLButtonElement).disabled).toBe(true);
  });

  it('keeps an apply refusal on screen when a method is picked', async () => {
    // Regression: detects a successful method listing wiping the refusal while the rejected draft is still unapplied.
    stubBridge({ ApplyAbcText: vi.fn(async () => JSON.stringify({ error: 'line 2: unknown instruction frob' })) });
    const script = await openAbcTab();
    fireEvent.change(script, { target: { value: 'frob' } });
    await act(async () => { fireEvent.click(screen.getByText('Apply script')); });
    await act(async () => { fireEvent.click(screen.getByText(/onLoad : void/)); });
    await waitFor(() => expect(screen.getByLabelText('instructions').textContent).toContain('returnvoid'));
    expect(screen.getByText('line 2: unknown instruction frob')).toBeTruthy();
  });

  it('keeps an unapplied ABC draft when the tab is switched away and back', async () => {
    // Invariant: both pages stay mounted, so an unapplied script draft survives a tab switch; detects a page remount that drops the edited text.
    stubBridge();
    const script = await openAbcTab();
    fireEvent.change(script, { target: { value: 'draft text' } });
    await act(async () => { fireEvent.click(screen.getByRole('tab', { name: 'Movie' })); });
    await act(async () => { fireEvent.click(screen.getByRole('tab', { name: 'ABC scripts' })); });
    expect((screen.getByLabelText('ABC script') as HTMLTextAreaElement).value).toBe('draft text');
    expect((screen.getByText('Apply script') as HTMLButtonElement).disabled).toBe(false);
  });

  // Constraint: detects Close discarding unsaved edits without asking, or a refused close dropping the open document.
  it('asks before closing with unsaved changes and keeps the document when refused', async () => {
    stubBridge({ Open: vi.fn(async () => JSON.stringify({ revision: 1, undo: 1, redo: 0, dirty: true })) });
    const onClose = vi.fn();
    render(<SwfPanel onClose={onClose} />);
    fireEvent.change(screen.getByPlaceholderText('Interface/Test.swf'), { target: { value: 'Interface/Test.swf' } });
    await act(async () => { fireEvent.click(screen.getByText('Open')); });
    confirm.mockResolvedValueOnce(false);
    fireEvent.click(screen.getByText('Close'));
    await waitFor(() => expect(confirm).toHaveBeenCalledTimes(1));
    expect(onClose).not.toHaveBeenCalled();
    expect(screen.getByText('modified')).toBeTruthy();
  });

  // Constraint: detects Close on a clean document prompting the user for nothing.
  it('closes a clean document without asking', async () => {
    stubBridge();
    const onClose = vi.fn();
    render(<SwfPanel onClose={onClose} />);
    fireEvent.click(screen.getByText('Close'));
    await waitFor(() => expect(onClose).toHaveBeenCalledTimes(1));
    expect(confirm).not.toHaveBeenCalled();
  });

  // Constraint: detects Browse or Open replacing unsaved edits without asking first.
  it('asks before Browse or Open replaces a dirty document', async () => {
    const swf = stubBridge({
      Open: vi.fn(async () => JSON.stringify({ revision: 1, undo: 1, redo: 0, dirty: true })),
      BrowseForFile: vi.fn(async () => 'Interface/Other.swf'),
    });
    render(<SwfPanel onClose={() => {}} />);
    fireEvent.change(screen.getByPlaceholderText('Interface/Test.swf'), { target: { value: 'Interface/Test.swf' } });
    await act(async () => { fireEvent.click(screen.getByText('Open')); });
    expect(swf.Open).toHaveBeenCalledTimes(1);
    confirm.mockResolvedValue(false);
    await act(async () => { fireEvent.click(screen.getByText('Open')); });
    await waitFor(() => expect(confirm).toHaveBeenCalledTimes(1));
    await act(async () => { fireEvent.click(screen.getByText('Browse…')); });
    await waitFor(() => expect(confirm).toHaveBeenCalledTimes(2));
    expect(swf.Open).toHaveBeenCalledTimes(1);
  });
});
import { act, cleanup, fireEvent, render, screen } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';
import type { MaterialField, MaterialHost } from './backend';
import MaterialEditor from './MaterialEditor';

afterEach(cleanup);

const FIRST_PATH = 'C:\\Data\\Materials\\First.bgsm';
const SECOND_PATH = 'C:\\Data\\Materials\\Second.bgem';

const FIELDS: MaterialField[] = [
  { name: 'Alpha', section: 'material', type: 'bool', value: 'false' },
  { name: 'AlphaTestRef', section: 'material', type: 'float', value: '0.5' },
  { name: 'RootMaterial', section: 'material', type: 'int', value: '1' },
  { name: 'SpecularColor', section: 'material', type: 'color', value: '0.2, 0.2, 0.2' },
  { name: 'DiffuseTexture', section: 'material', type: 'string', value: 'Textures\\First_d.dds' },
  { name: 'TileU', section: 'header', type: 'float', value: '1' },
  { name: 'AlphaBlendMode', section: 'header', type: 'string', value: 'None' },
];

const SECOND_FIELDS: MaterialField[] = [
  { name: 'BaseTexture', section: 'material', type: 'string', value: 'Textures\\Second_d.dds' },
  { name: 'TileU', section: 'header', type: 'float', value: '2' },
];

function stubMaterial() {
  return {
    BrowseForFile: vi.fn(async () => ''),
    Inspect: vi.fn(async () => ''),
    SetFields: vi.fn(async (_path: string, fieldsJson: string) =>
      `Set ${Object.keys(JSON.parse(fieldsJson) as Record<string, string>).length} field(s)`),
    OpenFolder: vi.fn(async () => ''),
    StageDroppedFile: vi.fn(async () => ''),
  };
}

function setup() {
  const material = stubMaterial();
  const onSaved = vi.fn();
  const onDirtyChange = vi.fn();
  const appendLog = vi.fn<(line: string) => void>();
  const tree = (path: string, fields: MaterialField[]) => (
    <MaterialEditor fields={fields} path={path} material={material as unknown as MaterialHost}
      onSaved={onSaved} onDirtyChange={onDirtyChange} appendLog={appendLog} />
  );
  const view = render(tree(FIRST_PATH, FIELDS));
  const show = (path: string, fields: MaterialField[] = FIELDS) => view.rerender(tree(path, fields));
  return { material, onSaved, onDirtyChange, appendLog, show, unmount: view.unmount };
}

const row = (name: string) => screen.getByText(name, { selector: '.nif-flabel' }).closest('.nif-frow') as HTMLElement;
const control = (name: string) => row(name).querySelector('input') as HTMLInputElement;
const revertButton = (name: string) => row(name).querySelector('.nif-revert') as HTMLButtonElement;
const saveButton = () => screen.getByRole('button', { name: /^Sav(e|ing…)$/ }) as HTMLButtonElement;
const revertAllButton = () => screen.getByTitle('Discard all changes') as HTMLButtonElement;

function edit(name: string, value: string) {
  fireEvent.change(control(name), { target: { value } });
}

describe('MaterialEditor', () => {
  it('renders the material fields and opens the header block on demand', () => {
    setup();

    expect(control('Alpha').type).toBe('checkbox');
    expect(control('AlphaTestRef').type).toBe('number');
    expect(control('RootMaterial').type).toBe('number');
    expect(control('SpecularColor').type).toBe('color');
    expect(control('DiffuseTexture').type).toBe('text');
    expect(control('DiffuseTexture').value).toBe('Textures\\First_d.dds');
    expect(screen.getByText('No changes')).toBeTruthy();

    expect(screen.queryByText('TileU')).toBeNull();
    fireEvent.click(screen.getByText('Header (tiling, alpha blend, ...)'));
    expect(control('TileU').value).toBe('1');
    expect(control('AlphaBlendMode').value).toBe('None');

    fireEvent.click(screen.getByText('Header (tiling, alpha blend, ...)'));
    expect(screen.queryByText('TileU')).toBeNull();
  });

  it('counts edits per type, reports dirty to the host and reverts one field or all of them', () => {
    const { onDirtyChange } = setup();

    fireEvent.click(control('Alpha'));
    expect(screen.getByText('1 unsaved change')).toBeTruthy();
    edit('RootMaterial', '3');
    edit('SpecularColor', '#ff8000');
    expect(row('SpecularColor').textContent).toContain('1, 0.502, 0');
    expect(screen.getByText('3 unsaved changes')).toBeTruthy();
    expect(onDirtyChange).toHaveBeenCalledWith(true);

    edit('AlphaTestRef', '0.5');
    expect(screen.getByText('3 unsaved changes')).toBeTruthy();

    fireEvent.click(revertButton('RootMaterial'));
    expect(control('RootMaterial').value).toBe('1');
    expect(screen.getByText('2 unsaved changes')).toBeTruthy();

    edit('AlphaTestRef', '0.75');
    expect(screen.getByText('3 unsaved changes')).toBeTruthy();
    edit('AlphaTestRef', '0.5');
    expect(screen.getByText('2 unsaved changes')).toBeTruthy();

    fireEvent.click(revertAllButton());
    expect(screen.getByText('No changes')).toBeTruthy();
    expect(control('Alpha').checked).toBe(false);
    expect(row('SpecularColor').textContent).toContain('0.2, 0.2, 0.2');
    expect(onDirtyChange).toHaveBeenLastCalledWith(false);
  });

  it('saves only the modified fields, logs the host reply and clears the edits', async () => {
    const { material, onSaved, appendLog } = setup();

    edit('AlphaTestRef', '0.75');
    edit('DiffuseTexture', 'Textures\\First_d2.dds');
    fireEvent.click(saveButton());

    expect(material.SetFields).toHaveBeenCalledWith(
      FIRST_PATH, JSON.stringify({ AlphaTestRef: '0.75', DiffuseTexture: 'Textures\\First_d2.dds' }), '');
    expect(screen.getByText('Saving…')).toBeTruthy();
    await vi.waitFor(() => expect(screen.getByText('No changes')).toBeTruthy());
    expect(appendLog).toHaveBeenCalledWith('✓ Set 2 field(s)');
    expect(onSaved).toHaveBeenCalledTimes(1);
    expect(saveButton().disabled).toBe(true);
  });

  it('does nothing when Save is pressed with no edits', () => {
    const { material } = setup();

    fireEvent.click(saveButton());

    expect(material.SetFields).not.toHaveBeenCalled();
    expect(saveButton().disabled).toBe(true);
  });

  it('keeps the edits when the host answers with something other than a field count', async () => {
    const { material, appendLog } = setup();
    vi.mocked(material.SetFields).mockResolvedValueOnce('MaterialFieldParseFailed: DiffuseTexture is not writable');

    edit('DiffuseTexture', 'Textures\\Missing_d.dds');
    fireEvent.click(saveButton());

    await vi.waitFor(() =>
      expect(appendLog).toHaveBeenCalledWith('✗ save failed -- MaterialFieldParseFailed: DiffuseTexture is not writable'));
    expect(screen.getByText('1 unsaved change')).toBeTruthy();
    expect(control('DiffuseTexture').value).toBe('Textures\\Missing_d.dds');
    expect(saveButton().disabled).toBe(false);
  });

  it('keeps the edits when the host rejects the write', async () => {
    const { material, onSaved, appendLog } = setup();
    vi.mocked(material.SetFields).mockRejectedValueOnce(new Error('the file is read only'));

    edit('DiffuseTexture', 'Textures\\First_d2.dds');
    fireEvent.click(saveButton());

    await vi.waitFor(() => expect(appendLog).toHaveBeenCalledWith('✗ save failed -- the file is read only'));
    expect(screen.getByText('1 unsaved change')).toBeTruthy();
    expect(control('DiffuseTexture').value).toBe('Textures\\First_d2.dds');
    expect(onSaved).not.toHaveBeenCalled();
    expect(saveButton().disabled).toBe(false);
  });

  it('clears the edits of the previous material so they cannot be written to the new one', () => {
    const { material, show } = setup();

    edit('AlphaTestRef', '0.75');
    expect(screen.getByText('1 unsaved change')).toBeTruthy();

    show(SECOND_PATH, SECOND_FIELDS);

    expect(screen.getByText('No changes')).toBeTruthy();
    expect(screen.getByTitle(SECOND_PATH)).toBeTruthy();
    expect(control('BaseTexture').value).toBe('Textures\\Second_d.dds');

    edit('BaseTexture', 'Textures\\Second_d2.dds');
    fireEvent.click(saveButton());
    expect(material.SetFields).toHaveBeenCalledWith(
      SECOND_PATH, JSON.stringify({ BaseTexture: 'Textures\\Second_d2.dds' }), '');
  });

  it('drops a save that resolves after the editor switched to another material', async () => {
    const { material, onSaved, appendLog, show } = setup();
    let finishSave: (value: string) => void = () => {};
    vi.mocked(material.SetFields).mockImplementationOnce(() => new Promise<string>(resolve => { finishSave = resolve; }));

    edit('AlphaTestRef', '0.75');
    fireEvent.click(saveButton());
    expect(screen.getByText('Saving…')).toBeTruthy();

    show(SECOND_PATH, SECOND_FIELDS);

    expect(screen.getByText('No changes')).toBeTruthy();
    expect(screen.queryByText('Saving…')).toBeNull();
    expect(saveButton().disabled).toBe(true);

    await act(async () => { finishSave('Set 1 field(s)'); });

    expect(material.SetFields).toHaveBeenCalledWith(FIRST_PATH, JSON.stringify({ AlphaTestRef: '0.75' }), '');
    expect(onSaved).not.toHaveBeenCalled();
    expect(appendLog).toHaveBeenCalledWith('✓ saved First.bgsm (Set 1 field(s))');
    expect(screen.getByText('No changes')).toBeTruthy();
  });

  it('logs a save failure that rejects after the editor switched without invoking onSaved', async () => {
    const { material, onSaved, appendLog, show } = setup();
    let failSave: (reason: Error) => void = () => {};
    vi.mocked(material.SetFields).mockImplementationOnce(
      () => new Promise<string>((_resolve, reject) => { failSave = reject; }));

    edit('AlphaTestRef', '0.75');
    fireEvent.click(saveButton());

    show(SECOND_PATH, SECOND_FIELDS);
    await act(async () => { failSave(new Error('the first material was deleted')); });

    expect(onSaved).not.toHaveBeenCalled();
    expect(appendLog).toHaveBeenCalledWith('✗ save failed First.bgsm -- the first material was deleted');
    expect(screen.getByText('No changes')).toBeTruthy();
  });

  it('keeps edits made on the new material while the superseded save is still running', async () => {
    const { material, show } = setup();
    let finishSave: (value: string) => void = () => {};
    vi.mocked(material.SetFields).mockImplementationOnce(() => new Promise<string>(resolve => { finishSave = resolve; }));

    edit('AlphaTestRef', '0.75');
    fireEvent.click(saveButton());

    show(SECOND_PATH, SECOND_FIELDS);
    edit('BaseTexture', 'Textures\\Second_d3.dds');
    expect(screen.getByText('1 unsaved change')).toBeTruthy();

    await act(async () => { finishSave('Set 1 field(s)'); });

    expect(screen.getByText('1 unsaved change')).toBeTruthy();
    expect(control('BaseTexture').value).toBe('Textures\\Second_d3.dds');

    fireEvent.click(saveButton());
    expect(material.SetFields).toHaveBeenLastCalledWith(
      SECOND_PATH, JSON.stringify({ BaseTexture: 'Textures\\Second_d3.dds' }), '');
  });

  it('switching A -> B -> A discards old edits, does not lock Save, and ignores the old in-flight save state', async () => {
    const { material, onSaved, appendLog, show } = setup();
    let finishSave: (value: string) => void = () => {};
    vi.mocked(material.SetFields).mockImplementationOnce(() => new Promise<string>(resolve => { finishSave = resolve; }));

    edit('AlphaTestRef', '0.75');
    fireEvent.click(saveButton());
    expect(screen.getByText('Saving…')).toBeTruthy();

    show(SECOND_PATH, SECOND_FIELDS);
    show(FIRST_PATH, FIELDS);

    expect(screen.getByText('No changes')).toBeTruthy();
    expect(screen.queryByText('Saving…')).toBeNull();
    expect(control('AlphaTestRef').value).toBe('0.5');

    edit('AlphaTestRef', '0.9');
    expect(saveButton().disabled).toBe(false);

    await act(async () => { finishSave('Set 1 field(s)'); });

    expect(onSaved).not.toHaveBeenCalled();
    expect(appendLog).toHaveBeenCalledWith('✓ saved First.bgsm (Set 1 field(s))');
    expect(screen.getByText('1 unsaved change')).toBeTruthy();
    expect(saveButton().disabled).toBe(false);
  });

  it('logs a superseded save when unmounted before SetFields resolves without calling onSaved', async () => {
    const { material, onSaved, appendLog, unmount } = setup();
    let finishSave: (value: string) => void = () => {};
    vi.mocked(material.SetFields).mockImplementationOnce(() => new Promise<string>(resolve => { finishSave = resolve; }));

    edit('AlphaTestRef', '0.75');
    fireEvent.click(saveButton());
    unmount();

    await act(async () => { finishSave('Set 1 field(s)'); });

    expect(onSaved).not.toHaveBeenCalled();
    expect(appendLog).toHaveBeenCalledWith('✓ saved First.bgsm (Set 1 field(s))');
  });

  it('preserves edits typed on the same material while a save is still in flight', async () => {
    const { material, onSaved } = setup();
    let finishSave: (value: string) => void = () => {};
    vi.mocked(material.SetFields).mockImplementationOnce(() => new Promise<string>(resolve => { finishSave = resolve; }));

    edit('AlphaTestRef', '0.75');
    fireEvent.click(saveButton());
    edit('DiffuseTexture', 'Textures\\First_newer.dds');

    await act(async () => { finishSave('Set 1 field(s)'); });

    expect(onSaved).toHaveBeenCalledTimes(1);
    expect(screen.getByText('1 unsaved change')).toBeTruthy();
    expect(control('DiffuseTexture').value).toBe('Textures\\First_newer.dds');
  });
});

import { cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';
import type { NifHost } from './backend';
import NifSkinTools from './NifSkinTools';
import { describeSkinEditReport, parseBoneInfluence, parseSkinEditReport, parseVertexInfluences } from './skinToolsClient';
import NifWeightTools from './NifWeightTools';
import {
  brushArgs, describeBrushReport, describeOsdApply, describeRemapRead, describeTransferReport, osdApplyArgs,
  parseSkinToolReport, remapAction, transferArgs, type BrushSettings, type OsdApplyChoice,
} from './weightToolsClient';

const NIF = 'C:\\mods\\body.nif';

const influencesJson = (vertex: number) => JSON.stringify({
  block: 1,
  name: 'body',
  vertex,
  bones: ['LArm', 'RArm', 'Spine'],
  influences: [{ bone: 0, name: 'LArm', weight: 0.75 }, { bone: 2, name: 'Spine', weight: 0.25 }],
});

const boneJson = JSON.stringify({
  block: 1,
  name: 'body',
  bone: 1,
  bone_name: 'RArm',
  vertices: [2, 3],
  weight: 2,
  minimum: 0.4,
  min: [-1, 0, 0],
  max: [-1, 1, 0],
});

const reportJson = JSON.stringify({
  block: 1, name: 'body', bones: 3, bind_transforms: 3, used_bones: 2, ok: true, errors: 0, warnings: 1,
  findings: [{ level: 'warning', where: 'bones', what: '1 move no vertex of this shape: Spine' }],
});

const mirroredReport =
  'RESULT: skin edit mirror_weights OK (in-process)\nSaved: C:\\mods\\body.nif\nBlock: 1\n' +
  'Vertices changed: 2, largest weight change 1.000000\n' +
  'Mirrored: 2, no partner vertex 0, ambiguous 0, no partner bone 0';

const makeHost = (overrides: Partial<NifHost> = {}) => ({
  SkinInfluences: vi.fn(async (_path: string, _block: number, vertex: number) => influencesJson(vertex)),
  SkinInfluencedBy: vi.fn(async () => boneJson),
  SkinEdit: vi.fn(async () => mirroredReport),
  SkinValidate: vi.fn(async () => reportJson),
  ...overrides,
} as unknown as NifHost);

const mount = (host: NifHost) => render(
  <NifSkinTools
    nif={host}
    nifPath={NIF}
    shapes={[{ id: 1, name: 'body' }]}
    disabled={false}
    appendLog={() => {}}
    onSaved={() => {}}
  />,
);

afterEach(cleanup);

describe('skinToolsClient', () => {
  it('parses the read replies and the edit report', () => {
    const read = parseVertexInfluences(influencesJson(3));
    expect(read.bones).toEqual(['LArm', 'RArm', 'Spine']);
    expect(read.influences[0]).toEqual({ bone: 0, name: 'LArm', weight: 0.75 });

    const bone = parseBoneInfluence(boneJson);
    expect(bone.boneName).toBe('RArm');
    expect(bone.vertices).toEqual([2, 3]);

    const report = parseSkinEditReport(mirroredReport);
    expect(report.ok).toBe(true);
    expect(report.mirrored).toBe(2);
    expect(describeSkinEditReport(report)).toContain('2 vert');

    const refused = parseSkinEditReport('Error: SkinEditFailed\nblock 1 has no skin');
    expect(refused.ok).toBe(false);
    expect(refused.refusal).toContain('has no skin');
  });
});

describe('NifSkinTools', () => {
  it('reads the selected shape and mirrors the positive side through SkinEdit', async () => {
    const host = makeHost();
    mount(host);

    fireEvent.change(screen.getByLabelText('Skin tools shape'), { target: { value: '1' } });
    await waitFor(() => expect(host.SkinInfluences).toHaveBeenCalledWith(NIF, 1, 0));

    expect((await screen.findByRole('status')).textContent).toBe('vertex 0: LArm 0.750, Spine 0.250');

    fireEvent.click(screen.getByText('Mirror weights'));
    await waitFor(() => expect(host.SkinEdit).toHaveBeenCalledWith(
      NIF, '', 'mirror_weights', 1, null, null, null, null, false, 0, true, false, ''));

    await waitFor(() => expect(host.SkinEdit).toHaveBeenCalledTimes(1));
    expect(screen.getByRole('status').textContent).toContain('2 mirrored');
  });

  it('sends the edited rows as set_influences and shows a refusal instead of claiming a save', async () => {
    const host = makeHost({
      SkinEdit: vi.fn(async () => 'Error: SkinEditFailed\nvertex 0 would end with no positive bone weight'),
      SkinInfluencedBy: vi.fn(async () => boneJson),
    });
    mount(host);

    fireEvent.change(screen.getByLabelText('Skin tools shape'), { target: { value: '1' } });
    await waitFor(() => expect(host.SkinInfluences).toHaveBeenCalled());

    fireEvent.change(screen.getByLabelText('Skin tools vertex'), { target: { value: '2' } });
    fireEvent.click(screen.getByText('Read influences'));
    await waitFor(() => expect(host.SkinInfluences).toHaveBeenCalledWith(NIF, 1, 2));

    fireEvent.change(screen.getByLabelText('Influence 0 weight'), { target: { value: '0.5' } });
    fireEvent.click(screen.getByText('Set influences'));
    await waitFor(() => expect(host.SkinEdit).toHaveBeenCalledWith(
      NIF, '', 'set_influences', 1, 2,
      [{ bone: 0, weight: 0.5 }, { bone: 2, weight: 0.25 }],
      null, null, false, 0, true, false, ''));

    await waitFor(() => expect(screen.getByRole('alert').textContent).toContain('no positive bone weight'));
    expect(screen.queryByText(/Mirrored/)).toBeNull();
  });

  it('shows the findings of a weight check without writing the file', async () => {
    const host = makeHost();
    mount(host);

    fireEvent.change(screen.getByLabelText('Skin tools shape'), { target: { value: '1' } });
    await waitFor(() => expect(host.SkinInfluences).toHaveBeenCalled());

    fireEvent.click(screen.getByText('Check weights'));
    await waitFor(() => expect(host.SkinValidate).toHaveBeenCalledWith(NIF, 1));
    await waitFor(() => expect(screen.getByRole('status').textContent).toContain('1 warning(s)'));
    expect(screen.getByLabelText('Skin findings').textContent)
      .toContain('bones: 1 move no vertex of this shape: Spine');
    expect(host.SkinEdit).not.toHaveBeenCalled();
  });

  it('reports what one bone moves without writing the file', async () => {
    const host = makeHost();
    mount(host);

    fireEvent.change(screen.getByLabelText('Skin tools shape'), { target: { value: '1' } });
    await waitFor(() => expect(host.SkinInfluences).toHaveBeenCalled());

    fireEvent.change(screen.getByLabelText('Skin tools bone'), { target: { value: '1' } });
    fireEvent.click(screen.getByText('Influenced by'));
    await waitFor(() => expect(host.SkinInfluencedBy).toHaveBeenCalledWith(NIF, 1, 1, 0.01));
    await waitFor(() => expect(screen.getByRole('status').textContent).toContain('moves 2 vertices'));
    expect(host.SkinEdit).not.toHaveBeenCalled();
  });
});

const BRUSH: BrushSettings = {
  mode: 'add', falloff: 'smoothstep', center: [1, 2, 3], radius: 5, strength: 0.5, bone: 1, target: 1, locked: [2, 0, 2],
};

const strokeJson = (saved: string) => JSON.stringify({
  ok: true, op: 'brush', block: 1, saved, in_reach: 3, changed: saved === '' ? 0 : 2, skipped: 1, max_weight_change: 0.5,
});

const remapJson = (remapBlock: number) => JSON.stringify({
  ok: true, op: 'remap_read', block: 1, saved: '', remap_block: remapBlock, new_bones_block: -1,
  records: remapBlock < 0 ? [] : [{ bones: [0, 0, 0, 0], weights: [1, 0, 0, 0] }],
  new_bones: [{ name: 'Extra', raw: '00'.repeat(208) }],
});

const makeWeightHost = (overrides: Partial<NifHost> = {}) => ({
  SkinInfluences: vi.fn(async (_path: string, _block: number, vertex: number) => influencesJson(vertex)),
  SkinToolOp: vi.fn(async () => strokeJson(NIF)),
  BrowseForFile: vi.fn(async () => 'C:\\mods\\donor.nif'),
  Tree: vi.fn(async () => JSON.stringify({
    blocks: [{ id: 0, name: 'root', group: 'Nodes' }, { id: 4, name: 'donor body', group: 'Shapes' }],
  })),
  ...overrides,
} as unknown as NifHost);

const mountWeights = (host: NifHost, onSaved: (result: string) => void = () => {}) => render(
  <NifWeightTools
    nif={host}
    nifPath={NIF}
    shapes={[{ id: 1, name: 'body' }]}
    disabled={false}
    expectedSource="abc"
    appendLog={() => {}}
    onSaved={onSaved}
  />,
);

const pickShape = async (host: NifHost) => {
  fireEvent.change(screen.getByLabelText('Weight tools shape'), { target: { value: '1' } });
  await waitFor(() => expect(host.SkinInfluences).toHaveBeenCalledWith(NIF, 1, 0));
  await waitFor(() => expect((screen.getByLabelText('Weight tools shape') as HTMLSelectElement).disabled).toBe(false));
};

describe('weightToolsClient', () => {
  it('builds one brush request per mode with the bone and target only where the mode reads them', () => {
    expect(brushArgs(1, BRUSH)).toEqual({
      block: 1, mode: 'add', falloff: 'smoothstep', center: [1, 2, 3], radius: 5, strength: 0.5, locked: [0, 2], bone: 1,
    });
    expect(brushArgs(1, { ...BRUSH, mode: 'subtract' }).mode).toBe('subtract');
    expect(brushArgs(1, { ...BRUSH, mode: 'replace', target: 0.25 })).toMatchObject({ bone: 1, target: 0.25 });
    const smooth = brushArgs(1, { ...BRUSH, mode: 'smooth', bone: 999 });
    expect(smooth).not.toHaveProperty('bone');
    expect(smooth).not.toHaveProperty('target');
  });

  it('refuses a bone byte that would wrap, an empty brush and a missing shape before calling the host', () => {
    expect(() => brushArgs(1, { ...BRUSH, bone: 256 })).toThrow('pick the bone');
    expect(() => brushArgs(1, { ...BRUSH, locked: [3, 256] })).toThrow('locked bone');
    expect(() => brushArgs(1, { ...BRUSH, radius: 0 })).toThrow('radius');
    expect(() => brushArgs(1, { ...BRUSH, mode: 'replace', target: 1.5 })).toThrow('target');
    expect(() => brushArgs(-1, BRUSH)).toThrow('select a shape');
    expect(() => transferArgs(1, '', 4, 'barycentric', 0)).toThrow('source NIF');
    expect(() => transferArgs(1, 'C:\\a.nif', 4, 'barycentric', -1)).toThrow('distance');
  });

  it('says nothing was saved when the host changed nothing and surfaces a host error', () => {
    const unchanged = parseSkinToolReport(strokeJson(''));
    expect(describeBrushReport(unchanged)).toContain('nothing saved');
    expect(describeBrushReport(parseSkinToolReport(strokeJson(NIF)))).toContain('changed 2 vertices of 3 in reach');
    expect(describeTransferReport(parseSkinToolReport(JSON.stringify({ saved: '', transferred: 0, out_of_reach: 4, unmapped: 0 }))))
      .toContain('nothing saved');
    expect(() => parseSkinToolReport(JSON.stringify({ error: 'the skin has no bone named LArm' }))).toThrow('no bone named');
  });

  it('offers Create only for a shape with no remap block', () => {
    const none = parseSkinToolReport(remapJson(-1)).remap;
    const owned = parseSkinToolReport(remapJson(7)).remap;
    expect(remapAction(none)).toBe('create');
    expect(remapAction(owned)).toBeNull();
    expect(describeRemapRead(none)).toContain('no customization remap data');
    expect(describeRemapRead(owned)).toBe('customization remap: 1 record, 1 new bone (Extra)');
  });
});

describe('NifWeightTools', () => {
  it('paints a stroke on the selected shape with the locked bones and reports the save', async () => {
    const host = makeWeightHost();
    const onSaved = vi.fn();
    mountWeights(host, onSaved);
    await pickShape(host);

    fireEvent.change(screen.getByLabelText('Brush mode'), { target: { value: 'subtract' } });
    fireEvent.change(screen.getByLabelText('Brush bone'), { target: { value: '2' } });
    fireEvent.click(screen.getByLabelText('Lock LArm'));
    fireEvent.click(screen.getByText('Paint stroke'));

    await waitFor(() => expect(host.SkinToolOp).toHaveBeenCalledTimes(1));
    const [path, out, op, json, overwrite, source] = (host.SkinToolOp as ReturnType<typeof vi.fn>).mock.calls[0];
    expect([path, out, op, overwrite, source]).toEqual([NIF, '', 'brush', false, 'abc']);
    expect(JSON.parse(json)).toMatchObject({ block: 1, mode: 'subtract', bone: 2, locked: [0], radius: 5 });
    await waitFor(() => expect(onSaved).toHaveBeenCalledWith(NIF));
    expect(screen.getByRole('status').textContent).toContain('changed 2 vertices');
  });

  it('does not report a save when the stroke reached no vertex', async () => {
    const host = makeWeightHost({ SkinToolOp: vi.fn(async () => strokeJson('')) } as Partial<NifHost>);
    const onSaved = vi.fn();
    mountWeights(host, onSaved);
    await pickShape(host);

    fireEvent.click(screen.getByText('Paint stroke'));
    await waitFor(() => expect(screen.getByRole('status').textContent).toContain('nothing saved'));
    expect(onSaved).not.toHaveBeenCalled();
  });

  it('transfers from the chosen source shape and shows a missing-bone refusal without saving', async () => {
    const host = makeWeightHost({
      SkinToolOp: vi.fn(async () => JSON.stringify({ error: 'the target skin has no bone named LArm' })),
    } as Partial<NifHost>);
    const onSaved = vi.fn();
    mountWeights(host, onSaved);
    await pickShape(host);

    fireEvent.click(screen.getByText('Transfer'));
    fireEvent.click(screen.getByText('Choose source NIF'));
    await waitFor(() => expect(host.Tree).toHaveBeenCalledWith('C:\\mods\\donor.nif'));
    await waitFor(() => expect((screen.getByLabelText('Source shape') as HTMLSelectElement).value).toBe('4'));

    await waitFor(() => expect((screen.getByText('Transfer weights') as HTMLButtonElement).disabled).toBe(false));
    fireEvent.click(screen.getByText('Transfer weights'));
    await waitFor(() => expect(host.SkinToolOp).toHaveBeenCalledTimes(1));
    const [, , op, json] = (host.SkinToolOp as ReturnType<typeof vi.fn>).mock.calls[0];
    expect(op).toBe('transfer');
    expect(JSON.parse(json)).toEqual({
      block: 1, source_path: 'C:\\mods\\donor.nif', source_block: 4, method: 'barycentric', max_distance: 0,
    });
    await waitFor(() => expect(screen.getByRole('alert').textContent).toContain('no bone named LArm'));
    expect(onSaved).not.toHaveBeenCalled();
  });

  it('creates the remap from the skin weights when the shape has no remap block', async () => {
    const SkinToolOp = vi.fn(async (_p: string, _o: string, op: string) => {
      if (op === 'remap_read') return remapJson(-1);
      return JSON.stringify({ ok: true, op, saved: NIF, records: 4, new_bones: 0 });
    });
    const host = makeWeightHost({ SkinToolOp } as Partial<NifHost>);
    const onSaved = vi.fn();
    mountWeights(host, onSaved);
    await pickShape(host);

    fireEvent.click(screen.getByText('Remap'));
    fireEvent.click(screen.getByText('Read remap'));
    await waitFor(() => expect((screen.getByText('Create remap from skin weights') as HTMLButtonElement).disabled).toBe(false));
    fireEvent.click(screen.getByText('Create remap from skin weights'));
    await waitFor(() => expect(onSaved).toHaveBeenCalledWith(NIF));
    expect(SkinToolOp.mock.calls.at(-1)![2]).toBe('remap_create');
    expect(JSON.parse((SkinToolOp.mock.calls.at(-1) as unknown as string[])[3])).toEqual({ block: 1 });
    expect(screen.getByRole('status').textContent).toContain('remap created: 4 records, 0 new bones');
    expect(screen.queryByText('Create remap from skin weights')).toBeNull();
  });

  it('reads the remap and lists validation problems without offering a save over the owned block', async () => {
    const SkinToolOp = vi.fn(async (_p: string, _o: string, op: string) => {
      if (op === 'remap_read') return remapJson(7);
      if (op === 'remap_validate') return JSON.stringify({ ok: true, messages: ['record 3 names bone 9'] });
      return JSON.stringify({ ok: true, op, saved: NIF, records: 1, new_bones: 1 });
    });
    const host = makeWeightHost({ SkinToolOp } as Partial<NifHost>);
    const onSaved = vi.fn();
    mountWeights(host, onSaved);
    await pickShape(host);

    fireEvent.click(screen.getByText('Remap'));
    fireEvent.click(screen.getByText('Read remap'));
    await waitFor(() => expect(screen.getByRole('status').textContent).toBe('customization remap: 1 record, 1 new bone (Extra)'));
    expect(screen.queryByText('Create remap from skin weights')).toBeNull();

    await waitFor(() => expect((screen.getByText('Validate remap') as HTMLButtonElement).disabled).toBe(false));
    fireEvent.click(screen.getByText('Validate remap'));
    await waitFor(() => expect(screen.getByLabelText('Remap problems').textContent).toContain('record 3 names bone 9'));

    expect(screen.queryByRole('button', { name: /remap from skin weights/ })).toBeNull();
    // Must catch a panel that offers or sends remap_write over a remap that already has its own records and new bones.
    expect(SkinToolOp.mock.calls.some(([, , op]) => op === 'remap_write')).toBe(false);
    expect(onSaved).not.toHaveBeenCalled();
  });
});

const OSD = 'C:\\mods\\Body.osd';
const OSP = 'C:\\mods\\Body.osp';

const CHOICE: OsdApplyChoice = { osdPath: OSD, ospPath: '', setName: '', slider: '', dataSet: 'Breasts', weight: 0.5 };

const osdReadJson = JSON.stringify({
  ok: true, op: 'osd_read', saved: '', version: 1, sets: [{ name: 'Breasts', offsets: 2 }, { name: 'Butt', offsets: 1 }],
});

const sliderJson = (name: string, invert: boolean) => ({
  name, invert, hidden: false, zap: false, uv: false, small: 0, big: 100,
  data: [{ name, target: 'Cuirass', local: true, file: 'Body.osd', name_in_file: name, legacy_bsd: false }],
});

const ospReadJson = JSON.stringify({
  ok: true, op: 'osp_read', saved: '', version: 1,
  sets: [{ name: 'Body', shapes: [{ name: 'body', target: 'Cuirass' }],
    sliders: [sliderJson('Breasts', false), sliderJson('Flipped', true)] }],
});

const appliedJson = (saved: string, dataSet = 'Breasts') => JSON.stringify({
  ok: true, op: 'osd_apply', block: 1, saved, moved: saved === '' ? 0 : 2, offsets: 2, data_set: dataSet,
});

describe('weightToolsClient Outfit Studio', () => {
  it('builds the data-set form and the slider form of osd_apply with only the keys each one reads', () => {
    // Must catch a data-set apply that also sends slider keys, or a slider apply that drops the .osp or the set.
    expect(osdApplyArgs(1, CHOICE)).toEqual({ block: 1, osd_path: OSD, weight: 0.5, data_set: 'Breasts' });
    expect(osdApplyArgs(1, { ...CHOICE, ospPath: OSP, setName: 'Body', slider: 'Breasts', dataSet: 'Butt' })).toEqual({
      block: 1, osd_path: OSD, weight: 0.5, osp_path: OSP, slider: 'Breasts', set: 'Body',
    });
    expect(osdApplyArgs(1, { ...CHOICE, ospPath: OSP, slider: 'Breasts' })).not.toHaveProperty('set');
  });

  it('refuses a missing shape or file, a weight outside 0 to 1 or not finite, and a missing choice before calling the host', () => {
    // Must catch a request that reaches the host with a weight that is not a finite number from 0 to 1.
    expect(() => osdApplyArgs(1, { ...CHOICE, weight: -0.1 })).toThrow('0 to 1');
    expect(() => osdApplyArgs(1, { ...CHOICE, weight: 1.01 })).toThrow('0 to 1');
    expect(() => osdApplyArgs(1, { ...CHOICE, weight: Number.NaN })).toThrow('0 to 1');
    expect(() => osdApplyArgs(1, { ...CHOICE, weight: Number.POSITIVE_INFINITY })).toThrow('0 to 1');
    expect(osdApplyArgs(1, { ...CHOICE, weight: 0 })).toMatchObject({ weight: 0 });
    expect(osdApplyArgs(1, { ...CHOICE, weight: 1 })).toMatchObject({ weight: 1 });
    // Must catch an apply with no shape, no .osd, a slider without its .osp, or no data set picked.
    expect(() => osdApplyArgs(-1, CHOICE)).toThrow('select a shape');
    expect(() => osdApplyArgs(1, { ...CHOICE, osdPath: '' })).toThrow('choose the .osd');
    expect(() => osdApplyArgs(1, { ...CHOICE, slider: 'Breasts' })).toThrow('choose the .osp');
    expect(() => osdApplyArgs(1, { ...CHOICE, dataSet: '' })).toThrow('data set');
  });

  it('reads the osp and osd listings and says nothing was saved when no vertex moved', () => {
    const osp = parseSkinToolReport(ospReadJson);
    // Must catch the slider flags or data entries being lost between the host reply and the panel.
    expect(osp.ospSets[0].sliders.map(s => [s.name, s.invert])).toEqual([['Breasts', false], ['Flipped', true]]);
    expect(osp.ospSets[0].sliders[0].data[0]).toMatchObject({ file: 'Body.osd', nameInFile: 'Breasts', legacyBsd: false });
    expect(osp.osdSets).toEqual([]);
    const osd = parseSkinToolReport(osdReadJson);
    expect(osd.osdSets).toEqual([{ name: 'Breasts', offsets: 2 }, { name: 'Butt', offsets: 1 }]);
    expect(osd.ospSets).toEqual([]);
    // Must catch a summary that claims a save when the host wrote nothing.
    expect(describeOsdApply(parseSkinToolReport(appliedJson('')))).toContain('nothing saved');
    expect(describeOsdApply(parseSkinToolReport(appliedJson(NIF)))).toBe('slider data Breasts moved 2 vertices');
  });
});

const makeSliderHost = (overrides: Partial<NifHost> = {}) => makeWeightHost({
  BrowseForFile: vi.fn(async (title: string) => (title.includes('.osd') ? OSD : OSP)),
  SkinToolOp: vi.fn(async (_p: string, _o: string, op: string, json: string) => {
    if (op === 'osd_read') return osdReadJson;
    if (op === 'osp_read') return ospReadJson;
    return appliedJson(NIF, (JSON.parse(json) as { data_set?: string }).data_set);
  }),
  ...overrides,
} as Partial<NifHost>);

const openSliders = async (host: NifHost, onSaved: (result: string) => void = () => {}) => {
  mountWeights(host, onSaved);
  await pickShape(host);
  fireEvent.click(screen.getByText('Sliders'));
  fireEvent.click(screen.getByText('Choose .osd'));
  await waitFor(() => expect((screen.getByLabelText('Slider data set') as HTMLSelectElement).value).toBe('Breasts'));
  await waitFor(() => expect((screen.getByText('Apply slider') as HTMLButtonElement).disabled).toBe(false));
};

const skinToolCalls = (host: NifHost) => (host.SkinToolOp as ReturnType<typeof vi.fn>).mock.calls;

describe('NifWeightTools Sliders', () => {
  it('applies the chosen data set at the chosen weight to the selected shape and reports the save', async () => {
    const host = makeSliderHost();
    const onSaved = vi.fn();
    await openSliders(host, onSaved);

    fireEvent.change(screen.getByLabelText('Slider data set'), { target: { value: 'Butt' } });
    fireEvent.change(screen.getByLabelText('Slider weight'), { target: { value: '0.5' } });
    fireEvent.click(screen.getByText('Apply slider'));

    await waitFor(() => expect(skinToolCalls(host)).toHaveLength(2));
    const [path, out, op, json, overwrite, source] = skinToolCalls(host)[1];
    // Must catch the wrong op, shape, data set, weight or source digest being sent for a slider apply.
    expect([path, out, op, overwrite, source]).toEqual([NIF, '', 'osd_apply', false, 'abc']);
    expect(JSON.parse(json)).toEqual({ block: 1, osd_path: OSD, weight: 0.5, data_set: 'Butt' });
    await waitFor(() => expect(onSaved).toHaveBeenCalledWith(NIF));
    expect(screen.getByRole('status').textContent).toBe('slider data Butt moved 2 vertices');
  });

  it('applies a picked slider through its project and offers no flagged slider', async () => {
    const host = makeSliderHost();
    await openSliders(host);

    fireEvent.click(screen.getByText('Choose .osp'));
    await waitFor(() => expect((screen.getByLabelText('Outfit Studio set') as HTMLSelectElement).value).toBe('Body'));
    await waitFor(() => expect((screen.getByLabelText('Slider') as HTMLSelectElement).disabled).toBe(false));
    // Must catch an inverted slider being offered as if its weight scale were known.
    expect((screen.getByText(/Flipped/) as HTMLOptionElement).disabled).toBe(true);
    fireEvent.change(screen.getByLabelText('Slider'), { target: { value: 'Breasts' } });
    expect((screen.getByLabelText('Slider data set') as HTMLSelectElement).disabled).toBe(true);
    fireEvent.click(screen.getByText('Apply slider'));

    await waitFor(() => expect(skinToolCalls(host).map(call => call[2])).toEqual(['osd_read', 'osp_read', 'osd_apply']));
    // Must catch the slider form dropping the .osp, the set or the slider name, which leaves the host nothing to resolve.
    expect(JSON.parse(skinToolCalls(host)[2][3])).toEqual({
      block: 1, osd_path: OSD, weight: 1, osp_path: OSP, slider: 'Breasts', set: 'Body',
    });
  });

  it('shows a host refusal and does not report a save', async () => {
    const host = makeSliderHost({
      SkinToolOp: vi.fn(async (_p: string, _o: string, op: string) => (op === 'osd_read'
        ? osdReadJson
        : JSON.stringify({ error: 'slider Breasts has no data for shape body in this .osd' }))),
    } as Partial<NifHost>);
    const onSaved = vi.fn();
    await openSliders(host, onSaved);

    fireEvent.click(screen.getByText('Apply slider'));
    // Must catch a host refusal being swallowed or shown as success.
    await waitFor(() => expect(screen.getByRole('alert').textContent).toContain('has no data for shape body'));
    expect(onSaved).not.toHaveBeenCalled();
  });

  it('says nothing was saved when the host moved no vertex', async () => {
    const host = makeSliderHost({
      SkinToolOp: vi.fn(async (_p: string, _o: string, op: string) => (op === 'osd_read' ? osdReadJson : appliedJson(''))),
    } as Partial<NifHost>);
    const onSaved = vi.fn();
    await openSliders(host, onSaved);

    fireEvent.click(screen.getByText('Apply slider'));
    // Must catch onSaved being called, which reloads the NIF, for an apply that wrote nothing.
    await waitFor(() => expect(screen.getByRole('status').textContent).toContain('nothing saved'));
    expect(onSaved).not.toHaveBeenCalled();
  });

  it('refuses a weight above 1 in the panel without calling the host', async () => {
    const host = makeSliderHost();
    await openSliders(host);

    fireEvent.change(screen.getByLabelText('Slider weight'), { target: { value: '1.5' } });
    fireEvent.click(screen.getByText('Apply slider'));
    // Must catch an out-of-range weight reaching the host from the panel.
    await waitFor(() => expect(screen.getByRole('alert').textContent).toContain('from 0 to 1'));
    expect(skinToolCalls(host).map(call => call[2])).toEqual(['osd_read']);
  });
});

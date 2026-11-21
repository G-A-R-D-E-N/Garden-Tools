import { cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import PrevisPatchWizard from './PrevisPatchWizard';
import { ocTemplateCandidates } from './previsOcTemplates';

const bridge = vi.hoisted(() => ({
  resolve: vi.fn<(...args: unknown[]) => Promise<unknown>>(),
}));

vi.mock('./backend', () => ({
  getPrevis: () => ({ ResolveOcTemplate: bridge.resolve, PatchWorkflow: async () => [] }),
}));

const template = (plugin: string, ocId: string) => ({
  path: `D:/Data/Meshes/PreCombined/${plugin}/00000fec_${ocId}_OC.NIF`,
  relative: `${plugin}/00000fec_${ocId}_OC.NIF`,
  plugin,
  ocId,
});

const resolved = (templates: ReturnType<typeof template>[]) => ({
  cell: '000FEC:Fallout4.esm',
  cellPrefix: '00000fec',
  dataDir: 'D:/Data',
  templates,
  suggestedSourceNif: templates[0]?.path ?? '',
  suggestedOutputOc: 'D:/Data/Meshes/PreCombined/xPrevisPatch.esp/00000fec_0a82376a_OC.NIF',
  truncated: false,
});

const wizard = () => (
  <PrevisPatchWizard selectedCell="000FEC:Fallout4.esm" selectedPlugin="Fallout4.esm" onLog={() => {}} />
);

const sourceBox = () => screen.getByPlaceholderText(/Measured vanilla/) as HTMLInputElement;
const fieldAfter = (label: string) => screen.getByText(label).nextElementSibling as HTMLInputElement;

beforeEach(() => {
  vi.resetAllMocks();
});

afterEach(cleanup);

describe('previs wizard template auto-fill', () => {
  // Detects: auto-fill that takes the patch's own _OC output as the template must fail.
  it('does not fill the patch own output as the template', async () => {
    bridge.resolve.mockResolvedValue(resolved([
      template('xPrevisPatch.esp', '0a82376a'),
      template('Fallout4.esm', '01b8656e'),
    ]));
    render(wizard());
    await waitFor(() => expect(sourceBox().value).toContain('Fallout4.esm/00000fec_01b8656e_OC.NIF'));
    expect(sourceBox().value).not.toContain('xPrevisPatch.esp');
  });

  // Detects: auto-fill that forces one shell when several are possible must fail.
  it('leaves the template empty and lists the candidates when several remain', async () => {
    bridge.resolve.mockResolvedValue(resolved([
      template('Fallout4.esm', '01b8656e'),
      template('DLCRobot.esm', '0a722d3c'),
      template('xPrevisPatch.esp', '0a82376a'),
    ]));
    render(wizard());
    const select = await screen.findByLabelText('Template candidates') as HTMLSelectElement;
    expect(sourceBox().value).toBe('');
    expect(Array.from(select.options).map(option => option.text)).toEqual([
      "Generate picks the cell's own _OC from Data",
      'Fallout4.esm/00000fec_01b8656e_OC.NIF',
      'DLCRobot.esm/00000fec_0a722d3c_OC.NIF',
    ]);

    fireEvent.change(select, { target: { value: template('DLCRobot.esm', '0a722d3c').path } });
    expect(sourceBox().value).toBe(template('DLCRobot.esm', '0a722d3c').path);
  });

  // Detects: the template fill overwriting an output path the user already typed must fail.
  it('fills the template after the user types an output path and keeps that path', async () => {
    const typed = 'D:/Data/Meshes/PreCombined/xPrevisPatch.esp/mine_OC.NIF';
    bridge.resolve.mockResolvedValue(resolved([template('Fallout4.esm', '01b8656e')]));
    render(wizard());
    const output = fieldAfter('Output _OC path');
    fireEvent.change(output, { target: { value: typed } });
    await waitFor(() => expect(sourceBox().value).toContain('Fallout4.esm/00000fec_01b8656e_OC.NIF'));
    expect(output.value).toBe(typed);
  });

  // Detects: a rescan refilling a template the user deliberately cleared must fail.
  it('keeps a cleared template empty across a rescan', async () => {
    bridge.resolve.mockResolvedValue(resolved([template('Fallout4.esm', '01b8656e')]));
    render(wizard());
    await waitFor(() => expect(sourceBox().value).toContain('01b8656e'));
    fireEvent.change(sourceBox(), { target: { value: '' } });
    fireEvent.change(fieldAfter('Source plugin (optional)'), { target: { value: '' } });
    await waitFor(() => expect(bridge.resolve).toHaveBeenCalledTimes(2));
    await new Promise(resolve => setTimeout(resolve, 100));
    expect(sourceBox().value).toBe('');
  });

  // Detects: a cleared template staying empty after the user switches to another cell must fail.
  it('refills a cleared template after the cell changes', async () => {
    bridge.resolve.mockResolvedValue(resolved([template('Fallout4.esm', '01b8656e')]));
    render(wizard());
    await waitFor(() => expect(sourceBox().value).toContain('01b8656e'));
    fireEvent.change(sourceBox(), { target: { value: '' } });
    fireEvent.change(fieldAfter('Cell'), { target: { value: '000FED:Fallout4.esm' } });
    await waitFor(() => expect(sourceBox().value).toContain('01b8656e'));
  });

  // Detects: a scan fired on every keystroke instead of once after typing stops must fail.
  it('scans once after typing stops', async () => {
    bridge.resolve.mockResolvedValue(resolved([]));
    render(wizard());
    const patch = screen.getByDisplayValue('xPrevisPatch.esp');
    for (const text of ['xPrevisPatch.esp1', 'xPrevisPatch.esp12', 'xPrevisPatch.esp123']) {
      fireEvent.change(patch, { target: { value: text } });
    }
    await waitFor(() => expect(bridge.resolve).toHaveBeenCalled());
    await new Promise(resolve => setTimeout(resolve, 500));
    expect(bridge.resolve).toHaveBeenCalledTimes(1);
    expect(bridge.resolve.mock.calls[0][2]).toBe('xPrevisPatch.esp123');
  });
});

describe('ocTemplateCandidates', () => {
  it('drops the patch folder and the exact output path, keeps other plugins', () => {
    const own = template('xPrevisPatch.esp', '0a82376a');
    const other = template('Fallout4.esm', '01b8656e');
    const another = template('DLCRobot.esm', '0a722d3c');
    expect(ocTemplateCandidates([own, other], 'xprevispatch.ESP', '')).toEqual([other]);
    expect(ocTemplateCandidates([own, other], 'C:\\mods\\xPrevisPatch.esp', '')).toEqual([other]);
    expect(ocTemplateCandidates([other, another], 'Patch.esp', other.path.replace(/\//g, '\\').toUpperCase()))
      .toEqual([another]);
  });
});

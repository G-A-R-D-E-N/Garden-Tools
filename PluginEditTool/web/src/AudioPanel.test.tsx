import { act, cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';
import type { AudioHost } from './backend';
import AudioPanel from './AudioPanel';

function installHost(): AudioHost {
  const host: AudioHost = {
    BrowseForFile: vi.fn(async () => ''),
    BrowseForFolder: vi.fn(async () => ''),
    BrowseForSave: vi.fn(async () => ''),
    ConvertToXwm: vi.fn(async () => 'RESULT: success'),
    ConvertFromXwm: vi.fn(async () => 'RESULT: success'),
    MakeFuz: vi.fn(async () => 'RESULT: success'),
    ExtractFuz: vi.fn(async () => 'RESULT: success'),
    OpenFolder: vi.fn(async () => ''),
    StageDroppedFile: vi.fn(async () => ''),
  };
  Object.assign(window, { chrome: { webview: { hostObjects: { audio: host } } } });
  return host;
}

const runButton = (name: RegExp) => {
  const all = screen.getAllByRole('button', { name });
  return all[all.length - 1] as HTMLButtonElement;
};
const tabButton = (label: string) => screen.getAllByRole('button', { name: label })[0];
const openFolder = () => screen.queryByRole('button', { name: /Open folder/ });

afterEach(() => {
  cleanup();
  localStorage.clear();
  Reflect.deleteProperty(window, 'chrome');
});

describe('AudioPanel without the audio bridge', () => {
  it('shows the unavailable warning and keeps the run button disabled even with a source filled in', () => {
    localStorage.setItem('audio.xwmSource', 'D:/voice.wav');
    render(<AudioPanel onClose={() => {}} />);
    expect(screen.getByText(/Audio bridge not available/)).toBeTruthy();
    expect(runButton(/Convert to XWM/).disabled).toBe(true);
  });
});

type ModeCase = {
  tab: string;
  stored: [string, string][];
  run: RegExp;
  result: string;
  log: RegExp;
  expectCall: (host: AudioHost) => void;
};

const MODE_CASES: ModeCase[] = [
  {
    tab: 'To XWM',
    stored: [['audio.xwmSource', 'D:/voice.wav']],
    run: /Convert to XWM/,
    result: 'RESULT: success\nencoded -> D:/to-xwm-out',
    log: /✓ encoded voice\.wav/,
    expectCall: host => expect(host.ConvertToXwm).toHaveBeenCalledWith('D:/voice.wav', '', 0),
  },
  {
    tab: 'From XWM',
    stored: [['audio.decSource', 'D:/voice.xwm']],
    run: /Convert from XWM/,
    result: 'RESULT: success\ndecoded -> D:/from-xwm-out',
    log: /✓ decoded voice\.xwm/,
    expectCall: host => expect(host.ConvertFromXwm).toHaveBeenCalledWith('D:/voice.xwm', '', 'wav'),
  },
  {
    tab: 'Make Fuz',
    stored: [['audio.fuzAudioSource', 'D:/voice.wav'], ['audio.fuzOutput', 'D:/voice.fuz']],
    run: /Make Fuz/,
    result: 'RESULT: success\npacked -> D:/fuz-make-out',
    log: /✓ packed voice\.wav/,
    expectCall: host => expect(host.MakeFuz).toHaveBeenCalledWith('D:/voice.wav', '', 'D:/voice.fuz', false),
  },
  {
    tab: 'Extract Fuz',
    stored: [['audio.extSource', 'D:/voice.fuz']],
    run: /Extract Fuz/,
    result: 'RESULT: success\nextracted -> D:/fuz-extract-out',
    log: /✓ extracted voice\.fuz/,
    expectCall: host => expect(host.ExtractFuz).toHaveBeenCalledWith('D:/voice.fuz', '', '', true),
  },
];

describe('AudioPanel mode runs', () => {
  for (const c of MODE_CASES) {
    it(`${c.tab} calls its host method and offers the created output folder`, async () => {
      const host = installHost();
      const method = c.tab === 'To XWM' ? 'ConvertToXwm'
        : c.tab === 'From XWM' ? 'ConvertFromXwm'
        : c.tab === 'Make Fuz' ? 'MakeFuz' : 'ExtractFuz';
      Object.assign(host, { [method]: vi.fn(async () => c.result) });
      for (const [key, value] of c.stored) localStorage.setItem(key, value);

      render(<AudioPanel onClose={() => {}} />);
      fireEvent.click(tabButton(c.tab));
      fireEvent.click(runButton(c.run));

      expect(await screen.findByText('Done')).toBeTruthy();
      c.expectCall(host);
      expect(openFolder()).not.toBeNull();
      expect(screen.getByText(c.log)).toBeTruthy();
      expect(runButton(c.run).disabled).toBe(false);
    });
  }
});

describe('AudioPanel mode switch during an in-flight conversion', () => {
  it('ignores a late ConvertToXwm result and leaves the previous folder closed', async () => {
    const host = installHost();
    let resolveConvert: (value: string) => void = () => {};
    host.ConvertToXwm = vi.fn()
      .mockResolvedValueOnce('RESULT: success\nencoded -> D:/first-out')
      .mockReturnValueOnce(new Promise<string>(resolve => { resolveConvert = resolve; }));
    localStorage.setItem('audio.xwmSource', 'D:/voice.wav');
    localStorage.setItem('audio.decSource', 'D:/voice.xwm');

    render(<AudioPanel onClose={() => {}} />);
    fireEvent.click(runButton(/Convert to XWM/));
    await waitFor(() => expect(openFolder()).not.toBeNull());

    fireEvent.click(runButton(/Convert to XWM/));
    await screen.findByRole('button', { name: /Working/ });

    fireEvent.click(tabButton('From XWM'));
    expect(openFolder()).toBeNull();
    expect(screen.queryByText(/Working/)).toBeNull();
    expect(runButton(/Convert from XWM/).disabled).toBe(false);

    await act(async () => { resolveConvert('RESULT: success\nencoded -> D:/late-out'); });

    expect(screen.queryByText(/D:\/late-out/)).toBeNull();
    expect(openFolder()).toBeNull();
    expect(runButton(/Convert from XWM/).disabled).toBe(false);
    expect(host.ConvertFromXwm).not.toHaveBeenCalled();
  });

  it('ignores a late ConvertToXwm rejection after switching modes', async () => {
    const host = installHost();
    let rejectConvert: (reason: unknown) => void = () => {};
    host.ConvertToXwm = vi.fn(() => new Promise<string>((_resolve, reject) => { rejectConvert = reject; }));
    localStorage.setItem('audio.xwmSource', 'D:/voice.wav');
    localStorage.setItem('audio.decSource', 'D:/voice.xwm');

    const { container } = render(<AudioPanel onClose={() => {}} />);
    fireEvent.click(runButton(/Convert to XWM/));
    await screen.findByRole('button', { name: /Working/ });

    fireEvent.click(tabButton('From XWM'));
    expect(screen.queryByText(/Working/)).toBeNull();

    await act(async () => { rejectConvert(new Error('late boom')); });

    expect(container.querySelector('.audio-banner.error')).toBeNull();
    expect(screen.queryByText(/late boom/)).toBeNull();
    expect(runButton(/Convert from XWM/).disabled).toBe(false);
  });

  it('keeps an in-flight run active when clicking the already-active mode tab', async () => {
    const host = installHost();
    let resolveConvert: (value: string) => void = () => {};
    host.ConvertToXwm = vi.fn(() => new Promise<string>(resolve => { resolveConvert = resolve; }));
    localStorage.setItem('audio.xwmSource', 'D:/voice.wav');

    render(<AudioPanel onClose={() => {}} />);
    fireEvent.click(runButton(/Convert to XWM/));
    const working = await screen.findByRole('button', { name: /Working/ });
    expect((working as HTMLButtonElement).disabled).toBe(true);

    fireEvent.click(tabButton('To XWM'));
    expect((screen.getByRole('button', { name: /Working/ }) as HTMLButtonElement).disabled).toBe(true);

    await act(async () => { resolveConvert('RESULT: success\nencoded -> D:/kept-out'); });

    await waitFor(() => expect(openFolder()?.getAttribute('title')).toBe('D:/kept-out'));
    expect(screen.getByText(/✓ encoded voice\.wav -- RESULT: success/)).toBeTruthy();
  });
});

describe('AudioPanel drag-and-drop staging', () => {
  it('chunks large dropped files into base64 and ignores events without dataTransfer', async () => {
    const host = installHost();
    host.StageDroppedFile = vi.fn(async () => 'D:/staged/big.wav');
    render(<AudioPanel onClose={() => {}} />);
    const dropZone = document.querySelector('.audio-drop')!;

    fireEvent.drop(dropZone, {});
    expect(host.StageDroppedFile).not.toHaveBeenCalled();

    const payload = new Uint8Array(0x8005);
    payload.fill(65);
    const file = new File([payload], 'big.wav');
    Object.defineProperty(file, 'arrayBuffer', { value: async () => payload.buffer });
    await act(async () => {
      fireEvent.drop(dropZone, { dataTransfer: { files: [file] } });
    });

    expect(host.StageDroppedFile).toHaveBeenCalledTimes(1);
    const [stagedName, b64] = (host.StageDroppedFile as ReturnType<typeof vi.fn>).mock.calls[0] as [string, string];
    expect(stagedName).toBe('big.wav');
    expect(atob(b64).length).toBe(0x8005);
    expect(screen.getByText(/• dropped big\.wav -> source set/)).toBeTruthy();
  });
});

describe('AudioPanel failed runs', () => {
  it('replaces a previous output folder with an error banner when the bridge rejects', async () => {
    const host = installHost();
    host.ConvertToXwm = vi.fn()
      .mockResolvedValueOnce('RESULT: success\nencoded -> D:/first-out')
      .mockRejectedValueOnce(new Error('xWMAEncode missing'));
    localStorage.setItem('audio.xwmSource', 'D:/voice.wav');

    const { container } = render(<AudioPanel onClose={() => {}} />);
    fireEvent.click(runButton(/Convert to XWM/));
    await waitFor(() => expect(openFolder()).not.toBeNull());

    fireEvent.click(runButton(/Convert to XWM/));
    await waitFor(() => expect(container.querySelector('.audio-banner.error')).not.toBeNull());
    expect(container.querySelector('.audio-banner.error')?.textContent).toContain('xWMAEncode missing');
    expect(openFolder()).toBeNull();
    expect(screen.getByText(/✗ encoded voice\.wav -- Error: xWMAEncode missing/)).toBeTruthy();
  });

  it('ignores the folder printed by a run that reports a failure', async () => {
    const host = installHost();
    host.ConvertToXwm = vi.fn()
      .mockResolvedValueOnce('RESULT: success\nencoded -> D:/first-out')
      .mockResolvedValueOnce('RESULT: 0/1 converted\nencoded -> D:/second-out');
    localStorage.setItem('audio.xwmSource', 'D:/voice.wav');

    const { container } = render(<AudioPanel onClose={() => {}} />);
    fireEvent.click(runButton(/Convert to XWM/));
    await waitFor(() => expect(openFolder()).not.toBeNull());

    fireEvent.click(runButton(/Convert to XWM/));
    await waitFor(() => expect(container.querySelector('.audio-banner.error')?.textContent).toContain('0/1 converted'));
    expect(openFolder()).toBeNull();
    expect(screen.getByText(/✗ encoded voice\.wav -- RESULT: 0\/1 converted/)).toBeTruthy();
  });
});

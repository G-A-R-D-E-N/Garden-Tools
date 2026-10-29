import { cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import SettingsModal from './SettingsModal';

const baseSettings = {
  AiProvider: 'anthropic', hasAnthropicApiKey: false, Model: 'claude-sonnet-4-6',
  hasGeminiApiKey: false, GeminiModel: 'gemini-2.0-flash', ClaudeCodePath: '', OllamaUrl: '',
  OllamaModel: '', OutputFolder: '', DataFolder: '', CkWikiPath: '', TexconvPath: '',
  PapyrusCompilerPath: '', PapyrusBaseImports: '', NiftoolPath: '', FfmpegPath: '',
  XwmaEncodePath: '', Archive2Path: '',
};
const saveSettings = vi.fn<(payload: string) => Promise<string>>(async () => 'Saved');

function stubSettings(overrides: Record<string, unknown>) {
  vi.stubGlobal('chrome', { webview: { hostObjects: { settings: {
    GetSettings: async () => JSON.stringify({ ...baseSettings, ...overrides }),
    SaveSettings: saveSettings,
  } } } });
}

function savedPayload(): Record<string, unknown> {
  return JSON.parse(saveSettings.mock.calls[0][0]) as Record<string, unknown>;
}

beforeEach(() => { vi.clearAllMocks(); });
afterEach(() => { cleanup(); vi.unstubAllGlobals(); });

describe('Settings API key labels', () => {
  it.each([
    ['anthropic', 'hasAnthropicApiKey', true, /configured/, /not set/],
    ['anthropic', 'hasAnthropicApiKey', false, /not set/, /configured/],
    ['gemini', 'hasGeminiApiKey', true, /configured/, /not set/],
    ['gemini', 'hasGeminiApiKey', false, /not set/, /configured/],
  ])('labels the %s key from %s=%s', async (provider, flag, value, shown, hidden) => {
    stubSettings({ AiProvider: provider, [flag]: value });
    render(<SettingsModal onClose={() => {}} />);
    expect(await screen.findByText(shown)).toBeTruthy();
    expect(screen.queryByText(hidden)).toBeNull();
  });
});

describe('Settings save payload', () => {
  it('omits both API key fields when the key inputs are blank', async () => {
    stubSettings({ AiProvider: 'anthropic', hasAnthropicApiKey: true });
    render(<SettingsModal onClose={() => {}} />);
    fireEvent.click(await screen.findByRole('button', { name: 'Save' }));
    await waitFor(() => expect(saveSettings).toHaveBeenCalledTimes(1));
    expect(savedPayload()).not.toHaveProperty('AnthropicApiKey');
    expect(savedPayload()).not.toHaveProperty('GeminiApiKey');
  });

  it('includes a typed Anthropic key, trimmed, in the save payload', async () => {
    stubSettings({ AiProvider: 'anthropic', hasAnthropicApiKey: true });
    render(<SettingsModal onClose={() => {}} />);
    fireEvent.change(await screen.findByPlaceholderText('leave blank to keep the current key'),
      { target: { value: '  sk-ant-typed  ' } });
    fireEvent.click(screen.getByRole('button', { name: 'Save' }));
    await waitFor(() => expect(saveSettings).toHaveBeenCalledTimes(1));
    expect(savedPayload().AnthropicApiKey).toBe('sk-ant-typed');
  });
});

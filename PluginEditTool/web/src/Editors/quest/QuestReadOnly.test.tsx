import { act, cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { afterEach, beforeEach, expect, it, vi } from 'vitest';
import QuestEditor from '../QuestEditor';
import { openQuest, openTab, stubBackend } from './questTestHarness';

const originalScrollIntoView = Element.prototype.scrollIntoView;

beforeEach(() => { Element.prototype.scrollIntoView = vi.fn(); });
afterEach(() => {
  cleanup();
  vi.unstubAllGlobals();
  Element.prototype.scrollIntoView = originalScrollIntoView;
});

async function settlePluginList(backend: ReturnType<typeof stubBackend>) {
  await waitFor(() => expect(backend.GetEditablePlugins).toHaveBeenCalled());
  await act(async () => { await new Promise(resolve => setTimeout(resolve, 0)); });
}

it('Detects: a game-file quest shows the read-only banner and keeps every tab button enabled', async () => {
  const backend = stubBackend({ GetEditablePlugins: vi.fn(async () => '["Other.esp"]') });
  render(<QuestEditor onBack={() => {}} />);
  await openQuest();
  expect(await screen.findByText(/Patch\.esp is a game file and cannot be changed/)).toBeTruthy();
  await settlePluginList(backend);
  for (const name of [/^Overview/, /^Stages/, /^Objectives/, /^Aliases/, /^Fragments/]) {
    expect(screen.getByRole('tab', { name }).matches(':disabled')).toBe(false);
  }
});

it('Detects: a game-file quest disables the objective text and number fields', async () => {
  stubBackend({ GetEditablePlugins: vi.fn(async () => '["Other.esp"]') });
  render(<QuestEditor onBack={() => {}} />);
  await openQuest();
  await screen.findByText(/is a game file and cannot be changed/);
  openTab('Objectives');
  expect((screen.getByLabelText('Objective 10 text') as HTMLTextAreaElement).matches(':disabled')).toBe(true);
  expect((screen.getByLabelText('Objective 10 number') as HTMLInputElement).matches(':disabled')).toBe(true);
  expect(screen.getByRole('button', { name: 'Add objective' }).matches(':disabled')).toBe(true);
});

it('Detects: a game-file quest disables the stage log text, and the flow chart keeps box selection and Check this quest enabled', async () => {
  stubBackend({ GetEditablePlugins: vi.fn(async () => '["Other.esp"]') });
  render(<QuestEditor onBack={() => {}} />);
  await openQuest();
  await screen.findByText(/is a game file and cannot be changed/);
  expect(screen.getByRole('button', { name: 'Check this quest' }).matches(':disabled')).toBe(false);
  fireEvent.click(await screen.findByRole('button', { name: /^Stage 10/ }));
  expect(screen.getByRole('button', { name: /^Stage 10/ }).matches(':disabled')).toBe(false);
  expect(screen.getByRole('button', { name: '+ next step' }).matches(':disabled')).toBe(true);
  expect(screen.getByRole('button', { name: 'Remove step' }).matches(':disabled')).toBe(true);
  expect((screen.getByLabelText('Stage 10 journal text') as HTMLTextAreaElement).matches(':disabled')).toBe(true);
  openTab('Stages');
  expect((screen.getByLabelText('Stage 10 log entry 1 text') as HTMLTextAreaElement).matches(':disabled')).toBe(true);
});

it('Detects: a writable quest shows no read-only banner and keeps the objective text enabled', async () => {
  const backend = stubBackend();
  render(<QuestEditor onBack={() => {}} />);
  await openQuest();
  await settlePluginList(backend);
  openTab('Objectives');
  expect(screen.queryByText(/is a game file and cannot be changed/)).toBeNull();
  expect((screen.getByLabelText('Objective 10 text') as HTMLTextAreaElement).matches(':disabled')).toBe(false);
});

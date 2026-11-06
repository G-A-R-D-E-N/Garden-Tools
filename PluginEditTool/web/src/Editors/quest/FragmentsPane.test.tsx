import { cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { afterEach, expect, it, vi } from 'vitest';
import QuestEditor from '../QuestEditor';
import { openQuest, stubBackend, undoButton } from './questTestHarness';

afterEach(() => { cleanup(); vi.unstubAllGlobals(); });

it('Detects: a second script write is sent, and Undo resends the previous actions for the same stage and log entry', async () => {
  const backend = stubBackend();
  render(<QuestEditor onBack={() => {}} />);
  await openQuest();
  fireEvent.change(await screen.findByLabelText('Script name'), { target: { value: 'QF_Courier_0010' } });
  fireEvent.change(screen.getByLabelText('Script output folder'), { target: { value: 'C:/Out' } });
  fireEvent.change(screen.getByLabelText('Script imports folder'), { target: { value: 'C:/Game/Scripts' } });
  fireEvent.change(screen.getByLabelText('Action 1 number'), { target: { value: '20' } });
  fireEvent.click(screen.getByRole('button', { name: 'Write script function' }));
  await waitFor(() => expect(backend.SetQuestFragment).toHaveBeenCalledTimes(1));
  fireEvent.change(screen.getByLabelText('Action 1 number'), { target: { value: '30' } });
  fireEvent.click(screen.getByRole('button', { name: 'Write script function' }));
  await waitFor(() => expect(backend.SetQuestFragment).toHaveBeenCalledTimes(2));
  await waitFor(() => expect(undoButton().disabled).toBe(false));
  fireEvent.click(undoButton());
  await waitFor(() => expect(backend.SetQuestFragment).toHaveBeenCalledTimes(3));
  expect(JSON.parse(String(backend.SetQuestFragment.mock.calls[1][7]))).toEqual([{ kind: 'stage', value: 30 }]);
  expect(JSON.parse(String(backend.SetQuestFragment.mock.calls[2][7]))).toEqual([{ kind: 'stage', value: 20 }]);
  expect(backend.SetQuestFragment.mock.calls[2].slice(0, 7)).toEqual(
    ['Patch.esp', '000800:Patch.esp', 0, 0, 'QF_Courier_0010', 'C:/Out', 'C:/Game/Scripts']);
});

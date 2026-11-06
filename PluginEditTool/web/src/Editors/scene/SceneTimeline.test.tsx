import { cleanup, render, screen, within } from '@testing-library/react';
import { afterEach, expect, it } from 'vitest';
import SceneTimeline from './SceneTimeline';
import { parseScene } from './sceneModel';

afterEach(cleanup);

const scene = parseScene({
  formKey: '000800:Patch.esp',
  phases: [{ index: 0, name: 'Walk' }, { index: 1, name: 'Talk' }, { index: 2, name: 'Leave' }],
  actors: [{ index: 0, alias: 3, flags: 0, behaviorFlags: 0 }],
  actions: [
    { index: 0, type: 0, name: 'Greeting', actorAlias: 3, startPhase: 1, endPhase: 2, payload: {} },
    { index: 1, type: 2, actorAlias: 9, startPhase: 0, endPhase: 9, payload: {} },
  ],
});

it('Detects: a block sits in its actor row across the phase columns it spans', () => {
  render(<SceneTimeline scene={scene} selection={null} onSelect={() => {}} />);
  const row = screen.getByRole('row', { name: 'Alias 3' });
  const block = within(row).getByRole('button', { name: 'Dialogue: Greeting, phases 2 to 3' });
  expect(block.style.gridColumn).toBe('3 / 5');
});

it('Detects: an action whose actor has no row is drawn in the No actor row, clamped to the last phase', () => {
  render(<SceneTimeline scene={scene} selection={null} onSelect={() => {}} />);
  const row = screen.getByRole('row', { name: 'No actor' });
  const block = within(row).getByRole('button', { name: 'Timer: Timer, phases 1 to 3' });
  expect(block.style.gridColumn).toBe('2 / 5');
});

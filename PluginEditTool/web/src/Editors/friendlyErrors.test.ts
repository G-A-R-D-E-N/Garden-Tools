import { expect, it } from 'vitest';
import { friendlyError } from './friendlyErrors';

it.each([
  {
    regression: 'removing a scene actor that an action still uses shows the raw backend text instead of a fix',
    raw: 'backend.RemoveSceneActor: RemoveSceneActorFailed: scene action 2 references the actor alias this edit removes',
    expected: 'A scene action still uses this actor.',
  },
  {
    regression: 'removing a scene phase that an action still uses shows the raw backend text instead of a fix',
    raw: 'backend.RemoveScenePhase: RemoveScenePhaseFailed: scene action 1 references the phase this edit removes',
    expected: 'A scene action still uses this phase.',
  },
  {
    regression: 'removing a phase that a script fragment still binds shows the raw backend text instead of a fix',
    raw: 'backend.RemoveScenePhase: RemoveScenePhaseFailed: VMAD scene phase fragment binds the phase this edit removes',
    expected: 'A script fragment is still attached to this phase.',
  },
  {
    regression: 'a duplicate objective index from SetQuestObjectives shows the raw backend text instead of a fix',
    raw: 'backend.SetQuestObjectives: InvalidQuestObjectivePayload: duplicate objective index 4',
    expected: 'Another objective in this quest already uses this number.',
  },
  {
    regression: 'an objective index already in use from the add path shows the raw backend text instead of a fix',
    raw: 'backend.SetQuestObjectives: SetQuestObjectivesFailed: QUST already has an objective with index 4',
    expected: 'Another objective in this quest already uses this number.',
  },
  {
    regression: 'a topic and branch from different quests shows the raw backend text instead of a fix',
    raw: 'backend.SetDialogueBranchStart: DialogueStructureInvalid: Dialogue structure does not allow this edit: branch 00ABCD',
    expected: 'The dialogue links do not fit together.',
  },
  {
    regression: 'text Windows-1252 cannot hold shows the raw backend text instead of a plain sentence',
    raw: 'backend.CreateInfo: InvalidDialogueArgument: responses has characters that Windows-1252 cannot hold',
    expected: 'This text has characters the game cannot store.',
  },
])('Detects: $regression', ({ raw, expected }) => {
  const friendly = friendlyError(raw);
  expect(friendly.text).toContain(expected);
  expect(friendly.details).toBe(raw);
});

it('Detects: the action number from the backend is lost, so the user cannot tell which scene action blocks the removal', () => {
  const raw = 'backend.RemoveSceneActor: RemoveSceneActorFailed: scene action 2 references the actor alias this edit removes';
  expect(friendlyError(raw).details).toContain('scene action 2');
});

it('Detects: an unrecognised backend message is replaced or dropped, so a real error the user must read would be hidden', () => {
  const raw = 'backend.GetScene: GetSceneFailed: something new went wrong';
  expect(friendlyError(raw)).toEqual({ text: raw });
});

import { fireEvent, screen } from '@testing-library/react';
import { vi } from 'vitest';

export const QUEST = '000800:Patch.esp';

export const SAMPLE = {
  stages: {
    stages: [{ index: 10, flags: 2, startUp: true, logEntries: [{ logText: 'Find the courier', flags: 0, note: '' }] }],
  },
  objectives: {
    objectives: [
      { index: 10, flags: 0, displayText: 'Locate the courier at the docks', targets: [{ alias: 0, flags: 0, aliasName: 'Courier' }] },
      { index: 20, flags: 0, displayText: 'Talk to the courier', targets: [] },
    ],
  },
  aliases: { nextAliasId: 1, aliases: [{ kind: 'reference', id: 0, name: 'Courier', flags: 0 }] },
};

export function stubBackend(overrides: Record<string, unknown> = {}) {
  const backend = {
    SearchRecords: vi.fn(async () => JSON.stringify([{ FormKey: QUEST, EditorID: 'CourierQuest', Type: 'QUST', Plugin: 'Patch.esp' }])),
    GetQuestStages: vi.fn(async () => JSON.stringify(SAMPLE.stages)),
    GetQuestObjectives: vi.fn(async () => JSON.stringify(SAMPLE.objectives)),
    GetQuestAliases: vi.fn(async () => JSON.stringify(SAMPLE.aliases)),
    SetQuestStages: vi.fn<(...args: string[]) => Promise<string>>(async () => 'ok'),
    SetQuestObjectives: vi.fn<(...args: string[]) => Promise<string>>(async () => 'ok'),
    SetQuestAliases: vi.fn<(...args: string[]) => Promise<string>>(async () => 'ok'),
    SetQuestFragment: vi.fn<(...args: unknown[]) => Promise<{ script: string; function: string; source: string; pex: string }>>(
      async () => ({ script: 'QF_Courier_0010', function: 'Fragment_Stage_0010_Item_00', source: 'C:/Out/QF_Courier_0010.psc', pex: 'C:/Out/QF_Courier_0010.pex' })),
    GetQuestFragments: vi.fn(async () => '[]'),
    GetQuestScript: vi.fn(async () => { throw new Error('QuestScriptNotFound'); }),
    ...overrides,
  };
  vi.stubGlobal('chrome', { webview: { hostObjects: { backend } } });
  return backend;
}

export async function openQuest() {
  fireEvent.click(screen.getByRole('button', { name: 'Choose quest' }));
  fireEvent.click(await screen.findByText(QUEST));
  await screen.findByText(`Editing ${QUEST}`);
}

export const undoButton = () => screen.getByRole('button', { name: 'Undo' }) as HTMLButtonElement;

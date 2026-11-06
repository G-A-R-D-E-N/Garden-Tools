import { expect, it } from 'vitest';
import type { Backend } from '../../backend';
import { readFragments, readQuestScope, type QuestSnapshot, type ScopeKind } from './questBackend';
import { localFindings, nativeFindings, parseCheckRows, type CheckFinding } from './questChecks';
import type { QuestAlias, QuestObjective, QuestStage } from './questModel';

const PLUGIN = 'Patch.esp';
const QUEST = '000800:Patch.esp';
const BRANCH = '000810:Patch.esp';
const INFO = '000812:Patch.esp';
const SCENE = '000900:Patch.esp';
const OTHER_SCENE = '000901:Patch.esp';
const VENDOR_SCENE = '000902:Other.esp';
const OTHER_QUEST = '000999:Patch.esp';

const stage = (index: number, logText: string, logFlags = 0): QuestStage =>
  ({ index, flags: 0, logEntries: [{ logText, note: '', flags: logFlags }] });
const objective = (index: number, aliasIds: number[]): QuestObjective =>
  ({ index, flags: 0, displayText: `Objective ${index}`, targets: aliasIds.map(id => ({ alias: id, aliasName: '', flags: 0 })) });
const alias = (id: number): QuestAlias => ({ id, kind: 'reference', name: `Alias ${id}`, flags: 0, collectionEntries: [] });
const clean = (changes: Partial<QuestSnapshot> = {}): QuestSnapshot => ({
  stages: [stage(10, 'Find the courier'), stage(20, 'Done', 1)],
  objectives: [objective(10, [0])],
  aliases: [alias(0)],
  jumps: [],
  fragmentScripts: [{ name: 'QF_Courier', parses: true }],
  ...changes,
});
const lines = (findings: CheckFinding[]) => findings.map(finding => `${finding.severity}: ${finding.message}`);

it('Detects: a clean quest gives no findings, so the check does not invent problems for a quest that works', () => {
  expect(localFindings(clean())).toEqual([]);
});

it('Detects: a quest where no stage completes or fails it is warned, and a stage that only fails it is enough', () => {
  expect(lines(localFindings(clean({ stages: [stage(10, 'Find the courier')] })))).toEqual([
    'warning: No stage completes or fails the quest, so the quest can never finish.',
  ]);
  expect(localFindings(clean({ stages: [stage(10, 'Find the courier'), stage(20, 'Gone', 2)] }))).toEqual([]);
  expect(lines(localFindings(clean({ stages: [], objectives: [], jumps: [] })))).not.toContain(
    'warning: No stage completes or fails the quest, so the quest can never finish.');
});

it('Detects: a stage with no journal text and no objective of its number is flagged, and an objective of its number clears it', () => {
  const findings = localFindings(clean({
    stages: [stage(10, 'Find the courier'), stage(20, 'Done', 1), stage(30, ''), stage(40, '')],
    objectives: [objective(10, [0]), objective(40, [0])],
  }));
  expect(lines(findings)).toEqual(['warning: Stage 30 has no journal text and no objective with its number.']);
  expect(findings[0]).toMatchObject({ pane: 'stages', stage: 30 });
});

it('Detects: an objective whose number matches no stage is warned, so the mismatch is not missed', () => {
  expect(lines(localFindings(clean({ objectives: [objective(10, [0]), objective(50, [0])] }))))
    .toEqual(['warning: Objective 50 has no stage with the same number.']);
});

it('Detects: an objective that targets a missing alias is an error, reported once for each missing alias id', () => {
  expect(lines(localFindings(clean({ objectives: [objective(10, [0, 7, 7, 9])] })))).toEqual([
    'error: Objective 10 points at alias 7, which does not exist.',
    'error: Objective 10 points at alias 9, which does not exist.',
  ]);
});

it('Detects: two stages with the same number are reported as an error instead of passing silently', () => {
  expect(lines(localFindings(clean({ stages: [stage(10, 'Find the courier'), stage(10, 'Again', 1)] }))))
    .toEqual(['error: Two or more stages use the number 10.']);
});

it('Detects: a script jump to a stage that does not exist is an error, reported once for each from and to pair', () => {
  expect(lines(localFindings(clean({
    jumps: [
      { from: 10, to: 20, function: 'Fragment_Stage_0010_Item_00' },
      { from: 20, to: 99, function: 'Fragment_Stage_0020_Item_00' },
      { from: 20, to: 99, function: 'Fragment_Stage_0020_Item_01' },
    ],
  })))).toEqual(['error: Stage 20 sets stage 99, which does not exist.']);
});

it('Detects: a fragment script that does not parse is an error, and one that could not be read is info, never an error', () => {
  expect(lines(localFindings(clean({
    fragmentScripts: [{ name: 'QF_Broken', parses: false }, { name: 'QF_Unread', parses: null }, { name: 'QF_Fine', parses: true }],
  })))).toEqual([
    'error: Fragment script QF_Broken has syntax errors, so the game cannot compile it.',
    'info: Fragment script QF_Unread could not be read, so it was not checked.',
  ]);
});

it('Detects: when the fragment scripts cannot be read at all, the check says so instead of passing silently', () => {
  expect(lines(localFindings(clean({ fragmentScripts: null })))).toEqual([
    'info: The fragment scripts could not be read, so script jumps and script errors were not checked.',
  ]);
});

it('Detects: a fragment script the game cannot read reaches the check as unknown, never as a syntax error', async () => {
  const backend = {
    GetQuestFragments: async () => JSON.stringify([
      { stage: 10, function: 'Fragment_Stage_0010_Item_00', fragmentScript: 'QF_Missing' },
      { stage: 20, function: 'Fragment_Stage_0020_Item_00', fragmentScript: 'QF_Broken' },
    ]),
    GetQuestScript: async (name: string) => {
      if (name === 'QF_Missing') throw new Error('QuestScriptNotFound');
      return JSON.stringify({ source: '', parses: false });
    },
  };
  expect(await readFragments(backend as unknown as Backend, PLUGIN, QUEST)).toEqual({
    jumps: [],
    scripts: [{ name: 'QF_Missing', parses: null }, { name: 'QF_Broken', parses: false }],
  });
});

it('Detects: only this quest, its dialogue and its own scenes are kept, so a scene of another quest or plugin shows no finding here', async () => {
  const backend = {
    GetDialogue: async () => JSON.stringify({
      branches: [{ formKey: BRANCH }],
      topics: [{ formKey: '000811:Patch.esp', infos: [{ formKey: INFO }] }],
    }),
    GetReferencedBy: async () => JSON.stringify([
      { Plugin: 'Patch.esp', FormKey: SCENE, Type: 'SCEN' },
      { Plugin: 'Patch.esp', FormKey: OTHER_SCENE, Type: 'SCEN' },
      { Plugin: 'Other.esp', FormKey: VENDOR_SCENE, Type: 'SCEN' },
    ]),
    GetScene: async (_plugin: string, sceneFormKey: string) => JSON.stringify({
      parentQuest: sceneFormKey === OTHER_SCENE ? OTHER_QUEST : QUEST,
    }),
  };
  const rows = parseCheckRows(JSON.stringify([
    { Code: 'unresolved_link', Severity: 'error', FormKey: QUEST, Subrecord: 'ALID', Message: 'quest' },
    { Code: 'dialogue_branch_category_invalid', Severity: 'error', FormKey: BRANCH, Message: 'branch' },
    { Code: 'dialogue_info_count_mismatch', Severity: 'warning', FormKey: INFO, Message: 'info' },
    { Code: 'scene_action_alias_unknown', Severity: 'error', FormKey: SCENE, Message: 'scene' },
    { Code: 'scene_action_alias_unknown', Severity: 'error', FormKey: OTHER_SCENE, Message: 'other quest scene' },
    { Code: 'scene_action_alias_unknown', Severity: 'error', FormKey: VENDOR_SCENE, Message: 'other plugin scene' },
    { Code: 'deleted_target', Severity: 'error', FormKey: '000555:Fallout4.esm', Message: 'vanilla' },
  ]));
  const scope = await readQuestScope(backend as unknown as Backend, PLUGIN, QUEST);
  expect(nativeFindings(rows, scope).map(finding => finding.message)).toEqual(['quest', 'branch', 'info', 'scene']);
});

it('Detects: a quest record problem is filed under the pane its subrecord belongs to, and a known code shows its plain-words message', () => {
  const rows = parseCheckRows(JSON.stringify([
    { Code: 'unresolved_link', Severity: 'error', FormKey: QUEST, Subrecord: 'ALID', Message: 'Alias link is broken' },
    { Code: 'unsupported_vmad_property', Severity: 'warning', FormKey: QUEST, Subrecord: 'VMAD', Message: 'Script property type is not supported' },
  ]));
  expect(nativeFindings(rows, new Map<string, ScopeKind>([[QUEST.toLowerCase(), 'quest']]))
    .map(({ severity, area, pane, message }) => ({ severity, area, pane, message }))).toEqual([
    { severity: 'error', area: 'Aliases', pane: 'aliases', message: 'Alias link is broken' },
    { severity: 'warning', area: 'Fragments', pane: 'fragments', message: 'A script property uses a type this editor cannot check.' },
  ]);
});

it('Detects: a dialogue or scene problem keeps its record id and is not filed under a quest pane, and a row with no message shows its code', () => {
  const rows = parseCheckRows(JSON.stringify([
    { Code: 'dialogue_branch_category_invalid', Severity: 'error', FormKey: BRANCH },
    { Code: 'scene_action_alias_unknown', Severity: 'error', FormKey: SCENE, Subrecord: 'ALST' },
    { Code: 'deleted_target', Severity: 'error', FormKey: QUEST },
  ]));
  const scope = new Map<string, ScopeKind>([
    [BRANCH.toLowerCase(), 'dialogue'],
    [SCENE.toLowerCase(), 'scene'],
    [QUEST.toLowerCase(), 'quest'],
  ]);
  expect(nativeFindings(rows, scope).map(finding => [finding.area, finding.record, finding.message])).toEqual([
    ['Dialogue record', BRANCH, 'dialogue_branch_category_invalid'],
    ['Scene record', SCENE, 'scene_action_alias_unknown'],
    ['Quest record', undefined, 'deleted_target'],
  ]);
});

it('Detects: a truncated record check keeps its warning, because the row has no record to match against this quest', () => {
  const rows = parseCheckRows(JSON.stringify([{ Code: 'results_truncated', Severity: 'warning' }]));
  expect(nativeFindings(rows, new Map<string, ScopeKind>()).map(({ severity, area }) => ({ severity, area })))
    .toEqual([{ severity: 'warning', area: 'Whole check' }]);
});

it('Detects: a record check reply that is not a JSON array of objects is rejected as unreadable, so a broken reply never reads as zero problems', () => {
  for (const raw of ['not json', '{"Code":"unresolved_link"}', '[1]', '[null]', 'null']) {
    expect(() => parseCheckRows(raw)).toThrow('The game record check returned a result the editor could not read.');
  }
});

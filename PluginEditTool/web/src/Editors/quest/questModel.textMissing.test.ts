import { expect, it } from 'vitest';
import { afterTextEdit, objectivesJson, parseObjectives, parseStages, renumberObjective, stagesJson } from './questModel';

it('Detects: a log entry read with logTextId 0 and empty text is saved with logText and logTextId, so native keeps its CNAM', () => {
  const stages = parseStages({ stages: [{ index: 10, flags: 0, logEntries: [{ logText: '', logTextId: 0, flags: 0, conditions: [] }] }] });
  expect(JSON.parse(stagesJson(stages))[0].logEntries[0]).toEqual({ flags: 0, logText: '', logTextId: 0, conditions: [] });
});

it('Detects: a log entry with textMissing is saved with textMissing, an empty logText and its logTextId, so native keeps its existing text ID', () => {
  const stages = parseStages({ stages: [{ index: 10, flags: 0, logEntries: [{ logText: null, logTextId: 999, flags: 0, textMissing: true, conditions: [] }] }] });
  expect(JSON.parse(stagesJson(stages))[0].logEntries[0]).toEqual({ flags: 0, logText: '', logTextId: 999, textMissing: true, conditions: [] });
});

it('Detects: a log entry with no text ID and empty text sends no logText, so no CNAM is written', () => {
  const stages = parseStages({ stages: [{ index: 10, flags: 0, logEntries: [{ flags: 0, conditions: [] }] }] });
  expect(JSON.parse(stagesJson(stages))[0].logEntries[0]).toEqual({ flags: 0, conditions: [] });
});

it('Detects: an objective read with null text and textMissing parses to empty text and sends textMissing, so native keeps its text ID', () => {
  const objectives = parseObjectives({ objectives: [{ index: 10, flags: 0, displayText: null, textMissing: true, targets: [] }] });
  expect(objectives[0].displayText).toBe('');
  expect(JSON.parse(objectivesJson(objectives))[0]).toEqual({ index: 10, flags: 0, displayText: '', targets: [], textMissing: true });
});

it('Detects: an objective read with displayTextId 0 and empty text parses without textMissing and sends no textMissing', () => {
  const objectives = parseObjectives({ objectives: [{ index: 10, flags: 0, displayText: '', displayTextId: 0, targets: [] }] });
  expect(objectives[0]).toEqual({ index: 10, flags: 0, displayText: '', targets: [] });
  expect(JSON.parse(objectivesJson(objectives))[0]).toEqual({ index: 10, flags: 0, displayText: '', targets: [] });
});

it('Detects: typing text clears textMissing, but clearing the text to empty keeps it so the existing text ID is still kept', () => {
  expect(afterTextEdit({ logText: '', flags: 0, textMissing: true }, 'Typed').textMissing).toBeUndefined();
  expect(afterTextEdit({ logText: '', flags: 0, textMissing: true }, '').textMissing).toBe(true);
});

it('Detects: renumbering an objective clears textMissing because the existing text ID belongs to the old number, and keeps it when the number is unchanged', () => {
  const objective = { index: 10, flags: 0, displayText: '', targets: [], textMissing: true };
  expect(renumberObjective(objective, 11)).toEqual({ index: 11, flags: 0, displayText: '', targets: [] });
  expect(renumberObjective(objective, 11).textMissing).toBeUndefined();
  expect(renumberObjective(objective, 10)).toBe(objective);
});

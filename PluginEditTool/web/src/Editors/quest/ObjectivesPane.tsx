import { useState } from 'react';
import HelpTip from '../help/HelpTip';
import { CommitInput, FlagChecks } from './questControls';
import {
  OBJECTIVE_FLAG_OPTIONS, duplicateObjectiveWarning,
  type QuestAlias, type QuestObjective,
} from './questModel';

interface Props {
  objectives: QuestObjective[];
  aliases: QuestAlias[];
  onCommit: (next: QuestObjective[], label: string) => void;
}

const aliasLabel = (alias: QuestAlias) => (alias.name ? `${alias.name} (alias ${alias.id})` : `Alias ${alias.id}`);

export default function ObjectivesPane({ objectives, aliases, onCommit }: Props) {
  const [refusal, setRefusal] = useState('');
  const warning = refusal || duplicateObjectiveWarning(objectives);

  const tryCommit = (next: QuestObjective[], label: string) => {
    const problem = duplicateObjectiveWarning(next);
    setRefusal(problem);
    if (problem === '') onCommit(next, label);
  };

  const replace = (position: number, next: QuestObjective, label: string) =>
    tryCommit(objectives.map((objective, i) => (i === position ? next : objective)), label);

  const setNumber = (position: number, objective: QuestObjective, text: string) => {
    const index = text.trim() === '' ? NaN : Number(text);
    if (!Number.isInteger(index) || index < 0 || index > 65535) {
      setRefusal('An objective number must be a whole number from 0 to 65535.');
      return;
    }
    replace(position, { ...objective, index }, `Change objective ${objective.index} number`);
  };

  const addObjective = () => {
    const index = objectives.reduce((next, objective) => Math.max(next, objective.index + 1), 0);
    tryCommit([...objectives, { index, flags: 0, displayText: '', targets: [] }], `Add objective ${index}`);
  };

  return (
    <section className="quest-pane" aria-labelledby="quest-objectives-title">
      <header className="quest-pane-header">
        <h4 id="quest-objectives-title" tabIndex={-1}>Objectives</h4>
        <HelpTip topicId="objective" />
        <button type="button" className="sidebar-action-btn" onClick={addObjective}>Add objective</button>
      </header>
      {warning && <p className="quest-warning" role="alert">{warning}</p>}
      {objectives.length === 0 && (
        <div className="editor-empty">This quest has no objectives yet. Add one with Add objective to give the player a goal.</div>
      )}
      {objectives.map((objective, position) => (
        <div key={position} className="quest-card">
          <div className="quest-row">
            <span>Number</span>
            <HelpTip topicId="quest-objective-index" />
            <CommitInput type="number" min={0} max={65535} aria-label={`Objective ${objective.index} number`}
              value={String(objective.index)} onCommit={text => setNumber(position, objective, text)} />
            <button type="button" className="sidebar-action-btn" onClick={() => tryCommit(
              objectives.filter((_, i) => i !== position), `Remove objective ${objective.index}`)}>
              Remove objective {objective.index}
            </button>
          </div>
          <div className="quest-row">
            <span>Text the player sees</span>
            <HelpTip topicId="quest-objective-text" />
          </div>
          <CommitInput type="text" aria-label={`Objective ${objective.index} text`} value={objective.displayText}
            onCommit={displayText => replace(position, { ...objective, displayText },
              `Change objective ${objective.index} text`)} />
          <div className="quest-row">
            <FlagChecks flags={objective.flags} options={OBJECTIVE_FLAG_OPTIONS}
              label={`Objective ${objective.index} flags`}
              onChange={flags => replace(position, { ...objective, flags }, `Change objective ${objective.index} flags`)} />
            <HelpTip topicId="quest-objective-flags" />
          </div>
          <div className="quest-row">
            <span>Targets</span>
            <HelpTip topicId="quest-objective-targets" />
          </div>
          {objective.targets.map((target, at) => (
            <div key={at} className="quest-row">
              <select aria-label={`Objective ${objective.index} target ${at + 1} alias`} value={target.alias}
                onChange={e => replace(position, {
                  ...objective,
                  targets: objective.targets.map((current, i) => (i === at ? { ...current, alias: Number(e.target.value) } : current)),
                }, `Change objective ${objective.index} target`)}>
                {aliases.map(alias => <option key={alias.id} value={alias.id}>{aliasLabel(alias)}</option>)}
                {!aliases.some(alias => alias.id === target.alias) && (
                  <option value={target.alias}>{target.aliasName || `Alias ${target.alias}`}</option>
                )}
              </select>
              <button type="button" className="sidebar-action-btn" onClick={() => replace(position, {
                ...objective, targets: objective.targets.filter((_, i) => i !== at),
              }, `Remove target from objective ${objective.index}`)}>Remove target</button>
            </div>
          ))}
          {aliases.length === 0 ? (
            <p className="quest-note">Add an alias on the Aliases pane first, then choose it as a target.</p>
          ) : (
            <button type="button" className="sidebar-action-btn" onClick={() => replace(position, {
              ...objective,
              targets: [...objective.targets, { alias: aliases[0].id, aliasName: aliases[0].name, flags: 0 }],
            }, `Add target to objective ${objective.index}`)}>Add target</button>
          )}
        </div>
      ))}
    </section>
  );
}

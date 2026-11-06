import { useState } from 'react';
import ConditionsEditor from '../../ConditionsEditor';
import HelpTip from '../help/HelpTip';
import { questPlugin } from './questBackend';
import { CommitInput, FlagChecks } from './questControls';
import { LOG_ENTRY_FLAG_OPTIONS, STAGE_FLAG_OPTIONS, type QuestStage, type StageLogEntry } from './questModel';

interface Props {
  formKey: string;
  stages: QuestStage[];
  onCommit: (next: QuestStage[], label: string) => void;
}

const withEntry = (stage: QuestStage, entryAt: number, entry: StageLogEntry): QuestStage =>
  ({ ...stage, logEntries: stage.logEntries.map((current, i) => (i === entryAt ? entry : current)) });

export default function StagesPane({ formKey, stages, onCommit }: Props) {
  const [conditionsAt, setConditionsAt] = useState<{ stage: number; entry: number } | null>(null);
  const [notice, setNotice] = useState('');

  const replaceStage = (position: number, next: QuestStage, label: string) =>
    onCommit(stages.map((stage, i) => (i === position ? next : stage)), label);

  const addStage = () => {
    const index = stages.reduce((next, stage) => Math.max(next, stage.index + 1), 0);
    onCommit([...stages, { index, flags: 0, logEntries: [] }].sort((a, b) => a.index - b.index), `Add stage ${index}`);
  };

  const removeStage = (position: number) =>
    onCommit(stages.filter((_, i) => i !== position), `Remove stage ${stages[position].index}`);

  return (
    <section className="quest-pane" aria-labelledby="quest-stages-title">
      <header className="quest-pane-header">
        <h4 id="quest-stages-title">Stages</h4>
        <HelpTip topicId="quest-stage" />
        <button type="button" className="sidebar-action-btn" onClick={addStage}>Add stage</button>
      </header>
      {stages.length === 0 && (
        <div className="editor-empty">This quest has no stages yet. Add one with Add stage to create its first checkpoint.</div>
      )}
      {stages.map((stage, position) => (
        <div key={stage.index} className="quest-card">
          <div className="quest-row">
            <strong>Stage {stage.index}</strong>
            <HelpTip topicId="quest-stage-flags" />
            <button type="button" className="sidebar-action-btn" onClick={() => removeStage(position)}>
              Remove stage {stage.index}
            </button>
          </div>
          <FlagChecks flags={stage.flags} options={STAGE_FLAG_OPTIONS} label={`Stage ${stage.index} flags`}
            onChange={flags => replaceStage(position, { ...stage, flags }, `Change stage ${stage.index} flags`)} />
          {stage.logEntries.length === 0 && (
            <p className="quest-note">This stage has no log entry. The log entry is the journal line the player reads.</p>
          )}
          {stage.logEntries.map((entry, entryAt) => (
            <div key={entryAt} className="quest-entry">
              <div className="quest-row">
                <span>Log entry {entryAt + 1}</span>
                <HelpTip topicId="quest-stage-log" />
              </div>
              <CommitInput type="text" aria-label={`Stage ${stage.index} log entry ${entryAt + 1} text`}
                value={entry.logText} placeholder="Journal text the player reads"
                onCommit={logText => replaceStage(position, withEntry(stage, entryAt, { ...entry, logText }),
                  `Change stage ${stage.index} log text`)} />
              <CommitInput type="text" aria-label={`Stage ${stage.index} log entry ${entryAt + 1} note`}
                value={entry.note} placeholder="Note for the Creation Kit (optional)"
                onCommit={note => replaceStage(position, withEntry(stage, entryAt, { ...entry, note }),
                  `Change stage ${stage.index} note`)} />
              <div className="quest-row">
                <FlagChecks flags={entry.flags} options={LOG_ENTRY_FLAG_OPTIONS}
                  label={`Stage ${stage.index} log entry ${entryAt + 1} outcome`}
                  onChange={flags => replaceStage(position, withEntry(stage, entryAt, { ...entry, flags }),
                    `Change stage ${stage.index} outcome`)} />
                <HelpTip topicId="quest-stage-entry-flags" />
              </div>
              <div className="quest-row">
                <button type="button" className="sidebar-action-btn"
                  aria-label={`Conditions for stage ${stage.index} log entry ${entryAt + 1}`}
                  onClick={() => setConditionsAt({ stage: position, entry: entryAt })}>Conditions</button>
                <HelpTip topicId="quest-stage-conditions" />
                <button type="button" className="sidebar-action-btn" onClick={() => replaceStage(position,
                  { ...stage, logEntries: stage.logEntries.filter((_, i) => i !== entryAt) },
                  `Remove log entry from stage ${stage.index}`)}>Remove entry</button>
              </div>
            </div>
          ))}
          <button type="button" className="sidebar-action-btn"
            onClick={() => replaceStage(position, { ...stage, logEntries: [...stage.logEntries, { logText: '', note: '', flags: 0 }] },
              `Add log entry to stage ${stage.index}`)}>
            Add log entry
          </button>
        </div>
      ))}
      {notice && <p className="quest-note" role="status">{notice}</p>}
      {conditionsAt && (
        <ConditionsEditor plugin={questPlugin(formKey)} record={formKey}
          path={`Stages[${conditionsAt.stage}].LogEntries[${conditionsAt.entry}].Conditions`}
          label={`Log entry ${conditionsAt.entry + 1} conditions`}
          onClose={() => setConditionsAt(null)} onSaved={msg => { setNotice(msg); setConditionsAt(null); }} />
      )}
    </section>
  );
}

import { useState } from 'react';
import { removeStage, setOutcome, setStageText } from './flowChartEdits';
import { hasFlag, journalEntryIndex } from './flowChartModel';
import { useReadOnly } from '../readOnlyContext';
import { CommitTextarea } from './questControls';
import { LOG_ENTRY_FLAG_OPTIONS, type QuestStage, type StageEdit } from './questModel';

interface Props {
  stage: QuestStage;
  hasObjective: boolean;
  onEdit: (edit: StageEdit, label: string) => void;
  onSelect: (index: number | null) => void;
  onEditInStages: () => void;
}

export default function FlowChartPanel({ stage, hasObjective, onEdit, onSelect, onEditInStages }: Props) {
  const [confirming, setConfirming] = useState(false);
  const readOnly = useReadOnly();
  const { index } = stage;
  const journal = stage.logEntries[journalEntryIndex(stage)]?.logText ?? '';
  return (
    <aside className="quest-flow-panel" aria-label={`Stage ${index} details`}>
      <div className="quest-row">
        <strong>Stage {index}</strong>
        <button type="button" className="sidebar-action-btn" onClick={onEditInStages}>Edit in Stages</button>
        <button type="button" className="sidebar-action-btn" onClick={() => onSelect(null)}>Close</button>
      </div>
      <CommitTextarea aria-label={`Stage ${index} journal text`} placeholder="Journal text the player reads"
        value={journal} disabled={readOnly}
        onCommit={text => onEdit(current => setStageText(current, index, text), `Change stage ${index} journal text`)} />
      <div className="quest-flags" role="group" aria-label={`Stage ${index} outcome`}>
        {LOG_ENTRY_FLAG_OPTIONS.map(option => (
          <label key={option.bit} className="quest-check">
            <input type="checkbox" checked={hasFlag(stage, option.bit)} disabled={readOnly}
              onChange={e => onEdit(current => setOutcome(current, index, option.bit, e.target.checked),
                `Change stage ${index} outcome`)} />
            {option.label}
          </label>
        ))}
      </div>
      {confirming ? (
        <div className="quest-flow-confirm">
          <p className="quest-note">
            Remove stage {index}? {hasObjective
              ? `Objective ${index} is NOT removed. Remove it in the Objectives pane if you do not need it.`
              : 'No objective has this number, so nothing else changes.'}
          </p>
          <div className="quest-row">
            <button type="button" className="sidebar-action-btn" onClick={() => {
              onEdit(current => removeStage(current, index), `Remove stage ${index}`);
              onSelect(null);
            }}>Yes, remove stage {index}</button>
            <button type="button" autoFocus className="sidebar-action-btn" onClick={() => setConfirming(false)}>
              Keep stage {index}
            </button>
          </div>
        </div>
      ) : (
        <button type="button" className="sidebar-action-btn" disabled={readOnly} onClick={() => setConfirming(true)}>Remove step</button>
      )}
    </aside>
  );
}

import { useState } from 'react';
import { removeStage, setOutcome, setStageText } from './flowChartEdits';
import { CommitTextarea } from './questControls';
import { LOG_ENTRY_FLAG_OPTIONS, type QuestStage } from './questModel';

interface Props {
  stage: QuestStage;
  stages: QuestStage[];
  hasObjective: boolean;
  onCommit: (next: QuestStage[], label: string) => void;
  onSelect: (index: number | null) => void;
}

export default function FlowChartPanel({ stage, stages, hasObjective, onCommit, onSelect }: Props) {
  const [confirming, setConfirming] = useState(false);
  const { index } = stage;
  const first = stage.logEntries[0];
  const flags = first?.flags ?? 0;
  return (
    <aside className="quest-flow-panel" aria-label={`Stage ${index} details`}>
      <strong>Stage {index}</strong>
      <CommitTextarea aria-label={`Stage ${index} journal text`} placeholder="Journal text the player reads"
        value={first?.logText ?? ''}
        onCommit={text => onCommit(setStageText(stages, index, text), `Change stage ${index} journal text`)} />
      <div className="quest-flags" role="group" aria-label={`Stage ${index} outcome`}>
        {LOG_ENTRY_FLAG_OPTIONS.map(option => (
          <label key={option.bit} className="quest-check">
            <input type="checkbox" checked={(flags & option.bit) !== 0}
              onChange={e => onCommit(setOutcome(stages, index, option.bit, e.target.checked), `Change stage ${index} outcome`)} />
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
              onCommit(removeStage(stages, index), `Remove stage ${index}`);
              onSelect(null);
            }}>Yes, remove stage {index}</button>
            <button type="button" className="sidebar-action-btn" onClick={() => setConfirming(false)}>Keep stage {index}</button>
          </div>
        </div>
      ) : (
        <button type="button" className="sidebar-action-btn" onClick={() => setConfirming(true)}>Remove step</button>
      )}
    </aside>
  );
}

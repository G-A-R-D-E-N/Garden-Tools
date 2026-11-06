import { useState } from 'react';
import { getBackend } from '../../backend';
import HelpTip from '../help/HelpTip';
import { useUndoStack } from '../useUndoStack';
import AliasesPane from './AliasesPane';
import CheckPanel from './CheckPanel';
import FlowChart from './FlowChart';
import FragmentsPane from './FragmentsPane';
import ObjectivesPane from './ObjectivesPane';
import { ErrorNote } from './questControls';
import { PANE_HEADINGS, type CheckFinding } from './questChecks';
import type { StageSelection } from './questModel';
import StagesPane from './StagesPane';
import { useQuestCheck } from './useQuestCheck';
import { useQuestRecord } from './useQuestRecord';
import './Quest.css';

export default function QuestWorkspace({ formKey, plugin }: { formKey: string; plugin: string }) {
  const backend = getBackend();
  const { onKeyDown, scopeRef, push, undo, redo, canUndo, canRedo } = useUndoStack();
  const { snapshot, error, commitStages, editStages, commitObjectives, commitAliases, refreshFragments } = useQuestRecord(plugin, formKey, push);
  const check = useQuestCheck(plugin, formKey, snapshot);
  const [selectedStage, setSelectedStage] = useState<StageSelection | null>(null);
  const pickStage = (index: number | null) => setSelectedStage(index === null ? null : { index });
  const checkReason = !backend ? 'No backend bridge.' : !snapshot ? (error ? 'The quest did not load.' : 'Wait for the quest to load.') : '';
  const openFinding = (finding: CheckFinding) => {
    if (finding.pane === 'stages' && finding.stage !== undefined) pickStage(finding.stage);
    else if (finding.pane) document.getElementById(PANE_HEADINGS[finding.pane])?.focus();
  };
  return (
    <section ref={scopeRef} tabIndex={-1} className="quest-workspace" onKeyDown={onKeyDown}>
      <div className="quest-toolbar">
        <span className="quest-current">Editing {formKey}</span>
        <button type="button" className="sidebar-action-btn" disabled={!canUndo} onClick={undo}>Undo</button>
        <button type="button" className="sidebar-action-btn" disabled={!canRedo} onClick={redo}>Redo</button>
        <button type="button" className="sidebar-action-btn" disabled={checkReason !== '' || check.running} onClick={check.run}
          aria-describedby={checkReason ? 'quest-check-reason' : undefined}>Check this quest</button>
        <HelpTip topicId="quest-check" />
        {checkReason && <span id="quest-check-reason" className="quest-check-reason">{checkReason}</span>}
      </div>
      {error && <ErrorNote error={error} />}
      {snapshot === null && !error && <p className="quest-note">Loading the quest...</p>}
      {snapshot && (
        <>
          <FlowChart stages={snapshot.stages} objectives={snapshot.objectives} jumps={snapshot.jumps}
            selected={selectedStage} onSelect={pickStage} onEdit={editStages} />
          <CheckPanel result={check.result} running={check.running} error={check.error} outOfDate={check.outOfDate} onOpen={openFinding} />
          <div className="quest-panes">
            <StagesPane stages={snapshot.stages} selected={selectedStage} onCommit={commitStages} />
            <ObjectivesPane objectives={snapshot.objectives} aliases={snapshot.aliases} onCommit={commitObjectives} />
            <AliasesPane aliases={snapshot.aliases} onCommit={commitAliases} />
            <FragmentsPane plugin={plugin} formKey={formKey} push={push} onWritten={refreshFragments} />
          </div>
        </>
      )}
    </section>
  );
}

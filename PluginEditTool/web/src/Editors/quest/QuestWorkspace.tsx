import { useState } from 'react';
import { useUndoStack } from '../useUndoStack';
import AliasesPane from './AliasesPane';
import FlowChart from './FlowChart';
import FragmentsPane from './FragmentsPane';
import ObjectivesPane from './ObjectivesPane';
import { ErrorNote } from './questControls';
import type { StageSelection } from './questModel';
import StagesPane from './StagesPane';
import { useQuestRecord } from './useQuestRecord';
import './Quest.css';

export default function QuestWorkspace({ formKey, plugin }: { formKey: string; plugin: string }) {
  const { onKeyDown, scopeRef, push, undo, redo, canUndo, canRedo } = useUndoStack();
  const { snapshot, error, commitStages, editStages, commitObjectives, commitAliases } = useQuestRecord(plugin, formKey, push);
  const [selectedStage, setSelectedStage] = useState<StageSelection | null>(null);
  const pickStage = (index: number | null) => setSelectedStage(index === null ? null : { index });
  return (
    <section ref={scopeRef} tabIndex={-1} className="quest-workspace" onKeyDown={onKeyDown}>
      <div className="quest-toolbar">
        <span className="quest-current">Editing {formKey}</span>
        <button type="button" className="sidebar-action-btn" disabled={!canUndo} onClick={undo}>Undo</button>
        <button type="button" className="sidebar-action-btn" disabled={!canRedo} onClick={redo}>Redo</button>
      </div>
      {error && <ErrorNote error={error} />}
      {snapshot === null && !error && <p className="quest-note">Loading the quest...</p>}
      {snapshot && (
        <>
          <FlowChart stages={snapshot.stages} objectives={snapshot.objectives} selected={selectedStage}
            onSelect={pickStage} onEdit={editStages} />
          <div className="quest-panes">
            <StagesPane stages={snapshot.stages} selected={selectedStage} onCommit={commitStages} />
            <ObjectivesPane objectives={snapshot.objectives} aliases={snapshot.aliases} onCommit={commitObjectives} />
            <AliasesPane aliases={snapshot.aliases} onCommit={commitAliases} />
            <FragmentsPane plugin={plugin} formKey={formKey} push={push} />
          </div>
        </>
      )}
    </section>
  );
}

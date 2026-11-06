import { useUndoStack } from '../useUndoStack';
import AliasesPane from './AliasesPane';
import FragmentsPane from './FragmentsPane';
import ObjectivesPane from './ObjectivesPane';
import { ErrorNote } from './questControls';
import StagesPane from './StagesPane';
import { useQuestRecord } from './useQuestRecord';
import './Quest.css';

export default function QuestWorkspace({ formKey }: { formKey: string }) {
  const { onKeyDown, scopeRef, push, undo, redo, canUndo, canRedo } = useUndoStack();
  const { snapshot, error, commitStages, commitObjectives, commitAliases } = useQuestRecord(formKey, push);
  return (
    <div ref={scopeRef} tabIndex={-1} className="quest-workspace" onKeyDown={onKeyDown}>
      <div className="quest-toolbar">
        <span className="quest-current">Editing {formKey}</span>
        <button type="button" className="sidebar-action-btn" disabled={!canUndo} onClick={undo}>Undo</button>
        <button type="button" className="sidebar-action-btn" disabled={!canRedo} onClick={redo}>Redo</button>
      </div>
      {error && <ErrorNote error={error} />}
      {snapshot === null && !error && <p className="quest-note">Loading the quest...</p>}
      {snapshot && (
        <div className="quest-panes">
          <StagesPane formKey={formKey} stages={snapshot.stages} onCommit={commitStages} />
          <ObjectivesPane objectives={snapshot.objectives} aliases={snapshot.aliases} onCommit={commitObjectives} />
          <AliasesPane aliases={snapshot.aliases} onCommit={commitAliases} />
          <FragmentsPane formKey={formKey} push={push} />
        </div>
      )}
    </div>
  );
}

import { useState } from 'react';
import { diffDraft, repointPhaseEdits, type Run, type SceneChanges } from './sceneChanges';
import {
  PHASE_FLAGS, actionsUsingPhase, phaseName, type SceneDto, type ScenePhase, type Spec,
} from './sceneModel';
import { ConditionsSummary, Field, FlagBoxes, RemovePanel, toNumber } from './sceneFormParts';

interface Props {
  scene: SceneDto;
  phase: ScenePhase;
  changes: SceneChanges;
  run: Run;
  onOpenConditions: (path: string, label: string) => void;
}

export default function PhaseForm({ scene, phase, changes, run, onOpenConditions }: Props) {
  const saved: Spec = {
    name: phase.name ?? '',
    editorWidth: phase.editorWidth ?? 0,
    flags: phase.flags ?? 0,
    questStageOnStart: phase.questStageOnStart ?? 0,
    questStageOnCompletion: phase.questStageOnCompletion ?? 0,
  };
  const [draft, setDraft] = useState<Spec>(saved);
  const setField = (key: string, value: unknown) => setDraft(current => ({ ...current, [key]: value }));
  const { next, prev } = diffDraft(draft, saved);
  const unreadable = !!phase.startConditionsUnreadable || !!phase.completionConditionsUnreadable;
  const blockedBy = phase.startFragment || phase.completionFragment
    ? 'A script fragment is attached to this phase. Remove the fragment first, then remove the phase.'
    : unreadable ? 'Its conditions could not be read, so removing it would lose them on undo.' : null;

  return (
    <section className="scene-form" aria-label={`Phase ${phase.index + 1}`}>
      <h4>{phaseName(phase)}</h4>
      <div className="scene-form-grid">
        <Field label="Name" topic="scene-phase-name">
          {id => <input id={id} type="text" value={String(draft.name)} onChange={e => setField('name', e.target.value)} />}
        </Field>
        <Field label="Editor width" topic="scene-editor-width">
          {id => (
            <input id={id} type="number" value={String(draft.editorWidth)}
              onChange={e => setField('editorWidth', toNumber(e.target.value))} />
          )}
        </Field>
        <Field label="Quest stage on start" topic="scene-phase-stage">
          {id => (
            <input id={id} type="number" value={String(draft.questStageOnStart)}
              onChange={e => setField('questStageOnStart', toNumber(e.target.value))} />
          )}
        </Field>
        <Field label="Quest stage on completion" topic="scene-phase-stage">
          {id => (
            <input id={id} type="number" value={String(draft.questStageOnCompletion)}
              onChange={e => setField('questStageOnCompletion', toNumber(e.target.value))} />
          )}
        </Field>
      </div>
      <FlagBoxes label="Phase options" topic="scene-phase-flags" names={PHASE_FLAGS} value={Number(draft.flags)}
        onChange={value => setField('flags', value)} />
      <div className="scene-form-actions">
        <button type="button" className="sidebar-action-btn" disabled={Object.keys(next).length === 0}
          onClick={() => run(changes.editPhase(phase.index, next, prev))}>Apply phase changes</button>
        <button type="button" className="sidebar-action-btn" disabled={phase.index <= 0}
          onClick={() => run(changes.movePhase(phase.index, phase.index - 1), { kind: 'phase', index: phase.index - 1 })}>
          Move earlier
        </button>
        <button type="button" className="sidebar-action-btn" disabled={phase.index >= scene.phases.length - 1}
          onClick={() => run(changes.movePhase(phase.index, phase.index + 1), { kind: 'phase', index: phase.index + 1 })}>
          Move later
        </button>
      </div>
      <ConditionsSummary label="Start conditions" count={phase.startConditions?.length ?? 0}
        unreadable={!!phase.startConditionsUnreadable}
        onEdit={() => onOpenConditions(`Phases[${phase.index}].Start Conditions`, 'Start conditions')} />
      <ConditionsSummary label="Completion conditions" count={phase.completionConditions?.length ?? 0}
        unreadable={!!phase.completionConditionsUnreadable}
        onEdit={() => onOpenConditions(`Phases[${phase.index}].Completion Conditions`, 'Completion conditions')} />
      <RemovePanel noun="phase" usage={actionsUsingPhase(scene, phase.index).length}
        targets={scene.phases.filter(other => other.index !== phase.index)
          .map(other => ({ value: other.index, label: phaseName(other) }))}
        blockedBy={blockedBy}
        onRepoint={to => run(changes.editActions('Re-point actions', repointPhaseEdits(scene.actions, phase.index, to)))}
        onRemove={() => run(changes.removePhase(phase), null)} />
    </section>
  );
}

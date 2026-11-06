import { useState } from 'react';
import { diffDraft, type Run, type SceneChanges } from './sceneChanges';
import { SCENE_FLAGS, type SceneDto, type Spec } from './sceneModel';
import { ConditionsSummary, Field, FlagBoxes } from './sceneFormParts';

interface Props {
  scene: SceneDto;
  changes: SceneChanges;
  run: Run;
  onOpenConditions: (path: string, label: string) => void;
}

export default function SceneSettingsForm({ scene, changes, run, onOpenConditions }: Props) {
  const saved: Spec = { flags: scene.flags ?? 0 };
  const [draft, setDraft] = useState<Spec>(saved);
  const { next, prev } = diffDraft(draft, saved);

  return (
    <section className="scene-form" aria-label="Scene settings">
      <h4>Scene settings</h4>
      <p className="scene-hint">Click a phase, actor or block in the timeline to edit it.</p>
      <div className="scene-form-grid">
        <Field label="Parent quest" topic="scene-parent-quest">
          {() => <span className="scene-readonly">{scene.parentQuest || 'None'}</span>}
        </Field>
      </div>
      <FlagBoxes label="Scene options" topic="scene-scene-flags" names={SCENE_FLAGS} value={Number(draft.flags)}
        onChange={value => setDraft({ flags: value })} />
      <div className="scene-form-actions">
        <button type="button" className="sidebar-action-btn" disabled={Object.keys(next).length === 0}
          onClick={() => run(changes.editScene(next, prev))}>Apply scene changes</button>
      </div>
      <ConditionsSummary label="Scene conditions" count={scene.conditions?.length ?? 0}
        unreadable={!!scene.conditionsUnreadable}
        onEdit={() => onOpenConditions('Conditions', 'Scene conditions')} />
    </section>
  );
}

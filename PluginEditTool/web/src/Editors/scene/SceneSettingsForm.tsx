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

const bits = (...list: number[]) => SCENE_FLAGS.filter(flag => list.includes(flag.bit));

export default function SceneSettingsForm({ scene, changes, run, onOpenConditions }: Props) {
  const saved: Spec = { flags: scene.flags ?? 0 };
  const [draft, setDraft] = useState<Spec>(saved);
  const { next, prev } = diffDraft(draft, saved);
  const setFlags = (value: number) => setDraft({ flags: value });

  return (
    <section className="scene-form scene-settings" aria-label="Scene settings">
      <h4>Scene settings</h4>
      <p className="scene-hint">These options apply to the whole scene. Bits this editor does not name are kept when you apply.</p>
      <div className="scene-settings-grid">
        <section className="scene-group" aria-labelledby="scene-settings-quest">
          <h5 id="scene-settings-quest">Quest</h5>
          <Field label="Parent quest" topic="scene-parent-quest">
            {() => <span className="scene-readonly">{scene.parentQuest || 'None'}</span>}
          </Field>
          <FlagBoxes label="Start and stop with the quest" topic="scene-scene-flags" names={bits(0, 1)}
            value={Number(draft.flags)} onChange={setFlags} />
        </section>
        <section className="scene-group" aria-labelledby="scene-settings-timing">
          <h5 id="scene-settings-timing">Timing</h5>
          <FlagBoxes label="Repeat" topic="scene-scene-flags" names={bits(3)}
            value={Number(draft.flags)} onChange={setFlags} />
          <ConditionsSummary label="Scene conditions" count={scene.conditions?.length ?? 0}
            unreadable={!!scene.conditionsUnreadable}
            onEdit={() => onOpenConditions('Conditions', 'Scene conditions')} />
        </section>
        <section className="scene-group" aria-labelledby="scene-settings-behaviour">
          <h5 id="scene-settings-behaviour">Behaviour</h5>
          <FlagBoxes label="Actors and camera" topic="scene-scene-flags" names={bits(4, 6, 9, 11, 12)}
            value={Number(draft.flags)} onChange={setFlags} />
        </section>
        <section className="scene-group" aria-labelledby="scene-settings-flags">
          <h5 id="scene-settings-flags">Flags</h5>
          <FlagBoxes label="Dialogue display" topic="scene-scene-flags" names={bits(2, 5)}
            value={Number(draft.flags)} onChange={setFlags} />
        </section>
      </div>
      <div className="scene-form-actions">
        <button type="button" className="sidebar-action-btn" disabled={Object.keys(next).length === 0}
          onClick={() => run(changes.editScene(next, prev))}>Apply scene changes</button>
      </div>
    </section>
  );
}

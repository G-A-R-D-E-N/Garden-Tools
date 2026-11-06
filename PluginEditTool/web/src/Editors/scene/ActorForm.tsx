import { useState } from 'react';
import { diffDraft, repointActorEdits, type Run, type SceneChanges } from './sceneChanges';
import {
  ACTOR_BEHAVIOR_FLAGS, ACTOR_FLAGS, actionsUsingActor, actorName, aliasLabel, toNumber, type SceneActor, type SceneDto, type Spec,
} from './sceneModel';
import { Field, FlagBoxes, RemovePanel } from './sceneFormParts';

interface Props {
  scene: SceneDto;
  actor: SceneActor;
  changes: SceneChanges;
  run: Run;
}

export default function ActorForm({ scene, actor, changes, run }: Props) {
  const saved: Spec = { alias: actor.alias, flags: actor.flags, behaviorFlags: actor.behaviorFlags };
  const [draft, setDraft] = useState<Spec>(saved);
  const setField = (key: string, value: unknown) => setDraft(current => ({ ...current, [key]: value }));
  const { next, prev } = diffDraft(draft, saved);
  const aliases = [...new Set([...scene.actors.map(entry => entry.alias), -1])].filter(alias => alias !== actor.alias);

  return (
    <section className="scene-form" aria-label={actorName(actor)}>
      <h4>{actorName(actor)}</h4>
      <div className="scene-form-grid">
        <Field label="Actor alias" topic="scene-actor-alias">
          {id => (
            <input id={id} type="number" value={String(draft.alias)} onChange={e => setField('alias', toNumber(e.target.value))} />
          )}
        </Field>
      </div>
      <FlagBoxes label="Actor options" topic="scene-actor-flags" names={ACTOR_FLAGS} value={Number(draft.flags)}
        onChange={value => setField('flags', value)} />
      <FlagBoxes label="Behaviour pauses" topic="scene-behaviour-flags" names={ACTOR_BEHAVIOR_FLAGS}
        value={Number(draft.behaviorFlags)} onChange={value => setField('behaviorFlags', value)} />
      <div className="scene-form-actions">
        <button type="button" className="sidebar-action-btn" disabled={Object.keys(next).length === 0}
          onClick={() => run(changes.editActor(actor.index, next, prev))}>Apply actor changes</button>
      </div>
      <RemovePanel noun="actor" usage={actionsUsingActor(scene, actor.alias).length}
        targets={aliases.map(alias => ({ value: alias, label: aliasLabel(alias) }))}
        blockedBy={null}
        onRepoint={to => run(changes.editActions('Re-point actions', repointActorEdits(scene.actions, actor.alias, to)))}
        onRemove={() => run(changes.removeActor(actor), null)} />
    </section>
  );
}

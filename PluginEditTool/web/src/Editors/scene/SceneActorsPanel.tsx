import ReadOnlyFieldset from '../ReadOnlyFieldset';
import ActorForm from './ActorForm';
import type { Run, SceneChanges } from './sceneChanges';
import {
  ACTOR_BEHAVIOR_FLAGS, ACTOR_FLAGS, actionsUsingActor, plural, type SceneDto,
} from './sceneModel';
import { aliasTitle, enabledCount } from './sceneTabModel';

interface Props {
  scene: SceneDto;
  selected: number | null;
  revision: number;
  names: ReadonlyMap<number, string>;
  changes: SceneChanges;
  run: Run;
  onSelect: (index: number) => void;
}

export default function SceneActorsPanel({ scene, selected, revision, names, changes, run, onSelect }: Props) {
  const actor = scene.actors.find(entry => entry.index === selected);
  return (
    <section className="scene-split" aria-label="Actors">
      <div className="scene-list">
        {scene.actors.length === 0 && (
          <div className="scene-note">No actors yet. An actor is one quest alias that takes part in the scene. Use Add in the tab bar, then Add actor.</div>
        )}
        <ul className="scene-cards">
          {scene.actors.map(entry => {
            const isSelected = entry.index === selected;
            const title = aliasTitle(entry.alias, names);
            const options = enabledCount(entry.flags, ACTOR_FLAGS) + enabledCount(entry.behaviorFlags, ACTOR_BEHAVIOR_FLAGS);
            return (
              <li key={entry.index}>
                <button type="button" className={`scene-card${isSelected ? ' is-selected' : ''}`}
                  aria-label={title} aria-pressed={isSelected} onClick={() => onSelect(entry.index)}>
                  <span className="scene-card-title">{title}</span>
                  <span className="scene-card-meta">{plural(actionsUsingActor(scene, entry.alias).length, 'action', 'actions')}</span>
                  <span className="scene-card-meta">{plural(options, 'option on', 'options on')}</span>
                </button>
              </li>
            );
          })}
        </ul>
      </div>
      <div className="scene-side">
        {actor ? (
          <ReadOnlyFieldset>
            <ActorForm key={`actor-${actor.index}-${revision}`} scene={scene} actor={actor} names={names} changes={changes} run={run} />
          </ReadOnlyFieldset>
        ) : (
          <div className="scene-placeholder">Pick an actor card to edit its alias, options and behaviour pauses.</div>
        )}
      </div>
    </section>
  );
}

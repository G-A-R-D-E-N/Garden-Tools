import ReadOnlyFieldset from '../ReadOnlyFieldset';
import type { Run, SceneChanges } from './sceneChanges';
import PhaseForm from './PhaseForm';
import { phaseName, plural, type SceneDto } from './sceneModel';
import { actionsCovering } from './sceneTabModel';

interface Props {
  scene: SceneDto;
  selected: number | null;
  revision: number;
  changes: SceneChanges;
  run: Run;
  onSelect: (index: number) => void;
  onOpenConditions: (path: string, label: string) => void;
}

export default function ScenePhasesPanel({ scene, selected, revision, changes, run, onSelect, onOpenConditions }: Props) {
  const phase = scene.phases.find(entry => entry.index === selected);
  return (
    <section className="scene-split" aria-label="Phases">
      <div className="scene-list">
        {scene.phases.length === 0 && (
          <div className="scene-note">No phases yet. Use Add in the tab bar, then Add phase.</div>
        )}
        <ul className="scene-cards">
          {scene.phases.map(entry => {
            const unreadable = !!entry.startConditionsUnreadable || !!entry.completionConditionsUnreadable;
            const isSelected = entry.index === selected;
            return (
              <li key={entry.index}>
                <button type="button" className={`scene-card${isSelected ? ' is-selected' : ''}`}
                  aria-label={phaseName(entry)} aria-pressed={isSelected} onClick={() => onSelect(entry.index)}>
                  <span className="scene-card-kicker">Phase {entry.index + 1}</span>
                  <span className="scene-card-title">{phaseName(entry)}</span>
                  <span className="scene-card-meta">{plural(actionsCovering(scene, entry.index), 'action', 'actions')}</span>
                  {(entry.questStageOnStart ?? 0) !== 0 && (
                    <span className="scene-card-meta">Stage {entry.questStageOnStart} on start</span>
                  )}
                  {unreadable && <span className="scene-badge scene-badge-warning">Conditions unreadable</span>}
                </button>
              </li>
            );
          })}
        </ul>
      </div>
      <div className="scene-side">
        {phase ? (
          <ReadOnlyFieldset>
            <PhaseForm key={`phase-${phase.index}-${revision}`} scene={scene} phase={phase} changes={changes} run={run}
              onOpenConditions={onOpenConditions} />
          </ReadOnlyFieldset>
        ) : (
          <div className="scene-placeholder">Pick a phase card to edit its name, quest stages, options and conditions.</div>
        )}
      </div>
    </section>
  );
}

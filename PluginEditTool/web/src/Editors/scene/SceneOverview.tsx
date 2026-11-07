import { actionTitle, plural, typeLabel, type SceneDto, type Selection } from './sceneModel';
import { actionDetail, aliasTitle, actorOf, outline, phaseLabel } from './sceneTabModel';

interface Props {
  scene: SceneDto;
  names: ReadonlyMap<number, string>;
  selection: Selection;
  onSelect: (selection: NonNullable<Selection>) => void;
}

export default function SceneOverview({ scene, names, selection, onSelect }: Props) {
  const groups = outline(scene);
  return (
    <section className="scene-overview" aria-label="Overview">
      <div className="scene-overview-head">
        <h4>{scene.editorId || scene.formKey}</h4>
        <dl className="scene-facts">
          <div>
            <dt>Form key</dt>
            <dd>{scene.formKey}</dd>
          </div>
          <div>
            <dt>Quest</dt>
            <dd>{scene.parentQuest || 'None'}</dd>
          </div>
        </dl>
      </div>
      <ul className="scene-stats" aria-label="Summary">
        <li>{plural(scene.phases.length, 'phase', 'phases')}</li>
        <li>{plural(scene.actors.length, 'actor', 'actors')}</li>
        <li>{plural(scene.actions.length, 'action', 'actions')}</li>
      </ul>
      {scene.phases.length === 0 && (
        <div className="scene-note">This scene has no phases yet. Use Add in the tab bar to start it.</div>
      )}
      <ol className="scene-outline" aria-label="Outline">
        {groups.map(group => {
          const phase = group.phase;
          return (
            <li key={phase ? phase.index : 'loose'} className="scene-outline-group">
              {phase ? (
                <button type="button" className="scene-outline-head"
                  onClick={() => onSelect({ kind: 'phase', index: phase.index })}>
                  {phaseLabel(phase)}
                </button>
              ) : (
                <span className="scene-outline-head">Actions not in a phase</span>
              )}
              {group.actions.length === 0 && phase && <span className="scene-note">No actions in this phase.</span>}
              <ul className="scene-outline-actions">
                {group.actions.map(action => {
                  const detail = actionDetail(action);
                  const selected = selection?.kind === 'action' && selection.index === action.index;
                  return (
                    <li key={action.index}>
                      <button type="button" className={`scene-outline-row${selected ? ' is-selected' : ''}`}
                        onClick={() => onSelect({ kind: 'action', index: action.index })}>
                        <span className="scene-outline-actor">{aliasTitle(actorOf(action), names)}</span>
                        <span className="scene-outline-type">{typeLabel(action.type)}</span>
                        <span className="scene-outline-text">{actionTitle(action)}</span>
                        {detail && <span className="scene-outline-detail">{detail}</span>}
                      </button>
                    </li>
                  );
                })}
              </ul>
            </li>
          );
        })}
      </ol>
    </section>
  );
}

import ReadOnlyFieldset from '../ReadOnlyFieldset';
import ActionForm from './ActionForm';
import type { Run, SceneChanges } from './sceneChanges';
import { Field } from './sceneFormParts';
import { actionTitle, typeLabel, phaseName, type SceneDto } from './sceneModel';
import {
  actorAliases, actorOf, aliasTitle, filterActions, sanitizeFilter, spanLabel, type ActionFilter,
} from './sceneTabModel';

interface Props {
  scene: SceneDto;
  selected: number | null;
  revision: number;
  active: boolean;
  filter: ActionFilter;
  names: ReadonlyMap<number, string>;
  changes: SceneChanges;
  run: Run;
  onFilter: (filter: ActionFilter) => void;
  onSelect: (index: number) => void;
}

const optionalNumber = (value: string): number | null => (value === '' ? null : Number(value));

export default function SceneActionsPanel({
  scene, selected, revision, active, filter, names, changes, run, onFilter, onSelect,
}: Props) {
  const effective = sanitizeFilter(scene, filter);
  const rows = filterActions(scene, effective);
  const action = scene.actions.find(entry => entry.index === selected);
  const hiddenAction = action !== undefined && !rows.some(entry => entry.index === action.index);
  return (
    <section className="scene-split" aria-label="Actions">
      <div className="scene-list">
        <div className="scene-filters">
          <Field label="Phase" topic="scene-timeline">
            {id => (
              <select id={id} value={effective.phase ?? ''}
                onChange={e => onFilter({ ...effective, phase: optionalNumber(e.target.value) })}>
                <option value="">All phases</option>
                {scene.phases.map(entry => <option key={entry.index} value={entry.index}>{phaseName(entry)}</option>)}
              </select>
            )}
          </Field>
          <Field label="Actor" topic="scene-action-actor">
            {id => (
              <select id={id} value={effective.actor ?? ''}
                onChange={e => onFilter({ ...effective, actor: optionalNumber(e.target.value) })}>
                <option value="">All actors</option>
                {actorAliases(scene).map(alias => <option key={alias} value={alias}>{aliasTitle(alias, names)}</option>)}
              </select>
            )}
          </Field>
        </div>
        {scene.actions.length === 0 && (
          <div className="scene-note">No actions yet. Use Add in the tab bar, then Add action and pick a type.</div>
        )}
        {scene.actions.length > 0 && rows.length === 0 && (
          <div className="scene-note">No actions match these filters.</div>
        )}
        {hiddenAction && <div className="scene-note">{scene.actions.length - rows.length} hidden by the filter</div>}
        {active && rows.length > 0 && (
          <div className="scene-table-wrap">
            <table className="scene-action-table">
              <thead>
                <tr>
                  <th scope="col">#</th>
                  <th scope="col">Type</th>
                  <th scope="col">Name</th>
                  <th scope="col">Actor</th>
                  <th scope="col">Phases</th>
                </tr>
              </thead>
              <tbody>
                {rows.map(entry => (
                  <tr key={entry.index} className={entry.index === selected ? 'is-selected' : undefined}>
                    <td>{entry.index + 1}</td>
                    <td>{typeLabel(entry.type)}</td>
                    <td>
                      <button type="button" className="scene-table-action" onClick={() => onSelect(entry.index)}>
                        {actionTitle(entry)}
                      </button>
                    </td>
                    <td>{aliasTitle(actorOf(entry), names)}</td>
                    <td>{spanLabel(entry, scene.phases.length)}</td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        )}
      </div>
      <div className="scene-side">
        {action ? (
          <ReadOnlyFieldset>
            <ActionForm key={`action-${action.index}-${revision}`} scene={scene} action={action} names={names}
              changes={changes} run={run} />
          </ReadOnlyFieldset>
        ) : (
          <div className="scene-placeholder">Pick an action in the list to edit its text, phases, actor and options.</div>
        )}
      </div>
    </section>
  );
}

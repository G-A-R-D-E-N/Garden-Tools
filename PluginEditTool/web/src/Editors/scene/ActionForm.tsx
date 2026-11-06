import { useState } from 'react';
import { diffDraft, type Run, type SceneChanges } from './sceneChanges';
import {
  ACTION_FLAGS, actionTitle, aliasLabel, payloadFieldsFor, phaseName, typeLabel, toNumber, unrestorableReason,
  type SceneAction, type SceneDto, type Spec,
} from './sceneModel';
import { Field, FlagBoxes, Notice, RemovePanel } from './sceneFormParts';

interface Props {
  scene: SceneDto;
  action: SceneAction;
  changes: SceneChanges;
  run: Run;
}

export default function ActionForm({ scene, action, changes, run }: Props) {
  const fields = payloadFieldsFor(action.type);
  const linkKeys = new Set(fields.filter(field => field.kind === 'link').map(field => field.key));
  const saved: Spec = {
    name: action.name ?? '',
    actorAlias: action.actorAlias ?? -1,
    startPhase: action.startPhase ?? 0,
    endPhase: action.endPhase ?? 0,
    flags: action.flags ?? 0,
    ...Object.fromEntries(fields.map((field): [string, unknown] => [
      field.key,
      field.kind === 'link' ? String(action.payload[field.key] ?? '') : Number(action.payload[field.key] ?? 0),
    ])),
  };
  const [draft, setDraft] = useState<Spec>(saved);
  const setField = (key: string, value: unknown) => setDraft(current => ({ ...current, [key]: value }));
  const { next, prev, cleared } = diffDraft(draft, saved, linkKeys);
  const aliases = [...new Set([...scene.actors.map(entry => entry.alias), -1])];
  const reason = unrestorableReason(action);
  const clearedLabels = cleared.map(key => fields.find(field => field.key === key)?.label ?? key);

  return (
    <section className="scene-form" aria-label={actionTitle(action)}>
      <h4>{actionTitle(action)}</h4>
      <div className="scene-form-grid">
        <Field label="Action name" topic="scene-action-name">
          {id => <input id={id} type="text" value={String(draft.name)} onChange={e => setField('name', e.target.value)} />}
        </Field>
        <Field label="Action type" topic="scene-action-type">
          {() => <span className="scene-readonly">{typeLabel(action.type)}. The type cannot be changed.</span>}
        </Field>
        <Field label="Actor for this action" topic="scene-action-actor">
          {id => (
            <select id={id} value={Number(draft.actorAlias)} onChange={e => setField('actorAlias', Number(e.target.value))}>
              {aliases.map(alias => <option key={alias} value={alias}>{aliasLabel(alias)}</option>)}
            </select>
          )}
        </Field>
        <Field label="Start phase" topic="scene-action-phases">
          {id => (
            <select id={id} value={Number(draft.startPhase)} onChange={e => setField('startPhase', Number(e.target.value))}>
              {scene.phases.map(phase => <option key={phase.index} value={phase.index}>{phaseName(phase)}</option>)}
            </select>
          )}
        </Field>
        <Field label="End phase" topic="scene-action-phases">
          {id => (
            <select id={id} value={Number(draft.endPhase)} onChange={e => setField('endPhase', Number(e.target.value))}>
              {scene.phases.map(phase => <option key={phase.index} value={phase.index}>{phaseName(phase)}</option>)}
            </select>
          )}
        </Field>
        {fields.map(field => (
          <Field key={field.key} label={field.label} topic={field.topic}>
            {id => field.kind === 'link' ? (
              <input id={id} type="text" placeholder="Form ID:Plugin name" value={String(draft[field.key] ?? '')}
                onChange={e => setField(field.key, e.target.value)} />
            ) : (
              <input id={id} type="number" value={String(draft[field.key] ?? 0)}
                onChange={e => setField(field.key, toNumber(e.target.value))} />
            )}
          </Field>
        ))}
      </div>
      <FlagBoxes label="Action options" topic="scene-action-flags" names={ACTION_FLAGS} value={Number(draft.flags)}
        onChange={value => setField('flags', value)} />
      {clearedLabels.length > 0 && (
        <Notice tone="info">
          This tool cannot clear a link yet, so these keep their value: {clearedLabels.join(', ')}.
        </Notice>
      )}
      <div className="scene-form-actions">
        <button type="button" className="sidebar-action-btn" disabled={Object.keys(next).length === 0}
          onClick={() => run(changes.editActions('Change action', [{ index: action.index, next, prev }]))}>
          Apply action changes
        </button>
        <button type="button" className="sidebar-action-btn" disabled={action.index <= 0}
          onClick={() => run(changes.moveAction(action.index, action.index - 1), { kind: 'action', index: action.index - 1 })}>
          Move earlier
        </button>
        <button type="button" className="sidebar-action-btn" disabled={action.index >= scene.actions.length - 1}
          onClick={() => run(changes.moveAction(action.index, action.index + 1), { kind: 'action', index: action.index + 1 })}>
          Move later
        </button>
      </div>
      <RemovePanel noun="action" usage={0} targets={[]}
        blockedBy={reason ? `Removing this action is turned off, because undo could not bring it back. ${reason}` : null}
        onRemove={() => run(changes.removeAction(action), null)} />
    </section>
  );
}

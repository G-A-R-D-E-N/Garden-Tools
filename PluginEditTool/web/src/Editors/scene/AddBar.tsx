import { useState } from 'react';
import { Plus } from 'lucide-react';
import { Field } from './sceneFormParts';
import { ACTION_TYPES } from './sceneModel';

interface Props {
  phaseCount: number;
  onAddPhase: () => void;
  onAddActor: () => void;
  onAddAction: (type: number) => void;
}

export default function AddBar({ phaseCount, onAddPhase, onAddActor, onAddAction }: Props) {
  const [type, setType] = useState<number>(ACTION_TYPES[0].type);
  return (
    <div className="scene-add-bar">
      <button type="button" className="sidebar-action-btn" onClick={onAddPhase}><Plus size={14} /> Add phase</button>
      <button type="button" className="sidebar-action-btn" onClick={onAddActor}><Plus size={14} /> Add actor</button>
      <Field label="New action type" topic="scene-action-type">
        {id => (
          <select id={id} value={type} onChange={e => setType(Number(e.target.value))}>
            {ACTION_TYPES.map(entry => <option key={entry.type} value={entry.type}>{entry.label}</option>)}
          </select>
        )}
      </Field>
      <button type="button" className="sidebar-action-btn" disabled={phaseCount === 0}
        title={phaseCount === 0 ? 'Add a phase first. An action runs across phases.' : undefined}
        onClick={() => onAddAction(type)}><Plus size={14} /> Add action</button>
    </div>
  );
}

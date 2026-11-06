import { useState } from 'react';
import RecordPicker from '../../RecordPicker';
import HelpTip from '../help/HelpTip';
import type { DialogueActions } from './dialogueActions';
import { REPLY_FLAGS, emotionLabel, isLocalized, textProblem, type DialogueInfo } from './dialogueModel';

interface DetailsProps {
  info: DialogueInfo | null;
  index: number;
  labels: Record<string, string>;
  actions: DialogueActions;
  onEditConditions: (info: DialogueInfo) => void;
}

export default function DialogueDetails({ info, index, labels, actions, onEditConditions }: DetailsProps) {
  if (!info || !info.responses[index]) {
    return (
      <aside className="dlg-pane" aria-label="Selected line">
        <p className="dlg-empty">Pick a line in the middle column to edit its text, number, emotion, flags and conditions.</p>
      </aside>
    );
  }
  const response = info.responses[index];
  return (
    <aside className="dlg-pane" aria-label="Selected line">
      <LineForm key={`${info.formKey}:${index}:${response.number}:${response.text ?? ''}:${response.emotion ?? ''}`}
        info={info} index={index} labels={labels} actions={actions} onEditConditions={onEditConditions} />
    </aside>
  );
}

interface LineFormProps {
  info: DialogueInfo;
  index: number;
  labels: Record<string, string>;
  actions: DialogueActions;
  onEditConditions: (info: DialogueInfo) => void;
}

function LineForm({ info, index, labels, actions, onEditConditions }: LineFormProps) {
  const response = info.responses[index];
  const localized = isLocalized(info);
  const [text, setText] = useState(response.text ?? '');
  const [number, setNumber] = useState(String(response.number));
  const [pickingEmotion, setPickingEmotion] = useState(false);
  const [armed, setArmed] = useState(false);
  const problem = textProblem(text);
  const numberValue = Number(number);
  const numberOk = Number.isInteger(numberValue) && numberValue >= 1 && numberValue <= 255;
  const dirty = text !== (response.text ?? '') || numberValue !== response.number;

  const saveLine = () => {
    if (problem || !numberOk) return;
    void actions.saveLine(info, index, { text, number: numberValue });
  };

  return (
    <>
      <div className="dlg-pane-head">
        <h4>Line {response.number}</h4>
      </div>
      {localized && (
        <p className="dlg-muted">This reply keeps its text in the string table, so its lines cannot be changed here yet.</p>
      )}
      <div className="dlg-field-head">
        <label htmlFor="dlg-line-text">Line text</label>
        <HelpTip topicId="dialogue-line-text" />
      </div>
      <textarea id="dlg-line-text" rows={4} value={text} disabled={localized} onChange={e => setText(e.target.value)} />
      {problem && <p role="alert" className="dlg-problem">{problem}</p>}
      <div className="dlg-field-head">
        <label htmlFor="dlg-line-number">Line number</label>
        <HelpTip topicId="dialogue-line-number" />
      </div>
      <input id="dlg-line-number" type="number" min={1} max={255} value={number} disabled={localized}
        onChange={e => setNumber(e.target.value)} />
      {!numberOk && <p role="alert" className="dlg-problem">Line number must be a whole number from 1 to 255.</p>}
      <button type="button" disabled={localized || !dirty || problem !== null || !numberOk} onClick={saveLine}>
        Save line
      </button>

      <div className="dlg-row">
        <span>Emotion: {emotionLabel(response, labels)}</span>
        <HelpTip topicId="dialogue-emotion" />
        <button type="button" disabled={localized} onClick={() => setPickingEmotion(true)}>Pick emotion</button>
        {response.emotion && (
          <button type="button" disabled={localized} onClick={() => void actions.saveLine(info, index, { emotion: null })}>
            Clear emotion
          </button>
        )}
      </div>
      {pickingEmotion && (
        <RecordPicker title="Emotion" refType="Keyword" refTypes="KYWD"
          onPick={key => { setPickingEmotion(false); void actions.saveLine(info, index, { emotion: key }); }}
          onClose={() => setPickingEmotion(false)} />
      )}

      <fieldset className="dlg-flags">
        <legend>Reply flags</legend>
        {REPLY_FLAGS.map(flag => (
          <div key={flag.bit} className="dlg-flag">
            <label>
              <input type="checkbox" checked={(info.flags & flag.bit) !== 0}
                onChange={() => void actions.setFlags(info, info.flags ^ flag.bit)} />
              {' '}{flag.label}
            </label>
            <HelpTip topicId={flag.help} />
          </div>
        ))}
      </fieldset>

      <div className="dlg-row">
        <span>Conditions: {info.conditions.length === 0 ? 'none' : `${info.conditions.length} set`}</span>
        <HelpTip topicId="dialogue-conditions" />
        <button type="button" onClick={() => onEditConditions(info)}>Edit conditions</button>
      </div>

      <div className="dlg-field-head">
        <span>Script fragments</span>
        <HelpTip topicId="dialogue-fragment" />
      </div>
      {info.fragments.length === 0
        ? <p className="dlg-muted">No script fragment is attached to this reply.</p>
        : <ul className="dlg-list">{info.fragments.map(f => <li key={f.index}>{f.script} · {f.function}</li>)}</ul>}

      <button type="button" className="dlg-danger"
        onClick={() => { if (armed) void actions.removeReply(info); else setArmed(true); }}>
        {armed ? 'Click again to remove this reply' : 'Remove reply'}
      </button>
      <p className="dlg-muted">Removing a reply cannot be undone with Ctrl+Z.</p>
    </>
  );
}

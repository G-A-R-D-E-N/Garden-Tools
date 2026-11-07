import { useState } from 'react';
import RecordPicker from '../../RecordPicker';
import HelpTip from '../help/HelpTip';
import ReadOnlyFieldset from '../ReadOnlyFieldset';
import type { DialogueActions } from './dialogueActions';
import { REPLY_FLAGS, emotionLabel, isLocalized, textProblem, type DialogueInfo } from './dialogueModel';

interface DetailsProps {
  info: DialogueInfo | null;
  index: number;
  labels: Record<string, string>;
  actions: DialogueActions;
  active: boolean;
}

export default function DialogueDetails({ info, index, labels, actions, active }: DetailsProps) {
  if (!info || !info.responses[index]) {
    return (
      <aside className="dlg-pane" aria-label="Selected line">
        <p className="dlg-empty">Pick a reply line in the Conversation tab to edit its text, number, emotion and flags.</p>
      </aside>
    );
  }
  const response = info.responses[index];
  return (
    <aside className="dlg-pane dlg-details" aria-label="Selected line">
      <ReadOnlyFieldset>
        <LineForm key={`${info.formKey}:${index}:${response.number}:${response.text ?? ''}:${response.emotion ?? ''}`}
          info={info} index={index} labels={labels} actions={actions} active={active} />
      </ReadOnlyFieldset>
    </aside>
  );
}

interface LineFormProps {
  info: DialogueInfo;
  index: number;
  labels: Record<string, string>;
  actions: DialogueActions;
  active: boolean;
}

function LineForm({ info, index, labels, actions, active }: LineFormProps) {
  const response = info.responses[index];
  const localized = isLocalized(info);
  const [text, setText] = useState(response.text ?? '');
  const [number, setNumber] = useState(String(response.number));
  const [pickingEmotion, setPickingEmotion] = useState(false);
  const problem = textProblem(text);
  const parsedNumber = Number(number);
  const numberOk = Number.isInteger(parsedNumber) && parsedNumber >= 1 && parsedNumber <= 255;
  const dirty = text !== (response.text ?? '') || parsedNumber !== response.number;

  const saveLine = () => {
    if (problem || !numberOk) return;
    void actions.saveLine(info, index, { text, number: parsedNumber });
  };

  return (
    <>
      <div className="dlg-pane-head">
        <h4>Line {response.number}</h4>
      </div>
      {localized && (
        <p className="dlg-muted">This reply keeps its text in the string table, so its lines cannot be changed here yet.</p>
      )}
      <div className="dlg-form-grid">
        <div className="dlg-group dlg-span-2">
          <h5>Text</h5>
          <div className="dlg-field-head">
            <label htmlFor="dlg-line-text">Line text</label>
            <HelpTip topicId="dialogue-line-text" />
          </div>
          <textarea id="dlg-line-text" rows={4} value={text} disabled={localized} onChange={e => setText(e.target.value)} />
          {problem && <p role="alert" className="dlg-problem">{problem}</p>}
        </div>

        <div className="dlg-group">
          <h5>Delivery</h5>
          <div className="dlg-field-head">
            <label htmlFor="dlg-line-number">Line number</label>
            <HelpTip topicId="dialogue-line-number" />
          </div>
          <input id="dlg-line-number" type="number" min={1} max={255} value={number} disabled={localized}
            onChange={e => setNumber(e.target.value)} />
          {!numberOk && <p role="alert" className="dlg-problem">Line number must be a whole number from 1 to 255.</p>}
        </div>

        <div className="dlg-group">
          <h5>Emotion</h5>
          <div className="dlg-field-head">
            <span>{emotionLabel(response, labels)}</span>
            <HelpTip topicId="dialogue-emotion" />
          </div>
          <div className="dlg-row">
            <button type="button" disabled={localized} onClick={() => setPickingEmotion(true)}>Pick emotion</button>
            {response.emotion && (
              <button type="button" disabled={localized} onClick={() => void actions.saveLine(info, index, { emotion: null })}>
                Clear emotion
              </button>
            )}
          </div>
        </div>
      </div>
      {pickingEmotion && (
        <RecordPicker title="Emotion" refType="Keyword" refTypes="KYWD"
          onPick={key => { setPickingEmotion(false); void actions.saveLine(info, index, { emotion: key }); }}
          onClose={() => setPickingEmotion(false)} />
      )}

      <fieldset className="dlg-flags">
        <legend>Reply flags</legend>
        <div className="dlg-flag-grid">
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
        </div>
      </fieldset>

      <div className="dlg-actions">
        <button type="button" disabled={localized || !dirty || problem !== null || !numberOk} onClick={saveLine}>
          Save line
        </button>
        <RemoveReply key={`${info.formKey}:${index}:${active}`} info={info} actions={actions} />
        <p className="dlg-muted">Removing a reply cannot be undone with Ctrl+Z.</p>
      </div>
    </>
  );
}

function RemoveReply({ info, actions }: { info: DialogueInfo; actions: DialogueActions }) {
  const [armed, setArmed] = useState(false);
  return (
    <button type="button" className="dlg-danger"
      onClick={() => { if (armed) void actions.removeReply(info); else setArmed(true); }}>
      {armed ? 'Click again to remove this reply' : 'Remove reply'}
    </button>
  );
}

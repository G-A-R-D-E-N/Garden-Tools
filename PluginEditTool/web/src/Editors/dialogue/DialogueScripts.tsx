import HelpTip from '../help/HelpTip';
import ReadOnlyFieldset from '../ReadOnlyFieldset';
import type { DialogueInfo } from './dialogueModel';

interface ScriptsProps {
  info: DialogueInfo | null;
  onEditConditions: (info: DialogueInfo) => void;
}

export default function DialogueScripts({ info, onEditConditions }: ScriptsProps) {
  if (!info) {
    return (
      <section className="dlg-pane" aria-label="Conditions and scripts">
        <p className="dlg-empty">Pick a reply in the Conversation tab first. Conditions and script fragments belong to one reply.</p>
      </section>
    );
  }
  const count = info.conditions.length;
  return (
    <section className="dlg-pane dlg-scripts" aria-label="Conditions and scripts">
      <div className="dlg-pane-head">
        <h4>Conditions</h4>
        <HelpTip topicId="dialogue-conditions" />
      </div>
      <p className="dlg-muted">{count === 0 ? 'Conditions: none' : `Conditions: ${count} set`}</p>
      <div className="dlg-row">
        <ReadOnlyFieldset>
          <button type="button" onClick={() => onEditConditions(info)}>Edit conditions</button>
        </ReadOnlyFieldset>
      </div>
      <div className="dlg-pane-head">
        <h4>Script fragments</h4>
        <HelpTip topicId="dialogue-fragment" />
      </div>
      {info.fragments.length === 0
        ? <p className="dlg-muted">No script fragment is attached to this reply.</p>
        : <ul className="dlg-list">{info.fragments.map(f => <li key={f.index}>{f.script} · {f.function}</li>)}</ul>}
    </section>
  );
}

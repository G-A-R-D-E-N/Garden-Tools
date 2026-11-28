import { useState } from 'react';
import HelpTip from '../help/HelpTip';
import ReadOnlyFieldset from '../ReadOnlyFieldset';
import type { DialogueActions } from './dialogueActions';
import {
  REPLY_FLAGS, emotionLabel, lineText, textProblem, topicLine, topicTitle, usesStringTable,
  type DialogueInfo, type DialogueTopic, type LineRef,
} from './dialogueModel';

interface ChatProps {
  topic: DialogueTopic | null;
  line: LineRef | null;
  labels: Record<string, string>;
  actions: DialogueActions;
  onSelectLine: (ref: LineRef) => void;
}

const conditionsChip = (info: DialogueInfo): string => {
  const count = info.conditions.length;
  if (count === 0) return 'No conditions';
  return `${count} ${count === 1 ? 'condition' : 'conditions'}`;
};

export default function DialogueChat({ topic, line, labels, actions, onSelectLine }: ChatProps) {
  const [reply, setReply] = useState('');
  const problem = textProblem(reply);

  if (!topic) {
    return (
      <section className="dlg-pane" aria-label="Conversation">
        <p className="dlg-empty">Pick a topic in the Topics tab to see what the player says and how the NPC answers.</p>
      </section>
    );
  }

  const addReply = async () => {
    if (problem || reply.trim() === '') return;
    if (await actions.addReply(topic, reply.trim())) setReply('');
  };

  return (
    <section className="dlg-pane dlg-chat" aria-label="Conversation">
      <div className="dlg-pane-head">
        <h4>{topicTitle(topic)}</h4>
        <HelpTip topicId="dialogue-topic" />
      </div>
      <div className="dlg-bubble dlg-bubble-player">
        <span className="dlg-who">Player</span>
        <span className={topic.name ? undefined : 'dlg-muted'}>{topicLine(topic)}</span>
      </div>
      {topic.infos.length === 0 && (
        <p className="dlg-empty">This topic has no replies yet. Add one below so the NPC has something to say.</p>
      )}
      {topic.infos.length > 0 && (
        <p className="dlg-muted">Click a line to edit it in the Details tab.</p>
      )}
      {topic.infos.map((info, replyIndex) => (
        <div key={info.formKey} className="dlg-reply-card">
          <div className="dlg-reply-head">
            <strong>Reply {replyIndex + 1}</strong>
            <span className={info.conditions.length === 0 ? 'dlg-chip dlg-chip-muted' : 'dlg-chip'}>{conditionsChip(info)}</span>
            {REPLY_FLAGS.filter(flag => (info.flags & flag.bit) !== 0).map(flag => (
              <span key={flag.bit} className="dlg-chip dlg-chip-flag" title={flag.label}>{flag.short}</span>
            ))}
            <HelpTip topicId="dialogue-reply" />
          </div>
          <ol className="dlg-lines" aria-label={`Lines of reply ${replyIndex + 1}`}>
            {info.responses.map((response, index) => (
              <li key={index}>
                <button type="button" className="dlg-line"
                  aria-pressed={line?.info === info.formKey && line.index === index}
                  onClick={() => onSelectLine({ info: info.formKey, index })}>
                  <span className="dlg-who">NPC · line {response.number} · {emotionLabel(response, labels)}</span>
                  <span className={usesStringTable(response) ? 'dlg-muted' : undefined}>{lineText(response)}</span>
                </button>
              </li>
            ))}
          </ol>
        </div>
      ))}
      <ReadOnlyFieldset>
        <form className="dlg-form" onSubmit={e => { e.preventDefault(); void addReply(); }}>
          <div className="dlg-field-head">
            <label htmlFor="dlg-new-reply">New NPC line</label>
            <HelpTip topicId="dialogue-line-text" />
          </div>
          <textarea id="dlg-new-reply" rows={2} value={reply} onChange={e => setReply(e.target.value)} />
          {problem && <p role="alert" className="dlg-problem">{problem}</p>}
          <button type="submit" disabled={reply.trim() === '' || problem !== null}>Add reply</button>
        </form>
      </ReadOnlyFieldset>
    </section>
  );
}

import { useState } from 'react';
import HelpTip from '../help/HelpTip';
import type { DialogueActions } from './dialogueActions';
import {
  emotionLabel, lineText, textProblem, topicLine, topicTitle,
  type DialogueTopic, type LineRef,
} from './dialogueModel';

interface ChatProps {
  topic: DialogueTopic | null;
  line: LineRef | null;
  labels: Record<string, string>;
  actions: DialogueActions;
  onSelectLine: (ref: LineRef) => void;
}

export default function DialogueChat({ topic, line, labels, actions, onSelectLine }: ChatProps) {
  const [reply, setReply] = useState('');
  const problem = textProblem(reply);

  if (!topic) {
    return (
      <section className="dlg-pane" aria-label="Conversation">
        <p className="dlg-empty">Pick a topic on the left to see what the player says and how the NPC answers.</p>
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
        <span>{topicLine(topic)}</span>
      </div>
      {topic.infos.length === 0 && (
        <p className="dlg-empty">This topic has no replies yet. Add one below so the NPC has something to say.</p>
      )}
      {topic.infos.map((info, replyIndex) => (
        <div key={info.formKey} className="dlg-reply">
          <div className="dlg-pane-head">
            <span className="dlg-muted">
              Reply {replyIndex + 1} · {info.conditions.length === 0 ? 'no conditions' : `${info.conditions.length} condition(s)`}
            </span>
            <HelpTip topicId="dialogue-reply" />
          </div>
          <ol className="dlg-lines" aria-label={`Lines of reply ${replyIndex + 1}`}>
            {info.responses.map((response, index) => (
              <li key={index}>
                <button type="button" className="dlg-line"
                  aria-pressed={line?.info === info.formKey && line.index === index}
                  onClick={() => onSelectLine({ info: info.formKey, index })}>
                  <span className="dlg-who">NPC · line {response.number} · {emotionLabel(response, labels)}</span>
                  <span>{lineText(response)}</span>
                </button>
              </li>
            ))}
          </ol>
        </div>
      ))}
      <form className="dlg-form" onSubmit={e => { e.preventDefault(); void addReply(); }}>
        <div className="dlg-field-head">
          <label htmlFor="dlg-new-reply">New NPC line</label>
          <HelpTip topicId="dialogue-line-text" />
        </div>
        <textarea id="dlg-new-reply" rows={2} value={reply} onChange={e => setReply(e.target.value)} />
        {problem && <p role="alert" className="dlg-problem">{problem}</p>}
        <button type="submit" disabled={reply.trim() === '' || problem !== null}>Add reply</button>
      </form>
    </section>
  );
}

import { useEffect, useId, useState, type FocusEvent } from 'react';
import { getHelpTopic } from './helpContent';
import './Help.css';

export default function HelpTip({ topicId, onLearnMore }: { topicId: string; onLearnMore?: (topicId: string) => void }) {
  const topic = getHelpTopic(topicId);
  const [open, setOpen] = useState(false);
  const tipId = useId();
  useEffect(() => {
    if (!open) return;
    const closeOnEscape = (e: KeyboardEvent) => { if (e.key === 'Escape') setOpen(false); };
    document.addEventListener('keydown', closeOnEscape);
    return () => document.removeEventListener('keydown', closeOnEscape);
  }, [open]);
  if (!topic) return null;

  const closeIfLeaving = (e: FocusEvent<HTMLSpanElement>) => {
    if (!e.currentTarget.contains(e.relatedTarget as Node | null)) setOpen(false);
  };

  return (
    <span className="help-tip" onMouseEnter={() => setOpen(true)} onMouseLeave={() => setOpen(false)}
      onFocus={() => setOpen(true)} onBlur={closeIfLeaving}>
      <button type="button" className="help-tip-button" aria-label={`Help: ${topic.title}`}
        aria-describedby={open ? tipId : undefined}>?</button>
      {open && (
        <span role="tooltip" id={tipId} className="help-tip-bubble">
          <strong>{topic.title}</strong> {topic.body}
          {onLearnMore && (
            <button type="button" className="help-tip-link" onClick={() => onLearnMore(topic.id)}>Learn more</button>
          )}
        </span>
      )}
    </span>
  );
}

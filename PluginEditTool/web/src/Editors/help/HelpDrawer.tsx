import { useEffect, useRef } from 'react';
import { X } from 'lucide-react';
import { getHelpTopic } from './helpContent';
import './Help.css';

export default function HelpDrawer({ topicIds, onClose }: { topicIds: readonly string[]; onClose: () => void }) {
  const topics = topicIds.flatMap(id => getHelpTopic(id) ?? []);
  const closeRef = useRef<HTMLButtonElement>(null);
  useEffect(() => { closeRef.current?.focus(); }, []);
  return (
    <aside className="help-drawer" aria-label="Help for this screen"
      onKeyDown={e => { if (e.key === 'Escape') onClose(); }}>
      <div className="help-drawer-header">
        <h3>Help for this screen</h3>
        <button ref={closeRef} type="button" className="papyrus-close" onClick={onClose} aria-label="Close help"><X size={16} /></button>
      </div>
      {topics.map(topic => (
        <section key={topic.id}>
          <h4>{topic.title}</h4>
          <p>{topic.body}</p>
        </section>
      ))}
    </aside>
  );
}

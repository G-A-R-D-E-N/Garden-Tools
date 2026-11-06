import { useState } from 'react';
import { X } from 'lucide-react';
import { GETTING_STARTED, HELP_TOPICS, getHelpTopic } from './helpContent';
import './Help.css';

export default function HelpPage({ initialTopicId, onClose }: { initialTopicId?: string; onClose: () => void }) {
  const [query, setQuery] = useState('');
  const needle = query.trim().toLowerCase();
  const topics = HELP_TOPICS
    .filter(topic => !needle || `${topic.title} ${topic.body}`.toLowerCase().includes(needle))
    .sort((a, b) => a.title.localeCompare(b.title));

  return (
    <div className="papyrus-overlay" onClick={onClose}>
      <div className="papyrus-modal glass-panel help-page" role="dialog" aria-modal="true" aria-labelledby="help-page-title"
        onClick={e => e.stopPropagation()}>
        <div className="papyrus-header">
          <h2 id="help-page-title" className="papyrus-title">Help</h2>
          <button type="button" className="papyrus-close" onClick={onClose} aria-label="Close help"><X size={16} /></button>
        </div>
        <div className="help-page-body">
          <section>
            <h3>Getting started</h3>
            <ol>
              {GETTING_STARTED.map(step => <li key={step}>{step}</li>)}
            </ol>
          </section>
          <section>
            <h3>Glossary</h3>
            <label htmlFor="help-search">Search help</label>
            <input id="help-search" type="search" value={query} onChange={e => setQuery(e.target.value)}
              placeholder="Type a word, such as alias" />
            {topics.length === 0 && <p>No help topic matches that word. Try a shorter word.</p>}
            {topics.map(topic => (
              <article key={topic.id} className={`help-topic${topic.id === initialTopicId ? ' is-current' : ''}`}>
                <h4>{topic.title}</h4>
                <p>{topic.body}</p>
                {topic.related.length > 0 && (
                  <p className="help-related">See also: {topic.related.map(id => getHelpTopic(id)?.title).filter(Boolean).join(', ')}</p>
                )}
              </article>
            ))}
          </section>
        </div>
      </div>
    </div>
  );
}

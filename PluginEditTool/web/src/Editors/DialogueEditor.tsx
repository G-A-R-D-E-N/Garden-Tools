import { useState } from 'react';
import { ChevronLeft } from 'lucide-react';
import HelpDrawer from './help/HelpDrawer';
import { SCREEN_TOPICS } from './help/helpContent';
import { useUndoStack } from './useUndoStack';

export default function DialogueEditor({ onBack }: { onBack: () => void }) {
  const [helpOpen, setHelpOpen] = useState(false);
  const { onKeyDown, scopeRef } = useUndoStack();
  return (
    <section ref={scopeRef} tabIndex={-1} className="editor-screen" aria-labelledby="dialogue-editor-title" onKeyDown={onKeyDown}>
      <header className="editor-screen-header">
        <button type="button" className="sidebar-action-btn" onClick={onBack}><ChevronLeft size={14} /> Back</button>
        <h3 id="dialogue-editor-title">Dialogue</h3>
        <button type="button" className="sidebar-action-btn" aria-expanded={helpOpen}
          onClick={() => setHelpOpen(open => !open)}>Help</button>
      </header>
      <p className="editor-purpose">
        Dialogue is what characters say. This screen will hold the topics of one quest, the responses an NPC gives
        under each topic, and the conditions that decide which response plays.
      </p>
      <div className="editor-empty">This editor is not ready yet. It will appear here in a later update.</div>
      {helpOpen && <HelpDrawer topicIds={SCREEN_TOPICS.dialogue} onClose={() => setHelpOpen(false)} />}
    </section>
  );
}

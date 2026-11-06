import { useState } from 'react';
import { ChevronLeft } from 'lucide-react';
import HelpDrawer from './help/HelpDrawer';
import { SCREEN_TOPICS } from './help/helpContent';
import { useUndoStack } from './useUndoStack';

export default function QuestEditor({ onBack }: { onBack: () => void }) {
  const [helpOpen, setHelpOpen] = useState(false);
  const { onKeyDown, scopeRef } = useUndoStack();
  return (
    <section ref={scopeRef} tabIndex={-1} className="editor-screen" aria-labelledby="quest-editor-title" onKeyDown={onKeyDown}>
      <header className="editor-screen-header">
        <button type="button" className="sidebar-action-btn" onClick={onBack}><ChevronLeft size={14} /> Back</button>
        <h3 id="quest-editor-title">Quest</h3>
        <button type="button" className="sidebar-action-btn" aria-expanded={helpOpen}
          onClick={() => setHelpOpen(open => !open)}>Help</button>
      </header>
      <p className="editor-purpose">
        A quest is a story task. This screen will hold its stages (checkpoints), objectives (goals the player sees)
        and aliases (named slots such as NPCs or places).
      </p>
      <div className="editor-empty">This editor is not ready yet. It will appear here in a later update.</div>
      {helpOpen && <HelpDrawer topicIds={SCREEN_TOPICS.quest} onClose={() => setHelpOpen(false)} />}
    </section>
  );
}

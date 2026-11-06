import { useState } from 'react';
import { ChevronLeft } from 'lucide-react';
import HelpDrawer from './help/HelpDrawer';
import { SCREEN_TOPICS } from './help/helpContent';
import { useUndoStack } from './useUndoStack';

export default function SceneEditor({ onBack }: { onBack: () => void }) {
  const [helpOpen, setHelpOpen] = useState(false);
  const { onKeyDown, scopeRef } = useUndoStack();
  return (
    <section ref={scopeRef} tabIndex={-1} className="editor-screen" aria-labelledby="scene-editor-title" onKeyDown={onKeyDown}>
      <header className="editor-screen-header">
        <button type="button" className="sidebar-action-btn" onClick={onBack}><ChevronLeft size={14} /> Back</button>
        <h3 id="scene-editor-title">Scene</h3>
        <button type="button" className="sidebar-action-btn" aria-expanded={helpOpen}
          onClick={() => setHelpOpen(open => !open)}>Help</button>
      </header>
      <p className="editor-purpose">
        A scene is a scripted sequence, such as actors talking or walking. This screen will show its phases as
        columns, its actors as rows and its actions as blocks.
      </p>
      <div className="editor-empty">This editor is not ready yet. It will appear here in a later update.</div>
      {helpOpen && <HelpDrawer topicIds={SCREEN_TOPICS.scene} onClose={() => setHelpOpen(false)} />}
    </section>
  );
}

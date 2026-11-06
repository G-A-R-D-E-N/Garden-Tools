import { useState } from 'react';
import { ChevronLeft } from 'lucide-react';
import HelpDrawer from './help/HelpDrawer';
import { SCREEN_TOPICS } from './help/helpContent';
import SceneWorkspace from './scene/SceneWorkspace';

export default function SceneEditor({ onBack }: { onBack: () => void }) {
  const [helpOpen, setHelpOpen] = useState(false);
  const [sceneKey, setSceneKey] = useState<string | null>(null);
  return (
    <section className="editor-screen" aria-labelledby="scene-editor-title">
      <header className="editor-screen-header">
        <button type="button" className="sidebar-action-btn" onClick={onBack}><ChevronLeft size={14} /> Back</button>
        <h3 id="scene-editor-title">Scene</h3>
        <button type="button" className="sidebar-action-btn" aria-expanded={helpOpen}
          onClick={() => setHelpOpen(open => !open)}>Help</button>
      </header>
      <p className="editor-purpose">
        A scene is a scripted sequence, such as actors talking or walking. Its phases run as columns, its actors as rows
        and its actions as blocks.
      </p>
      <SceneWorkspace key={sceneKey ?? 'none'} sceneKey={sceneKey} onPickScene={setSceneKey} />
      {helpOpen && <HelpDrawer topicIds={SCREEN_TOPICS.scene} onClose={() => setHelpOpen(false)} />}
    </section>
  );
}

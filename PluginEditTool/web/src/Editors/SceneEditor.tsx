import { useState } from 'react';
import { ChevronLeft } from 'lucide-react';
import type { PickedRecord } from '../RecordPicker';
import HelpDrawer from './help/HelpDrawer';
import { SCREEN_TOPICS } from './help/helpContent';
import SceneWorkspace from './scene/SceneWorkspace';
import WritableScope from './WritableScope';

export default function SceneEditor({ onBack }: { onBack: () => void }) {
  const [helpOpen, setHelpOpen] = useState(false);
  const [scene, setScene] = useState<PickedRecord | null>(null);
  return (
    <section className="editor-screen" aria-labelledby="scene-editor-title">
      <div className="editor-screen-body scene-screen-body">
        <header className="editor-screen-header">
          <button type="button" className="sidebar-action-btn" onClick={onBack}><ChevronLeft size={14} /> Back</button>
          <h3 id="scene-editor-title">Scene</h3>
          <button type="button" className="sidebar-action-btn" aria-expanded={helpOpen}
            onClick={() => setHelpOpen(open => !open)}>Help</button>
        </header>
        <p className="editor-purpose">
          Scenes run phases and actions between actors. Use the tabs to look at the whole scene, its timeline, phases,
          actors, actions and settings.
        </p>
        <WritableScope plugin={scene?.plugin ?? ''} formKey={scene?.formKey ?? ''}
          onCopied={target => { if (scene) setScene({ formKey: scene.formKey, plugin: target }); }}>
          <SceneWorkspace key={scene ? `${scene.plugin}|${scene.formKey}` : 'none'} sceneKey={scene?.formKey ?? null}
            plugin={scene?.plugin ?? ''} onPickScene={(formKey, plugin) => setScene({ formKey, plugin })} />
        </WritableScope>
      </div>
      {helpOpen && <HelpDrawer topicIds={SCREEN_TOPICS.scene} onClose={() => setHelpOpen(false)} />}
    </section>
  );
}

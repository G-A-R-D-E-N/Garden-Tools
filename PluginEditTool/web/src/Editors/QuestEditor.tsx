import { useState } from 'react';
import { ChevronLeft } from 'lucide-react';
import RecordPicker, { type PickedRecord } from '../RecordPicker';
import HelpDrawer from './help/HelpDrawer';
import { SCREEN_TOPICS } from './help/helpContent';
import QuestWorkspace from './quest/QuestWorkspace';

export default function QuestEditor({ onBack }: { onBack: () => void }) {
  const [helpOpen, setHelpOpen] = useState(false);
  const [picking, setPicking] = useState(false);
  const [quest, setQuest] = useState<PickedRecord | null>(null);
  return (
    <section className="editor-screen" aria-labelledby="quest-editor-title">
      <header className="editor-screen-header">
        <button type="button" className="sidebar-action-btn" onClick={onBack}><ChevronLeft size={14} /> Back</button>
        <h3 id="quest-editor-title">Quest</h3>
        <button type="button" className="sidebar-action-btn" onClick={() => setPicking(true)}>Choose quest</button>
        <button type="button" className="sidebar-action-btn" aria-expanded={helpOpen}
          onClick={() => setHelpOpen(open => !open)}>Help</button>
      </header>
      <p className="editor-purpose">
        A quest is a story task. Choose one quest, then edit its stages (checkpoints), objectives (goals the player
        sees), aliases (named slots such as NPCs or places) and script fragments.
      </p>
      {quest === null
        ? <div className="editor-empty">Choose a quest to start editing it. Nothing changes until you edit a field.</div>
        : <QuestWorkspace key={`${quest.plugin}|${quest.formKey}`} formKey={quest.formKey} plugin={quest.plugin} />}
      {picking && (
        <RecordPicker title="Quest" refType="Quest" refTypes="QUST"
          onPick={(formKey, plugin) => { setQuest({ formKey, plugin }); setPicking(false); }}
          onClose={() => setPicking(false)} />
      )}
      {helpOpen && <HelpDrawer topicIds={SCREEN_TOPICS.quest} onClose={() => setHelpOpen(false)} />}
    </section>
  );
}

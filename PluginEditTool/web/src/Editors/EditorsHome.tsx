import { useState } from 'react';
import { Clapperboard, Flag, MessageSquare, X } from 'lucide-react';
import QuestEditor from './QuestEditor';
import DialogueEditor from './DialogueEditor';
import SceneEditor from './SceneEditor';
import HelpPage from './help/HelpPage';
import { useTour } from './help/useTour';
import './Editors.css';

type EditorKind = 'quest' | 'dialogue' | 'scene';

const CARDS = [
  { kind: 'quest', title: 'Quest', text: 'Stages, objectives and aliases for one quest.', icon: <Flag size={24} strokeWidth={1.5} /> },
  { kind: 'dialogue', title: 'Dialogue', text: 'Topics and the responses an NPC gives, for one quest.', icon: <MessageSquare size={24} strokeWidth={1.5} /> },
  { kind: 'scene', title: 'Scene', text: 'Phases, actors and actions of one scripted scene.', icon: <Clapperboard size={24} strokeWidth={1.5} /> },
] as const;

export default function EditorsHome({ onClose }: { onClose: () => void }) {
  const [screen, setScreen] = useState<EditorKind | null>(null);
  const [helpOpen, setHelpOpen] = useState(false);
  const tour = useTour();
  const back = () => setScreen(null);

  return (
    <>
      <div className="papyrus-overlay" onClick={onClose}>
        <div className="papyrus-modal glass-panel editors-modal" role="dialog" aria-modal="true" aria-labelledby="editors-title"
          onClick={e => e.stopPropagation()}>
          <div className="papyrus-header">
            <h2 id="editors-title" className="papyrus-title">Editors</h2>
            <button type="button" className="sidebar-action-btn" onClick={() => setHelpOpen(true)}>Help page</button>
            <button type="button" className="papyrus-close" onClick={onClose} aria-label="Close editors"><X size={16} /></button>
          </div>
          {tour.active && (
            <div className="editors-tour" role="region" aria-label="Quick tour">
              <p>{tour.text}</p>
              <div className="editors-tour-actions">
                <button type="button" className="sidebar-action-btn" onClick={tour.skip}>Skip tour</button>
                <button type="button" className="sidebar-action-btn" onClick={tour.next}>{tour.last ? 'Done' : 'Next'}</button>
              </div>
            </div>
          )}
          {screen === null && (
            <div className="editors-cards">
              {CARDS.map(card => (
                <button key={card.kind} type="button" className="editors-card" onClick={() => setScreen(card.kind)}>
                  {card.icon}
                  <span className="editors-card-title">{card.title}</span>
                  <span className="editors-card-text">{card.text}</span>
                </button>
              ))}
            </div>
          )}
          {screen === 'quest' && <QuestEditor onBack={back} />}
          {screen === 'dialogue' && <DialogueEditor onBack={back} />}
          {screen === 'scene' && <SceneEditor onBack={back} />}
        </div>
      </div>
      {helpOpen && <HelpPage onClose={() => setHelpOpen(false)} />}
    </>
  );
}

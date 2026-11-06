import { useEffect, useState } from 'react';
import { ChevronLeft } from 'lucide-react';
import RecordPicker, { type PickedRecord } from '../../RecordPicker';
import { getBackend, type ActivePlugin } from '../../backend';
import { errorText } from '../../errors';
import { friendlyError, type FriendlyMessage } from '../friendlyErrors';
import HelpDrawer from '../help/HelpDrawer';
import HelpTip from '../help/HelpTip';
import { SCREEN_TOPICS } from '../help/helpContent';
import { ErrorNote } from '../quest/questControls';
import { buildQuestCalls, isLinked, runQuestCalls, type Answers, type FormKeys, type QuestRun } from './questCalls';
import type { PluginQuestion, QuestTemplate, RecordQuestion, WizardStep } from './templateTypes';
import { QUEST_TEMPLATES } from './templates/registry';
import './NewQuestWizard.css';

const PLUGIN_QUESTION: PluginQuestion = {
  id: 'plugin', kind: 'plugin', label: 'Which plugin should the quest go in?', help: 'wizard-plugin',
};

export default function NewQuestWizard({ onBack, onCreated }: {
  onBack: () => void;
  onCreated: (quest: PickedRecord) => void;
}) {
  const backend = getBackend();
  const [template, setTemplate] = useState<QuestTemplate | null>(null);
  const [step, setStep] = useState(0);
  const [answers, setAnswers] = useState<Answers>({});
  const [formKeys, setFormKeys] = useState<FormKeys>({});
  const [plugins, setPlugins] = useState<ActivePlugin[]>([]);
  const [picking, setPicking] = useState<RecordQuestion | null>(null);
  const [helpOpen, setHelpOpen] = useState(false);
  const [busy, setBusy] = useState(false);
  const [madeKey, setMadeKey] = useState<string | null>(null);
  const [error, setError] = useState<FriendlyMessage | null>(() => (backend ? null : { text: 'No backend bridge.' }));

  useEffect(() => {
    if (!backend) return;
    let live = true;
    (async () => {
      try {
        const list = JSON.parse(await backend.GetActivePlugins()) as ActivePlugin[];
        if (live) setPlugins(list.filter(plugin => plugin.Editable));
      } catch (e) {
        if (live) setError(friendlyError(errorText(e)));
      }
    })();
    return () => { live = false; };
  }, [backend]);

  const flow: WizardStep[] = template ? [PLUGIN_QUESTION, ...template.questions] : [];
  const current = flow[step];
  const answered = (question: WizardStep) => (answers[question.id] ?? '').trim() !== '';
  const setAnswer = (id: string, value: string) => setAnswers(prev => ({ ...prev, [id]: value }));

  const choose = (next: QuestTemplate) => {
    setTemplate(next);
    setStep(0);
    setAnswers({});
    setFormKeys({});
    setError(null);
    setMadeKey(null);
  };

  const chooseRecord = async (question: RecordQuestion, formKey: string) => {
    setPicking(null);
    if (!backend) return;
    try {
      const labels = JSON.parse(await backend.ResolveFormKeyLabels(formKey)) as Record<string, string>;
      setAnswer(question.id, labels[formKey] || formKey);
      setFormKeys(prev => ({ ...prev, [question.id]: formKey }));
    } catch (e) {
      setError(friendlyError(errorText(e)));
    }
  };

  const create = async () => {
    if (!backend || !template) return;
    setBusy(true);
    setError(null);
    setMadeKey(null);
    const run: QuestRun = { formKey: null };
    try {
      await runQuestCalls(backend, buildQuestCalls(template, answers, formKeys), run);
      onCreated({ formKey: run.formKey ?? '', plugin: answers.plugin });
    } catch (e) {
      const failure = friendlyError(errorText(e));
      setError(run.formKey
        ? { ...failure, text: `${failure.text} Part of the quest was made. Open it to see what is there.` }
        : failure);
      setMadeKey(run.formKey);
    } finally {
      setBusy(false);
    }
  };

  return (
    <section className="editor-screen" aria-labelledby="new-quest-title">
      <header className="editor-screen-header">
        <button type="button" className="sidebar-action-btn" onClick={onBack}><ChevronLeft size={14} /> Back</button>
        <h3 id="new-quest-title">New quest from template</h3>
        <button type="button" className="sidebar-action-btn" aria-expanded={helpOpen}
          onClick={() => setHelpOpen(open => !open)}>Help</button>
      </header>
      <p className="editor-purpose">
        Answer a few plain questions. The wizard builds a quest draft from your answers, then opens it in the Quest
        editor.
      </p>
      {error && <ErrorNote error={error} />}

      {template === null && (
        <div className="editors-cards">
          {QUEST_TEMPLATES.map(item => (
            <button key={item.id} type="button" className="editors-card" onClick={() => choose(item)}>
              <span className="editors-card-title">{item.title}</span>
              <span className="editors-card-text">{item.summary}</span>
            </button>
          ))}
        </div>
      )}

      {template !== null && current && (
        <div className="wizard-body">
          <p className="wizard-count">Question {step + 1} of {flow.length}</p>
          <p className="wizard-question">{current.label} <HelpTip topicId={current.help} /></p>
          {current.kind === 'text' && (
            <input className="wizard-input" aria-label={current.label} placeholder={current.placeholder}
              value={answers[current.id] ?? ''} onChange={e => setAnswer(current.id, e.target.value)} />
          )}
          {current.kind === 'plugin' && (
            <select aria-label={current.label} value={answers[current.id] ?? ''}
              onChange={e => setAnswer(current.id, e.target.value)}>
              <option value="">Choose a plugin</option>
              {plugins.map(plugin => <option key={plugin.Name} value={plugin.Name}>{plugin.Name}</option>)}
            </select>
          )}
          {current.kind === 'record' && (
            <div className="wizard-answer-row">
              <span>{answers[current.id] || 'Nothing chosen yet.'}</span>
              <button type="button" className="sidebar-action-btn" onClick={() => setPicking(current)}>
                {answers[current.id] ? 'Change' : 'Choose'}
              </button>
            </div>
          )}
          <div className="wizard-nav">
            <button type="button" className="sidebar-action-btn"
              onClick={() => (step === 0 ? setTemplate(null) : setStep(step - 1))}>Previous</button>
            <button type="button" className="sidebar-action-btn" disabled={!answered(current)}
              onClick={() => setStep(step + 1)}>Next</button>
          </div>
        </div>
      )}

      {template !== null && step === flow.length && (
        <div className="wizard-body">
          <p className="editor-purpose">
            Check the answers. The wizard writes the internal name, the journal lines, the objectives and the alias
            names. It makes the quest start by itself with the game, at its first stage. It links each NPC and
            location you picked to its alias. It places each item you picked at the location as a new copy. A record
            marked not linked still has to be set in the Creation Kit.
          </p>
          <dl className="wizard-review">
            {flow.map(question => (
              <div key={question.id}>
                <dt>{question.label}</dt>
                <dd>
                  {answers[question.id]}
                  {question.kind === 'record' && !isLinked(template, question.id) && ' (not linked: set it in the Creation Kit)'}
                </dd>
              </div>
            ))}
          </dl>
          <div className="wizard-nav">
            <button type="button" className="sidebar-action-btn" disabled={busy}
              onClick={() => setStep(step - 1)}>Previous</button>
            {madeKey === null
              ? <button type="button" className="sidebar-action-btn" disabled={busy} onClick={create}>Create quest</button>
              : <button type="button" className="sidebar-action-btn"
                  onClick={() => onCreated({ formKey: madeKey, plugin: answers.plugin })}>Open the quest anyway</button>}
          </div>
        </div>
      )}

      {picking && (
        <RecordPicker title={picking.label} refType={picking.recordType} refTypes={picking.recordType}
          onPick={formKey => { void chooseRecord(picking, formKey); }}
          onClose={() => setPicking(null)} />
      )}
      {helpOpen && <HelpDrawer topicIds={SCREEN_TOPICS.wizard} onClose={() => setHelpOpen(false)} />}
    </section>
  );
}

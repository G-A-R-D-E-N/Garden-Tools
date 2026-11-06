import { useEffect, useRef, useState } from 'react';
import { getBackend } from '../../backend';
import { errorText } from '../../errors';
import { friendlyError, type FriendlyMessage } from '../friendlyErrors';
import type { UndoEntry } from '../useUndoStack';
import { readQuest, writeAliases, writeObjectives, writeStages, type QuestSnapshot } from './questBackend';
import type { QuestAlias, QuestObjective, QuestStage, StageEdit } from './questModel';

export function useQuestRecord(plugin: string, formKey: string, push: (entry: UndoEntry) => void) {
  const backend = getBackend();
  const [snapshot, setSnapshot] = useState<QuestSnapshot | null>(null);
  const [error, setError] = useState<FriendlyMessage | null>(() => (backend ? null : { text: 'No backend bridge.' }));
  const latestStages = useRef<QuestStage[]>([]);

  useEffect(() => {
    if (!backend) return;
    let live = true;
    readQuest(backend, plugin, formKey).then(
      loaded => {
        if (!live) return;
        latestStages.current = loaded.stages;
        setSnapshot(loaded);
      },
      (e: unknown) => { if (live) setError(friendlyError(errorText(e))); });
    return () => { live = false; };
  }, [backend, plugin, formKey]);

  const run = (job: () => Promise<unknown>, onDone: () => void) => {
    setError(null);
    job().then(onDone, (e: unknown) => setError(friendlyError(errorText(e))));
  };

  const commit = <T,>(label: string, prev: T, next: T, write: (value: T) => Promise<unknown>,
                       store: (value: T) => void) => {
    const apply = (value: T) => run(() => write(value), () => store(value));
    run(() => write(next), () => {
      store(next);
      push({ label, undo: () => apply(prev), redo: () => apply(next) });
    });
  };

  const storeField = <K extends keyof QuestSnapshot>(key: K) => (value: QuestSnapshot[K]) =>
    setSnapshot(s => (s ? { ...s, [key]: value } : s));

  const editStages = (edit: StageEdit, label: string) => {
    if (!backend || !snapshot) return;
    const prev = latestStages.current;
    const next = edit(prev);
    latestStages.current = next;
    const store = (value: QuestStage[]) => {
      latestStages.current = value;
      storeField('stages')(value);
    };
    commit(label, prev, next, value => writeStages(backend, plugin, formKey, value), store);
  };

  const commitStages = (next: QuestStage[], label: string) => editStages(() => next, label);

  const commitObjectives = (next: QuestObjective[], label: string) => {
    if (!backend || !snapshot) return;
    commit(label, snapshot.objectives, next, value => writeObjectives(backend, plugin, formKey, value), storeField('objectives'));
  };

  const commitAliases = (next: QuestAlias[], label: string) => {
    if (!backend || !snapshot) return;
    commit(label, snapshot.aliases, next, value => writeAliases(backend, plugin, formKey, value), storeField('aliases'));
  };

  return { snapshot, error, commitStages, editStages, commitObjectives, commitAliases };
}

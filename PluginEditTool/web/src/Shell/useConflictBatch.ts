import { useEffect, useState } from 'react';
import type { ConflictEntry } from '../backend';
import { errorMessage, errorText } from '../errors';
import { useDialogs } from '../useDialogs';

export function useConflictBatch({ activeTab, treeMode, setStatus, setErrorStr }: {
  activeTab: string;
  treeMode: 'files' | 'conflicts';
  setStatus: (message: string) => void;
  setErrorStr: (message: string) => void;
}) {
  const { pickPlugin: askForTarget, confirm: askConfirm } = useDialogs();
  const [conflicts, setConflicts] = useState<ConflictEntry[] | null>(null);
  const [conflictSel, setConflictSel] = useState<Record<string, boolean>>({});
  const [conflictBusy, setConflictBusy] = useState(false);

  const loadConflicts = () => window.chrome?.webview?.hostObjects?.backend?.GetConflicts()
    .then(raw => setConflicts(JSON.parse(raw)))
    .catch(e => { setErrorStr(errorText(e)); setConflicts([]); });

  useEffect(() => {
    if (activeTab === 'explorer' && treeMode === 'conflicts' && conflicts === null) loadConflicts();
  }, [activeTab, treeMode]);

  const batchCopyAsOverride = async () => {
    const b = window.chrome?.webview?.hostObjects?.backend;
    if (!b || !conflicts) return;
    const picked = conflicts.filter(c => conflictSel[c.FormKey]);
    if (picked.length === 0) { setStatus('Select at least one conflict first.'); return; }
    const chosen = await askForTarget({
      title: `Copy ${picked.length} record(s) as override`,
      description: 'Every selected conflict is copied into this plugin as an override. It still has to be saved and enabled last in the load order.',
      confirmLabel: 'Copy',
      defaultTarget: 'ConflictPatch.esp',
    });
    if (!chosen) return;
    const patch = chosen.target;
    setConflictBusy(true);
    try {
      const items = picked.map(c => ({ formKey: c.FormKey, source: c.Winner }));
      let overwrite = false;
      let allowLosing = false;
      let res = JSON.parse(await b.CopyAsOverrideMany(JSON.stringify(items), patch, overwrite, allowLosing));
      if (res.requiresOverwrite) {
        const existing: { formKey: string; editorId?: string }[] = Array.isArray(res.existing) ? res.existing : [];
        const sample = existing.slice(0, 5).map(x => `${x.editorId || x.formKey} (${x.formKey})`).join('\n');
        const more = existing.length > 5 ? `\n...and ${existing.length - 5} more.` : '';
        const proceed = await askConfirm({
          title: 'Overwrite existing overrides?',
          message: `${existing.length} of the ${res.total} selected records already exist in ${patch}. ` +
            `Nothing has been copied yet.\n\n${sample}${more}`,
          confirmLabel: 'Overwrite and copy',
          danger: true,
        });
        if (!proceed) {
          setStatus(`Batch copy cancelled -- ${patch} was left unchanged.`);
          return;
        }
        overwrite = true;
        res = JSON.parse(await b.CopyAsOverrideMany(JSON.stringify(items), patch, overwrite, allowLosing));
      }
      if (res.requiresLoadOrderConfirmation) {
        const losing: { formKey: string; loadsAfter?: string[] }[] =
          Array.isArray(res.losing) ? res.losing : [];
        const sample = losing.slice(0, 5)
          .map(x => `${x.formKey} (loses to ${(x.loadsAfter ?? []).join(', ')})`).join('\n');
        const more = losing.length > 5 ? `\n...and ${losing.length - 5} more.` : '';
        const proceed = await askConfirm({
          title: 'Overrides will not win',
          message: `${patch} loads before other plugins that override ${losing.length} of the ` +
            `${res.total} selected records, so those copies will lose until ${patch} loads after them. ` +
            `Nothing has been copied yet.\n\n${sample}${more}`,
          confirmLabel: 'Copy anyway',
        });
        if (!proceed) {
          setStatus(`Batch copy cancelled -- ${patch} was left unchanged.`);
          return;
        }
        allowLosing = true;
        res = JSON.parse(await b.CopyAsOverrideMany(JSON.stringify(items), patch, overwrite, allowLosing));
      }
      const firstFailure = Array.isArray(res.failures) && res.failures.length ? res.failures[0]?.reason : '';
      const failureNote = res.failed
        ? ` ${res.failed} failed${firstFailure ? `: ${firstFailure}` : ''}.`
        : '';
      setStatus(`Copied ${res.ok}/${res.total} into ${patch}.${failureNote} Save ${patch} from a record's context menu, then enable it last.`);
      if (!res.failed) setConflictSel({});
    } catch (e) {
      setStatus('Batch copy failed: ' + (errorMessage(e) ?? String(e)));
    } finally { setConflictBusy(false); }
  };

  return {
    conflicts, setConflicts, conflictSel, setConflictSel, conflictBusy, loadConflicts, batchCopyAsOverride,
  };
}

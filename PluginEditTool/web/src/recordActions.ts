import type { ConflictFieldRow, ConflictMatrix } from './backend';
import { useDialogs } from './dialogs';
import type { CompareRow } from './PluginCompareDialog';

const errorText = (e: unknown) => (e as { message?: string } | null)?.message || String(e);

export const back = () => window.chrome?.webview?.hostObjects?.backend;

export function useRecordActions({ matrix, editablePlugins, closeMenu, setStatus, onReload, onPluginsChanged }: {
  matrix: ConflictMatrix;
  editablePlugins: string[];
  closeMenu: () => void;
  setStatus: (status: string) => void;
  onReload: () => Promise<void>;
  onPluginsChanged?: () => void;
}) {
  const { confirm: askConfirm, prompt: askPrompt, pickPlugin: askForTarget, compare: showCompare } = useDialogs();

  const copyAsOverrideInto = async (col: number) => {
    closeMenu();
    const picked = await askForTarget({
      title: 'Copy as override into',
      description: `Copies ${matrix.EditorID || matrix.FormKey} from ${matrix.Plugins[col]} into the plugin you choose, as an override.`,
      confirmLabel: 'Copy', defaultTarget: editablePlugins[0] ?? '',
    });
    if (picked) await copyInto(col, picked.target);
  };

  const copyAsDisabledOverrideInto = async (col: number) => {
    closeMenu();
    const b = back();
    if (!b) return;
    const picked = await askForTarget({
      title: 'Copy as disabled override into',
      description: `Copies ${matrix.EditorID || matrix.FormKey} from ${matrix.Plugins[col]} as an override and marks the copy Initially Disabled.`,
      confirmLabel: 'Copy', defaultTarget: editablePlugins[0] ?? '',
    });
    if (!picked) return;
    setStatus('Copying…');
    try {
      const msg = await b.CopyAsDisabledOverride(matrix.Plugins[col], matrix.FormKey, picked.target, false);
      setStatus(msg.startsWith('LOSES:') || msg.startsWith('EXISTS:')
        ? `Not copied -- ${msg.slice(msg.indexOf(':') + 1).trim()}` : msg);
      await onReload();
    } catch (e) { setStatus('Copy failed: ' + errorText(e)); }
  };

  const copyInto = async (col: number, target: string) => {
    closeMenu();
    const b = back();
    if (!b) return;
    setStatus('Copying…');
    try {
      let allowLosing = false;
      let msg = await b.CopyAsOverride(matrix.Plugins[col], matrix.FormKey, target, false);

      if (msg.startsWith('LOSES:')) {
        const proceed = await askConfirm({
          title: 'Override will not win',
          message: msg.slice('LOSES:'.length).trim(),
          confirmLabel: 'Copy anyway',
        });
        if (!proceed) { setStatus(`Copy cancelled -- ${target} was left unchanged.`); return; }
        allowLosing = true;
        msg = await b.CopyAsOverride(matrix.Plugins[col], matrix.FormKey, target, false, true);
      }

      if (msg.startsWith('EXISTS:')) {
        const proceed = await askConfirm({
          title: 'Record already exists in target plugin',
          message: msg.slice('EXISTS:'.length).trim(),
          confirmLabel: 'Overwrite',
          danger: true,
        });
        if (!proceed) { setStatus('Copy cancelled -- the existing override was left alone.'); return; }
        msg = await b.CopyAsOverride(matrix.Plugins[col], matrix.FormKey, target, true, allowLosing);
      }
      setStatus(msg);


      await onReload();
      onPluginsChanged?.();
    }
    catch (e) { setStatus('Copy failed: ' + errorText(e)); }
  };

  const savePlugin = async (col: number) => {
    closeMenu();
    const b = back();
    if (!b) return;
    setStatus('Saving plugin…');
    try { setStatus(await b.SavePlugin(matrix.Plugins[col], '')); }
    catch (e) { setStatus('Save failed: ' + errorText(e)); }
  };

  const deleteRecord = async (col: number) => {
    closeMenu();
    const b = back();
    if (!b) return;
    const plug = matrix.Plugins[col];
    if (!await askConfirm({ title: 'Remove record', danger: true, confirmLabel: 'Remove',
      message: `Remove ${matrix.EditorID || matrix.FormKey} from ${plug}?` })) return;
    setStatus('Deleting…');
    try { setStatus(await b.DeleteRecord(plug, matrix.FormKey)); await onReload(); }
    catch (e) { setStatus('Delete failed: ' + errorText(e)); }
  };

  const renumberFormId = async (col: number) => {
    closeMenu();
    const b = back();
    if (!b) return;
    const plug = matrix.Plugins[col];
    const cur = matrix.FormKey.split(':')[0];
    const next = await askPrompt({
      title: 'Change FormID',
      description: `Renumbers ${matrix.EditorID || matrix.FormKey} in ${plug}.`,
      label: 'New FormID (6-digit hex)', defaultValue: cur, placeholder: '001000',
      validate: v => /^[0-9a-fA-F]{1,6}$/.test(v.trim()) ? null : 'Six hex digits or fewer, e.g. 001000.',
    });
    if (!next) return;
    setStatus('Renumbering…');
    try { setStatus(await b.RenumberFormId(plug, matrix.FormKey, next)); await onReload(); }
    catch (e) { setStatus('Renumber failed: ' + errorText(e)); }
  };

  const compactEsl = async (col: number) => {
    closeMenu();
    const b = back();
    if (!b) return;
    if (!await askConfirm({ title: 'Compact to ESL', danger: true, confirmLabel: 'Compact',
      message: `Compact ${matrix.Plugins[col]} to ESL?\n\nThis renumbers every record's FormID into the 0x800-0xFFF range and can break references from other plugins.` })) return;
    setStatus('Compacting…');
    try { setStatus(await b.CompactToEsl(matrix.Plugins[col])); await onReload(); }
    catch (e) { setStatus('Compact failed: ' + errorText(e)); }
  };

  const cleanMasters = async (col: number) => {
    closeMenu();
    const b = back();
    if (!b) return;
    const plug = matrix.Plugins[col];
    try {
      const preview = await b.CleanUnusedMasters(plug, false);
      if (!preview.startsWith('DRY RUN:')) { setStatus(preview); return; }
      if (!await askConfirm({ title: 'Clean Masters', confirmLabel: 'Remove unused',
        message: preview.replace('DRY RUN: would remove', 'Remove') + '\n\nSave Plugin afterwards to write the change.' })) return;
      setStatus('Cleaning masters…');
      setStatus(await b.CleanUnusedMasters(plug, true));
      await onReload();
    } catch (e) { setStatus('Clean masters failed: ' + errorText(e)); }
  };

  const cleanPlugin = async (col: number) => {
    closeMenu();
    const b = back();
    if (!b) return;
    if (!await askConfirm({ title: 'Clean (UDR)', confirmLabel: 'Clean',
      message: `Clean (UDR) ${matrix.Plugins[col]}?\n\nThis undeletes and disables every deleted record in the plugin.` })) return;
    setStatus('Cleaning…');
    try { setStatus(await b.CleanPlugin(matrix.Plugins[col])); await onReload(); }
    catch (e) { setStatus('Clean failed: ' + errorText(e)); }
  };


  const copyAsNewRecord = async (col: number) => {
    closeMenu();
    const b = back();
    if (!b) return;
    const picked = await askForTarget({
      title: 'Copy as new record',
      description: `Duplicates ${matrix.EditorID || matrix.FormKey} under a fresh FormID in the plugin you choose.`,
      confirmLabel: 'Duplicate',
      defaultTarget: editablePlugins[0] ?? '',
      extraField: {
        label: 'EditorID for the new record',
        defaultValue: (matrix.EditorID || 'NewRecord') + 'DUP',
        placeholder: 'MyNewRecord',
      },
    });
    if (!picked) return;
    const { target, extra: edid } = picked;
    setStatus('Duplicating…');
    try { setStatus(await b.CopyAsNewRecord(matrix.Plugins[col], matrix.FormKey, target, edid)); await onReload(); }
    catch (e) { setStatus('Copy as new record failed: ' + errorText(e)); }
  };

  const deepCopyAsOverride = async (col: number) => {
    closeMenu();
    const b = back();
    if (!b) return;
    if (matrix.Type === 'Cell' || matrix.Type === 'Worldspace') {
      setStatus(`${matrix.Type} does not support deep copy here -- copy it with Copy as Override and its placed objects individually.`);
      return;
    }
    const picked = await askForTarget({
      title: 'Deep copy as override',
      description: 'Copies the record and the content it references into the plugin you choose. You get a dry run to confirm first.',
      confirmLabel: 'Dry run',
      defaultTarget: editablePlugins[0] ?? 'ConflictPatch.esp',
    });
    if (!picked) return;
    const target = picked.target;
    setStatus('Scanning references…');
    try {
      const preview = await b.DeepCopyAsOverride(matrix.Plugins[col], matrix.FormKey, target, false, false);
      setStatus(preview);
      if (!preview.startsWith('DRY RUN')) return;
      if (!await askConfirm({ title: 'Confirm', message: preview, confirmLabel: 'Proceed' })) return;
      let allowLosing = false;
      let result = await b.DeepCopyAsOverride(matrix.Plugins[col], matrix.FormKey, target, true, false);
      if (result.startsWith('LOSES:')) {
        const proceed = await askConfirm({
          title: 'Overrides will not win',
          message: result.slice('LOSES:'.length).trim(),
          confirmLabel: 'Copy anyway',
        });
        if (!proceed) { setStatus('Deep copy cancelled -- the target plugin was left unchanged.'); return; }
        allowLosing = true;
        result = await b.DeepCopyAsOverride(matrix.Plugins[col], matrix.FormKey, target, true, false, 200, true);
      }
      if (result.startsWith('EXISTS:')) {
        const proceed = await askConfirm({
          title: 'Deep copy would replace existing overrides',
          message: result.slice('EXISTS:'.length).trim(),
          confirmLabel: 'Overwrite all and copy',
          danger: true,
        });
        if (!proceed) {
          setStatus('Deep copy cancelled -- the target plugin was left unchanged.');
          return;
        }
        result = await b.DeepCopyAsOverride(matrix.Plugins[col], matrix.FormKey, target, true, true, 200, allowLosing);
      }
      setStatus(result);
      await onReload();
    } catch (e) { setStatus('Deep copy failed: ' + errorText(e)); }
  };

  const changeReferencingRecords = async () => {
    closeMenu();
    const b = back();
    if (!b) return;
    const to = await askPrompt({
      title: 'Change referencing records',
      description: `Every record that references ${matrix.EditorID || matrix.FormKey} will point at this instead.`,
      label: 'Replacement record (FormKey or EditorID)', placeholder: '000800:MyPlugin.esp',
      validate: v => v.trim().length > 0 ? null : 'Enter a FormKey or EditorID.',
    });
    if (!to) return;
    const targetPick = await askForTarget({
      title: 'Write repointed records into',
      description: 'The rewritten records are written here as overrides.',
      confirmLabel: 'Dry run', defaultTarget: editablePlugins[0] ?? 'ConflictPatch.esp',
    });
    const target = targetPick?.target ?? '';
    if (!target) return;
    setStatus('Finding referencing records…');
    try {
      const preview = await b.ChangeReferencingRecords(matrix.FormKey, to, target, false);
      setStatus(preview);
      if (!preview.startsWith('DRY RUN')) return;
      if (!await askConfirm({ title: 'Confirm', message: preview, confirmLabel: 'Proceed' })) return;
      setStatus(await b.ChangeReferencingRecords(matrix.FormKey, to, target, true));
      await onReload();
    } catch (e) { setStatus('Change referencing records failed: ' + errorText(e)); }
  };



  const removeItm = async (col: number) => {
    closeMenu();
    const b = back();
    if (!b) return;
    const plug = matrix.Plugins[col];
    setStatus('Scanning for identical-to-master records…');
    try {
      const preview = await b.RemoveIdenticalToMaster(plug, false);
      setStatus(preview);
      if (!preview.startsWith('DRY RUN')) return;
      if (!await askConfirm({ title: 'Remove from selected records', danger: true, confirmLabel: 'Remove',
        message: `${preview}\n\nRemove them from ${plug}?` })) return;
      setStatus(await b.RemoveIdenticalToMaster(plug, true));
      await onReload();
    } catch (e) { setStatus('Remove ITM failed: ' + errorText(e)); }
  };

  const addMasters = async (col: number) => {
    closeMenu();
    const b = back();
    if (!b) return;
    const plug = matrix.Plugins[col];
    const list = await askPrompt({
      title: 'Add masters',
      description: `Declares master files on ${plug}.`,
      label: 'Plugin filenames, comma separated', placeholder: 'Fallout4.esm, MyMod.esp',
      validate: v => v.trim().length > 0 ? null : 'Enter at least one plugin filename.',
    });
    if (!list) return;
    try { setStatus(await b.AddMasters(plug, list)); await onReload(); }
    catch (e) { setStatus('Add masters failed: ' + errorText(e)); }
  };



  const renumberPlugin = async (col: number) => {
    closeMenu();
    const b = back();
    if (!b) return;
    const plug = matrix.Plugins[col];
    const start = await askPrompt({
      title: 'Renumber FormIDs',
      description: `Renumbers every record in ${plug}. You get a dry run to confirm first.`,
      label: 'Starting object id (hex)', defaultValue: '001000',
      validate: v => /^[0-9a-fA-F]{1,6}$/.test(v.trim()) ? null : 'Six hex digits or fewer, e.g. 001000.',
    });
    if (!start) return;
    try {
      const preview = await b.RenumberPluginFormIds(plug, start, false);
      setStatus(preview);
      if (!preview.startsWith('DRY RUN')) return;
      if (!await askConfirm({ title: 'Confirm', message: preview, confirmLabel: 'Proceed' })) return;
      setStatus(await b.RenumberPluginFormIds(plug, start, true));
      await onReload();
    } catch (e) { setStatus('Renumber failed: ' + errorText(e)); }
  };

  const createSeq = async (col: number) => {
    closeMenu();
    const b = back();
    if (!b) return;
    try { setStatus(await b.CreateSeqFile(matrix.Plugins[col], '')); }
    catch (e) { setStatus('Create SEQ failed: ' + errorText(e)); }
  };

  const compareTo = async (col: number) => {
    closeMenu();
    const b = back();
    if (!b) return;
    const plug = matrix.Plugins[col];
    const path = await askPrompt({
      title: 'Compare to...',
      description: `Diffs ${plug} against another copy of it. The other file is only read.`,
      label: 'Path to the other .esp/.esm/.esl', placeholder: 'C:\\Backups\\MyMod.esp',
      validate: v => v.trim().length > 0 ? null : 'Enter a file path.',
    });
    if (!path) return;
    const otherPath = path.trim();
    try {
      const rows: CompareRow[] = JSON.parse(await b.ComparePluginTo(plug, otherPath));
      const count = (c: string) => rows.filter(r => r.Change === c).length;
      setStatus(rows.length === 0 ? `${plug} matches ${otherPath}.`
        : `${count('changed')} changed, ${count('added')} added, ${count('removed')} removed vs ${otherPath}.`);
      await showCompare({
        plugin: plug, otherPath, rows,
        loadRecord: async formKey => JSON.parse(await b.ComparePluginTo(plug, otherPath, formKey)),
      });
    } catch (e) { setStatus('Compare failed: ' + errorText(e)); }
  };

  const checkForErrors = async (col: number) => {
    closeMenu();
    const b = back();
    if (!b) return;
    const plug = matrix.Plugins[col];
    setStatus(`Checking ${plug} for errors…`);
    try {
      const rows: { Code: string; Severity: string; FormKey: string; Message: string }[] =
        JSON.parse(await b.CheckForErrors(plug, 500));
      const errors = rows.filter(r => r.Severity === 'error').length;
      setStatus(rows.length === 0 ? `No errors found in ${plug}.`
        : `${errors} error(s), ${rows.length - errors} warning(s) in ${plug}:\n`
          + rows.slice(0, 20).map(r => `${r.Severity} ${r.Code} ${r.FormKey}: ${r.Message}`).join('\n')
          + (rows.length > 20 ? `\n…and ${rows.length - 20} more` : ''));
    } catch (e) { setStatus('Check failed: ' + errorText(e)); }
  };

  const lootReport = async (col: number) => {
    closeMenu();
    const b = back();
    if (!b) return;
    try { setStatus(await b.LootCleaningReport(matrix.Plugins[col])); }
    catch (e) { setStatus('LOOT report failed: ' + errorText(e)); }
  };

  const checkCircularLeveled = async (col: number) => {
    closeMenu();
    const b = back();
    if (!b) return;
    setStatus('Checking leveled lists…');
    try { setStatus(await b.CheckCircularLeveledLists(matrix.Plugins[col])); }
    catch (e) { setStatus('Check failed: ' + errorText(e)); }
  };

  const copyText = (what: string, text: string) => {
    closeMenu();
    void navigator.clipboard?.writeText(text);
    setStatus(`Copied ${what}: ${text}`);
  };

  const elementAction = async (
    kind: 'add' | 'remove' | 'up' | 'down' | 'clear',
    r: ConflictFieldRow, col: number, template = '',
  ) => {
    closeMenu();
    const b = back();
    if (!b) return;
    const plug = matrix.Plugins[col];
    try {
      let msg: string;
      switch (kind) {
        case 'add':    msg = await b.AddElement(plug, matrix.FormKey, r.Field, template); break;
        case 'remove': msg = await b.RemoveElement(plug, matrix.FormKey, r.Field); break;
        case 'up':     msg = await b.MoveElement(plug, matrix.FormKey, r.Field, -1); break;
        case 'down':   msg = await b.MoveElement(plug, matrix.FormKey, r.Field, 1); break;
        case 'clear':
          if (!await askConfirm({ title: 'Clear list', danger: true, confirmLabel: 'Clear',
            message: `Clear every entry from ${r.DisplayLabel || r.Field} in ${plug}?` })) return;
          msg = await b.ClearElement(plug, matrix.FormKey, r.Field); break;
      }
      setStatus(msg);
      await onReload();
    } catch (e) { setStatus('Failed: ' + errorText(e)); }
  };

  const askImpact = () => {
    closeMenu();
    const id = matrix.EditorID || matrix.FormKey;
    const prompt =
      `What breaks if I change or renumber the record ${id} [${matrix.FormKey}] (${matrix.Type})? ` +
      `Use get_referenced_by on ${matrix.FormKey} to list every record and plugin that references it, ` +
      `then tell me which would break and how to patch them.`;
    window.dispatchEvent(new CustomEvent('fo4:ask-ai', { detail: prompt }));
    setStatus('Asked Claude about impact. See the chat panel.');
  };

  return {
    copyAsOverrideInto,
    copyInto,
    savePlugin,
    deleteRecord,
    renumberFormId,
    compactEsl,
    cleanMasters,
    cleanPlugin,
    copyAsNewRecord,
    deepCopyAsOverride,
    changeReferencingRecords,
    removeItm,
    addMasters,
    renumberPlugin,
    createSeq,
    checkCircularLeveled,
    compareTo,
    checkForErrors,
    lootReport,
    copyAsDisabledOverrideInto,
    copyText,
    elementAction,
    askImpact,
  };
}

import { useState } from 'react';
import { UserCheck } from 'lucide-react';
import type { HkxHost } from './backend';
import {
  describeCharacter, describeFinding, describeRepair, describeSave, describeStep, isCleanCharacter, readCharacterCheck,
  readCharacterSave, readCharacterSavePlan,
  type HkxCharacterPaths, type HkxCharacterReport, type HkxCharacterSavePlan, type HkxCharacterSaveResult,
  type HkxCharacterSaveTargets,
} from './hkxCharacterClient';

const messageOf = (e: unknown) => (e instanceof Error ? e.message : String(e));

const Lines = ({ label, items, warn }: { label: string; items: string[]; warn?: boolean }) =>
  items.length > 0 ? (
    <ul className={warn ? 'papyrus-warn' : 'nif-flabel'} role="list" aria-label={label}>
      {items.map((item, i) => <li key={`${i}:${item}`}>{item}</li>)}
    </ul>
  ) : null;

interface Props {
  host?: HkxHost;
  disabled: boolean;
}

const emptyPaths: HkxCharacterPaths = {
  skeleton: '', ragdoll: '', mapper: '', bodyNif: '', skeletonNif: '', projectHkx: '', dataFolder: '', mo2Instance: '', mo2Profile: '',
};
const emptyTargets: HkxCharacterSaveTargets = {
  skeletonOut: '', ragdollOut: '', mapperOut: '', skin: '', skinOut: '', edits: '', ragdollEdits: '', mapperEdits: '',
  mapperIndex: '', skinEdits: '', overwrite: false,
};

const trimAll = <T extends object>(value: T): T =>
  Object.fromEntries(Object.entries(value).map(([k, v]) => [k, typeof v === 'string' ? v.trim() : v])) as T;

export default function HkxCharacterTools({ host, disabled }: Props) {
  const [paths, setPaths] = useState<HkxCharacterPaths>(emptyPaths);
  const [targets, setTargets] = useState<HkxCharacterSaveTargets>(emptyTargets);
  const [report, setReport] = useState<HkxCharacterReport | null>(null);
  const [plan, setPlan] = useState<HkxCharacterSavePlan | null>(null);
  const [planKey, setPlanKey] = useState('');
  const [saved, setSaved] = useState<HkxCharacterSaveResult | null>(null);
  const [notice, setNotice] = useState('');
  const [error, setError] = useState('');
  const [busy, setBusy] = useState(false);

  const off = disabled || busy;
  const trimmed = trimAll(paths);
  const clean = trimAll(targets);
  const currentKey = JSON.stringify([trimmed, clean]);
  const planCurrent = plan !== null && plan.ok && planKey === currentKey;
  const setPath = (key: keyof HkxCharacterPaths) => (e: { target: { value: string } }) =>
    setPaths(p => ({ ...p, [key]: e.target.value }));
  const setTarget = (key: keyof HkxCharacterSaveTargets) => (e: { target: { value: string } }) =>
    setTargets(t => ({ ...t, [key]: e.target.value }));

  const run = async (work: () => Promise<string>) => {
    if (!host) return;
    setBusy(true);
    setError('');
    setNotice('');
    try {
      setNotice(await work());
    } catch (e) {
      setError(messageOf(e));
    } finally {
      setBusy(false);
    }
  };

  const check = () => run(async () => {
    setReport(null);
    const found = await readCharacterCheck(host!, trimmed);
    setReport(found);
    return describeCharacter(found);
  });

  const planSave = () => run(async () => {
    setPlan(null);
    setSaved(null);
    const found = await readCharacterSavePlan(host!, trimmed, { ...clean, edits: targets.edits });
    setPlan(found);
    setPlanKey(currentKey);
    return found.ok ? `Save plan: ${found.steps.length} step(s), nothing written` : 'Save plan refused, nothing written';
  });

  const save = () => run(async () => {
    const result = await readCharacterSave(host!, trimmed, clean, plan);
    setSaved(result);
    setPlan(null);
    setPlanKey('');
    if (!result.saved) throw new Error(describeSave(result));
    return describeSave(result);
  });

  return (
    <div className="nif-flags">
      <div className="nif-flags-head">
        <span className="nif-flabel"><UserCheck size={12} /> Character workspace</span>
        <input className="nif-in" value={paths.skeleton} disabled={off} aria-label="Character skeleton hkx"
               placeholder="Rig file (.hkx) of the character" onChange={setPath('skeleton')} />
        <input className="nif-in" value={paths.ragdoll} disabled={off} aria-label="Character ragdoll hkx"
               placeholder="Ragdoll .hkx (optional, empty means the skeleton file)" onChange={setPath('ragdoll')} />
        <input className="nif-in" value={paths.mapper} disabled={off} aria-label="Character mapper hkx"
               placeholder="Mapper file (.hkx, optional, empty means the ragdoll or skeleton file)" onChange={setPath('mapper')} />
        <input className="nif-in" value={paths.bodyNif} disabled={off} aria-label="Character body nif"
               placeholder="Skinned body .nif (optional)" onChange={setPath('bodyNif')} />
        <input className="nif-in" value={paths.skeletonNif} disabled={off} aria-label="Character skeleton nif"
               placeholder="Rig .nif for helper bones (optional)" onChange={setPath('skeletonNif')} />
        <input className="nif-in" value={paths.projectHkx} disabled={off} aria-label="Character project hkx"
               placeholder="Project file for the clip links (optional, Data-relative, e.g. Actors/Foo/Foo.hkx)" onChange={setPath('projectHkx')} />
        <input className="nif-in" value={paths.dataFolder} disabled={off} aria-label="Character data folder"
               placeholder="Data folder for the clip links (needs the project file)" onChange={setPath('dataFolder')} />
        <input className="nif-in" value={paths.mo2Instance} disabled={off} aria-label="Character MO2 instance"
               placeholder="MO2 instance folder (optional, instead of the data folder)" onChange={setPath('mo2Instance')} />
        <input className="nif-in" value={paths.mo2Profile} disabled={off} aria-label="Character MO2 profile"
               placeholder="MO2 profile (optional)" onChange={setPath('mo2Profile')} />
        <button className="sidebar-action-btn" disabled={off || !trimmed.skeleton} onClick={() => void check()}>
          Check character
        </button>
      </div>

      {report ? (
        <>
          <div className={isCleanCharacter(report) ? 'nif-flabel' : 'papyrus-warn'} aria-label="Character verdict">
            {isCleanCharacter(report) ? 'Clean' : 'Not clean'}
          </div>
          <div className="nif-flabel" aria-label="Character skeleton summary">
            {report.skeleton.name}: {report.skeleton.bones} bone(s), {report.skeleton.mirror.pairs} mirror pair(s)
          </div>
          <div className="nif-flabel" aria-label="Character ragdoll summary">
            {report.ragdoll.present
              ? `Ragdoll: ${report.ragdoll.bodies} body(ies), ${report.ragdoll.constraints} constraint(s), ${report.ragdoll.bone_bindings} binding(s)`
              : `No ragdoll: ${report.ragdoll.headline || report.ragdoll.status}`}
          </div>
          <Lines label="Character mappers" items={report.mappers.map(m => `mapper ${m.object_index}: ${m.rows} row(s), ${m.chains} chain(s)`)} />
          <div className="nif-flabel" aria-label="Character animation links">
            {report.chain?.checked
              ? `Animation links: ${report.chain.animations} clip(s), ${report.chain.missing_animations} missing, ${report.chain.links} link(s) from ${report.chain.root}`
              : 'Animation links: not checked (give a project file and a data folder)'}
          </div>
          <Lines label="Character findings" items={report.findings.map(describeFinding)} warn />
          <Lines label="Character skin shapes"
                 items={report.skin.shapes.map(s =>
                   `${s.binding.name || `block ${s.binding.block}`}: ${s.binding.vertices} vertex(es), ${s.skin_errors} skin error(s), ${s.unbound_vertices} unbound`)} />
          <Lines label="Character skin repairs"
                 items={report.skin.shapes.flatMap(s => s.repairs.map(r => describeRepair(s, r)))} warn />
          <Lines label="Character skin refused" items={report.skin.refused.map(r => `block ${r.block} ${r.name}: ${r.reason}`)} warn />
          <div className="nif-flabel">
            Repairs are suggestions: apply them with the NIF shape and skin edit tools, then check again.
          </div>
        </>
      ) : null}

      <div className="nif-flags-head">
        <span className="nif-flabel"><UserCheck size={12} /> Save the character</span>
        <input className="nif-in" value={targets.skeletonOut} disabled={off} aria-label="Plan skeleton out"
               placeholder="Skeleton out path (empty saves in place)" onChange={setTarget('skeletonOut')} />
        <input className="nif-in" value={targets.edits} disabled={off} aria-label="Plan skeleton edits"
               placeholder='Skeleton edits JSON, e.g. [{"kind":"rename","bone":2,"name":"Skull"}]' onChange={setTarget('edits')} />
        <input className="nif-in" value={targets.ragdollOut} disabled={off} aria-label="Plan ragdoll out"
               placeholder="Ragdoll out path" onChange={setTarget('ragdollOut')} />
        <input className="nif-in" value={targets.ragdollEdits} disabled={off} aria-label="Plan ragdoll edits"
               placeholder='Ragdoll edits JSON, e.g. {"bodies":[{"body":0,"mass":2}]}' onChange={setTarget('ragdollEdits')} />
        <input className="nif-in" value={targets.mapperOut} disabled={off} aria-label="Plan mapper out"
               placeholder="Mapper out path" onChange={setTarget('mapperOut')} />
        <input className="nif-in" value={targets.mapperEdits} disabled={off} aria-label="Plan mapper edits"
               placeholder='Mapper edits JSON list, e.g. [{"kind":"recompute_row","row":0}]' onChange={setTarget('mapperEdits')} />
        <input className="nif-in" value={targets.mapperIndex} disabled={off} aria-label="Plan mapper index"
               placeholder="Mapper index in the file (default 0)" onChange={setTarget('mapperIndex')} />
        <input className="nif-in" value={targets.skin} disabled={off} aria-label="Plan skin nif"
               placeholder="Skinned .nif source" onChange={setTarget('skin')} />
        <input className="nif-in" value={targets.skinOut} disabled={off} aria-label="Plan skin out"
               placeholder="Skinned .nif out path" onChange={setTarget('skinOut')} />
        <input className="nif-in" value={targets.skinEdits} disabled={off} aria-label="Plan skin edits"
               placeholder='Skin edits JSON list, e.g. [{"kind":"copy_bone","block":3,"from_bone":1,"to_bone":2}]' onChange={setTarget('skinEdits')} />
        <label className="nif-flabel">
          <input type="checkbox" checked={targets.overwrite} disabled={off} aria-label="Plan overwrite"
                 onChange={e => setTargets(t => ({ ...t, overwrite: e.target.checked }))} />
          Replace out files that already exist
        </label>
        <button className="sidebar-action-btn"
                disabled={off || (!trimmed.skeleton && !trimmed.ragdoll && !trimmed.mapper && !clean.skin)}
                onClick={() => void planSave()}>
          Plan save
        </button>
        <button className="sidebar-action-btn" disabled={off || !planCurrent} onClick={() => void save()}>
          Save
        </button>
      </div>
      <div className="nif-flabel">
        Plan save writes nothing. Save runs the planned chain in order (skeleton, ragdoll, mapper, skin): nothing is written unless every step
        checks out first, each step is written atomically and read back, and if a step fails the files already written are listed. Change a field after planning and the plan must be run again.
        Constraint add and remove (lane RC) is not part of this panel; a ragdoll file it writes is checked and planned here unchanged.
      </div>

      {plan ? (
        <>
          <div className={plan.ok ? 'nif-flabel' : 'papyrus-warn'} aria-label="Plan verdict">
            {plan.ok ? 'Plan ready' : 'Plan refused'}
            {plan.shifted_references > 0 ? `, ${plan.shifted_references} reference(s) would shift` : ''}
          </div>
          <Lines label="Plan steps" items={plan.steps.map(describeStep)} />
          <Lines label="Plan findings" items={plan.findings.map(describeFinding)} warn />
        </>
      ) : null}

      {saved ? (
        <>
          <div className={saved.saved ? 'nif-flabel' : 'papyrus-warn'} aria-label="Save verdict">{describeSave(saved)}</div>
          <Lines label="Files written" items={saved.written} />
          <Lines label="Save findings" items={saved.findings.map(describeFinding)} warn />
        </>
      ) : null}

      {error ? <div className="papyrus-warn" role="alert">{error}</div> : null}
      {notice ? <div className="nif-flabel" role="status">{notice}</div> : null}
    </div>
  );
}

import { useState } from 'react';
import { Boxes } from 'lucide-react';
import type { HkxHost } from './backend';
import { EditorTabPanel, EditorTabs } from './Editors/EditorTabs';
import {
  describeBehavior, describeBehaviorModel, describeBone, describeConstraint, describeMapperRow, describeMappers, describeModelNode, describeObjectView, describeProject, describeRagdoll, describeReference, describeRootMotion, fixed,
  editSkeleton, readBehavior, readBehaviorModel, readMappers, readObjects, readProjectIndex, readRagdoll, readRootMotion, readSkeletonDocument,
  type HkxBehaviorGraph, type HkxBehaviorModel, type HkxMapperFile, type HkxObjectView, type HkxProjectIndex, type HkxRagdoll, type HkxRootMotion,
  type HkxSkeletonDocument, type HkxSkeletonEditKind,
} from './hkxToolsClient';

const messageOf = (e: unknown) => (e instanceof Error ? e.message : String(e));

type PackTab = 'objects' | 'root' | 'bones' | 'behavior' | 'ragdoll' | 'mapper' | 'project';

// Invariant: every tab is one inspection surface, so a lane that adds a report adds a tab here.
const TABS = [
  { id: 'objects', label: 'Objects' },
  { id: 'root', label: 'Root motion' },
  { id: 'bones', label: 'Skeleton' },
  { id: 'behavior', label: 'Behavior' },
  { id: 'ragdoll', label: 'Ragdoll' },
  { id: 'mapper', label: 'Mapper' },
  { id: 'project', label: 'Project' },
];

interface Props {
  host?: HkxHost;
  initialPath: string;
  skeletonPath: string;
  animationPath: string;
  disabled: boolean;
}

export default function HkxBgsTools({ host, initialPath, skeletonPath, animationPath, disabled }: Props) {
  const [tab, setTab] = useState<PackTab>('objects');
  const [path, setPath] = useState(initialPath);
  const [prefix, setPrefix] = useState('');
  const [knownOnly, setKnownOnly] = useState(false);
  const [view, setView] = useState<HkxObjectView | null>(null);
  const [chosen, setChosen] = useState(0);
  const [skeleton, setSkeleton] = useState(skeletonPath);
  const [animation, setAnimation] = useState(animationPath);
  const [bone, setBone] = useState(0);
  const [motion, setMotion] = useState<HkxRootMotion | null>(null);
  const [document, setDocument] = useState<HkxSkeletonDocument | null>(null);
  const [graph, setGraph] = useState<HkxBehaviorGraph | null>(null);
  const [model, setModel] = useState<HkxBehaviorModel | null>(null);
  const [ragdoll, setRagdoll] = useState<HkxRagdoll | null>(null);
  const [mapperFile, setMapperFile] = useState<HkxMapperFile | null>(null);
  const [recompute, setRecompute] = useState(false);
  const [root, setRoot] = useState('');
  const [query, setQuery] = useState('');
  const [index, setIndex] = useState<HkxProjectIndex | null>(null);
  const [editKind, setEditKind] = useState<HkxSkeletonEditKind>('rename');
  const [editBone, setEditBone] = useState(0);
  const [editName, setEditName] = useState('');
  const [editParent, setEditParent] = useState(-1);
  const [notice, setNotice] = useState('');
  const [error, setError] = useState('');
  const [busy, setBusy] = useState(false);

  const read = async () => {
    if (!host || !path.trim()) return;
    setBusy(true);
    setError('');
    try {
      const found = await readObjects(host, path.trim(), prefix, knownOnly, 200);
      setView(found);
      setChosen(0);
      setNotice(describeObjectView(found));
    } catch (e) {
      setView(null);
      setError(messageOf(e));
      setNotice('');
    } finally {
      setBusy(false);
    }
  };

  const readMotion = async () => {
    if (!host || !skeleton.trim() || !animation.trim()) return;
    setBusy(true);
    setError('');
    try {
      const found = await readRootMotion(host, skeleton.trim(), animation.trim(), bone, 64);
      setMotion(found);
      setNotice(describeRootMotion(found));
    } catch (e) {
      setMotion(null);
      setError(messageOf(e));
      setNotice('');
    } finally {
      setBusy(false);
    }
  };

  const loadDocument = async () => {
    if (!host || !skeleton.trim()) return;
    setBusy(true);
    setError('');
    try {
      const found = await readSkeletonDocument(host, skeleton.trim());
      setDocument(found);
      setNotice(`${found.bone_count} bones, ${found.roots.length} roots, reference pose ${found.has_reference_pose ? 'present' : 'absent'}`);
    } catch (e) {
      setDocument(null);
      setError(messageOf(e));
      setNotice('');
    } finally {
      setBusy(false);
    }
  };

  const applyEdit = async () => {
    if (!host || !skeleton.trim()) return;
    setBusy(true);
    setError('');
    try {
      const found = await editSkeleton(host, skeleton.trim(), editKind, editBone, editName, editParent);
      setDocument(found);
      setNotice(`${editKind} applied: ${found.bone_count} bones remain, nothing is written to disk`);
    } catch (e) {
      setError(messageOf(e));
      setNotice('');
    } finally {
      setBusy(false);
    }
  };

  const readModel = async () => {
    if (!host || !path.trim()) return;
    setBusy(true);
    setError('');
    try {
      const found = await readBehaviorModel(host, path.trim(), 2000);
      setModel(found);
      setNotice(describeBehaviorModel(found));
    } catch (e) {
      setModel(null);
      setError(messageOf(e));
      setNotice('');
    } finally {
      setBusy(false);
    }
  };

  const readGraph = async () => {
    if (!host || !path.trim()) return;
    setBusy(true);
    setError('');
    try {
      const found = await readBehavior(host, path.trim(), 200);
      setGraph(found);
      setNotice(describeBehavior(found));
    } catch (e) {
      setGraph(null);
      setError(messageOf(e));
      setNotice('');
    } finally {
      setBusy(false);
    }
  };

  const readRagdollFile = async () => {
    if (!host || !path.trim()) return;
    setBusy(true);
    setError('');
    try {
      const found = await readRagdoll(host, path.trim(), 2000);
      setRagdoll(found);
      setNotice(describeRagdoll(found));
    } catch (e) {
      setRagdoll(null);
      setError(messageOf(e));
      setNotice('');
    } finally {
      setBusy(false);
    }
  };

  const readMapperFile = async () => {
    if (!host || !path.trim()) return;
    setBusy(true);
    setError('');
    try {
      const found = await readMappers(host, path.trim(), recompute, 2000);
      setMapperFile(found);
      setNotice(describeMappers(found));
    } catch (e) {
      setMapperFile(null);
      setError(messageOf(e));
      setNotice('');
    } finally {
      setBusy(false);
    }
  };

  const readIndex = async () => {
    if (!host || !root.trim()) return;
    setBusy(true);
    setError('');
    try {
      const found = await readProjectIndex(host, root.trim(), query, 400);
      setIndex(found);
      setNotice(describeProject(found));
    } catch (e) {
      setIndex(null);
      setError(messageOf(e));
      setNotice('');
    } finally {
      setBusy(false);
    }
  };

  const object = view && view.objects.length > 0 ? view.objects[Math.min(chosen, view.objects.length - 1)] : null;

  return (
    <div className="nif-meshtools">
      <div className="nif-flags-head">
        <span className="nif-flabel"><Boxes size={12} /> Packfile tools</span>
        <input className="nif-in" value={path} disabled={disabled || busy} aria-label="Packfile path"
               placeholder="Path to a .hkx file"
               onChange={e => setPath(e.target.value)} />
        <input className="nif-in" value={prefix} disabled={disabled || busy} aria-label="Class prefix"
               placeholder="class prefix"
               onChange={e => setPrefix(e.target.value)} />
        <label className="nif-flabel">
          <input type="checkbox" checked={knownOnly} disabled={disabled || busy}
                 onChange={e => setKnownOnly(e.target.checked)} />
          known classes only
        </label>
        <button className="sidebar-action-btn" disabled={disabled || busy || !path.trim()} onClick={() => void read()}>
          Inspect objects
        </button>
      </div>

      <EditorTabs ariaLabel="Packfile tools" idPrefix="pack" tabs={TABS} active={tab}
                  onChange={id => setTab(id as PackTab)} />

      <EditorTabPanel id="objects" idPrefix="pack" active={tab === 'objects'}>
        {view ? (
          <>
            <ul className="nif-flabel" role="list" aria-label="Packfile objects">
              {view.objects.map((entry, at) => (
                <li key={`${entry.index}:${entry.offset}`}>
                  <button className={at === chosen ? 'active' : ''} onClick={() => setChosen(at)}>
                    {entry.index}: {entry.class_name || '(unnamed)'} @0x{entry.offset.toString(16)} · {entry.size} bytes
                    {entry.layout_known ? '' : ' (no layout)'}
                  </button>
                </li>
              ))}
            </ul>
            {object ? (
              <div className="nif-editor-actions">
                <div className="nif-flabel" aria-label="Object detail">
                  {object.class_name} · signature 0x{object.signature.toString(16)} · {object.fields.length} fields ·{' '}
                  {object.references.length} references
                </div>
                <ul className="nif-flabel" role="list" aria-label="Object fields">
                  {object.fields.map(field => (
                    <li key={field.name}>{field.name} at {field.offset} ({field.size} bytes): {field.hex || '(empty)'}</li>
                  ))}
                </ul>
                <ul className="nif-flabel" role="list" aria-label="Object references">
                  {object.references.map(reference => (
                    <li key={`${reference.source}:${reference.target_offset}`}>{describeReference(reference)}</li>
                  ))}
                </ul>
              </div>
            ) : null}
            {view.diagnostics.length > 0 ? (
              <ul className="papyrus-warn" role="list" aria-label="Packfile diagnostics">
                {view.diagnostics.map(line => <li key={line}>{line}</li>)}
              </ul>
            ) : null}
          </>
        ) : null}
      </EditorTabPanel>

      <EditorTabPanel id="root" idPrefix="pack" active={tab === 'root'}>
        <div className="nif-editor-actions">
          <input className="nif-in" value={skeleton} disabled={disabled || busy} aria-label="Root motion skeleton"
                 placeholder="Skeleton .hkx" onChange={e => setSkeleton(e.target.value)} />
          <input className="nif-in" value={animation} disabled={disabled || busy} aria-label="Root motion clip"
                 placeholder="Animation .hkx" onChange={e => setAnimation(e.target.value)} />
          <input className="nif-in nif-num" type="number" min={0} step={1} value={bone}
                 disabled={disabled || busy} aria-label="Root motion bone"
                 onChange={e => setBone(Number(e.target.value))} />
          <button className="sidebar-action-btn" disabled={disabled || busy || !skeleton.trim() || !animation.trim()}
                  onClick={() => void readMotion()}>
            Read root motion
          </button>
        </div>
        {motion ? (
          <ul className="nif-flabel" role="list" aria-label="Root motion samples">
            <li>
              {motion.bone_name} on track {motion.track}: {motion.frame_count} frames, {motion.duration.toFixed(3)} s,
              velocity {motion.velocity.map(value => value.toFixed(3)).join(', ')}
            </li>
            {motion.samples.map(sample => (
              <li key={sample.frame}>
                frame {sample.frame} at {sample.time.toFixed(3)} s: {sample.translation.map(v => v.toFixed(3)).join(', ')}
                {sample.frame > 0 ? ` (+${sample.delta.map(v => v.toFixed(3)).join(', ')})` : ''}
              </li>
            ))}
          </ul>
        ) : null}
      </EditorTabPanel>

      <EditorTabPanel id="bones" idPrefix="pack" active={tab === 'bones'}>
        <div className="nif-editor-actions">
          <input className="nif-in" value={skeleton} disabled={disabled || busy} aria-label="Skeleton path"
                 placeholder="Skeleton .hkx" onChange={e => setSkeleton(e.target.value)} />
          <button className="sidebar-action-btn" disabled={disabled || busy || !skeleton.trim()}
                  onClick={() => void loadDocument()}>
            Read skeleton
          </button>
        </div>
        {document ? (
          <>
            <div className="nif-flabel" aria-label="Skeleton summary">
              {document.name || '(unnamed)'}: {document.bone_count} bones, roots {document.roots.join(', ') || 'none'}
            </div>
            <ul className="nif-flabel" role="list" aria-label="Skeleton bones">
              {document.bones.map(entry => <li key={entry.index}>{describeBone(entry)}</li>)}
            </ul>
            <div className="nif-editor-actions">
              <select className="nif-in" value={editKind} disabled={disabled || busy} aria-label="Skeleton edit"
                      onChange={e => setEditKind(e.target.value as HkxSkeletonEditKind)}>
                <option value="rename">Rename</option>
                <option value="reparent">Reparent</option>
                <option value="remove">Remove</option>
              </select>
              <input className="nif-in nif-num" type="number" min={0} step={1} value={editBone}
                     disabled={disabled || busy} aria-label="Skeleton edit bone"
                     onChange={e => setEditBone(Number(e.target.value))} />
              <input className="nif-in" value={editName} disabled={disabled || busy || editKind !== 'rename'}
                     aria-label="Skeleton edit name" placeholder="new name"
                     onChange={e => setEditName(e.target.value)} />
              <input className="nif-in nif-num" type="number" min={-1} step={1} value={editParent}
                     disabled={disabled || busy || editKind !== 'reparent'} aria-label="Skeleton edit parent"
                     onChange={e => setEditParent(Number(e.target.value))} />
              <button className="sidebar-action-btn" disabled={disabled || busy} onClick={() => void applyEdit()}>
                Apply edit
              </button>
            </div>
            {document.diagnostics.length > 0 ? (
              <ul className="papyrus-warn" role="list" aria-label="Skeleton diagnostics">
                {document.diagnostics.map(line => <li key={line}>{line}</li>)}
              </ul>
            ) : null}
          </>
        ) : null}
      </EditorTabPanel>

      <EditorTabPanel id="behavior" idPrefix="pack" active={tab === 'behavior'}>
        <div className="nif-editor-actions">
          <button className="sidebar-action-btn" disabled={disabled || busy || !path.trim()}
                  onClick={() => void readGraph()}>
            Read behavior
          </button>
          <button className="sidebar-action-btn" disabled={disabled || busy || !path.trim()}
                  onClick={() => void readModel()}>
            Read typed graph
          </button>
        </div>
        {model ? (
          <>
            <ul className="nif-flabel" role="list" aria-label="Typed graph nodes">
              {model.nodes.map((node, at) => <li key={node.object}>{describeModelNode(model, node, at)}</li>)}
            </ul>
            <ul className="nif-flabel" role="list" aria-label="Typed graph events">
              {model.events.map((event, at) => <li key={`${at}:${event.name}`}>event {at}: {event.name}</li>)}
            </ul>
            <ul className="nif-flabel" role="list" aria-label="Typed graph variables">
              {model.variables.map((variable, at) => <li key={`${at}:${variable.name}`}>variable {at}: {variable.name}</li>)}
            </ul>
            {model.diagnostics.length > 0 ? (
              <ul className="papyrus-warn" role="list" aria-label="Typed graph diagnostics">
                {model.diagnostics.map(line => <li key={line}>{line}</li>)}
              </ul>
            ) : null}
          </>
        ) : null}
        {graph ? (
          <>
            <ul className="nif-flabel" role="list" aria-label="Behavior nodes">
              {graph.nodes.map(node => (
                <li key={node.object}>
                  object {node.object}: {node.class_name} · {node.size} bytes · out {node.references_out} in {node.references_in}
                  {node.layout_known ? '' : ' (no layout)'}
                </li>
              ))}
            </ul>
            <ul className="nif-flabel" role="list" aria-label="Behavior links">
              {graph.links.map(link => (
                <li key={`${link.from}:${link.to}:${link.member}`}>
                  {link.from} to {link.to} via {link.member || '(unnamed member)'}
                </li>
              ))}
            </ul>
            {graph.diagnostics.length > 0 ? (
              <ul className="papyrus-warn" role="list" aria-label="Behavior diagnostics">
                {graph.diagnostics.map(line => <li key={line}>{line}</li>)}
              </ul>
            ) : null}
          </>
        ) : null}
      </EditorTabPanel>

      <EditorTabPanel id="ragdoll" idPrefix="pack" active={tab === 'ragdoll'}>
        <div className="nif-editor-actions">
          <button className="sidebar-action-btn" disabled={disabled || busy || !path.trim()}
                  onClick={() => void readRagdollFile()}>
            Read ragdoll
          </button>
        </div>
        {ragdoll ? (
          <>
            {ragdoll.loaded ? (
              <>
                <div className="nif-flabel" aria-label="Ragdoll skeletons">
                  physics skeleton {ragdoll.skeleton ? `${ragdoll.skeleton.name} (${ragdoll.skeleton.bone_count} bones)` : 'none'},
                  {' '}animation skeleton {ragdoll.animation_skeleton ? `${ragdoll.animation_skeleton.name} (${ragdoll.animation_skeleton.bone_count} bones)` : 'none'}
                </div>
                <ul className="nif-flabel" role="list" aria-label="Ragdoll bodies">
                  {(ragdoll.bodies ?? []).map(body => (
                    <li key={body.id}>
                      {body.id}: {body.name || '(unnamed)'} shape {body.shape_id}, bone {body.bone_index}, mass {fixed(body.mass)}
                    </li>
                  ))}
                </ul>
                <ul className="nif-flabel" role="list" aria-label="Ragdoll constraints">
                  {(ragdoll.constraints ?? []).map(constraint => <li key={constraint.id}>{describeConstraint(constraint)}</li>)}
                </ul>
                {(ragdoll.validation ?? []).length > 0 ? (
                  <ul className="papyrus-warn" role="list" aria-label="Ragdoll validation">
                    {(ragdoll.validation ?? []).map(finding => (
                      <li key={`${finding.where}:${finding.message}`}>{finding.level} {finding.where}: {finding.message}</li>
                    ))}
                  </ul>
                ) : null}
              </>
            ) : null}
            {ragdoll.findings.length > 0 ? (
              <ul className="papyrus-warn" role="list" aria-label="Ragdoll load findings">
                {ragdoll.findings.map(finding => (
                  <li key={finding.message}>{finding.severity}: {finding.message}{finding.hint ? ` ${finding.hint}` : ''}</li>
                ))}
              </ul>
            ) : null}
          </>
        ) : null}
      </EditorTabPanel>

      <EditorTabPanel id="mapper" idPrefix="pack" active={tab === 'mapper'}>
        <div className="nif-editor-actions">
          <label className="nif-flabel">
            <input type="checkbox" checked={recompute} disabled={disabled || busy}
                   onChange={e => setRecompute(e.target.checked)} />
            recompute rows from the reference poses
          </label>
          <button className="sidebar-action-btn" disabled={disabled || busy || !path.trim()}
                  onClick={() => void readMapperFile()}>
            Read mappers
          </button>
        </div>
        {mapperFile ? mapperFile.mappers.map(mapper => (
          <div key={mapper.object_index}>
            <div className="nif-flabel" aria-label="Mapper summary">
              object {mapper.object_index}: {mapper.skeleton_a.name} ({mapper.skeleton_a.bone_count} bones) to{' '}
              {mapper.skeleton_b.name} ({mapper.skeleton_b.bone_count} bones), type {mapper.mapping_type},
              {' '}{mapper.unmapped_bones.length} unmapped bones{mapper.keep_unmapped_local ? ', unmapped bones keep local pose' : ''}
            </div>
            <ul className="nif-flabel" role="list" aria-label="Mapper rows">
              {mapper.rows.map((row, at) => <li key={`${at}:${row.bone_a}:${row.bone_b}`}>{describeMapperRow(row)}</li>)}
            </ul>
            <ul className="nif-flabel" role="list" aria-label="Mapper chains">
              {mapper.chains.map((chain, at) => (
                <li key={`${at}:${chain.start_bone_a}`}>
                  chain A {chain.start_bone_a} to {chain.end_bone_a}, B {chain.start_bone_b} to {chain.end_bone_b}
                </li>
              ))}
            </ul>
          </div>
        )) : null}
      </EditorTabPanel>

      <EditorTabPanel id="project" idPrefix="pack" active={tab === 'project'}>
        <div className="nif-editor-actions">
          <input className="nif-in" value={root} disabled={disabled || busy} aria-label="Project root"
                 placeholder="Folder to scan" onChange={e => setRoot(e.target.value)} />
          <input className="nif-in" value={query} disabled={disabled || busy} aria-label="Project query"
                 placeholder="name or class filter" onChange={e => setQuery(e.target.value)} />
          <button className="sidebar-action-btn" disabled={disabled || busy || !root.trim()}
                  onClick={() => void readIndex()}>
            Read index
          </button>
        </div>
        {index ? (
          <>
            <ul className="nif-flabel" role="list" aria-label="Project files">
              {index.files.map(file => (
                <li key={file.name}>
                  {file.name}: {file.object_count} objects, {file.behavior_objects} behavior,{' '}
                  {file.animation_objects} animation, {file.skeleton_objects} skeleton
                </li>
              ))}
            </ul>
            {index.duplicates.length > 0 ? (
              <ul className="papyrus-warn" role="list" aria-label="Project duplicates">
                {index.duplicates.map(group => <li key={group.join('|')}>same behavior bytes: {group.join(', ')}</li>)}
              </ul>
            ) : null}
            {index.unreadable.length > 0 ? (
              <ul className="papyrus-warn" role="list" aria-label="Project unreadable">
                {index.unreadable.map(entry => <li key={entry.name}>{entry.name}: {entry.error}</li>)}
              </ul>
            ) : null}
            {index.diagnostics.length > 0 ? (
              <ul className="papyrus-warn" role="list" aria-label="Project diagnostics">
                {index.diagnostics.map(line => <li key={line}>{line}</li>)}
              </ul>
            ) : null}
          </>
        ) : null}
      </EditorTabPanel>

      {error ? <div className="papyrus-warn" role="alert">{error}</div> : null}
      {notice ? <div className="nif-flabel" role="status">{notice}</div> : null}
    </div>
  );
}

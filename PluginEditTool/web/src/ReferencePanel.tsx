import { createContext, useCallback, useContext, useState, type ReactNode } from 'react';
import { Copy } from 'lucide-react';
import type { CellEditChanges } from './backend';
import ReferenceFlagsEditor from './ReferenceFlagsEditor';
import ReferenceLockEditor from './ReferenceLockEditor';
import { PatchChooserHint } from './CellPatchPicker';
import ReferenceOwnershipEditor from './ReferenceOwnershipEditor';
import ReferencePrimitiveEditor from './ReferencePrimitiveEditor';
import type {
  ReferenceDeclaredProperty,
  ReferenceDetails,
  ReferenceError,
  ReferenceLink,
  ReferenceScript,
  ReferenceScriptMember,
  ReferenceScriptProperty,
  ReferenceScriptSet,
} from './referenceDetails';
import { isReferenceError, type ReferenceData } from './useReferenceData';
import { primitiveBoxSize, primitiveTypeName } from './cellPrimitive';
import './ReferencePanel.css';

interface PropertyRow {
  name: string;
  type: string;
  value: string;
  badges: string[];
  removed: boolean;
}

interface PropertyStatus {
  badges: string[];
  removed: boolean;
}

export interface ReferenceEditProps {
  patchChosen: boolean;
  onEdit: (changes: CellEditChanges) => Promise<boolean>;
  onChoosePatch?: () => void;
}

const scriptFlagNames: Partial<Record<number, string>> = { 0: 'Local', 1: 'Inherited', 3: 'Removed' };

function scriptFlagLabel(flags: number): string {
  return scriptFlagNames[flags] ?? String(flags);
}

type Vec = { x: number; y: number; z: number };

function membersOf(value: unknown): ReferenceScriptMember[] | null {
  if (typeof value !== 'object' || value === null) return null;
  const members = (value as { members?: unknown }).members;
  return Array.isArray(members) ? (members as ReferenceScriptMember[]) : null;
}

function linkText(link: ReferenceLink | null | undefined): string {
  if (!link) return '—';
  return link.editorId ? `${link.editorId} (${link.formKey})` : link.formKey;
}

function aliasSuffix(alias: unknown): string {
  return typeof alias === 'number' && alias !== -1 ? ` (alias ${alias})` : '';
}

function valueText(value: unknown): string {
  if (value === null || value === undefined) return '—';
  if (typeof value !== 'object') return String(value);
  if (Array.isArray(value)) return value.map(valueText).join(', ');
  const record = value as Record<string, unknown>;
  if ('formKey' in record) return linkText(record as unknown as ReferenceLink);
  if ('object' in record) return linkText(record.object as ReferenceLink | null) + aliasSuffix(record.alias);
  const members = membersOf(value);
  if (members) return members.map((member) => `${member.name}: ${valueText(member.value)}`).join('; ');
  return JSON.stringify(value);
}

function storedBadge(flags: number): string {
  if (flags === 0) return 'Stored';
  if (flags === 1) return 'Edited (1)';
  if (flags === 3) return 'Removed (3)';
  return `Stored (${flags})`;
}

function storedStatus(prop: ReferenceScriptProperty): PropertyStatus {
  const badges = [storedBadge(prop.flags)];
  if (prop.undecoded) badges.push('Undecoded');
  return { badges, removed: prop.flags === 3 };
}

function flattenValue(name: string, type: string, value: unknown, status: PropertyStatus): PropertyRow[] {
  const members = membersOf(value);
  if (members) {
    return members.map((member) => ({
      name: `${name}.${member.name}`,
      type: member.typeName,
      value: valueText(member.value),
      ...status,
    }));
  }
  return [{ name, type, value: valueText(value), ...status }];
}

function flattenProperty(prop: ReferenceScriptProperty): PropertyRow[] {
  const type = prop.typeName;
  const status = storedStatus(prop);
  if (Array.isArray(prop.elements)) {
    const elements = prop.elements as unknown[];
    if (elements.length === 0) return [{ name: prop.name, type, value: '[] (empty)', ...status }];
    return elements.flatMap((element, index) => flattenValue(`${prop.name}[${index}]`, type, element, status));
  }
  return flattenValue(prop.name, type, prop.value, status);
}

function declaredRow(prop: ReferenceDeclaredProperty): PropertyRow {
  const badges = ['Default'];
  if (prop.readOnly) badges.push('Read only');
  if (prop.conditional) badges.push('Conditional');
  return { name: prop.name, type: prop.type, value: valueText(prop.default), badges, removed: false };
}

function vecText(v: Vec): string {
  return `${v.x.toFixed(2)}, ${v.y.toFixed(2)}, ${v.z.toFixed(2)}`;
}

function primitiveSizeText(bounds: Vec): string {
  const size = primitiveBoxSize(bounds);
  return size ? vecText({ x: size[0], y: size[1], z: size[2] }) : '';
}

function yesNo(value: boolean | null | undefined): string {
  return value ? 'Yes' : 'No';
}

function lightRows(light: NonNullable<ReferenceDetails['lightData']>): [string, number][] {
  const entries: [string, number | null][] = [
    ['FOV', light.fov],
    ['Fade', light.fade],
    ['End distance cap', light.endDistanceCap],
    ['Shadow depth bias', light.shadowDepthBias],
    ['Near clip', light.nearClip],
    ['Volumetric intensity', light.volumetricIntensity],
  ];
  return entries.filter((entry): entry is [string, number] => typeof entry[1] === 'number');
}

type SectionMemory = {
  formKey: string;
  open: Record<string, boolean>;
  setOpen: (key: string, open: boolean) => void;
};
const SectionMemoryContext = createContext<SectionMemory | null>(null);

function Section({ title, defaultOpen = true, children }: { title: string; defaultOpen?: boolean; children: ReactNode }) {
  const memory = useContext(SectionMemoryContext);
  const key = `${memory?.formKey ?? ''}|${title}`;
  return (
    <details
      className="ref-section"
      open={memory?.open[key] ?? defaultOpen}
      onToggle={e => memory?.setOpen(key, e.currentTarget.open)}
    >
      <summary>{title}</summary>
      <div className="ref-section-body">{children}</div>
    </details>
  );
}

function Row({ label, children }: { label: string; children: ReactNode }) {
  return (
    <div className="cell-selected-row ref-row">
      <span>{label}</span>
      <strong>{children}</strong>
    </div>
  );
}

function CopyButton({ label, text }: { label: string; text: string }) {
  return (
    <button className="papyrus-copy ref-copy" onClick={() => void navigator.clipboard?.writeText(text).catch(() => undefined)}>
      <Copy size={12} /> {label}
    </button>
  );
}

function PropertyTable({ script }: { script: ReferenceScript }) {
  const stored = script.properties ?? [];
  const declared = (script.declared ?? []).filter((prop) => prop.status === 'default');
  const rows = [...stored.flatMap(flattenProperty), ...declared.map(declaredRow)];
  return (
    <>
      {stored.length === 0 && <div className="ref-muted">No properties stored in the plugin; script defaults apply.</div>}
      {script.declaredError && <div className="ref-muted">Declared defaults unavailable: {script.declaredError}</div>}
      {rows.length > 0 && (
        <table className="ref-props">
          <thead>
            <tr><th>Name</th><th>Type</th><th>Value</th><th>Status</th></tr>
          </thead>
          <tbody>
            {rows.map((row, index) => (
              <tr key={`${row.name}-${index}`} className={row.removed ? 'ref-row-removed' : undefined}>
                <td>{row.name}</td>
                <td>{row.type}</td>
                <td>{row.value}</td>
                <td>
                  {row.badges.map((badge) => <span key={badge} className="ref-badge">{badge}</span>)}
                </td>
              </tr>
            ))}
          </tbody>
        </table>
      )}
    </>
  );
}

function ScriptSetView({ set }: { set: ReferenceScriptSet | ReferenceError }) {
  if (isReferenceError(set)) return <div className="ref-error">{set.error}</div>;
  const scripts = set.scripts ?? [];
  const fragmentBytes = set.fragmentBytes ?? 0;
  return (
    <div className="ref-scripts">
      {set.unsupported && <div className="ref-muted">Unsupported script data is not shown.</div>}
      {fragmentBytes > 0 && <div className="ref-muted">{fragmentBytes} bytes of fragment data (not decoded)</div>}
      {scripts.length === 0 && <div className="ref-muted">No scripts attached.</div>}
      {scripts.map((script, index) => (
        <details key={`${script.name}-${index}`} className="ref-script">
          <summary>
            <span className="ref-script-name">{script.name}</span>
            <span className="ref-muted"> ({scriptFlagLabel(script.flags)})</span>
          </summary>
          {script.parent && <div className="ref-muted">Extends {script.parent}</div>}
          <PropertyTable script={script} />
        </details>
      ))}
    </div>
  );
}

function ReferenceDetailSections({ details: d, edit }: { details: ReferenceDetails; edit: ReferenceEditProps }) {
  const linked = d.linkedReferences ?? [];
  const patrols = d.patrols ?? [];
  const portals = d.portals ?? [];
  const roomLinks = d.room?.linkedRooms ?? [];
  const locationTypes = d.locationReferenceTypes ?? [];
  const placement: ReactNode[] = [];
  if (d.layer) placement.push(<Row key="layer" label="Layer">{linkText(d.layer)}</Row>);
  if (d.encounterZone) placement.push(<Row key="zone" label="Encounter zone">{linkText(d.encounterZone)}</Row>);
  if (d.locationReference) placement.push(<Row key="location" label="Location">{linkText(d.locationReference)}</Row>);
  if (locationTypes.length > 0) placement.push(<Row key="location-types" label="Location types">{locationTypes.map((type) => linkText(type)).join(', ')}</Row>);
  if (d.emittance) placement.push(<Row key="emittance" label="Emittance">{linkText(d.emittance)}</Row>);
  if (d.attachRef) placement.push(<Row key="attach" label="Attach ref">{linkText(d.attachRef)}</Row>);
  if (typeof d.radius === 'number') placement.push(<Row key="radius" label="Radius">{d.radius}</Row>);
  if (typeof d.count === 'number') placement.push(<Row key="count" label="Count">{d.count}</Row>);
  if (typeof d.levelModifier === 'number') placement.push(<Row key="level-modifier" label="Level modifier">{d.levelModifier}</Row>);
  if (d.mapMarker) placement.push(<Row key="marker" label="Map marker">{d.mapMarker.name || (typeof d.mapMarker.type === 'number' ? `Type ${d.mapMarker.type}` : '(unnamed)')}</Row>);
  return (
    <>
      <Section title="Identity">
        <Row label="Form key">{d.formKey}</Row>
        {d.editorId && <Row label="Editor ID">{d.editorId}</Row>}
        {d.signature && <Row label="Signature">{d.signature}</Row>}
        {d.winningPlugin && <Row label="Winning plugin">{d.winningPlugin}</Row>}
        {d.cell && <Row label="Cell">{linkText(d.cell)}</Row>}
        {d.base && (
          <>
            <Row label="Base">{linkText(d.base)}</Row>
            {d.base.name && <Row label="Base name">{d.base.name}</Row>}
            {d.base.type && <Row label="Base type">{d.base.type}</Row>}
            {d.base.model && <Row label="Model">{d.base.model}</Row>}
            {d.base.missing && <Row label="Base status">Missing</Row>}
          </>
        )}
        <div className="ref-actions">
          <CopyButton label="Copy reference key" text={d.formKey} />
          {d.base && <CopyButton label="Copy base key" text={d.base.formKey} />}
        </div>
      </Section>

      {d.transform && (
        <Section title="Transform">
          {d.transform.position && <Row label="Position">{vecText(d.transform.position)}</Row>}
          {d.transform.rotation && <Row label="Rotation">{vecText(d.transform.rotation)}</Row>}
          {typeof d.transform.scale === 'number' && <Row label="Scale">{d.transform.scale}</Row>}
        </Section>
      )}

      {d.flags && (
        <Section title="Flags" defaultOpen={false}>
          <Row label="Raw">{d.flags.raw}</Row>
          <Row label="Respawns">{yesNo(d.flags.respawns)}</Row>
          <ReferenceFlagsEditor
            key={`${d.formKey}:${d.flags.raw}:${d.openByDefault}:${d.ignoredBySandbox}`}
            flags={d.flags}
            edit={edit}
            markers={{ openByDefault: d.openByDefault, ignoredBySandbox: d.ignoredBySandbox }}
          />
        </Section>
      )}

      {placement.length > 0 && <Section title="Placement">{placement}</Section>}

      {d.enableParent && (
        <Section title="Enable parent" defaultOpen={false}>
          <Row label="Parent">{linkText(d.enableParent.reference)}</Row>
          {typeof d.enableParent.flags === 'number' && <Row label="Flags">{d.enableParent.flags}</Row>}
          <Row label="Opposite to parent">{yesNo(d.enableParent.setOppositeToParent)}</Row>
          <Row label="Pop in">{yesNo(d.enableParent.popIn)}</Row>
        </Section>
      )}

      {linked.length > 0 && (
        <Section title="Linked references" defaultOpen={false}>
          {linked.map((link, index) => (
            <Row key={`linked-${index}`} label={linkText(link.keyword)}>{linkText(link.reference)}</Row>
          ))}
        </Section>
      )}

      {(d.ownership || typeof d.factionRank === 'number' || edit.patchChosen) && (
        <Section title="Owner and faction">
          {edit.patchChosen
            ? (
              <ReferenceOwnershipEditor
                key={`${d.formKey}:${JSON.stringify(d.ownership)}:${d.factionRank}`}
                ownership={d.ownership}
                factionRank={d.factionRank}
                edit={edit}
              />
            )
            : (
              <>
                {d.ownership && <Row label="Owner">{linkText(d.ownership.owner)}</Row>}
                {d.ownership && <Row label="No crime">{yesNo(d.ownership.noCrime)}</Row>}
                {typeof d.factionRank === 'number' && <Row label="Faction rank">{d.factionRank}</Row>}
                <PatchChooserHint onChoose={edit.onChoosePatch} />
              </>
            )}
        </Section>
      )}

      {(d.lock || edit.patchChosen || edit.onChoosePatch) && (
        <Section title="Lock">
          {edit.patchChosen
            ? <ReferenceLockEditor key={`${d.formKey}:${JSON.stringify(d.lock)}`} lock={d.lock} edit={edit} />
            : (
              <>
                <PatchChooserHint onChoose={edit.onChoosePatch} />
                {d.lock && (
                  <>
                    <Row label="Level">{d.lock.level}</Row>
                    {d.lock.levelName && <Row label="Level name">{d.lock.levelName}</Row>}
                    {d.lock.key && <Row label="Key">{linkText(d.lock.key)}</Row>}
                  </>
                )}
              </>
            )}
        </Section>
      )}

      {(d.activateParents?.parents ?? []).length > 0 && (
        <Section title="Activate parents" defaultOpen={false}>
          {typeof d.activateParents?.flags === 'number' && <Row label="Flags">{d.activateParents.flags}</Row>}
          {(d.activateParents?.parents ?? []).map((parent, index) => (
            <Row key={`parent-${index}`} label={`Parent ${index + 1}`}>
              {linkText(parent.reference)}
              {typeof parent.delay === 'number' ? ` after ${parent.delay}` : ''}
            </Row>
          ))}
        </Section>
      )}

      {d.teleport && (
        <Section title="Teleport" defaultOpen={false}>
          <Row label="Door">{linkText(d.teleport.door)}</Row>
          {d.teleport.position && <Row label="Position">{vecText(d.teleport.position)}</Row>}
          {d.teleport.rotation && <Row label="Rotation">{vecText(d.teleport.rotation)}</Row>}
          {typeof d.teleport.flags === 'number' && <Row label="Flags">{d.teleport.flags}</Row>}
          {d.teleport.transitionInterior && <Row label="Interior">{linkText(d.teleport.transitionInterior)}</Row>}
        </Section>
      )}

      {d.primitive && (
        <Section title="Primitive" defaultOpen={false}>
          {edit.patchChosen
            ? (
              <>
                <Row label="Size">{primitiveSizeText(d.primitive.bounds)}</Row>
                <ReferencePrimitiveEditor key={`${d.formKey}:${JSON.stringify(d.primitive)}`} primitive={d.primitive} edit={edit} />
              </>
            )
            : (
              <>
                <Row label="Shape">{primitiveTypeName(d.primitive.type)}</Row>
                {d.primitive.bounds && <Row label="Size">{primitiveSizeText(d.primitive.bounds)}</Row>}
                {d.primitive.bounds && <Row label="Bounds (half-extent)">{vecText(d.primitive.bounds)}</Row>}
                {d.primitive.color && <Row label="Color">{d.primitive.color.join(', ')}</Row>}
                <PatchChooserHint onChoose={edit.onChoosePatch} />
              </>
            )}
        </Section>
      )}

      {(d.roomPortal || d.room || portals.length > 0) && (
        <Section title="Room and portal" defaultOpen={false}>
          {d.roomPortal && <Row label="Portal size">{`${d.roomPortal.width ?? 0} × ${d.roomPortal.height ?? 0}`}</Row>}
          {d.roomPortal?.position && <Row label="Portal position">{vecText(d.roomPortal.position)}</Row>}
          {d.room && <Row label="Linked room count">{d.room.linkedRoomCount}</Row>}
          {typeof d.room?.flags === 'number' && <Row label="Room flags">{d.room.flags}</Row>}
          {d.room && <Row label="Lighting template">{linkText(d.room.lightingTemplate)}</Row>}
          {d.room && <Row label="Image space">{linkText(d.room.imageSpace)}</Row>}
          {roomLinks.map((room, index) => (
            <Row key={`room-${index}`} label="Room">{linkText(room)}</Row>
          ))}
          {portals.map((portal, index) => (
            <Row key={`portal-${index}`} label="Portal">{`${linkText(portal.origin)} → ${linkText(portal.destination)}`}</Row>
          ))}
        </Section>
      )}

      {d.lightData && (
        <Section title="Light" defaultOpen={false}>
          {lightRows(d.lightData).map(([label, value]) => (
            <Row key={label} label={label}>{value}</Row>
          ))}
        </Section>
      )}

      {patrols.length > 0 && (
        <Section title="Patrols" defaultOpen={false}>
          {patrols.map((patrol, index) => (
            <Row key={`patrol-${index}`} label={`Patrol ${index + 1}`}>
              {[
                linkText(patrol.idle),
                typeof patrol.idleTime === 'number' ? `idle ${patrol.idleTime}` : '',
                linkText(patrol.topic),
                (patrol.topics ?? []).map((topic) => `${topic.type ?? ''} ${linkText(topic.topic)}`.trim()).join('; '),
              ].filter((part) => part !== '').join(' · ')}
            </Row>
          ))}
        </Section>
      )}

      {d.comments && (
        <Section title="Comments" defaultOpen={false}>
          <div className="ref-comment">{d.comments}</div>
        </Section>
      )}
    </>
  );
}

function ScriptSections({ details: d }: { details: ReferenceDetails }) {
  if (!d.scripts && !d.baseScripts) return <div className="ref-muted">No scripts on this reference or its base.</div>;
  return (
    <>
      {d.scripts && (
        <details className="ref-subsection" open>
          <summary>On the reference</summary>
          <ScriptSetView set={d.scripts} />
        </details>
      )}
      {d.baseScripts && (
        <details className="ref-subsection" open>
          <summary>On the base object</summary>
          <ScriptSetView set={d.baseScripts} />
        </details>
      )}
    </>
  );
}

function withDetails(data: ReferenceData, render: (details: ReferenceDetails) => ReactNode): ReactNode {
  if (data.loading) return <div className="ref-status">Loading reference details…</div>;
  if (data.error) return <div className="ref-status ref-error">{data.error}</div>;
  if (!data.details) return <div className="ref-muted">No details returned.</div>;
  return render(data.details);
}

export function ReferenceTab({ data, edit }: { data: ReferenceData; edit: ReferenceEditProps }) {
  const [open, setOpen] = useState<Record<string, boolean>>({});
  const setSectionOpen = useCallback((key: string, value: boolean) => {
    setOpen(prev => (prev[key] === value ? prev : { ...prev, [key]: value }));
  }, []);
  return (
    <SectionMemoryContext.Provider value={{ formKey: data.details?.formKey ?? '', open, setOpen: setSectionOpen }}>
      {withDetails(data, (details) => <ReferenceDetailSections details={details} edit={edit} />)}
    </SectionMemoryContext.Provider>
  );
}

export function ScriptsTab({ data }: { data: ReferenceData }) {
  return <>{withDetails(data, (details) => <ScriptSections details={details} />)}</>;
}

export function PluginsTab({ data }: { data: ReferenceData }) {
  if (data.loading) return <div className="ref-status">Loading reference details…</div>;
  const pluginNames = data.details?.pluginsWithRecord ?? [];
  if (data.plugins.length === 0 && pluginNames.length === 0) return <div className="ref-muted">No plugin records for this reference.</div>;
  if (data.plugins.length === 0) return <Row label="Plugins">{pluginNames.join(', ')}</Row>;
  return (
    <table className="ref-props">
      <thead>
        <tr><th>Plugin</th><th>Kind</th><th>Changes</th><th>Conflicts</th><th>Status</th></tr>
      </thead>
      <tbody>
        {data.plugins.map((row, index) => (
          <tr key={`${row.Plugin}-${index}`}>
            <td>{String(row.Plugin)}</td>
            <td>{String(row.Kind ?? '')}</td>
            <td>{String(row.Changes ?? '')}</td>
            <td>{String(row.Conflicts ?? '')}</td>
            <td>
              {row.IsWinner && <span className="ref-badge ref-badge-winner">Winner</span>}
              {row.IsOverride && <span className="ref-badge">Override</span>}
            </td>
          </tr>
        ))}
      </tbody>
    </table>
  );
}

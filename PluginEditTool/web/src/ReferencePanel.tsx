import { useEffect, useState, type ReactNode } from 'react';
import { Copy, EyeOff, X } from 'lucide-react';
import { getBackend, getCell, type CellPlacedReference, type PluginMatrixRow } from './backend';
import type {
  ReferenceDetails,
  ReferenceError,
  ReferenceLink,
  ReferenceScriptMember,
  ReferenceScriptProperty,
  ReferenceScriptSet,
} from './referenceDetails';
import { errorText } from './errors';
import './ReferencePanel.css';

export interface ReferencePanelProps {
  reference: CellPlacedReference;
  refRevision: number;
  expanded: boolean;
  onToggle: () => void;
  onClear: () => void;
  onHide: () => void;
}

interface LoadedReference {
  key: string;
  details: ReferenceDetails | null;
  error: string | null;
  plugins: PluginMatrixRow[];
}

interface PropertyRow {
  name: string;
  type: string;
  value: string;
}

type Vec = { x: number; y: number; z: number };

function isReferenceError(value: unknown): value is ReferenceError {
  return typeof value === 'object' && value !== null && typeof (value as { error?: unknown }).error === 'string';
}

function membersOf(value: unknown): ReferenceScriptMember[] | null {
  if (typeof value !== 'object' || value === null) return null;
  const members = (value as { members?: unknown }).members;
  return Array.isArray(members) ? (members as ReferenceScriptMember[]) : null;
}

function linkText(link: ReferenceLink | null | undefined): string {
  if (!link) return '—';
  return link.editorId ? `${link.editorId} (${link.formKey})` : link.formKey;
}

function valueText(value: unknown): string {
  if (value === null || value === undefined) return '—';
  if (typeof value !== 'object') return String(value);
  if (Array.isArray(value)) return value.map(valueText).join(', ');
  const record = value as Record<string, unknown>;
  if ('formKey' in record) return linkText(record as unknown as ReferenceLink);
  if ('object' in record) return linkText(record.object as ReferenceLink | null);
  const members = membersOf(value);
  if (members) return members.map((member) => `${member.name}: ${valueText(member.value)}`).join('; ');
  return JSON.stringify(value);
}

function flattenValue(name: string, type: string, value: unknown): PropertyRow[] {
  const members = membersOf(value);
  if (members) {
    return members.map((member) => ({
      name: `${name}.${member.name}`,
      type: member.typeName,
      value: valueText(member.value),
    }));
  }
  return [{ name, type, value: valueText(value) }];
}

function flattenProperty(prop: ReferenceScriptProperty): PropertyRow[] {
  const type = prop.typeName;
  if (Array.isArray(prop.elements)) {
    return (prop.elements as unknown[]).flatMap((element, index) => flattenValue(`${prop.name}[${index}]`, type, element));
  }
  return flattenValue(prop.name, type, prop.value);
}

function vecText(v: Vec): string {
  return `${v.x.toFixed(2)}, ${v.y.toFixed(2)}, ${v.z.toFixed(2)}`;
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

async function loadReference(formKey: string): Promise<Omit<LoadedReference, 'key'>> {
  const backend = getBackend();
  const cell = getCell();
  let details: ReferenceDetails | null = null;
  let error: string | null = null;
  let plugins: PluginMatrixRow[] = [];
  try {
    if (!cell) throw new Error('Cell host unavailable');
    const parsed: unknown = JSON.parse(await cell.GetReferenceDetails(formKey));
    if (isReferenceError(parsed)) error = parsed.error;
    else details = parsed as ReferenceDetails | null;
  } catch (err) {
    error = errorText(err);
  }
  try {
    if (backend?.GetRecordPluginMatrix) {
      const rows: unknown = JSON.parse(await backend.GetRecordPluginMatrix(formKey));
      if (Array.isArray(rows)) plugins = rows as PluginMatrixRow[];
    }
  } catch {
    plugins = [];
  }
  return { details, error, plugins };
}

function Section({ title, defaultOpen = true, children }: { title: string; defaultOpen?: boolean; children: ReactNode }) {
  return (
    <details className="ref-section" open={defaultOpen}>
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

function PropertyTable({ properties }: { properties: ReferenceScriptProperty[] }) {
  const rows = properties.flatMap(flattenProperty);
  if (rows.length === 0) return <div className="ref-muted">No properties.</div>;
  return (
    <table className="ref-props">
      <thead>
        <tr><th>Name</th><th>Type</th><th>Value</th></tr>
      </thead>
      <tbody>
        {rows.map((row, index) => (
          <tr key={`${row.name}-${index}`}>
            <td>{row.name}</td>
            <td>{row.type}</td>
            <td>{row.value}</td>
          </tr>
        ))}
      </tbody>
    </table>
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
          <summary>{script.flags ? `${script.name} (${script.flags})` : script.name}</summary>
          <PropertyTable properties={script.properties ?? []} />
        </details>
      ))}
    </div>
  );
}

function ReferenceDetailSections({ details: d, plugins }: { details: ReferenceDetails; plugins: PluginMatrixRow[] }) {
  const linked = d.linkedReferences ?? [];
  const patrols = d.patrols ?? [];
  const portals = d.portals ?? [];
  const roomLinks = d.room?.linkedRooms ?? [];
  const locationTypes = d.locationReferenceTypes ?? [];
  const pluginNames = d.pluginsWithRecord ?? [];
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
          <Row label="Open by default">{yesNo(d.openByDefault)}</Row>
          <Row label="Ignored by sandbox">{yesNo(d.ignoredBySandbox)}</Row>
          <div className="ref-badges">
            {d.flags.names.map((flag) => (
              <span key={flag.bit} className="ref-badge">{flag.name}</span>
            ))}
          </div>
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

      {(d.ownership || typeof d.factionRank === 'number') && (
        <Section title="Owner and faction">
          {d.ownership && <Row label="Owner">{linkText(d.ownership.owner)}</Row>}
          {d.ownership && <Row label="No crime">{yesNo(d.ownership.noCrime)}</Row>}
          {typeof d.factionRank === 'number' && <Row label="Faction rank">{d.factionRank}</Row>}
        </Section>
      )}

      {d.lock && (
        <Section title="Lock">
          <Row label="Level">{d.lock.level}</Row>
          {d.lock.levelName && <Row label="Level name">{d.lock.levelName}</Row>}
          {d.lock.key && <Row label="Key">{linkText(d.lock.key)}</Row>}
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
          <Row label="Shape">{d.primitive.type}</Row>
          {d.primitive.bounds && <Row label="Bounds">{vecText(d.primitive.bounds)}</Row>}
          {d.primitive.color && <Row label="Color">{d.primitive.color.join(', ')}</Row>}
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

      {(d.scripts || d.baseScripts) && (
        <Section title="Scripts" defaultOpen={false}>
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
        </Section>
      )}

      {(plugins.length > 0 || pluginNames.length > 0) && (
        <Section title="Plugins and overrides" defaultOpen={false}>
          {plugins.length > 0 ? (
            <table className="ref-props">
              <thead>
                <tr><th>Plugin</th><th>Kind</th><th>Changes</th><th>Conflicts</th><th>Status</th></tr>
              </thead>
              <tbody>
                {plugins.map((row, index) => (
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
          ) : (
            <Row label="Plugins">{pluginNames.join(', ')}</Row>
          )}
        </Section>
      )}
    </>
  );
}

export default function ReferencePanel({ reference, refRevision, expanded, onToggle, onClear, onHide }: ReferencePanelProps) {
  const formKey = reference.formKey;
  const loadKey = `${formKey}#${refRevision}`;
  const [loaded, setLoaded] = useState<LoadedReference | null>(null);

  useEffect(() => {
    if (!expanded) return undefined;
    let cancelled = false;
    void loadReference(formKey).then((result) => {
      if (!cancelled) setLoaded({ key: loadKey, ...result });
    });
    return () => {
      cancelled = true;
    };
  }, [expanded, formKey, loadKey]);

  const current = loaded?.key === loadKey ? loaded : null;
  const loading = expanded && current === null;

  return (
    <div className="cell-selected ref-panel">
      <div className="cell-selected-head">
        <span>Selected</span>
        <button className="cell-selected-clear" onClick={onClear} title="Deselect">
          <X size={12} />
        </button>
      </div>
      <div className="cell-selected-row"><span>Ref</span><strong>{reference.editorId || reference.formKey}</strong></div>
      <div className="cell-selected-row"><span>Type</span><strong>{reference.baseType || reference.recordType}</strong></div>
      <div className="cell-selected-row"><span>Base</span><strong>{reference.baseEditorId || reference.baseFormKey}</strong></div>
      <div className="cell-selected-row"><span>Pos</span><strong>{reference.position.x.toFixed(0)}, {reference.position.y.toFixed(0)}, {reference.position.z.toFixed(0)}</strong></div>
      <button className="ref-toggle" aria-expanded={expanded} onClick={onToggle}>
        {expanded ? 'Hide reference details' : 'Show reference details'}
      </button>
      <button className="cell-hide-btn" onClick={onHide}>
        <EyeOff size={12} /> Hide this mesh <kbd>1</kbd>
      </button>
      {expanded && (
        <div className="ref-details">
          {loading && <div className="ref-status">Loading reference details…</div>}
          {current?.error && <div className="ref-status ref-error">{current.error}</div>}
          {current && !current.error && current.details && (
            <ReferenceDetailSections details={current.details} plugins={current.plugins} />
          )}
          {current && !current.error && !current.details && <div className="ref-muted">No details returned.</div>}
        </div>
      )}
    </div>
  );
}

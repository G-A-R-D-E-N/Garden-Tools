import { cleanup, render, screen, waitFor, within } from '@testing-library/react';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import ReferenceInspector from './ReferenceInspector';
import type { CellPlacedReference } from './backend';
import type { InspectorTab } from './util/referenceInspector';

const rpc = vi.hoisted(() => ({
  backend: {} as Record<string, unknown>,
  cell: {} as Record<string, unknown>,
}));

vi.mock('./backend', () => ({
  getBackend: () => rpc.backend,
  getCell: () => rpc.cell,
}));

const reference = {
  formKey: '00000ABC:Plugin.esp',
  editorId: 'Ref1',
  baseType: 'STAT',
  recordType: 'REFR',
  baseEditorId: 'Rock01',
  baseFormKey: '00012345:Base.esm',
  position: { x: 1, y: 2, z: 3 },
} as CellPlacedReference;

const details = {
  formKey: '00000ABC:Plugin.esp',
  editorId: 'Ref1',
  signature: 'REFR',
  winningPlugin: 'Plugin.esp',
  pluginsWithRecord: ['Plugin.esp'],
  flags: {
    raw: 0x400,
    deleted: false,
    persistent: true,
    initiallyDisabled: false,
    respawns: true,
    names: [{ name: 'Persistent', bit: 10 }],
  },
  base: {
    formKey: '00012345:Base.esm',
    editorId: 'Rock01',
    signature: 'STAT',
    type: 'STAT',
    model: 'rock.nif',
    missing: false,
  },
  baseScripts: null,
  scripts: null,
  transform: { position: { x: 1, y: 2, z: 3 }, rotation: { x: 0, y: 0, z: 90 }, scale: 1 },
  openByDefault: false,
  ignoredBySandbox: false,
};

const editHandlers = () => ({
  patchChosen: false,
  canUndo: false,
  canRedo: false,
  onEdit: vi.fn(async () => true),
  onDelete: vi.fn(),
  onDuplicate: vi.fn(),
  onUndoEdit: vi.fn(),
  onRedoEdit: vi.fn(),
});

function panel(formKey = reference.formKey, refRevision = 0, tab: InspectorTab = 'reference') {
  return (
    <ReferenceInspector
      reference={{ ...reference, formKey } as CellPlacedReference}
      refRevision={refRevision}
      tab={tab}
      onTab={vi.fn()}
      onClose={vi.fn()}
      onDeselect={vi.fn()}
      onHide={vi.fn()}
      width={420}
      viewportWidth={1600}
      onResize={vi.fn()}
      onCommitWidth={vi.fn()}
      edit={editHandlers()}
    />
  );
}

describe('ReferencePanel sections', () => {
  beforeEach(() => {
    rpc.cell = {
      GetReferenceDetails: vi.fn(async () => JSON.stringify(details)),
    };
    rpc.backend = {
      GetRecordPluginMatrix: vi.fn(async () => '[]'),
    };
  });

  afterEach(() => {
    cleanup();
  });

  it('shows the transform section when the details carry transform data', async () => {
    render(panel());
    expect(await screen.findByText('Transform')).toBeTruthy();
    expect(screen.getByText('Identity')).toBeTruthy();
  });

  it('shows the lock as read-only rows without a patch plugin', async () => {
    rpc.cell = {
      GetReferenceDetails: vi.fn(async () => JSON.stringify({
        ...details,
        lock: { level: 254, levelName: 'Inaccessible', key: null, flags: 0 },
      })),
    };
    render(panel());
    expect(await screen.findByText('Inaccessible')).toBeTruthy();
    expect(screen.queryByRole('button', { name: 'Apply lock' })).toBeNull();
  });

  it('hides sections whose data is absent from the details', async () => {
    render(panel());
    await screen.findByText('Transform');
    expect(screen.queryByText('Lock')).toBeNull();
    expect(screen.queryByText('Teleport')).toBeNull();
    expect(screen.queryByText('Patrols')).toBeNull();
  });

  it('shows a primitive as its shape, full size and stored half-extents', async () => {
    rpc.cell = {
      GetReferenceDetails: vi.fn(async () => JSON.stringify({
        ...details,
        primitive: { type: 1, bounds: { x: 512, y: 288, z: 192 }, color: [0, 0.5, 1, 0.4] },
      })),
    };
    render(panel());
    expect(await screen.findByText('Box')).toBeTruthy();
    expect(screen.getByText('1024.00, 576.00, 384.00')).toBeTruthy();
    expect(screen.getByText('512.00, 288.00, 192.00')).toBeTruthy();
  });

  it('shows the loading status until the details arrive', async () => {
    rpc.cell = { GetReferenceDetails: vi.fn(() => new Promise<string>(() => undefined)) };
    render(panel());
    expect(screen.getByText('Loading reference details…')).toBeTruthy();
  });

  it('renders the location reference types as link text', async () => {
    rpc.cell = {
      GetReferenceDetails: vi.fn(async () => JSON.stringify({
        ...details,
        locationReferenceTypes: [{ formKey: '00002:Base.esm', editorId: 'LocType', signature: 'LCTN' }],
      })),
    };
    render(panel());
    expect(await screen.findByText('LocType (00002:Base.esm)')).toBeTruthy();
  });

  it('renders script properties and expands array elements into indexed rows', async () => {
    rpc.cell = {
      GetReferenceDetails: vi.fn(async () => JSON.stringify({
        ...details,
        scripts: {
          version: 6,
          objectFormat: 2,
          unsupported: false,
          fragmentBytes: 0,
          scripts: [{
            name: 'MyScript',
            flags: 1,
            properties: [
              { name: 'Count', type: 3, typeName: 'Int', flags: 1, elementCount: null, value: 3, elements: null },
              {
                name: 'Items',
                type: 11,
                typeName: 'Object',
                flags: 1,
                elementCount: 1,
                value: null,
                elements: [{ object: { formKey: '00001:X.esp', editorId: 'Item', signature: null }, alias: -1 }],
              },
            ],
          }],
        },
      })),
    };
    render(panel(reference.formKey, 0, 'scripts'));
    expect(await screen.findByText('On the reference')).toBeTruthy();
    expect(screen.getByText('MyScript')).toBeTruthy();
    expect(screen.getByText('Count')).toBeTruthy();
    expect(screen.getByText('Items[0]')).toBeTruthy();
    expect(screen.getByText('Item (00001:X.esp)')).toBeTruthy();
  });

  it('says when neither the reference nor its base has scripts', async () => {
    render(panel(reference.formKey, 0, 'scripts'));
    expect(await screen.findByText('No scripts on this reference or its base.')).toBeTruthy();
  });

  it('reports fragment bytes as not decoded', async () => {
    rpc.cell = {
      GetReferenceDetails: vi.fn(async () => JSON.stringify({
        ...details,
        scripts: { version: 6, objectFormat: 2, unsupported: false, fragmentBytes: 12, scripts: [] },
      })),
    };
    render(panel(reference.formKey, 0, 'scripts'));
    expect(await screen.findByText('12 bytes of fragment data (not decoded)')).toBeTruthy();
  });

  it('shows the error carried in the returned JSON', async () => {
    rpc.cell = {
      GetReferenceDetails: vi.fn(async () => '{"error":"Record not found"}'),
    };
    render(panel());
    expect(await screen.findByText('Record not found')).toBeTruthy();
  });

  it('handles a rejected details call without crashing', async () => {
    rpc.cell = {
      GetReferenceDetails: vi.fn(async () => {
        throw new Error('boom');
      }),
    };
    render(panel());
    expect(await screen.findByText(/boom/)).toBeTruthy();
    expect(screen.getByText(/Hide this mesh/)).toBeTruthy();
  });

  it('tolerates a backend that lacks GetRecordPluginMatrix', async () => {
    rpc.backend = {};
    render(panel());
    expect(await screen.findByText('Transform')).toBeTruthy();
  });

  it('shows plugin override badges from the plugin matrix', async () => {
    rpc.backend = {
      GetRecordPluginMatrix: vi.fn(async () => JSON.stringify([
        { Plugin: 'Plugin.esp', LoadOrder: 1, Kind: 'Override', Changes: 2, Conflicts: 0, IsOverride: true, IsWinner: true, LastModified: '' },
      ])),
    };
    render(panel(reference.formKey, 0, 'plugins'));
    expect(await screen.findByText('Winner')).toBeTruthy();
    expect(screen.getAllByText('Override').length).toBeGreaterThan(0);
  });

  it('lists the plugins from the details when the plugin matrix is empty', async () => {
    render(panel(reference.formKey, 0, 'plugins'));
    expect(await screen.findByText('Plugin.esp')).toBeTruthy();
  });

  it('refetches details when the refresh revision changes', async () => {
    const view = render(panel(reference.formKey, 0));
    await screen.findByText('Transform');
    view.rerender(panel(reference.formKey, 1));
    await waitFor(() => {
      const getDetails = rpc.cell.GetReferenceDetails as ReturnType<typeof vi.fn>;
      expect(getDetails).toHaveBeenCalledTimes(2);
    });
  });

  it('refetches details when the selected form key changes', async () => {
    const view = render(panel(reference.formKey, 0));
    await screen.findByText('Transform');
    view.rerender(panel('00000DEF:Plugin.esp', 0));
    await waitFor(() => {
      const getDetails = rpc.cell.GetReferenceDetails as ReturnType<typeof vi.fn>;
      expect(getDetails).toHaveBeenLastCalledWith('00000DEF:Plugin.esp');
    });
  });
});

const storedProperty = (name: string, flags: number, value: unknown, undecoded = false) => ({
  name,
  type: 3,
  typeName: 'Int',
  flags,
  elementCount: null,
  value,
  elements: null,
  undecoded,
});

const declaredProperty = (name: string, status: 'stored' | 'default', value: unknown) => ({
  name,
  type: 'Int',
  auto: true,
  conditional: false,
  readOnly: false,
  default: value,
  status,
  declaredBy: 'MyScript',
});

const scriptEntry = (overrides: Record<string, unknown>) => ({
  name: 'MyScript',
  flags: 0,
  properties: [],
  parent: null,
  declared: [],
  declaredError: null,
  ...overrides,
});

const scriptSet = (scripts: unknown[]) => ({
  version: 6,
  objectFormat: 2,
  unsupported: false,
  fragmentBytes: 0,
  scripts,
});

function scriptsPanel(scripts: unknown, baseScripts: unknown = null) {
  rpc.cell = {
    GetReferenceDetails: vi.fn(async () => JSON.stringify({ ...details, scripts, baseScripts })),
  };
  return panel(reference.formKey, 0, 'scripts');
}

describe('ReferencePanel script properties', () => {
  beforeEach(() => {
    rpc.backend = {
      GetRecordPluginMatrix: vi.fn(async () => '[]'),
    };
  });

  afterEach(() => {
    cleanup();
  });

  it('shows stored, edited, removed and default statuses for the reference set', async () => {
    render(scriptsPanel(scriptSet([scriptEntry({
      parent: 'BaseScript',
      properties: [
        storedProperty('Edited', 1, 5),
        storedProperty('Gone', 3, 1),
        storedProperty('Plain', 0, 2),
        storedProperty('Unread', 0, 4, true),
      ],
      declared: [
        declaredProperty('Edited', 'stored', 0),
        declaredProperty('Gone', 'stored', 0),
        declaredProperty('Fallback', 'default', 9),
      ],
    })])));
    expect(await screen.findByText('On the reference')).toBeTruthy();
    expect(screen.getByText('(Local)')).toBeTruthy();
    expect(screen.getByText('Extends BaseScript')).toBeTruthy();
    expect(screen.getByText('Edited (1)')).toBeTruthy();
    expect(screen.getByText('Removed (3)').closest('tr')?.className).toBe('ref-row-removed');
    expect(screen.getByText('Plain').closest('tr')?.className).toBe('');
    expect(screen.getAllByText('Stored')).toHaveLength(2);
    expect(screen.getByText('Undecoded')).toBeTruthy();
    expect(screen.getAllByText('Default')).toHaveLength(1);
    const fallback = screen.getByText('Fallback').closest('tr') as HTMLElement;
    expect(within(fallback).getByText('9')).toBeTruthy();
    expect(within(fallback).getByText('Default')).toBeTruthy();
    expect(screen.queryAllByText('Edited')).toHaveLength(1);
  });

  it('shows the alias on object values and on each element of an object array', async () => {
    render(scriptsPanel(scriptSet([scriptEntry({
      properties: [
        {
          name: 'Door',
          type: 1,
          typeName: 'Object',
          flags: 1,
          elementCount: null,
          value: { object: { formKey: '00001:X.esp', editorId: 'Gate', signature: null }, alias: 2 },
          elements: null,
          undecoded: false,
        },
        {
          name: 'Triggers',
          type: 11,
          typeName: 'Object',
          flags: 1,
          elementCount: 2,
          value: null,
          elements: [
            { object: { formKey: '00002:X.esp', editorId: 'Trig', signature: null }, alias: -1 },
            { object: { formKey: '00003:X.esp', editorId: 'Trig2', signature: null }, alias: 0 },
          ],
          undecoded: false,
        },
      ],
    })])));
    expect(await screen.findByText('Gate (00001:X.esp) (alias 2)')).toBeTruthy();
    expect(screen.getByText('Trig (00002:X.esp)')).toBeTruthy();
    expect(screen.getByText('Trig2 (00003:X.esp) (alias 0)')).toBeTruthy();
  });

  it('shows an empty array as an empty row', async () => {
    render(scriptsPanel(scriptSet([scriptEntry({
      properties: [{
        name: 'Empty',
        type: 11,
        typeName: 'Object',
        flags: 0,
        elementCount: 0,
        value: null,
        elements: [],
        undecoded: false,
      }],
    })])));
    expect(await screen.findByText('[] (empty)')).toBeTruthy();
    expect(screen.getByText('Empty')).toBeTruthy();
  });

  it('explains when the plugin stores no properties for a script', async () => {
    render(scriptsPanel(scriptSet([scriptEntry({ name: 'Quiet' })])));
    expect(await screen.findByText('No properties stored in the plugin; script defaults apply.')).toBeTruthy();
    expect(screen.getByText('Quiet')).toBeTruthy();
  });

  it('shows declared defaults for the base object set and the declaration error', async () => {
    render(scriptsPanel(null, scriptSet([scriptEntry({
      name: 'BaseScript',
      declared: [declaredProperty('Speed', 'default', 2.5)],
      declaredError: 'no compiled script BaseScript in the Data archives or Scripts folder',
    })])));
    expect(await screen.findByText('On the base object')).toBeTruthy();
    expect(screen.getByText('Speed')).toBeTruthy();
    expect(screen.getByText('2.5')).toBeTruthy();
    expect(screen.getByText('Default')).toBeTruthy();
    expect(screen.getByText(/Declared defaults unavailable: no compiled script BaseScript/)).toBeTruthy();
  });
});

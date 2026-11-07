import { cleanup, render, screen, waitFor } from '@testing-library/react';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import ReferencePanel from './ReferencePanel';
import type { CellPlacedReference } from './backend';

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

function panel(formKey = reference.formKey, refRevision = 0, expanded = true) {
  return (
    <ReferencePanel
      reference={{ ...reference, formKey } as CellPlacedReference}
      refRevision={refRevision}
      expanded={expanded}
      onToggle={vi.fn()}
      onClear={vi.fn()}
      onHide={vi.fn()}
    />
  );
}

describe('ReferencePanel', () => {
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

  it('hides sections whose data is absent from the details', async () => {
    render(panel());
    await screen.findByText('Transform');
    expect(screen.queryByText('Lock')).toBeNull();
    expect(screen.queryByText('Teleport')).toBeNull();
    expect(screen.queryByText('Patrols')).toBeNull();
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
    render(panel());
    expect(await screen.findByText('On the reference')).toBeTruthy();
    expect(screen.getByText('MyScript')).toBeTruthy();
    expect(screen.getByText('Count')).toBeTruthy();
    expect(screen.getByText('Items[0]')).toBeTruthy();
    expect(screen.getByText('Item (00001:X.esp)')).toBeTruthy();
  });

  it('reports fragment bytes as not decoded', async () => {
    rpc.cell = {
      GetReferenceDetails: vi.fn(async () => JSON.stringify({
        ...details,
        scripts: { version: 6, objectFormat: 2, unsupported: false, fragmentBytes: 12, scripts: [] },
      })),
    };
    render(panel());
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
    render(panel());
    expect(await screen.findByText('Winner')).toBeTruthy();
    expect(screen.getAllByText('Override').length).toBeGreaterThan(0);
  });

  it('does not fetch details while the panel is collapsed', async () => {
    render(panel(reference.formKey, 0, false));
    expect(screen.queryByText('Transform')).toBeNull();
    const getDetails = rpc.cell.GetReferenceDetails as ReturnType<typeof vi.fn>;
    expect(getDetails).not.toHaveBeenCalled();
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

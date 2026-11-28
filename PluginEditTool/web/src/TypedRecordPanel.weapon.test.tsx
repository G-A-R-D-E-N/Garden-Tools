import { cleanup, render, screen } from '@testing-library/react';
import { afterEach, expect, it, vi } from 'vitest';
import type { ConflictMatrix } from './backend';
import TypedRecordPanel from './TypedRecordPanel';

const formKey = '000B00:Fallout4.esm';
const matrix: ConflictMatrix = {
  FormKey: formKey, EditorID: 'TestWeapon', Type: 'WEAP', Winner: 'Fallout4.esm', Plugins: ['Fallout4.esm'], Level: 'onlyone', Rows: [],
};
const weapon = {
  formKey,
  damageTypes: [{ type: '000B01:Fallout4.esm', amount: 25, curveTable: '000B02:Fallout4.esm' }],
};

afterEach(() => { cleanup(); vi.unstubAllGlobals(); });

it('renders the damage types of the winning WEAP read-only', async () => {
  vi.stubGlobal('chrome', { webview: { hostObjects: { backend: { GetWeaponDamageTypes: vi.fn(async () => JSON.stringify(weapon)) } } } });
  render(<TypedRecordPanel matrix={matrix} plugin="Fallout4.esm" onChanged={async () => {}} />);
  expect(await screen.findByText('000B01:Fallout4.esm')).toBeTruthy();
  expect(screen.getByText('25')).toBeTruthy();
  expect(screen.getByText('000B02:Fallout4.esm')).toBeTruthy();
  expect(screen.queryAllByRole('button')).toHaveLength(0);
});

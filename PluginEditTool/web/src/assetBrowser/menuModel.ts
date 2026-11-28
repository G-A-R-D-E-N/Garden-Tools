export type MenuAction =
  | 'placeViewCentre'
  | 'placeOrigin'
  | 'copyHexId'
  | 'copyEditorId'
  | 'copyModelPath'
  | 'copyFormKey'
  | 'preview';

export interface MenuEntry {
  action: MenuAction;
  label: string;
  disabled: boolean;
}

export function menuEntries(count: number, anyModelPath: boolean): MenuEntry[] {
  const many = count > 1;
  return [
    { action: 'placeViewCentre', label: many ? `Place ${count} at view centre` : 'Place at view centre', disabled: false },
    { action: 'placeOrigin', label: many ? `Place ${count} at cell origin` : 'Place at cell origin', disabled: false },
    { action: 'copyHexId', label: many ? `Copy ${count} Form IDs` : 'Copy Form ID', disabled: false },
    { action: 'copyEditorId', label: many ? `Copy ${count} editor IDs` : 'Copy editor ID', disabled: false },
    { action: 'copyModelPath', label: many ? `Copy ${count} model paths` : 'Copy model path', disabled: !anyModelPath },
    { action: 'copyFormKey', label: many ? `Copy ${count} form keys` : 'Copy form key', disabled: false },
    { action: 'preview', label: 'Preview at view centre', disabled: false },
  ];
}

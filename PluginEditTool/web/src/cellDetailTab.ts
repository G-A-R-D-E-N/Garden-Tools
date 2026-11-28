import { useEffect, useRef, useState } from 'react';
import { LS, setLS } from './cellStorage';

export const DETAIL_TABS = [
  { id: 'cell', label: 'Cell' },
  { id: 'terrain', label: 'Terrain' },
  { id: 'world', label: 'World' },
  { id: 'view', label: 'View' }
] as const;

export type DetailTab = (typeof DETAIL_TABS)[number]['id'];

export const rememberedDetailTab = (): DetailTab => {
  const saved = LS('detailTab', '');
  return DETAIL_TABS.some(t => t.id === saved) ? (saved as DetailTab) : 'cell';
};

export const rememberDetailTab = (tab: DetailTab) => setLS('detailTab', tab);

export const tabHasContent = (tab: DetailTab, hasLand: boolean, inWorldspace: boolean) =>
  tab === 'terrain' ? hasLand : tab === 'world' ? inWorldspace : true;

export function useDetailTab(
  loadedKey: string | undefined, interior: boolean | undefined, busy: boolean,
  landLoaded: boolean, hasLand: boolean, loadSeq: { current: number }
) {
  const [detailTab, setDetailTab] = useState<DetailTab>(rememberedDetailTab);
  const chosenFor = useRef('');
  useEffect(() => {
    if (!loadedKey || busy || !landLoaded) return;
    const stamp = `${loadedKey}:${loadSeq.current}`;
    if (chosenFor.current === stamp) return;
    chosenFor.current = stamp;
    // eslint-disable-next-line react-hooks/set-state-in-effect -- once per loaded cell (stamp guard)
    if (!tabHasContent(detailTab, hasLand, !interior)) setDetailTab('cell');
  }, [loadedKey, interior, busy, landLoaded, hasLand, detailTab, loadSeq]);
  const tabSelect = (tab: DetailTab) => {
    rememberDetailTab(tab);
    setDetailTab(tab);
  };
  return [detailTab, tabSelect] as const;
}

import { useEffect, useRef, useState } from 'react';
import { Boxes, PanelTopClose } from 'lucide-react';
import { getCell, type CellLightingData, type CellPlacedReference } from '../backend';
import CellViewport, { type CellGeoMap } from '../CellViewport';
import { fetchMissingGeometry } from '../cellGeometry';
import { LIGHTING_SETTINGS_STORAGE_KEY } from '../cellLighting';
import { readStoredSettings } from '../cellLightingSettings';
import { loadCellTexture } from '../cellTextureSource';
import { openCellPopoutLink, type CellPopoutLink } from './cellPopoutChannel';
import { isCellMainMessage, type CellPopoutIntent } from './cellPopoutProtocol';
import './CellPopoutWindow.css';

export default function CellPopoutWindow() {
  const [references, setReferences] = useState<CellPlacedReference[]>([]);
  const [lighting, setLighting] = useState<CellLightingData | null>(null);
  const [hiddenTypes, setHiddenTypes] = useState<Set<string>>(() => new Set());
  const [hiddenRefs, setHiddenRefs] = useState<Set<string>>(() => new Set());
  const [selectedKey, setSelectedKey] = useState<string | null>(null);
  const [lightSettings, setLightSettings] = useState(readStoredSettings);
  const [geometry, setGeometry] = useState<CellGeoMap>({});
  const [status, setStatus] = useState('Waiting for the cell viewer…');
  const linkRef = useRef<CellPopoutLink | null>(null);
  const geometryRef = useRef<CellGeoMap>({});

  useEffect(() => {
    const link = openCellPopoutLink(value => {
      if (!isCellMainMessage(value)) return;
      if (value.kind === 'references') {
        setReferences(value.references);
        setLighting(value.lighting);
      } else if (value.kind === 'sync') {
        setHiddenTypes(new Set(value.hiddenTypes));
        setHiddenRefs(new Set(value.hiddenRefs));
        setSelectedKey(value.selectedKey);
      } else {
        window.close();
      }
    });
    linkRef.current = link;
    link.post({ kind: 'hello' });
    const bye = () => link.post({ kind: 'bye' });
    window.addEventListener('pagehide', bye);
    return () => {
      window.removeEventListener('pagehide', bye);
      bye();
      link.close();
      linkRef.current = null;
    };
  }, []);

  useEffect(() => {
    const onStorage = (event: StorageEvent) => {
      if (event.key === null || event.key === LIGHTING_SETTINGS_STORAGE_KEY) setLightSettings(readStoredSettings());
    };
    window.addEventListener('storage', onStorage);
    return () => window.removeEventListener('storage', onStorage);
  }, []);

  useEffect(() => {
    geometryRef.current = geometry;
  }, [geometry]);

  useEffect(() => {
    let live = true;
    void fetchMissingGeometry(getCell() ?? null, references, geometryRef.current, () => live, line => setStatus(line))
      .then(fetched => {
        if (live && fetched) setGeometry(prev => ({ ...prev, ...fetched }));
      });
    return () => {
      live = false;
    };
  }, [references]);

  useEffect(() => {
    const onKey = (event: KeyboardEvent) => {
      if (event.key !== 'F11') return;
      event.preventDefault();
      const toggle = document.fullscreenElement ? document.exitFullscreen() : document.documentElement.requestFullscreen();
      void toggle.catch(() => undefined);
    };
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
  }, []);

  const send = (intent: CellPopoutIntent) => linkRef.current?.post(intent);

  const dockBack = () => {
    send({ kind: 'bye' });
    window.close();
  };

  return (
    <div className="cell-popout-root">
      <div className="cell-popout-bar">
        <span className="cell-popout-title"><Boxes size={14} /> Cell viewport</span>
        <span className="cell-popout-status">{status}</span>
        <button className="papyrus-copy" onClick={dockBack} title="Return the viewport to the cell viewer">
          <PanelTopClose size={13} /> Dock back
        </button>
      </div>
      <div className="cell-popout-stage">
        <CellViewport
          references={references}
          geometry={geometry}
          lighting={lighting}
          lightSettings={lightSettings}
          loadTexture={loadCellTexture}
          hiddenTypes={hiddenTypes}
          hiddenRefs={hiddenRefs}
          selectedKey={selectedKey}
          onSelect={ref => send({ kind: 'select', formKey: ref?.formKey ?? null })}
          onHideSelected={() => { if (selectedKey) send({ kind: 'hide', formKey: selectedKey }); }}
          onUnhideAll={() => send({ kind: 'unhideAll' })}
          onToggleMarkers={() => send({ kind: 'toggleMarkers' })}
          onDeleteSelected={() => { if (selectedKey) send({ kind: 'delete', formKey: selectedKey }); }}
          onDuplicateSelected={() => { if (selectedKey) send({ kind: 'duplicate', formKey: selectedKey }); }}
          onMoveEnd={reference => send({ kind: 'move', reference })}
          onReferenceDoubleClick={reference => send({ kind: 'details', formKey: reference.formKey })}
          onPlaceDrop={(payload, point) => send({ kind: 'drop', payload, point })}
          onDropLog={line => send({ kind: 'log', line })}
        />
        {references.length === 0 && <div className="nif-view-empty cell-view-overlay">No cell is loaded in the cell viewer.</div>}
      </div>
    </div>
  );
}

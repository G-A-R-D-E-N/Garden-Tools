import { useState } from 'react';
import { Layers } from 'lucide-react';
import type { HkxHost } from './backend';
import { describeScene, readScene, type HkxScene } from './hkxSceneClient';

const messageOf = (e: unknown) => (e instanceof Error ? e.message : String(e));

interface Props {
  host?: HkxHost;
  initialPath: string;
  disabled: boolean;
}

export default function HkxSceneTools({ host, initialPath, disabled }: Props) {
  const [path, setPath] = useState(initialPath);
  const [scene, setScene] = useState<HkxScene | null>(null);
  const [notice, setNotice] = useState('');
  const [error, setError] = useState('');
  const [busy, setBusy] = useState(false);

  const read = async () => {
    if (!host || !path.trim()) return;
    setBusy(true);
    setError('');
    try {
      const found = await readScene(host, path.trim());
      setScene(found);
      setNotice(describeScene(found));
    } catch (e) {
      setScene(null);
      setError(messageOf(e));
      setNotice('');
    } finally {
      setBusy(false);
    }
  };

  return (
    <div className="nif-flags">
      <div className="nif-flags-head">
        <span className="nif-flabel"><Layers size={12} /> Asset structure</span>
        <input className="nif-in" value={path} disabled={disabled || busy} aria-label="Asset path"
               placeholder="Path to an .hkx file"
               onChange={e => setPath(e.target.value)} />
        <button className="sidebar-action-btn" disabled={disabled || busy || !path.trim()} onClick={() => void read()}>
          Read asset structure
        </button>
      </div>

      {scene ? (
        <>
          <ul className="nif-flabel" role="list" aria-label="Named variants">
            {scene.variants.map((variant, at) => (
              <li key={at}>
                {variant.name || `variant ${at}`}: {variant.class_name || variant.declared_class || 'no object'}
                {variant.object >= 0 ? ` (object ${variant.object})` : ''}
                {variant.object >= 0 && !variant.layout_known ? ' no verified layout' : ''}
              </li>
            ))}
          </ul>
          {scene.scene_objects.length > 0 ? (
            <ul className="nif-flabel" role="list" aria-label="Scene classes">
              {scene.scene_objects.map(item => (
                <li key={item.object}>{item.class_name} (object {item.object}, {item.size} bytes)</li>
              ))}
            </ul>
          ) : null}
          {scene.unknown_classes.length > 0 ? (
            <div className="papyrus-warn" aria-label="Unknown classes">
              Kept as opaque bytes: {scene.unknown_classes.join(', ')}
            </div>
          ) : null}
          {!scene.metadata.decoded ? (
            <div className="papyrus-warn" aria-label="Scene metadata">Scene metadata: {scene.metadata.reason}</div>
          ) : null}
          {scene.diagnostics.length > 0 ? (
            <ul className="papyrus-warn" role="list" aria-label="Scene diagnostics">
              {scene.diagnostics.map((line, at) => <li key={at}>{line}</li>)}
            </ul>
          ) : null}
        </>
      ) : null}

      {error ? <div className="papyrus-warn" role="alert">{error}</div> : null}
      {notice ? <div className="nif-flabel" role="status">{notice}</div> : null}
    </div>
  );
}

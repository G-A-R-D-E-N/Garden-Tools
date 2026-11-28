import { useState } from 'react';
import { FileInput } from 'lucide-react';
import type { HkxHost } from './backend';
import { describeGltfSave, saveGltfAnimation } from './hkxGltfSaveClient';

const messageOf = (e: unknown) => (e instanceof Error ? e.message : String(e));

interface Props {
  host?: HkxHost;
  skeletonPath: string;
  templatePath: string;
  disabled: boolean;
}

export default function HkxGltfSavePanel({ host, skeletonPath, templatePath, disabled }: Props) {
  const [template, setTemplate] = useState(templatePath);
  const [skeleton, setSkeleton] = useState(skeletonPath);
  const [gltf, setGltf] = useState('');
  const [output, setOutput] = useState('');
  const [animation, setAnimation] = useState('');
  const [skin, setSkin] = useState('');
  const [replaceAnnotations, setReplaceAnnotations] = useState(false);
  const [replaceMotion, setReplaceMotion] = useState(false);
  const [overwrite, setOverwrite] = useState(false);
  const [notice, setNotice] = useState('');
  const [error, setError] = useState('');
  const [busy, setBusy] = useState(false);

  // Invariant: a path the clip loader changes replaces the typed one, so the panel never saves over a stale template or skeleton.
  const [seenTemplate, setSeenTemplate] = useState(templatePath);
  if (seenTemplate !== templatePath) {
    setSeenTemplate(templatePath);
    setTemplate(templatePath);
  }
  const [seenSkeleton, setSeenSkeleton] = useState(skeletonPath);
  if (seenSkeleton !== skeletonPath) {
    setSeenSkeleton(skeletonPath);
    setSkeleton(skeletonPath);
  }

  const save = async () => {
    if (!host) return;
    setBusy(true);
    setError('');
    setNotice('');
    try {
      const saved = await saveGltfAnimation(host, {
        templatePath: template, skeletonPath: skeleton, gltfPath: gltf, outputPath: output, animation, skin,
        replaceAnnotations, replaceMotion, overwrite,
      });
      setNotice(describeGltfSave(saved));
    } catch (e) {
      setError(messageOf(e));
    } finally {
      setBusy(false);
    }
  };

  const off = disabled || busy;
  return (
    <div className="nif-meshtools">
      <div className="nif-flags-head">
        <span className="nif-flabel"><FileInput size={12} /> Save glTF animation into a template</span>
        <input className="nif-in" value={template} disabled={off} aria-label="Template HKX path"
               placeholder="Template .hkx (one clip and its binding)" onChange={e => setTemplate(e.target.value)} />
        <input className="nif-in" value={skeleton} disabled={off} aria-label="Skeleton HKX path"
               placeholder="Skeleton .hkx" onChange={e => setSkeleton(e.target.value)} />
        <input className="nif-in" value={gltf} disabled={off} aria-label="glTF path"
               placeholder="glTF or glb file" onChange={e => setGltf(e.target.value)} />
        <input className="nif-in" value={output} disabled={off} aria-label="Output HKX path"
               placeholder="New .hkx to write" onChange={e => setOutput(e.target.value)} />
        <input className="nif-in" value={animation} disabled={off} aria-label="glTF animation name"
               placeholder="animation name (optional)" onChange={e => setAnimation(e.target.value)} />
        <input className="nif-in" value={skin} disabled={off} aria-label="glTF skin name"
               placeholder="skin name (optional)" onChange={e => setSkin(e.target.value)} />
        <label className="nif-flabel">
          <input type="checkbox" checked={replaceAnnotations} disabled={off}
                 onChange={e => setReplaceAnnotations(e.target.checked)} />
          replace the template's notes
        </label>
        <label className="nif-flabel">
          <input type="checkbox" checked={replaceMotion} disabled={off}
                 onChange={e => setReplaceMotion(e.target.checked)} />
          replace the template's root motion
        </label>
        <label className="nif-flabel">
          <input type="checkbox" checked={overwrite} disabled={off}
                 onChange={e => setOverwrite(e.target.checked)} />
          overwrite an existing output
        </label>
        <button className="sidebar-action-btn" disabled={off || !host || !template.trim() || !skeleton.trim() || !gltf.trim() || !output.trim()}
                onClick={() => void save()}>
          Save animation
        </button>
      </div>
      {notice && <div className="nif-geo-info" role="status">{notice}</div>}
      {error && <div className="papyrus-banner error" role="alert"><span className="papyrus-banner-text">{error}</span></div>}
    </div>
  );
}

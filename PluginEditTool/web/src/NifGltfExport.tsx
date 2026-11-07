import { useState } from 'react';
import { getNif } from './backend';
import { useDialogs } from './useDialogs';

const GLB_FILTER = 'glTF binary (*.glb)|*.glb';
const baseName = (p: string) => p.replace(/[\\/]+$/, '').split(/[\\/]/).pop() || p;

const TEXTURE_MODES = [
  { value: 'png', label: 'Decode to PNG beside the .glb (default)' },
  { value: 'copy', label: 'Copy DDS beside the .glb' },
  { value: 'embed', label: 'Embed DDS in the .glb' },
  { value: 'uri', label: 'Relative paths only' },
];

export default function NifGltfExport({ nifPath, onResult, appendLog }: {
  nifPath: string;
  onResult: (text: string) => void;
  appendLog: (line: string) => void;
}) {
  const [busy, setBusy] = useState(false);
  const [materials, setMaterials] = useState(true);
  const [textures, setTextures] = useState('png');
  const [tangents, setTangents] = useState(true);
  const [vertexColours, setVertexColours] = useState(true);
  const { confirm: askConfirm } = useDialogs();
  const nif = getNif();

  const exportGltf = async () => {
    if (!nif || !nifPath.trim()) return;
    setBusy(true);
    try {
      const out = await nif.BrowseForSave('Export glTF as', GLB_FILTER);
      if (!out) return;
      const run = (overwrite: boolean) =>
        nif.ExportGltf(nifPath, out, overwrite, materials, textures, tangents, vertexColours);
      let text = await run(false);
      if (text.startsWith('EXISTS:')) {
        const existing = baseName(text.slice(text.lastIndexOf(': ') + 2));
        const replace = await askConfirm({
          title: 'Replace existing file?',
          message: `${existing} already exists. Overwrite it with the export of ${baseName(nifPath)}?`,
          confirmLabel: 'Overwrite',
          danger: true,
        });
        if (!replace) {
          onResult(`Export cancelled -- ${baseName(out)} was left unchanged.`);
          return;
        }
        text = await run(true);
      }
      onResult(text);
      appendLog(`${/RESULT: gltf export OK/.test(text) ? '✓' : '✗'} export ${baseName(nifPath)} → ${baseName(out)}`);
    } catch (e) {
      const msg = 'Error: ' + (e instanceof Error ? e.message : String(e));
      onResult(msg);
      appendLog(`✗ export -- ${msg}`);
    } finally { setBusy(false); }
  };

  return (
    <div className="papyrus-field">
      <label>
        <input type="checkbox" checked={materials} onChange={e => setMaterials(e.target.checked)} /> Materials and textures
      </label>
      <label>
        <select value={textures} disabled={!materials} onChange={e => setTextures(e.target.value)}>
          {TEXTURE_MODES.map(mode => <option key={mode.value} value={mode.value}>{mode.label}</option>)}
        </select>
      </label>
      <label>
        <input type="checkbox" checked={tangents} onChange={e => setTangents(e.target.checked)} /> Tangents
      </label>
      <label>
        <input type="checkbox" checked={vertexColours} onChange={e => setVertexColours(e.target.checked)} /> Vertex colours
      </label>
      <button className="sidebar-action-btn" onClick={exportGltf} disabled={!nif || busy || !nifPath.trim()}>
        Export glTF…
      </button>
    </div>
  );
}

import { useState } from 'react';
import { getNif } from './backend';
import { useDialogs } from './useDialogs';

const baseName = (p: string) => p.replace(/[\\/]+$/, '').split(/[\\/]/).pop() || p;

export default function NifGltfImport({ gltfPath, outNif, skeleton, material, texDiffuse, texNormal, collision, onResult, appendLog }: {
  gltfPath: string;
  outNif: string;
  skeleton: string;
  material: string;
  texDiffuse: string;
  texNormal: string;
  collision: boolean;
  onResult: (text: string) => void;
  appendLog: (line: string) => void;
}) {
  const [busy, setBusy] = useState(false);
  const { confirm: askConfirm } = useDialogs();
  const nif = getNif();

  const run = async () => {
    if (!nif || !gltfPath.trim() || !outNif.trim()) return;
    const importGltf = (overwrite: boolean) =>
      nif.ImportGltf(gltfPath, outNif, material, texDiffuse, texNormal, collision, skeleton, overwrite);
    setBusy(true);
    try {
      let text = await importGltf(false);
      if (text.startsWith('EXISTS:')) {
        const replace = await askConfirm({
          title: 'Replace existing NIF?',
          message: `${baseName(outNif)} already exists. Overwrite it with the import of ${baseName(gltfPath)}?`,
          confirmLabel: 'Overwrite',
          danger: true,
        });
        if (!replace) {
          onResult(`Import cancelled -- ${baseName(outNif)} was left unchanged.`);
          return;
        }
        text = await importGltf(true);
      }
      onResult(text);
      appendLog(`${/RESULT: import glTF OK/.test(text) ? '✓' : '✗'} import ${baseName(gltfPath)} → ${baseName(outNif)}`);
    } catch (e) {
      const msg = 'Error: ' + (e instanceof Error ? e.message : String(e));
      onResult(msg);
      appendLog(`✗ import glTF -- ${msg}`);
    } finally { setBusy(false); }
  };

  return (
    <button className="sidebar-action-btn" onClick={run} disabled={!nif || busy || !gltfPath.trim() || !outNif.trim()}>
      Import glTF
    </button>
  );
}

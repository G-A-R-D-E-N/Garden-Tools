import { useState } from 'react';
import { numOf } from './util/numInput';

type Props = {
  hasSel: boolean;
  canAverage: boolean;
  canPaste: boolean;
  stretch: boolean;
  allShapes: boolean;
  onAllShapes: (on: boolean) => void;
  onStretch: (on: boolean) => void;
  onPack: (margin: number) => void;
  onAverage: () => void;
  onSmart: (margin: number) => void;
  onRelax: (iterations: number) => void;
  onStitch: (eps: number) => void;
  onSelectOverlap: () => void;
  onCopy: () => void;
  onPaste: () => void;
  seamCount: number;
  onMarkSeam: () => void;
  onClearSeams: () => void;
  onSplitSeam: () => void;
  pinCount: number;
  onPin: () => void;
  onUnpin: () => void;
  canUnwrap: boolean;
  onUnwrap: () => void;
  onExportPng: () => void;
};

export default function UvLayoutTools({
  hasSel, canAverage, canPaste, stretch, allShapes, onAllShapes, onStretch, onPack, onAverage, onSmart, onRelax, onStitch, onSelectOverlap, onCopy, onPaste,
  seamCount, onMarkSeam, onClearSeams, onSplitSeam, pinCount, onPin, onUnpin, canUnwrap, onUnwrap, onExportPng,
}: Props) {
  const [margin, setMargin] = useState(0.01);
  const [iterations, setIterations] = useState(10);
  const [eps, setEps] = useState(0.0001);
  return (
    <div className="nif-uv-toolbar">
      <button className="sidebar-action-btn" disabled={!hasSel && !allShapes} onClick={() => onPack(margin)} title="Pack selected islands into 0-1">Pack</button>
      <input className="nif-in nif-uv-num" type="number" step="any" aria-label="Pack margin" value={margin}
        onChange={e => setMargin(Math.max(0, numOf(e)))} />
      <button className="sidebar-action-btn" disabled={!canAverage && !allShapes} onClick={onAverage} title="Give the two or more selected islands the average UV scale">Average scale</button>
      <button className="sidebar-action-btn" disabled={!hasSel && !allShapes} onClick={() => onSmart(margin)} title="Project each selected island onto its own plane and pack them">Smart project</button>
      <button className="sidebar-action-btn" disabled={!hasSel && !allShapes} onClick={() => onRelax(iterations)} title="Relax selected UVs toward even triangles">Relax</button>
      <input className="nif-in nif-uv-num" type="number" step="1" min={0} max={200} aria-label="Relax iterations" value={iterations}
        onChange={e => setIterations(Math.min(200, Math.max(0, Math.round(numOf(e)))))} />
      <button className="sidebar-action-btn" disabled={!hasSel && !allShapes} onClick={() => onStitch(eps)} title="Merge selected UVs that sit within the distance">Stitch</button>
      <input className="nif-in nif-uv-num" type="number" step="any" aria-label="Stitch distance" value={eps}
        onChange={e => setEps(Math.max(0, numOf(e)))} />
      <button className="sidebar-action-btn" onClick={onSelectOverlap} title="Select faces whose UVs overlap other faces">Select overlap</button>
      <label className="nif-uv-check"><input type="checkbox" checked={stretch} onChange={e => onStretch(e.target.checked)} /> Stretch</label>
      <button className="sidebar-action-btn" onClick={onCopy} title="Copy this shape's UVs">Copy UVs</button>
      <button className="sidebar-action-btn" disabled={!canPaste} onClick={onPaste} title="Paste copied UVs onto selected vertices (all if none selected)">Paste UVs</button>
      <button className="sidebar-action-btn" disabled={!hasSel} onClick={onMarkSeam} title="Mark every selected mesh edge as a UV seam">Mark seam</button>
      <button className="sidebar-action-btn" disabled={!seamCount} onClick={onClearSeams} title="Clear all seam marks">Clear seams</button>
      <button className="sidebar-action-btn" disabled={!seamCount} onClick={onSplitSeam} title="Duplicate the vertices along the marked seams so each side gets its own UVs">Split seams</button>
      <button className="sidebar-action-btn" disabled={!hasSel} onClick={onPin} title="Pin the selected UVs so unwrap holds them">Pin</button>
      <button className="sidebar-action-btn" disabled={!pinCount} onClick={onUnpin} title="Unpin the selected UVs (all pins if none selected)">Unpin</button>
      <button className="sidebar-action-btn" disabled={!canUnwrap} onClick={onUnwrap} title="LSCM unwrap the selected islands with the pins held">Unwrap</button>
      <button className="sidebar-action-btn" onClick={onExportPng} title="Save the current UV layout as a PNG">Export PNG</button>
      <label className="nif-uv-check"><input type="checkbox" checked={allShapes} onChange={e => onAllShapes(e.target.checked)} /> All shapes</label>
    </div>
  );
}

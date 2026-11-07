import { useEffect, useMemo, useRef, useState, type KeyboardEvent, type MouseEvent, type WheelEvent } from 'react';
import { RotateCcw, RotateCw, Save, Undo2 } from 'lucide-react';
import type { NifHost } from './backend';
import type { NifGeo, NifShapeGeo } from './NifViewport';
import { allVerts, frameView, islands, resolvePivot, transformSel, type PivotMode, type UvBox, type UvPoint, type UvView, type UvXform } from './util/uvMath';
import { alignSel, boxVerts, clampTile, invert, linked, mergeAtCentre, mergeByDistance, pickEdge, pickFace, selectedTris, snapGrid, snapPixels, visibleVerts, type Align, type SelectMode } from './util/uvOps';
import { pushHistory, redoHistory, undoHistory, type UndoHistory } from './util/undoHistory';
import { averageIslandScale, packIslands, triStretch } from './util/uvPack';
import { overlappingFaces, relaxUvs, stitchSelection } from './util/uvSolve';
import { smartProjectInfo } from './util/uvProject';
import { numOf } from './util/numInput';
import { edgeKey, seamsInSelection, splitPlan } from './util/uvSplit';
import { lscmUnwrapInfo } from './util/uvUnwrap';
import UvLayoutTools from './UvLayoutTools';

type UvShape = NifShapeGeo & { block: number };
type UvMap = Record<number, number[]>;
type Doc = { cur: UvMap; hist: UndoHistory<UvMap> };
type Gesture = { kind: 'G' | 'S' | 'R'; start: number[]; sel: ReadonlySet<number>; pivot: UvPoint; origin: UvPoint; pointer: UvPoint; axis: 'x' | 'y' | null };
type Press = { x: number; y: number; hits: number[]; shift: boolean; moved: boolean };
type Rect = { x0: number; y0: number; x1: number; y1: number };
type Xf = { du: number; dv: number; sx: number; sy: number; deg: number };
type Paint = { w: number; h: number; view: UvView; tris: number[]; uvs: number[]; sel: ReadonlySet<number>; box: Rect | null; tex: HTMLImageElement | undefined; label: string; faces: number[]; vis: ReadonlySet<number>; hidden: ReadonlySet<number>; stretch: number[] | null; seams: ReadonlySet<number>; pins: ReadonlySet<number> };

const PICK_PX = 8;
const BG = '#17181c';
const ORANGE = '#ff9800';
const FLIP_PIVOT: UvPoint = { u: 0.5, v: 0.5 };
const DEFAULT_XF: Xf = { du: 0, dv: 0, sx: 1, sy: 1, deg: 0 };
const MODES: readonly SelectMode[] = ['vertex', 'edge', 'face', 'island'];

const EMPTY_UVS: number[] = [];
const isUvShape = (s: NifShapeGeo): s is UvShape => (s.uvs.length > 0 || (s.uvs2?.length ?? 0) > 0) && typeof s.block === 'number';
const initialUvs = (shapes: UvShape[], channel: number): UvMap =>
  Object.fromEntries(shapes.map((s): [number, number[]] => [s.block, channel === 1 ? (s.uvs2 ?? EMPTY_UVS) : s.uvs]));
const nearestOf = (uvs: readonly number[], verts: ReadonlySet<number>, u: number, v: number, maxDist: number) => {
  let best = -1, bestD = maxDist * maxDist;
  for (const i of verts) {
    const d = (uvs[2 * i] - u) ** 2 + (uvs[2 * i + 1] - v) ** 2;
    if (d <= bestD) { best = i; bestD = d; }
  }
  return best;
};

function gestureXf(g: Gesture, p: UvPoint): UvXform {
  const id: UvXform = { tx: 0, ty: 0, sx: 1, sy: 1, rotRad: 0, pivot: g.pivot };
  if (g.kind === 'G') return { ...id, tx: g.axis === 'y' ? 0 : p.u - g.origin.u, ty: g.axis === 'x' ? 0 : p.v - g.origin.v };
  if (g.kind === 'R') return { ...id, rotRad: Math.atan2(p.v - g.pivot.v, p.u - g.pivot.u) - Math.atan2(g.origin.v - g.pivot.v, g.origin.u - g.pivot.u) };
  const a = Math.hypot(g.origin.u - g.pivot.u, g.origin.v - g.pivot.v);
  const d = a < 1e-9 ? 1 : Math.hypot(p.u - g.pivot.u, p.v - g.pivot.v) / a;
  return { ...id, sx: g.axis === 'y' ? 1 : d, sy: g.axis === 'x' ? 1 : d };
}

const modalLabel = (g: Gesture) =>
  `${g.kind === 'G' ? 'Move' : g.kind === 'S' ? 'Scale' : 'Rotate'}${g.axis ? ` (${g.axis.toUpperCase()} only)` : ''} -- Enter or click commits, Esc cancels`;

function checker(ctx: CanvasRenderingContext2D, x0: number, y0: number, s: number) {
  const c = s / 8;
  for (let i = 0; i < 8; i++) for (let j = 0; j < 8; j++) {
    ctx.fillStyle = (i + j) % 2 ? '#2a2c33' : '#23252b';
    ctx.fillRect(x0 + i * c, y0 + j * c, c, c);
  }
}

function grid(ctx: CanvasRenderingContext2D, p: Paint) {
  const { scale: s, px, py } = p.view;
  let step = 0.125;
  while (step * s < 12) step *= 2;
  while (step * s > 96) step /= 2;
  ctx.beginPath();
  for (let u = Math.floor(-px / s / step) * step; u * s + px <= p.w; u += step) {
    ctx.moveTo(u * s + px, 0); ctx.lineTo(u * s + px, p.h);
  }
  for (let v = Math.floor(-py / s / step) * step; v * s + py <= p.h; v += step) {
    ctx.moveTo(0, v * s + py); ctx.lineTo(p.w, v * s + py);
  }
  ctx.strokeStyle = 'rgba(255,255,255,0.07)';
  ctx.stroke();
}

function paint(ctx: CanvasRenderingContext2D, p: Paint) {
  const { scale: s, px, py } = p.view;
  const X = (u: number) => u * s + px;
  const Y = (v: number) => v * s + py;
  ctx.fillStyle = BG;
  ctx.fillRect(0, 0, p.w, p.h);
  if (p.tex) ctx.drawImage(p.tex, px, py, s, s);
  else checker(ctx, px, py, s);
  grid(ctx, p);
  ctx.strokeStyle = '#00a8a8';
  ctx.strokeRect(px, py, s, s);
  if (p.stretch) {
    for (let f = 0; 3 * f + 2 < p.tris.length; f++) {
      if (p.hidden.has(f)) continue;
      const a = p.tris[3 * f], b = p.tris[3 * f + 1], c = p.tris[3 * f + 2];
      ctx.beginPath();
      ctx.moveTo(X(p.uvs[2 * a]), Y(p.uvs[2 * a + 1]));
      ctx.lineTo(X(p.uvs[2 * b]), Y(p.uvs[2 * b + 1]));
      ctx.lineTo(X(p.uvs[2 * c]), Y(p.uvs[2 * c + 1]));
      ctx.closePath();
      ctx.fillStyle = `hsla(${120 - 120 * p.stretch[f]}, 70%, 45%, 0.45)`;
      ctx.fill();
    }
  }
  ctx.beginPath();
  for (const f of p.faces) {
    const a = p.tris[3 * f], b = p.tris[3 * f + 1], c = p.tris[3 * f + 2];
    ctx.moveTo(X(p.uvs[2 * a]), Y(p.uvs[2 * a + 1]));
    ctx.lineTo(X(p.uvs[2 * b]), Y(p.uvs[2 * b + 1]));
    ctx.lineTo(X(p.uvs[2 * c]), Y(p.uvs[2 * c + 1]));
    ctx.closePath();
  }
  ctx.fillStyle = 'rgba(255,152,0,0.25)';
  ctx.fill();
  const edges = (keep: (a: number, b: number) => boolean, color: string, width: number) => {
    ctx.beginPath();
    for (let f = 0; 3 * f + 2 < p.tris.length; f++) {
      if (p.hidden.has(f)) continue;
      for (let e = 0; e < 3; e++) {
        const a = p.tris[3 * f + e], b = p.tris[3 * f + ((e + 1) % 3)];
        if (!keep(a, b)) continue;
        ctx.moveTo(X(p.uvs[2 * a]), Y(p.uvs[2 * a + 1]));
        ctx.lineTo(X(p.uvs[2 * b]), Y(p.uvs[2 * b + 1]));
      }
    }
    ctx.strokeStyle = color;
    ctx.lineWidth = width;
    ctx.stroke();
  };
  edges(() => true, 'rgba(170,215,235,0.45)', 1);
  edges((a, b) => p.sel.has(a) && p.sel.has(b), ORANGE, 2);
  edges((a, b) => p.seams.has(edgeKey(a, b)), '#ff3b3b', 2);
  for (let i = 0; 2 * i + 1 < p.uvs.length; i++) {
    if (!p.vis.has(i)) continue;
    ctx.fillStyle = p.pins.has(i) ? '#00b0ff' : p.sel.has(i) ? ORANGE : '#e6e8eb';
    ctx.fillRect(X(p.uvs[2 * i]) - 2.5, Y(p.uvs[2 * i + 1]) - 2.5, 5, 5);
  }
  ctx.lineWidth = 1;
  if (p.box) {
    const { x0, y0, x1, y1 } = p.box;
    ctx.setLineDash([4, 3]);
    ctx.strokeStyle = ORANGE;
    ctx.strokeRect(Math.min(x0, x1), Math.min(y0, y1), Math.abs(x1 - x0), Math.abs(y1 - y0));
    ctx.setLineDash([]);
  }
  if (p.label) {
    ctx.fillStyle = '#e6e8eb';
    ctx.font = '12px sans-serif';
    ctx.fillText(p.label, 8, 16);
  }
}

export default function NifUvEditor(
  { geo, nif, nifPath, texRoot, onSaved, onDirtyChange, appendLog, onSelectionChange, onApi }: {
    geo: NifGeo;
    nif: NifHost;
    nifPath: string;
    texRoot: string;
    onSaved: (result: string) => void;
    onDirtyChange: (dirty: boolean) => void;
    appendLog: (line: string) => void;
    onSelectionChange?: (block: number | undefined, verts: readonly number[]) => void;
    onApi?: (api: { applyPick: (block: number, verts: readonly number[]) => void } | null) => void;
  }
) {
  const shapes = useMemo(() => geo.shapes.filter(isUvShape), [geo]);
  const [channel, setChannel] = useState(0);
  const base = useMemo(() => initialUvs(shapes, channel), [shapes, channel]);
  const reported = useRef<{ blk?: number; sel: ReadonlySet<number> } | null>(null);
  const [doc, setDoc] = useState<Doc>(() => ({ cur: base, hist: { undo: [], redo: [] } }));
  const [chosen, setChosen] = useState<number | null>(null);
  const [sel, setSel] = useState<ReadonlySet<number>>(() => new Set<number>());
  const [modal, setModal] = useState<Gesture | null>(null);
  const [box, setBox] = useState<Rect | null>(null);
  const [view, setView] = useState<UvView | null>(null);
  const [texes, setTexes] = useState<Record<number, HTMLImageElement>>({});
  const [xf, setXf] = useState<Xf>(DEFAULT_XF);
  const [lock, setLock] = useState(true);
  const [pivotMode, setPivotMode] = useState<PivotMode>('centre');
  const [selectMode, setSelectMode] = useState<SelectMode>('vertex');
  const [allShapes, setAllShapes] = useState(false);
  const [hidden, setHidden] = useState<ReadonlySet<number>>(() => new Set<number>());
  const [weld, setWeld] = useState(0.001);
  const [alignTo, setAlignTo] = useState<Align>('centre');
  const [snapStep, setSnapStep] = useState(0.125);
  const [texelCentre, setTexelCentre] = useState(false);
  const [stretch, setStretch] = useState(false);
  const [seams, setSeams] = useState<ReadonlySet<number>>(() => new Set<number>());
  const [pins, setPins] = useState<ReadonlySet<number>>(() => new Set<number>());
  const [clipCount, setClipCount] = useState(0);
  const [saving, setSaving] = useState(false);
  const [saveError, setSaveError] = useState('');
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const clipRef = useRef<number[]>([]);
  const hoverRef = useRef<UvPoint | null>(null);
  const pressRef = useRef<Press | null>(null);
  const panRef = useRef<{ x: number; y: number; view: UvView } | null>(null);
  const requested = useRef(new Set<string>());

  const shape: UvShape | undefined = shapes.find(s => s.block === chosen) ?? shapes[0];
  const blk = shape?.block;
  const uvs = blk === undefined ? [] : doc.cur[blk];
  const ids = useMemo(() => (shape ? islands(shape.verts.length / 3, shape.tris) : []), [shape]);
  const islandCount = new Set(ids).size;
  const dirtyBlocks = shapes.filter(s => doc.cur[s.block] !== base[s.block]).map(s => s.block);
  const dirty = dirtyBlocks.length > 0;
  const hasUv2 = shapes.some(s => (s.uvs2?.length ?? 0) > 0);
  const pickChannel = (next: number) => {
    if (next === channel) return;
    if (dirty) { appendLog('✗ save or revert the current channel first'); return; }
    setChannel(next);
    const withData = shapes.filter(s => (next === 1 ? (s.uvs2?.length ?? 0) : s.uvs.length) > 0);
    if (withData.length > 0 && shape && !withData.some(s => s.block === shape.block)) setChosen(withData[0].block);
    setSel(new Set<number>());
    setDoc({ cur: initialUvs(shapes, next), hist: { undo: [], redo: [] } });
  };
  const tex = blk === undefined ? undefined : texes[blk];
  const preview = modal ? transformSel(modal.start, modal.sel, gestureXf(modal, modal.pointer)) : null;
  const drawUvs = preview ?? uvs;
  const tris = shape?.tris ?? [];
  const vc = uvs.length >> 1;
  const vis = visibleVerts(vc, tris, hidden);
  const hideable = selectedTris(tris, vc, sel, hidden).length > 0;
  const canPaste = clipCount > 0 && clipCount === uvs.length;
  const stretchVals = useMemo(() => (stretch && shape && !modal ? triStretch(uvs, shape.verts, tris) : null), [stretch, shape, tris, uvs, modal]);
  const selIslandCount = useMemo(() => new Set(Array.from(sel).map(i => ids[i])).size, [sel, ids]);

  const canvasSize = () => ({ w: canvasRef.current?.clientWidth || 600, h: canvasRef.current?.clientHeight || 400 });
  const viewOf = (): UvView => {
    if (view) return view;
    const { w, h } = canvasSize();
    return frameView(shape?.uvs ?? [], w, h);
  };
  const frameAll = () => { const { w, h } = canvasSize(); setView(frameView(uvs, w, h)); };
  const local = (e: MouseEvent<HTMLCanvasElement>) => {
    const r = e.currentTarget.getBoundingClientRect();
    return { x: e.clientX - r.left, y: e.clientY - r.top };
  };
  const toUv = (x: number, y: number, v: UvView): UvPoint => ({ u: (x - v.px) / v.scale, v: (y - v.py) / v.scale });
  const nearestVert = (p: UvPoint, v: UvView) => nearestOf(uvs, vis, p.u, p.v, PICK_PX / v.scale);
  const islandOf = (i: number) => linked(ids, new Set([i])).filter(k => vis.has(k));

  useEffect(() => {
    onDirtyChange(dirty);
    return () => onDirtyChange(false);
  }, [dirty, onDirtyChange]);

  useEffect(() => {
    if (!onSelectionChange) return;
    if (blk === undefined) {
      if (reported.current !== null) { reported.current = null; onSelectionChange(undefined, []); }
      return;
    }
    const prev = reported.current;
    if (prev && prev.blk === blk && prev.sel.size === sel.size && Array.from(sel).every(i => prev.sel.has(i))) return;
    reported.current = { blk, sel };
    onSelectionChange(blk, Array.from(sel));
  }, [sel, blk, onSelectionChange]);

  useEffect(() => {
    if (!onSelectionChange) return;
    return () => onSelectionChange(undefined, []);
  }, [onSelectionChange]);

  useEffect(() => {
    if (!onApi) return;
    onApi({ applyPick: (block, verts) => { setChosen(block); setSel(new Set(verts)); } });
    return () => onApi(null);
  }, [onApi]);

  useEffect(() => {
    if (!shape) return;
    const block = shape.block;
    const rel = shape.textures?.find(t => t.slot === 0)?.path;
    const key = `${texRoot}|${block}`;
    if (!rel || requested.current.has(key)) return;
    requested.current.add(key);
    nif.GetTexture(nifPath, rel, texRoot).then(url => {
      if (!url) return;
      const img = new Image();
      img.onload = () => setTexes(t => ({ ...t, [block]: img }));
      img.src = url;
    }).catch(() => {});
  }, [shape, nif, nifPath, texRoot]);

  useEffect(() => {
    const c = canvasRef.current;
    const ctx = c?.getContext('2d');
    if (!c || !ctx || !shape) return;
    const dpr = window.devicePixelRatio || 1;
    const w = c.clientWidth, h = c.clientHeight;
    if (c.width !== Math.round(w * dpr) || c.height !== Math.round(h * dpr)) {
      c.width = Math.round(w * dpr);
      c.height = Math.round(h * dpr);
    }
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    paint(ctx, {
      w, h, view: viewOf(), tris: shape.tris, uvs: drawUvs, sel, box, tex, label: modal ? modalLabel(modal) : '',
      faces: selectedTris(shape.tris, vc, sel, hidden), vis, hidden, stretch: stretchVals, seams, pins,
    });
  });

  const applyUvs = (fn: (u: number[]) => number[], allowEmpty = false) => {
    if (blk === undefined || (!allowEmpty && !sel.size)) return;
    setModal(null);
    setDoc(d => {
      const prev = d.cur[blk];
      const next = fn(prev);
      if (next.length === prev.length && next.every((x, i) => x === prev[i])) return d;
      return { cur: { ...d.cur, [blk]: next }, hist: pushHistory(d.hist, d.cur) };
    });
  };
  const undo = () => {
    setModal(null);
    setDoc(d => { const prev = d.hist.undo.at(-1); return prev ? { cur: prev, hist: undoHistory(d.hist, d.cur) } : d; });
  };
  const redo = () => { setModal(null); setDoc(d => { const next = d.hist.redo.at(-1); return next ? { cur: next, hist: redoHistory(d.hist, d.cur) } : d; }); };
  const revert = () => { setModal(null); setDoc(d => ({ cur: base, hist: pushHistory(d.hist, d.cur) })); };

  const startGesture = (kind: 'G' | 'S' | 'R') => {
    if (!sel.size) return;
    const pivot = resolvePivot(uvs, sel, pivotMode);
    const origin = hoverRef.current ?? pivot;
    setModal({ kind, start: uvs, sel, pivot, origin, pointer: origin, axis: null });
    canvasRef.current?.focus();
  };
  const commitModal = () => {
    if (!modal) return;
    const next = transformSel(modal.start, modal.sel, gestureXf(modal, modal.pointer));
    if (next.some((x, i) => x !== modal.start[i])) applyUvs(() => next);
    setModal(null);
  };
  const flip = (axis: 'u' | 'v') => applyUvs(u => transformSel(u, sel, {
    tx: 0, ty: 0, sx: axis === 'u' ? -1 : 1, sy: axis === 'v' ? -1 : 1, rotRad: 0, pivot: FLIP_PIVOT,
  }));
  const applyNumeric = () => {
    applyUvs(u => transformSel(u, sel, {
      tx: xf.du, ty: xf.dv, sx: xf.sx, sy: xf.sy, rotRad: (xf.deg * Math.PI) / 180, pivot: resolvePivot(u, sel, pivotMode),
    }));
    setXf(DEFAULT_XF);
  };
  const setScale = (axis: 'sx' | 'sy', val: number) =>
    setXf(x => (lock ? { ...x, sx: val, sy: val } : { ...x, [axis]: val }));
  const mirror = (axis: 'u' | 'v') => applyUvs(u => transformSel(u, sel, {
    tx: 0, ty: 0, sx: axis === 'u' ? -1 : 1, sy: axis === 'v' ? -1 : 1, rotRad: 0, pivot: resolvePivot(u, sel, pivotMode),
  }));
  const hideSel = () => {
    const faces = selectedTris(tris, vc, sel, hidden);
    if (!faces.length) return;
    const nextHidden = new Set([...hidden, ...faces]);
    const nextVis = visibleVerts(vc, tris, nextHidden);
    setHidden(nextHidden);
    setSel(new Set([...sel].filter(i => nextVis.has(i))));
  };
  const linkSel = () => setSel(new Set(linked(ids, sel).filter(i => vis.has(i))));
  const invertSel = () => setSel(invert(vis, sel));
  const snapToPixels = (t: HTMLImageElement) =>
    applyUvs(u => snapPixels(u, sel, t.naturalWidth, t.naturalHeight, texelCentre));
  const selectOverlap = () => {
    setModal(null);
    const verts = new Set<number>();
    for (const f of overlappingFaces(uvs, tris, hidden)) {
      for (let k = 0; k < 3; k++) if (vis.has(tris[3 * f + k])) verts.add(tris[3 * f + k]);
    }
    setSel(verts);
    if (!verts.size) appendLog('✗ no overlapping faces');
  };
  const copyUvs = () => { setModal(null); clipRef.current = uvs.slice(); setClipCount(uvs.length); };
  const pasteUvs = () => {
    const clip = clipRef.current;
    if (!clip || clip.length !== uvs.length) return;
    const targets = sel.size ? sel : allVerts(uvs);
    applyUvs(u => {
      const out = u.slice();
      for (const i of targets) { out[2 * i] = clip[2 * i]; out[2 * i + 1] = clip[2 * i + 1]; }
      return out;
    }, true);
  };

  const idsOf = (s: UvShape): number[] => islands(Math.floor(s.verts.length / 3), s.tris);
  const allOf = (u: readonly number[]): ReadonlySet<number> => new Set(Array.from({ length: u.length >> 1 }, (_, i) => i));
  const applyAllShapes = (fn: (s: UvShape, u: number[]) => number[]): void => {
    setModal(null);
    setDoc(d => {
      const cur = { ...d.cur };
      let changed = false;
      for (const s of shapes) {
        const prev = d.cur[s.block] ?? [];
        const next = fn(s, prev);
        if (next.length === prev.length && next.every((x, i) => x === prev[i])) continue;
        cur[s.block] = next;
        changed = true;
      }
      return changed ? { cur, hist: pushHistory(d.hist, d.cur) } : d;
    });
  };
  const markSeams = () => setSeams(s => new Set([...s, ...seamsInSelection(tris, sel, hidden)]));
  const clearSeams = () => setSeams(new Set<number>());
  const pinSel = () => { setModal(null); setPins(new Set([...pins, ...sel])); };
  const unpinSel = () => {
    setModal(null);
    if (!sel.size) { setPins(new Set<number>()); return; }
    const next = new Set(pins);
    for (const i of sel) next.delete(i);
    setPins(next);
  };
  const unwrap = () => {
    const pinned = new Map<number, [number, number]>();
    for (const i of pins) if (2 * i + 1 < uvs.length) pinned.set(i, [uvs[2 * i], uvs[2 * i + 1]]);
    if (pinned.size < 2) { appendLog('✗ unwrap needs at least two pins'); return; }
    const info = lscmUnwrapInfo(uvs, shape.verts, tris, ids, sel.size ? sel : new Set(vis), pinned);
    if (!info.unwrapped) { appendLog('✗ unwrap did not converge or its pins sit at the same UV -- nothing changed'); return; }
    if (info.skipped) appendLog(`• unwrap skipped ${info.skipped} island(s) without usable pins`);
    applyUvs(() => info.uvs);
  };
  const exportPng = () => {
    try {
      const c = canvasRef.current;
      if (!c) { appendLog('✗ export failed -- no canvas'); return; }
      const url = c.toDataURL('image/png');
      if (!url || url === 'data:,' || url.length < 32) { appendLog('✗ export failed -- the canvas has no image'); return; }
      const name = `uv-layout-${blk ?? 'shape'}.png`;
      const a = document.createElement('a');
      a.href = url;
      a.download = name;
      a.click();
      appendLog(`✓ exported ${name}`);
    } catch (e) {
      appendLog('✗ export failed -- ' + (e instanceof Error ? e.message : String(e)));
    }
  };
  const splitSeams = async () => {
    if (blk === undefined || saving) return;
    const plan = splitPlan(vc, tris, seams);
    if (!plan.sources.length) { appendLog('✗ no seam edges to split'); return; }
    const grown = uvs.slice();
    for (const src of plan.sources) grown.push(uvs[2 * src], uvs[2 * src + 1]);
    const edits: { id: number; cat: string; key: string; value: number[] }[] = [];
    for (const b of dirtyBlocks) if (b !== blk) edits.push({ id: b, cat: 'shape', key: channel === 1 ? 'uvs2' : 'uvs', value: doc.cur[b] });
    edits.push({ id: blk, cat: 'shape', key: 'split', value: [plan.sources.length, ...plan.sources, ...plan.tris] });
    edits.push({ id: blk, cat: 'shape', key: channel === 1 ? 'uvs2' : 'uvs', value: grown });
    setSaving(true);
    try {
      const res = await nif.ApplyEdits(nifPath, JSON.stringify(edits), '');
      const ok = /set OK/.test(res);
      appendLog(ok ? `✓ split ${plan.sources.length} seam vert(s)` : `✗ split failed -- ${res}`);
      setSaveError(ok ? '' : res);
      if (ok) { setSeams(new Set<number>()); onSaved(res); }
    } catch (e) {
      const msg = e instanceof Error ? e.message : String(e);
      appendLog('✗ split failed -- ' + msg);
      setSaveError(msg);
    } finally {
      setSaving(false);
    }
  };

  const applyPick = (verts: number[], toggle: boolean) => {
    if (!toggle) { setSel(new Set(verts)); return; }
    const allIn = verts.every(i => sel.has(i));
    const next = new Set(sel);
    for (const i of verts) { if (allIn) next.delete(i); else next.add(i); }
    setSel(next);
  };
  const pickVerts = (p: UvPoint, v: UvView): number[] => {
    const d = PICK_PX / v.scale;
    if (selectMode === 'edge') return pickEdge(uvs, tris, hidden, p.u, p.v, d) ?? [];
    if (selectMode === 'face') return pickFace(uvs, tris, hidden, p.u, p.v) ?? [];
    const i = nearestVert(p, v);
    if (selectMode === 'vertex') return i >= 0 ? [i] : [];
    const seed = i >= 0 ? i : pickFace(uvs, tris, hidden, p.u, p.v)?.[0];
    return seed === undefined ? [] : islandOf(seed);
  };
  const boxSelect = (a: UvPoint, b: UvPoint, add: boolean) => {
    const r: UvBox = { minU: Math.min(a.u, b.u), minV: Math.min(a.v, b.v), maxU: Math.max(a.u, b.u), maxV: Math.max(a.v, b.v) };
    const hit = boxVerts(uvs, tris, hidden, r, selectMode === 'island' ? 'vertex' : selectMode);
    const verts = selectMode === 'island' ? linked(ids, new Set(hit)) : hit;
    const next = new Set<number>(add ? sel : undefined);
    for (const i of verts) if (vis.has(i)) next.add(i);
    setSel(next);
  };
  const pickShape = (block: number) => {
    setChosen(block); setSel(new Set<number>()); setModal(null); setBox(null); setView(null); setHidden(new Set<number>());
    setSeams(new Set<number>()); setPins(new Set<number>());
  };

  const onMouseDown = (e: MouseEvent<HTMLCanvasElement>) => {
    const { x, y } = local(e);
    if (e.button === 1) { e.preventDefault(); panRef.current = { x, y, view: viewOf() }; return; }
    if (e.button !== 0) return;
    if (modal) { commitModal(); return; }
    const v = viewOf();
    pressRef.current = { x, y, hits: pickVerts(toUv(x, y, v), v), shift: e.shiftKey, moved: false };
  };
  const onMouseMove = (e: MouseEvent<HTMLCanvasElement>) => {
    const { x, y } = local(e);
    const p = toUv(x, y, viewOf());
    hoverRef.current = p;
    if (modal) setModal({ ...modal, pointer: p });
    const pan = panRef.current;
    if (pan) setView({ scale: pan.view.scale, px: pan.view.px + x - pan.x, py: pan.view.py + y - pan.y });
    const press = pressRef.current;
    if (!press) return;
    if (Math.hypot(x - press.x, y - press.y) > 3) press.moved = true;
    if (press.moved) setBox({ x0: press.x, y0: press.y, x1: x, y1: y });
  };
  const onMouseUp = (e: MouseEvent<HTMLCanvasElement>) => {
    if (e.button === 1) { panRef.current = null; return; }
    const press = pressRef.current;
    pressRef.current = null;
    if (e.button !== 0 || !press) return;
    setBox(null);
    const { x, y } = local(e);
    if (!press.moved) {
      if (press.hits.length) applyPick(press.hits, press.shift);
      else if (!press.shift) setSel(new Set<number>());
    } else {
      const v = viewOf();
      boxSelect(toUv(press.x, press.y, v), toUv(x, y, v), press.shift);
    }
  };
  const onMouseLeave = () => { panRef.current = null; pressRef.current = null; hoverRef.current = null; setBox(null); };
  const onWheel = (e: WheelEvent<HTMLCanvasElement>) => {
    const { x, y } = local(e);
    const v = viewOf();
    const scale = Math.min(Math.max(v.scale * Math.exp(-e.deltaY * 0.0015), 2), 1e6);
    const k = scale / v.scale;
    setView({ scale, px: x - (x - v.px) * k, py: y - (y - v.py) * k });
  };
  const onKeyDown = (e: KeyboardEvent<HTMLCanvasElement>) => {
    const k = e.key.toLowerCase();
    const mod = e.ctrlKey || e.metaKey;
    if (modal && mod && k === 'z' && !e.shiftKey) { e.preventDefault(); setModal(null); return; }
    if (mod && (k === 'y' || (k === 'z' && e.shiftKey))) { e.preventDefault(); redo(); return; }
    if (mod && k === 'z') { e.preventDefault(); undo(); return; }
    if (mod && !modal && !e.shiftKey && !e.altKey && k === 'i') { e.preventDefault(); invertSel(); return; }
    if (mod && !modal && !e.shiftKey && !e.altKey && k === 'l') { e.preventDefault(); linkSel(); return; }
    if (mod) return;
    if (modal) {
      if (k === 'enter') commitModal();
      else if (k === 'escape') setModal(null);
      else if ((k === 'x' || k === 'y') && modal.kind !== 'R') setModal({ ...modal, axis: modal.axis === k ? null : k });
      return;
    }
    const h = hoverRef.current;
    if (k === 'a') setSel(e.altKey ? new Set<number>() : new Set(vis));
    else if (k === 'h') { if (e.altKey) setHidden(new Set<number>()); else hideSel(); }
    else if (k === 'l' && h) {
      const hit = nearestVert(h, viewOf());
      if (hit >= 0) applyPick(islandOf(hit), e.shiftKey);
    }
    else if (/^[1-4]$/.test(k)) setSelectMode(MODES[Number(k) - 1]);
    else if (k === 'g' || k === 's' || k === 'r') startGesture(k === 'g' ? 'G' : k === 's' ? 'S' : 'R');
    else if (k === 'home' || k === 'f') frameAll();
  };

  const save = async () => {
    if (!dirty || saving) return;
    const edits = dirtyBlocks.map(b => ({ id: b, cat: 'shape', key: channel === 1 ? 'uvs2' : 'uvs', value: doc.cur[b] }));
    setSaving(true);
    try {
      const res = await nif.ApplyEdits(nifPath, JSON.stringify(edits), '');
      const ok = /set OK/.test(res);
      appendLog(ok ? `✓ saved UVs for ${dirtyBlocks.length} shape(s)` : `✗ UV save failed -- ${res}`);
      setSaveError(ok ? '' : res);
      if (ok) onSaved(res);
    } catch (e) {
      const msg = e instanceof Error ? e.message : String(e);
      appendLog('✗ UV save failed -- ' + msg);
      setSaveError(msg);
    } finally { setSaving(false); }
  };

  if (!shape) return <div className="nif-view-empty">No shapes with UVs and a block id in this NIF.</div>;

  return (
    <div className="nif-uv">
      <div className="nif-uv-toolbar">
        <label className="nif-uv-field">Shape
          <select value={shape.block} onChange={e => pickShape(Number(e.target.value))}>
            {shapes.map(s => <option key={s.block} value={s.block}>{s.name || `shape ${s.block}`}</option>)}
          </select>
        </label>
        {hasUv2 && (
          <label className="nif-uv-field">Channel
            <select aria-label="UV channel" value={channel} onChange={e => pickChannel(Number(e.target.value))}>
              <option value={0}>UV0</option>
              <option value={1}>UV2</option>
            </select>
          </label>
        )}
        <button className="sidebar-action-btn" disabled={!sel.size} onClick={() => startGesture('G')} title="Move (G)">Move</button>
        <button className="sidebar-action-btn" disabled={!sel.size} onClick={() => startGesture('S')} title="Scale (S)">Scale</button>
        <button className="sidebar-action-btn" disabled={!sel.size} onClick={() => startGesture('R')} title="Rotate (R)"><RotateCw size={13} /> Rotate</button>
        <button className="sidebar-action-btn" disabled={!sel.size} onClick={() => flip('u')} title="Flip U about 0.5">Flip U</button>
        <button className="sidebar-action-btn" disabled={!sel.size} onClick={() => flip('v')} title="Flip V about 0.5">Flip V</button>
        <button className="sidebar-action-btn" onClick={frameAll} title="Frame all (Home)">Frame</button>
        <select aria-label="Select mode" value={selectMode} onChange={e => setSelectMode(e.target.value as SelectMode)}>
          <option value="vertex">Vertex</option>
          <option value="edge">Edge</option>
          <option value="face">Face</option>
          <option value="island">Island</option>
        </select>
        <button className="sidebar-action-btn" disabled={!doc.hist.undo.length} onClick={undo} title="Undo (Ctrl+Z)"><Undo2 size={13} /> Undo</button>
        <button className="sidebar-action-btn" disabled={!doc.hist.redo.length} onClick={redo} title="Redo (Ctrl+Y)">Redo</button>
      </div>
      <div className="nif-uv-toolbar">
        <span className="nif-uv-label">Move</span>
        <input className="nif-in nif-uv-num" type="number" step="any" aria-label="Move U" value={xf.du} onChange={e => setXf({ ...xf, du: numOf(e) })} />
        <input className="nif-in nif-uv-num" type="number" step="any" aria-label="Move V" value={xf.dv} onChange={e => setXf({ ...xf, dv: numOf(e) })} />
        <span className="nif-uv-label">Scale</span>
        <input className="nif-in nif-uv-num" type="number" step="any" aria-label="Scale U" value={xf.sx} onChange={e => setScale('sx', numOf(e))} />
        <input className="nif-in nif-uv-num" type="number" step="any" aria-label="Scale V" value={xf.sy} onChange={e => setScale('sy', numOf(e))} />
        <label className="nif-uv-check"><input type="checkbox" checked={lock} onChange={e => setLock(e.target.checked)} /> Lock</label>
        <span className="nif-uv-label">Rotate</span>
        <input className="nif-in nif-uv-num" type="number" step="any" aria-label="Rotate degrees" value={xf.deg} onChange={e => setXf({ ...xf, deg: numOf(e) })} />
        <select aria-label="Pivot" value={pivotMode} onChange={e => setPivotMode(e.target.value as PivotMode)}>
          <option value="centre">Selection centre</option>
          <option value="median">Median</option>
          <option value="tile">0.5, 0.5</option>
        </select>
        <button className="sidebar-action-btn" disabled={!sel.size} onClick={applyNumeric}>Apply</button>
      </div>
      <div className="nif-uv-toolbar">
        <button className="sidebar-action-btn" disabled={!hideable} onClick={hideSel} title="Hide selected faces (H); select whole faces first">Hide</button>
        <button className="sidebar-action-btn" disabled={!hidden.size} onClick={() => setHidden(new Set<number>())} title="Reveal hidden faces (Alt+H)">Reveal</button>
        <button className="sidebar-action-btn" onClick={invertSel} title="Invert selection (Ctrl+I)">Invert</button>
        <button className="sidebar-action-btn" disabled={!sel.size} onClick={linkSel} title="Select linked (Ctrl+L)">Linked</button>
        <span className="nif-uv-label">Merge</span>
        <button className="sidebar-action-btn" disabled={!sel.size} onClick={() => applyUvs(u => mergeAtCentre(u, sel))}>Merge centre</button>
        <button className="sidebar-action-btn" disabled={!sel.size} onClick={() => applyUvs(u => mergeByDistance(u, sel, weld))}>Weld</button>
        <input className="nif-in nif-uv-num" type="number" step="any" aria-label="Weld distance" value={weld} onChange={e => setWeld(Math.max(0, numOf(e)))} />
        <button className="sidebar-action-btn" disabled={!sel.size} onClick={() => mirror('u')} title="Mirror U about the selection pivot">Mirror U</button>
        <button className="sidebar-action-btn" disabled={!sel.size} onClick={() => mirror('v')} title="Mirror V about the selection pivot">Mirror V</button>
        <span className="nif-uv-label">Align</span>
        <select aria-label="Align to" value={alignTo} onChange={e => setAlignTo(e.target.value as Align)}>
          <option value="min">Min</option>
          <option value="centre">Centre</option>
          <option value="max">Max</option>
        </select>
        <button className="sidebar-action-btn" disabled={!sel.size} onClick={() => applyUvs(u => alignSel(u, sel, 'u', alignTo))}>Align U</button>
        <button className="sidebar-action-btn" disabled={!sel.size} onClick={() => applyUvs(u => alignSel(u, sel, 'v', alignTo))}>Align V</button>
        <span className="nif-uv-label">Snap</span>
        <select aria-label="Snap step" value={snapStep} onChange={e => setSnapStep(Number(e.target.value))}>
          <option value={0.125}>1/8</option>
          <option value={0.0625}>1/16</option>
          <option value={0.03125}>1/32</option>
          <option value={0.015625}>1/64</option>
          <option value={0.0078125}>1/128</option>
        </select>
        <button className="sidebar-action-btn" disabled={!sel.size} onClick={() => applyUvs(u => snapGrid(u, sel, snapStep))}>Snap grid</button>
        <button className="sidebar-action-btn" disabled={!tex || !sel.size} onClick={() => { if (tex) snapToPixels(tex); }}>Snap pixels</button>
        <label className="nif-uv-check"><input type="checkbox" checked={texelCentre} onChange={e => setTexelCentre(e.target.checked)} /> Texel centre</label>
        <button className="sidebar-action-btn" disabled={!sel.size} onClick={() => applyUvs(u => clampTile(u, sel))}>Clamp 0-1</button>
      </div>
      <UvLayoutTools hasSel={sel.size > 0} canAverage={selIslandCount > 1} canPaste={canPaste} stretch={stretch} onStretch={setStretch}
        allShapes={allShapes} onAllShapes={setAllShapes}
        onPack={m => allShapes ? applyAllShapes((s, u) => packIslands(u, idsOf(s), allOf(u), m)) : applyUvs(u => packIslands(u, ids, sel, m))}
        onAverage={() => allShapes
          ? applyAllShapes((s, u) => averageIslandScale(u, s.verts, s.tris, idsOf(s), allOf(u)))
          : applyUvs(u => averageIslandScale(u, shape.verts, tris, ids, sel))}
        onSmart={margin => {
          if (allShapes) { applyAllShapes((s, u) => smartProjectInfo(u, s.verts, s.tris, idsOf(s), allOf(u), margin).uvs); return; }
          const info = smartProjectInfo(uvs, shape.verts, tris, ids, sel, margin);
          if (info.projected === 0) { appendLog('✗ smart project: no projectable islands'); return; }
          if (info.skipped) appendLog(`• smart project skipped ${info.skipped} island(s) with cancelling normals`);
          applyUvs(() => info.uvs);
        }}
        onRelax={n => allShapes ? applyAllShapes((s, u) => relaxUvs(u, s.tris, allOf(u), n)) : applyUvs(u => relaxUvs(u, tris, sel, n))}
        onStitch={eps => allShapes
          ? applyAllShapes((s, u) => stitchSelection(u, s.verts, idsOf(s), allOf(u), eps))
          : applyUvs(u => stitchSelection(u, shape.verts, ids, sel, eps))}
        onSelectOverlap={selectOverlap} onCopy={copyUvs} onPaste={pasteUvs}
        seamCount={seams.size} onMarkSeam={markSeams} onClearSeams={clearSeams} onSplitSeam={splitSeams}
        pinCount={pins.size} onPin={pinSel} onUnpin={unpinSel} canUnwrap={pins.size > 1} onUnwrap={unwrap}
        onExportPng={exportPng} />
      <canvas ref={canvasRef} className="nif-uv-canvas" tabIndex={0}
        onMouseDown={onMouseDown} onMouseMove={onMouseMove} onMouseUp={onMouseUp} onMouseLeave={onMouseLeave}
        onWheel={onWheel} onKeyDown={onKeyDown} />
      <div className="nif-uv-footer">
        <span className={`nif-dirty-count ${dirty ? 'on' : ''}`}>
          {dirty ? `${dirtyBlocks.length} shape${dirtyBlocks.length > 1 ? 's' : ''} changed` : 'No changes'}
        </span>
        <span className="nif-geo-info">{`${(uvs.length >> 1).toLocaleString()} verts · ${(shape.tris.length / 3).toLocaleString()} tris · ${islandCount} island(s) · ${sel.size} selected${hidden.size ? `, ${hidden.size} hidden` : ''}`}</span>
        {stretch && <span className="nif-uv-label">Stretch: green = even texel density, red = stretched</span>}
        {saveError && <span className="papyrus-warn" title={saveError}>{`Save failed -- ${saveError.split('\n')[0]}`}</span>}
        <div className="nif-uv-actions">
          <button className="sidebar-action-btn" disabled={!dirty || saving} onClick={revert}><RotateCcw size={13} /> Revert</button>
          <button className="papyrus-run nif-save-btn" disabled={!dirty || saving} onClick={save}>
            <Save size={14} /> {saving ? 'Saving…' : 'Save'}
          </button>
        </div>
      </div>
    </div>
  );
}

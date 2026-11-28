export interface SwfHost {
  Open(path: string): Promise<string>;
  Characters(): Promise<string>;
  FrameLabels(timeline: number): Promise<string>;
  Frame(timeline: number, frame: number): Promise<string>;
  Timeline(index: number): Promise<string>;
  RenameLabel(timeline: number, labelIndex: number, name: string): Promise<string>;
  Undo(): Promise<string>;
  Redo(): Promise<string>;
  State(): Promise<string>;
  Save(): Promise<string>;
  AddLabel(frame: number, name: string, timeline: number): Promise<string>;
  RemoveLabel(timeline: number, labelIndex: number): Promise<string>;
  Placements(): Promise<string>;
  SetDepth(tagIndex: number, depth: number): Promise<string>;
  PlacementMatrix(tagIndex: number): Promise<string>;
  SetTransform(tagIndex: number, x: number, y: number, scaleX: number, scaleY: number, rotate0: number, rotate1: number): Promise<string>;
  InsertKeyframe(tagIndex: number, frame: number): Promise<string>;
  ReadEditText(tagIndex: number): Promise<string>;
  EditEditText(tagIndex: number, text: string, colorHex: string, x0: number, x1: number, y0: number, y1: number): Promise<string>;
  AbcBlocks(): Promise<string>;
  Disassemble(abc: number, methodIndex: number): Promise<string>;
  ShapeSvg(characterId: number): Promise<string>;
  FrameSvg(frame: number): Promise<string>;
  BitmapPng(tagIndex: number): Promise<string>;
  BitmapJpeg(tagIndex: number): Promise<string>;
  InspectText(): Promise<string>;
  ActionTags(): Promise<string>;
  Avm1Disassemble(tagIndex: number): Promise<string>;
  BrowseForFile(title: string, filter: string): Promise<string>;
  ArchiveSwfs(): Promise<string>;
  OpenArchived(name: string): Promise<string>;
  SaveAs(title: string, filter: string): Promise<string>;
  AbcText(abc: number): Promise<string>;
  ApplyAbcText(abc: number, text: string): Promise<string>;
}

export const getSwf = (): SwfHost | undefined =>
  window.chrome?.webview?.hostObjects?.swf;

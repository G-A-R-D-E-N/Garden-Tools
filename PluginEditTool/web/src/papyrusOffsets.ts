import type { PapyrusRenameEdit } from './backend';

export function byteOffset(text: string, caret: number): number {
  return new TextEncoder().encode(text.slice(0, caret)).length;
}

export function utf8ToUtf16Index(text: string, offset: number): number {
  let seen = 0;
  let index = 0;
  while (index < text.length && seen < offset) {
    const code = text.codePointAt(index) ?? 0;
    seen += code < 0x80 ? 1 : code < 0x800 ? 2 : code < 0x10000 ? 3 : 4;
    index += code > 0xffff ? 2 : 1;
  }
  return index;
}

export function editorIndex(text: string, offset: number): number {
  const index = utf8ToUtf16Index(text, offset);
  return index - (text.slice(0, index).match(/\r\n/g)?.length ?? 0);
}

export function applyByteEdits(text: string, edits: Pick<PapyrusRenameEdit, 'start' | 'length' | 'newText'>[]): string {
  return edits
    .filter((edit, i) => edits.findIndex(other => other.start === edit.start) === i)
    .map(edit => ({
      from: utf8ToUtf16Index(text, edit.start),
      to: utf8ToUtf16Index(text, edit.start + edit.length),
      newText: edit.newText,
    }))
    .sort((a, b) => b.from - a.from)
    .reduce((out, span) => out.slice(0, span.from) + span.newText + out.slice(span.to), text);
}

export function samePath(a: string, b: string): boolean {
  return a.replace(/\//g, '\\').toLowerCase() === b.replace(/\//g, '\\').toLowerCase();
}

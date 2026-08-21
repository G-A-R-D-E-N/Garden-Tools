import { RUNTIME_ORDER, type Runtime } from "../config.js";

export type ReferenceKind = "rel_offset" | "rel_id" | "vtable_symbol";
export type SlotMethod = "id" | "offset";
export type ReferenceForm = "id_scalar" | "id_list" | "variant" | "offset" | "vtable_symbol";

export interface ParsedSlot {
  runtime: Runtime;
  method: SlotMethod;
  rawValue: string;
  // Upstream writes 0 for "this function does not exist on this runtime" (inlined, removed). It is
  // a sentinel, not an id — id 0 is a real entry in the OG library, so resolving it would invent a
  // hook address for a function the author explicitly marked absent.
  declaredUnavailable: boolean;
}

export interface ParsedReference {
  lineNumber: number;
  kind: ReferenceKind;
  form: ReferenceForm;
  // The literal as written, for display and for the stored uniqueness key.
  rawValue: string;
  slots: ParsedSlot[];
}

const UNAVAILABLE = "0";

// CommonLibF4 spellings, in match order. VariantID must precede ID: they share a prefix, and
// matching "REL::ID" inside "REL::VariantID" would parse the wrong construct.
const CONSTRUCTS: { token: string; kind: ReferenceKind; method: SlotMethod }[] = [
  { token: "REL::VariantID", kind: "rel_id", method: "id" },
  { token: "REL::RelocationID", kind: "rel_id", method: "id" },
  { token: "REL::ID", kind: "rel_id", method: "id" },
  { token: "REL::Offset", kind: "rel_offset", method: "offset" },
];

// CommonLibF4 exposes a class's vtables two ways and consumer code uses both: the namespace table
// `RE::VTABLE::Class[n]` and the static member `RE::Class::VTABLE[n]`. Matching only the first
// reported "no vtable references" for repos built entirely on the second.
const VTABLE_NAMESPACE = /RE::VTABLE::([A-Za-z_]\w*)\s*\[\s*(\d+)\s*\]/g;
const VTABLE_MEMBER = /\b((?:[A-Za-z_]\w*::)*[A-Za-z_]\w*)::VTABLE\s*\[\s*(\d+)\s*\]/g;

const IDENT = /[A-Za-z_]\w*/y;
const WS = /\s*/y;

function skip(text: string, i: number, re: RegExp): number {
  re.lastIndex = i;
  return re.exec(text) ? re.lastIndex : i;
}

// Returns the index just past the matching close, or -1 if the delimiter never closes.
function matchDelimited(text: string, open: number): number {
  const close = text[open] === "(" ? ")" : "}";
  const openCh = text[open];
  let depth = 0;
  for (let i = open; i < text.length; i++) {
    const c = text[i];
    if (c === openCh) depth++;
    else if (c === close) {
      depth--;
      if (depth === 0) return i + 1;
    }
  }
  return -1;
}

// Splits on top-level commas only, so REL::VariantID(REL::ID(1), REL::Offset(0x2)) keeps its args.
function splitArgs(inner: string): string[] {
  const parts: string[] = [];
  let depth = 0;
  let start = 0;
  for (let i = 0; i < inner.length; i++) {
    const c = inner[i];
    if (c === "(" || c === "{" || c === "[") depth++;
    else if (c === ")" || c === "}" || c === "]") depth--;
    else if (c === "," && depth === 0) {
      parts.push(inner.slice(start, i));
      start = i + 1;
    }
  }
  parts.push(inner.slice(start));
  return parts.map((p) => p.trim()).filter((p) => p.length > 0);
}

interface ArgValue {
  method: SlotMethod;
  rawValue: string;
}

// An argument is a bare integer, a nested REL::ID(n), or a nested REL::Offset(0x…). Anything else
// (a constant, a macro) is not something this index can resolve, so it yields null rather than a
// guess.
function parseArg(arg: string, outerMethod: SlotMethod): ArgValue | null {
  const nested = /^REL::(ID|Offset)\s*[({]\s*(0x[0-9A-Fa-f]+|\d+)\s*[)}]$/.exec(arg);
  if (nested) {
    return { method: nested[1] === "Offset" ? "offset" : "id", rawValue: nested[2] };
  }
  if (/^0x[0-9A-Fa-f]+$/.test(arg)) return { method: "offset", rawValue: arg };
  if (/^\d+$/.test(arg)) return { method: outerMethod, rawValue: arg };
  return null;
}

// COMMONLIB_RUNTIMECOUNT slots, filled left to right; a short list repeats its LAST value into the
// remaining slots (REL/ID.h and REL/VariantID.h both do this). So { og, ae } means kAE takes the
// second value and kNG takes it too — not that kAE is unset.
function expand(values: (ArgValue | null)[]): ParsedSlot[] {
  const n = RUNTIME_ORDER.length;
  const clipped = values.slice(0, n);
  if (clipped.length === 0) return [];

  const slots: ParsedSlot[] = [];
  let last = clipped[clipped.length - 1];
  for (let i = 0; i < n; i++) {
    const v = i < clipped.length ? clipped[i] : last;
    if (!v) continue;
    slots.push({
      runtime: RUNTIME_ORDER[i],
      method: v.method,
      rawValue: v.rawValue,
      declaredUnavailable: v.rawValue === UNAVAILABLE,
    });
  }
  return slots;
}

export function parseReferences(text: string): ParsedReference[] {
  const refs: ParsedReference[] = [];
  const consumed: [number, number][] = [];

  // Line numbers from offsets, so a declaration may wrap across lines without being missed.
  const lineStarts: number[] = [0];
  for (let i = 0; i < text.length; i++) if (text[i] === "\n") lineStarts.push(i + 1);
  const lineOf = (offset: number): number => {
    let lo = 0;
    let hi = lineStarts.length - 1;
    while (lo < hi) {
      const mid = (lo + hi + 1) >> 1;
      if (lineStarts[mid] <= offset) lo = mid;
      else hi = mid - 1;
    }
    return lo + 1;
  };

  for (const { token, kind, method } of CONSTRUCTS) {
    let from = 0;
    for (;;) {
      const at = text.indexOf(token, from);
      if (at < 0) break;
      from = at + token.length;

      // A longer construct already claimed this text (REL::ID inside REL::VariantID(...)).
      if (consumed.some(([s, e]) => at >= s && at < e)) continue;
      // Reject a prefix hit inside a longer identifier, e.g. REL::IDDB.
      if (/\w/.test(text[at + token.length] ?? "")) continue;

      let i = skip(text, at + token.length, WS);
      // `REL::VariantID Name{ … }` — a declaration name may sit between type and initializer.
      const afterIdent = skip(text, i, IDENT);
      if (afterIdent > i) i = skip(text, afterIdent, WS);

      const ch = text[i];
      // `std::array<REL::ID, 17>` is the type being named, not a reference. No delimiter, no ref.
      if (ch !== "(" && ch !== "{") continue;

      const end = matchDelimited(text, i);
      if (end < 0) continue;

      const inner = text.slice(i + 1, end - 1);
      const args = splitArgs(inner).map((a) => parseArg(a, method));
      const slots = expand(args);
      if (slots.length === 0) continue;

      consumed.push([at, end]);

      const isList = args.length > 1;
      const form: ReferenceForm =
        token === "REL::VariantID" || token === "REL::RelocationID"
          ? "variant"
          : kind === "rel_offset"
            ? "offset"
            : isList
              ? "id_list"
              : "id_scalar";

      refs.push({
        lineNumber: lineOf(at),
        kind,
        form,
        rawValue: isList ? `{${args.map((a) => a?.rawValue ?? "?").join(",")}}` : slots[0].rawValue,
        slots,
      });
    }
  }

  const seenVtable = new Set<string>();
  for (const re of [VTABLE_NAMESPACE, VTABLE_MEMBER]) {
    re.lastIndex = 0;
    let m: RegExpExecArray | null;
    while ((m = re.exec(text)) !== null) {
      // Reported without the RE:: namespace prefix so both spellings of one class agree.
      const cls = m[1].replace(/^RE::/, "");
      const rawValue = `${cls}[${m[2]}]`;
      const key = `${lineOf(m.index)}:${rawValue}`;
      if (seenVtable.has(key)) continue;
      seenVtable.add(key);
      refs.push({
        lineNumber: lineOf(m.index),
        kind: "vtable_symbol",
        form: "vtable_symbol",
        rawValue,
        slots: [],
      });
    }
  }

  return refs.sort((a, b) => a.lineNumber - b.lineNumber || a.rawValue.localeCompare(b.rawValue));
}

// Every scalar id literal in a file, which is what the provenance test measures. List forms are
// excluded on purpose: they already say which slot each value belongs to, so they carry no
// evidence about an *unlabelled* id's origin.
export function scalarIdsIn(refs: ParsedReference[]): number[] {
  const out: number[] = [];
  for (const ref of refs) {
    if (ref.form !== "id_scalar") continue;
    const n = Number(ref.slots[0]?.rawValue);
    if (Number.isFinite(n) && n > 0) out.push(n);
  }
  return out;
}

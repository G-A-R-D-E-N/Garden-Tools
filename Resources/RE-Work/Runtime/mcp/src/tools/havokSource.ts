import { readdirSync, readFileSync, statSync } from "node:fs";
import { extname, join, relative } from "node:path";
import { HAVOK_SDK_DIR, HAVOK_SOURCE_DIRS } from "../paths.js";

const SOURCE_EXTENSIONS = new Set([".h", ".inl", ".cpp", ".cxx"]);

const MAX_FILES_PER_SEARCH = 40;
const MAX_MATCHES_PER_FILE = 8;
const SNIPPET_RADIUS = 240;

/// Every source file under the Havok tree, in the order the directories are listed, so a behaviour
/// class is found before a physics header that happens to use the same word.
function sourceFiles(): string[] {
  const found: string[] = [];

  function walk(dir: string) {
    let entries: string[];
    try {
      entries = readdirSync(dir);
    } catch {
      return;
    }
    for (const entry of entries) {
      const full = join(dir, entry);
      let isDir: boolean;
      try {
        isDir = statSync(full).isDirectory();
      } catch {
        continue;
      }
      if (isDir) walk(full);
      else if (SOURCE_EXTENSIONS.has(extname(entry))) found.push(full);
    }
  }

  for (const dir of HAVOK_SOURCE_DIRS) walk(dir);
  return found;
}

export interface HavokClassResult {
  /// Empty when the tree is not on this machine, with `note` saying so rather than looking like a
  /// class that does not exist.
  file: string;
  declaration: string;
}

/// A class declaration with its members and the notes attached to them.
///
/// Named exactly: `hkbClipGenerator` finds the class, not every file mentioning it. Use
/// `search_havok_source` for the loose question.
export function havokClass(name: string): { note: string; results: HavokClassResult[] } {
  const files = sourceFiles();
  if (files.length === 0) {
    return {
      note:
        `No Havok source found under ${HAVOK_SDK_DIR}. It is extracted from the SDK installer and ` +
        `is not in any repository, so a machine without it gets nothing here rather than a wrong answer.`,
      results: [],
    };
  }

  const results: HavokClassResult[] = [];

  // The real declaration is followed by a base list or by its opening brace. Without that trailing
  // requirement this matches `class hkbClipGenerator;`, the forward declaration every header that
  // merely mentions the class carries, and then reads whichever unrelated class comes next.
  const opener = new RegExp(`\\b(?:class|struct)\\s+${escape(name)}\\s*(?::|\\{)`);

  // A class is nearly always declared in the file named after it, so look there first and let the
  // rest be a fallback rather than a race.
  const ordered = [...files].sort((left, right) => rank(left, name) - rank(right, name));

  for (const file of ordered) {
    let content: string;
    try {
      content = readFileSync(file, "utf8");
    } catch {
      continue;
    }
    const match = opener.exec(content);
    if (match === null) continue;

    results.push({
      file: relative(HAVOK_SDK_DIR, file),
      declaration: block(content, match.index),
    });
    if (results.length >= 4) break;
  }

  return {
    note:
      results.length > 0
        ? "Reference only. Havok's material is confidential; write conclusions in your own words, " +
          "and take field offsets from HavokClassTypes.json or the game binary, never from here."
        : `No declaration of ${name} found. Bethesda's own BS* classes are not in Havok's source; ` +
          `those stay reverse engineering work.`,
    results,
  };
}

/// From the declaration to the brace that closes it, so members and their notes come back together.
/// Falls back to a fixed span when the braces do not balance, which beats returning nothing.
function block(content: string, from: number): string {
  const open = content.indexOf("{", from);
  if (open === -1) return content.slice(from, from + 2000);

  let depth = 0;
  for (let at = open; at < content.length; at++) {
    if (content[at] === "{") depth++;
    else if (content[at] === "}") {
      depth--;
      if (depth === 0) return content.slice(from, at + 1);
    }
  }
  return content.slice(from, from + 4000);
}

export interface HavokSourceMatch {
  file: string;
  snippet: string;
}

/// Keyword search across the Havok tree, for the questions a class name does not answer: what a
/// flag bit means, which classes mention a field, how something is described.
export function searchHavokSource(query: string): { note: string; results: HavokSourceMatch[] } {
  const files = sourceFiles();
  if (files.length === 0) {
    return { note: `No Havok source found under ${HAVOK_SDK_DIR}.`, results: [] };
  }

  const needle = query.toLowerCase();
  const results: HavokSourceMatch[] = [];
  let touched = 0;

  for (const file of files) {
    if (touched >= MAX_FILES_PER_SEARCH) break;

    let content: string;
    try {
      content = readFileSync(file, "utf8");
    } catch {
      continue;
    }
    const haystack = content.toLowerCase();
    if (!haystack.includes(needle)) continue;
    touched++;

    let idx = 0;
    let matches = 0;
    while (matches < MAX_MATCHES_PER_FILE) {
      const found = haystack.indexOf(needle, idx);
      if (found === -1) break;
      const start = Math.max(0, found - SNIPPET_RADIUS);
      const end = Math.min(content.length, found + needle.length + SNIPPET_RADIUS);
      results.push({
        file: relative(HAVOK_SDK_DIR, file),
        snippet:
          (start > 0 ? "..." : "") +
          content.slice(start, end).replace(/\s+/g, " ").trim() +
          (end < content.length ? "..." : ""),
      });
      idx = found + needle.length;
      matches++;
    }
  }

  return {
    note:
      `${results.length} match(es) across ${touched} file(s). Reference only: write conclusions in ` +
      `your own words, and confirm anything load bearing against a real file before relying on it.`,
    results,
  };
}

/// Files named after the class first, then the rest.
function rank(file: string, name: string): number {
  const base = file.slice(file.lastIndexOf("/") + 1);
  if (base === `${name}.h`) return 0;
  if (base.startsWith(name)) return 1;
  return 2;
}

function escape(value: string): string {
  return value.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
}

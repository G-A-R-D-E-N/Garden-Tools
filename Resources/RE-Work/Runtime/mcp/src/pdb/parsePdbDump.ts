export interface PdbFunctionLine {
  pdbRva: number;
  sizeof: number;
  isFpo: boolean;
  signature: string;
}

const LINE_RE =
  /^\s*func \[0x([0-9A-Fa-f]+)\+\s*\d+\s*-\s*0x[0-9A-Fa-f]+-\s*\d+\s*\|\s*sizeof=\s*(\d+)\]\s*(\(FPO\)\s*)?(.*)$/;

export function parsePdbLine(line: string): PdbFunctionLine | null {
  const match = line.match(LINE_RE);
  if (!match) return null;
  return {
    pdbRva: parseInt(match[1], 16),
    sizeof: parseInt(match[2], 10),
    isFpo: match[3] !== undefined,
    signature: match[4],
  };
}

export function extractCallingConvention(signature: string): string | null {
  const match = signature.match(/(__cdecl|__thiscall|__stdcall|__fastcall|__vectorcall)/);
  return match ? match[1] : null;
}

// ---------------------------------------------------------------------------
// extractSignatureName
//
// The dump's signatures follow MSVC's PDB layout:
//
//   [return type] [calling convention] [qualified name]([params]) [const] [= 0]
//
// e.g. `bool __cdecl BSTArrayConstIterator<NiPointer<CombatInventoryItem> >::operator*()`
//
// The function must return `null` rather than a wrong name when it cannot
// confidently extract one (plan invariant). The known failure modes this
// guards against, all observed in the real 411,713-line dump:
//
//  - return type / calling convention swallowed into the name (the plan's
//    own pasted regex did this)
//  - operator overloads losing class qualification (nested templates like
//    `> >::operator*`, functor-call `operator()`, lambda qualifiers)
//  - conversion operators collapsing to their conversion type (`__ptr64`,
//    `BSContainer::ForEachResult` — names that collide with real methods)
//  - qualifier chains broken by nested `<...>` groups containing `::`
//    (`std::_Func_impl<...>::_Get` degraded to `_Get`)
// ---------------------------------------------------------------------------

/** A segment of a qualified name: identifier, `Ident<...>`, `~Dtor`, `<lambda_hash>`, or
 * an MSVC backtick-quoted marker like `` `anonymous namespace' `` (which may appear
 * as a qualifier segment or inside template arguments). */
function isValidSegment(seg: string): boolean {
  if (!seg) return false;
  // Backtick-quoted marker segments: `anonymous namespace', `scalar deleting destructor',
  // `dynamic initializer for 'X'' — validated by shape rather than char-by-char.
  if (seg.startsWith("`")) {
    return /^`[^`']+'(?:[^`'()]*')?'?$/.test(seg);
  }
  if (!/^[A-Za-z_~<]/.test(seg)) return false;
  let depth = 0;
  let quoted = false;
  for (let i = 0; i < seg.length; i++) {
    const ch = seg[i];
    if (ch === "<") {
      depth++;
    } else if (ch === ">") {
      depth--;
      if (depth < 0) return false;
    } else if (ch === "`") {
      quoted = !quoted;
    } else if (ch === "'") {
      if (!quoted) return false;
      quoted = false;
    } else if (ch === " ") {
      // A space at bracket depth 0 is only legal as a nested-template close
      // (`> >`) or inside a backtick-quoted marker. Any other depth-0 space
      // means the segment swallowed a return type / calling convention /
      // conversion type.
      if (depth === 0 && !quoted && (seg[i - 1] !== ">" || seg[i + 1] !== ">")) return false;
    } else if (!/[\w:,*&$~\[\]\-]/.test(ch)) {
      return false;
    }
  }
  return depth === 0 && !quoted;
}

/**
 * Splits a qualified-name region on `::` that sit OUTSIDE angle brackets, so
 * `Scaleform::ArrayBase<Scaleform::ArrayData<...> >::Method` splits into
 * ["Scaleform", "ArrayBase<Scaleform::ArrayData<...> >", "Method"]. Returns
 * null if the region is bracket-unbalanced (cannot be a name).
 */
function splitQualified(region: string): string[] | null {
  const segments: string[] = [];
  let depth = 0;
  let start = 0;
  for (let i = 0; i < region.length; i++) {
    const ch = region[i];
    if (ch === "<") {
      depth++;
    } else if (ch === ">") {
      depth--;
      if (depth < 0) return null;
    } else if (ch === ":" && depth === 0 && region[i + 1] === ":") {
      segments.push(region.slice(start, i));
      i++;
      start = i + 1;
    }
  }
  if (depth !== 0) return null;
  segments.push(region.slice(start));
  return segments;
}

/**
 * Validates and normalizes a qualifier (everything before the method name).
 * Returns "" for the empty qualifier (legal for global `operator new`), the
 * normalized qualifier when valid, or null when it cannot be trusted.
 */
function extractValidQualifier(qual: string): string | null {
  if (!qual) return "";
  const segments = splitQualified(qual);
  if (!segments || segments.length === 0) return null;
  for (const seg of segments) {
    if (!isValidSegment(seg)) return null;
  }
  return segments.join("::");
}

/**
 * Classifies the operator token after `::operator` / `operator`:
 * allocator forms (`new`, `delete`, `new[]`, `delete[]`, templated), symbol
 * operators (`operator=`, `operator*`, `operator[]`, `operator()`, ...), and
 * conversion operators (`operator bool`, `operator enum X::Y`, ...).
 * Returns the normalized token, or null when it cannot be trusted.
 */
function classifyOperatorToken(token: string): string | null {
  const t = token.trim();
  if (!t) return null;
  if (/^operator\s+(?:new|delete)(?:\s*\[\])?(?:<[^>]*>)?$/.test(t)) {
    return t.replace(/\s+/g, " ");
  }
  if (/^operator[^\w\s(]+$/.test(t)) {
    return t;
  }
  // conversion operator: `operator <type>`. The type runs until the parameter
  // list's first `(`; MSVC appends fn-pointer tails after it that are not part
  // of the name (`operator bool (__cdecl*)(...)` -> `operator bool`).
  if (/^operator\s+\S/.test(t)) {
    const type = t.slice("operator".length).trim().split("(")[0].trim();
    if (!type) return null;
    return `operator ${type}`;
  }
  return null;
}

export function extractSignatureName(signature: string): string | null {
  // 1. Strip the return type + calling convention. The RIGHTMOST convention
  //    keyword is the function's own — function-pointer RETURN types carry
  //    their own `__cdecl`, e.g.
  //    `bool (__cdecl *)(int) __cdecl Foo::Bar(...)`.
  // The function's own calling convention is the LAST one at paren depth 0:
  // fn-pointer RETURN types and fn-pointer PARAMS carry their own `__cdecl`
  // inside parentheses (`void (__cdecl *cb)(int) op`).
  const CC_KEYWORDS = ["__cdecl", "__thiscall", "__stdcall", "__fastcall", "__vectorcall"];
  let cleaned = signature;
  let depth = 0;
  let ownCCStart = -1;
  for (let i = 0; i < signature.length; i++) {
    const ch = signature[i];
    if (ch === "(") {
      depth++;
    } else if (ch === ")") {
      depth = Math.max(0, depth - 1);
    } else if (depth === 0) {
      for (const cc of CC_KEYWORDS) {
        if (
          signature.startsWith(cc, i) &&
          /\s/.test(signature[i + cc.length] ?? "") &&
          ownCCStart < i
        ) {
          ownCCStart = i;
          i += cc.length - 1;
          break;
        }
      }
    }
  }
  if (ownCCStart >= 0) {
    const ccLen = CC_KEYWORDS.find((k) => signature.startsWith(k, ownCCStart))!.length;
    const afterCC = signature.slice(ownCCStart + ccLen);
    const ws = afterCC.match(/^\s+/);
    cleaned = afterCC.slice(ws ? ws[0].length : 0);
  }

  // 2. MSVC special forms: `scalar deleting destructor', `dynamic initializer for 'X'', etc.
  //    The form must end right at the parameter list — backticks inside template
  //    args/params (`anonymous namespace'::X) are NOT special forms.
  const backtick = cleaned.match(/^((?:[\w:<>,~\s*&]+::)?`[^`']+'(?:[^`'()]*')?'?)\s*\(/);
  if (backtick) {
    const captured = backtick[1].trim();
    const tickIndex = captured.indexOf("`");
    if (tickIndex > 0) {
      // Check if there's a class name (contains ::) before the backtick
      const beforeTick = captured.substring(0, tickIndex);
      if (beforeTick.includes("::")) {
        // Keep the class name and backtick form
        return captured;
      } else {
        // Strip return type/calling convention, keep from backtick onwards
        return captured.substring(tickIndex);
      }
    }
    // Starts with backtick or nothing before it
    return captured;
  }

  // 3. Functor-call operator: `operator()` — the parentheses are part of the
  //    name and precede the parameter list (`<lambda_x>::operator()(int)`).
  const callIdx = cleaned.indexOf("::operator()");
  if (callIdx >= 0) {
    const after = cleaned.slice(callIdx + "::operator()".length);
    if (/^\s*\(/.test(after)) {
      const qual = extractValidQualifier(cleaned.slice(0, callIdx));
      if (qual) return `${qual}::operator()`;
      return null;
    }
  }

  // 4. Everything before the parameter list's first `(`.
  const parenIndex = cleaned.indexOf("(");
  if (parenIndex < 0) return null;
  let head = cleaned.slice(0, parenIndex).trim();
  if (!head) return null;

  // 5. Operator overloads. The qualifier ends at the LAST `::operator`; the
  //    conversion type itself may contain `::` (`operator enum X::Y`), so
  //    split there rather than on `::` generally.
  // Token-bounded: `::operator` must not be the prefix of a longer identifier
  // (`::operator_outofline` is a plain function, not an operator overload).
  let opIdx = -1;
  const opRe = /::operator(?!\w)/g;
  let opMatch: RegExpExecArray | null;
  while ((opMatch = opRe.exec(head)) !== null) {
    opIdx = opMatch.index;
  }
  if (opIdx >= 0) {
    const qual = extractValidQualifier(head.slice(0, opIdx));
    if (qual === null) return null;
    const opName = classifyOperatorToken(head.slice(opIdx + 2));
    if (opName === null) return null;
    return qual ? `${qual}::${opName}` : opName;
  }

  // 6. Global-scope allocator operators: `operator new<512,8>` etc. These have
  //    no qualifier and are confidently extractable.
  if (/^operator\s+(?:new|delete)(?:\s*\[\])?(?:<[^>]*>)?$/.test(head)) {
    return head.replace(/\s+/g, " ");
  }

  // 7. Plain qualified name. A `operator` word that reached this point is an
  //    unparseable operator form — null rather than a wrong name.
  if (/\boperator\b/.test(head)) return null;

  // Split on depth-0 `::` and validate the segments. Leading junk (a return
  // type on a signature with no calling convention) is dropped by retrying
  // after the last depth-0 space that is not a `> >` template close.
  for (let attempt = 0; attempt < 2; attempt++) {
    const segments = splitQualified(head);
    if (segments && segments.length > 0 && segments.every(isValidSegment)) {
      const name = segments.join("::");
      if (segments.length === 1 && /^operator/.test(name)) return null;
      return name;
    }
    let boundary = -1;
    let depth = 0;
    for (let i = 0; i < head.length; i++) {
      const ch = head[i];
      if (ch === "<") depth++;
      else if (ch === ">") depth--;
      else if (ch === " " && depth === 0 && (head[i - 1] !== ">" || head[i + 1] !== ">")) {
        boundary = i;
      }
    }
    if (boundary < 0) return null;
    const next = head.slice(boundary + 1).trimStart();
    if (!next || next === head) return null;
    head = next;
  }
  return null;
}

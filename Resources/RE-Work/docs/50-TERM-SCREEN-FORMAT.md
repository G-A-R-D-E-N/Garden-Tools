# 50 - TERM screen/text mini-format

Follow-up to docs 45/39: the terminal text structure promised in doc 45
§2, byte-decoded from Fallout4.esm samples and validated across all
1,004 vanilla TERM records.

## 1. Record header block

`EDID OBND [NAM0] [WNAM] FULL MODL MODT KSIZ/KWDA PNAM FNAM [COCT] MNAM
WBDT [XMRK]` then the text section:

* `MNAM` u32 flags - canonical value 0x40000001 on most records.
* `SNAM` fixed 0x18-byte block, mostly zeros, trailing dword
  0xFFFFFFFF sentinel (internal layout not fully decoded - copy as-is).
* `BSIZ` u32 - **declared** screen count. Advisory only: 548/1004
  vanilla records declare 0 yet still ship a complete screen block.
  Writers should emit the true count; readers must not trust it.

## 2. Screen blocks

A terminal carries one or more screens. Each screen:

```
[BTXT u32]          # optional: screen-title string ref
ISIZ u32            # item count N for THIS screen
N x item:
    ITXT u32        # item display-text string ref (required)
    [RNAM u32]      # response-text string ref (some items)
    ANAM u8         # item kind: 8 (dominant, 2375x), 4 (518x),
                    #   5 (60x), 6 (56x) across vanilla
    ITID u16        # item index within the screen
    [UNAM u32]      # target formID (linked terminal/display)
```

* All *TXT fields are **localization references**: ID32 into the
  STRINGS tables in localized plugins (per doc 26 §2a), inline
  NUL-terminated text otherwise. Never hardcode one shape.
* `CTXT` never appears in any vanilla master (0 occurrences) despite
  being accepted by the loader - do not emit it.
* Item order inside a screen follows ascending ITID starting at 1.

## 3. Validation stance

Too much benign vanilla variance for hard contracts; the validator
does not enforce TERM beyond structural parsing. The reliable
invariants are: ISIZ == number of following items, ITID sequences from
1, and every ITXT/RNAM/BTXT being exactly 4 bytes in localized files.

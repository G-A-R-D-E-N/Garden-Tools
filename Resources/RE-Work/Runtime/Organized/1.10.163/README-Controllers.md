# Controller button-prompt glyphs — fully decompiled

Not Pip-Boy-specific — this is FO4's general-purpose UI subsystem for rendering "press &lt;button&gt;"
prompts (message boxes, tooltips, HUD prompts, everywhere). Investigated because `PrismaPipboy`
needed a verified char→button table for its own message-box glyphs instead of guessing one, and was
about to build it purely from in-game runtime capture logging. Found the real function and decompiled
the entire chain instead — no more log-capture guessing needed for the table itself.

## The whole chain, decompiled top to bottom

```
UIUtils::GetButtonHTMLFromUserEvent(userEvent, outHtml, size)   RVA 0x12bea10
  -> ControlMap::GetButtonNameFromUserEvent(event, device, index, &outName)   RVA 0x1b289a0
       -> BSInputDeviceManager::GetButtonNameFromID(device, buttonID, &outName)   RVA 0x1b2bea0
            -> BSInputDevice::GetButtonNameFromID(buttonID, &outName)   -- per-device virtual, not
               chased further (thin dispatcher stub, see below)
  -> [name string, e.g. "Xenon_A"] looked up in a name->char scatter-table (built once, lazily,
     inside GetButtonHTMLFromUserEvent itself) -> single glyph char
  -> formatted into <font face='$Controller_Buttons_Inverted' size='%u'>%c</font>
  -> falls back to plain "[%s]" bracketed text if the name has no glyph entry
```

### `UIUtils::GetButtonHTMLFromUserEvent` (RVA `0x12bea10`) — the entrypoint and the glyph table

On first call ever, lazily builds a static `BSTScatterTableKernel<BSFixedStringCS, char>` mapping
button-name strings to single glyph characters (all `SetAt` calls decompiled in full — this is the
complete, real table, not a partial sample):

| Button name | Glyph char | Codepoint |
|---|---|---|
| `Xenon_A` | `A` | `0x41` |
| `Xenon_B` | `B` | `0x42` |
| `Xenon_X` | `C` | `0x43` |
| `Xenon_Y` | `D` | `0x44` |
| `Xenon_Select` | `E` | `0x45` |
| `Xenon_LS` | `F` | `0x46` |
| `Xenon_L1` | `G` | `0x47` |
| `Xenon_L3` | `H` | `0x48` |
| `Xenon_L2` | `I` | `0x49` |
| `Xenon_RS` | `K` | `0x4B` |
| `Xenon_R1` | `L` | `0x4C` |
| `Xenon_R3` | `M` | `0x4D` |
| `Xenon_R2` | `N` | `0x4E` |
| `Xenon_Start` | `O` | `0x4F` |
| `PSN_A` | `a` | `0x61` |
| `PSN_Y` | `b` | `0x62` |
| `PSN_X` | `c` | `0x63` |
| `PSN_B` | `d` | `0x64` |
| `PSN_L3` | `f` | `0x66` |
| `PSN_L1` | `g` | `0x67` |
| `PSN_LS` | `i` | `0x69` |
| `PSN_L2` | `j` | `0x6A` |
| `PSN_R3` | `l` | `0x6C` |
| `PSN_R1` | `m` | `0x6D` |
| `PSN_RS` | `n` | `0x6E` |
| `PSN_R2` | `o` | `0x6F` |
| `PSN_Start` | `p` | `0x70` |
| `PSN_Select` | `z` | `0x7A` |
| `_Dpad_None` | `s` | `0x73` |
| `DPad_Left` | `t` | `0x74` |
| `DPad_Right` | `u` | `0x75` |
| `DPad_Down` | `v` | `0x76` |
| `DPad_Up` | `w` | `0x77` |

(Codepoints 0x4A/0x65/0x68/0x6B/0x71/0x72/0x78/0x79 are gaps — not assigned to anything, presumably
reserved slots in the `$Controller_Buttons_Inverted` font that nothing currently maps to. D-pad glyphs
are shared between platforms — no `Xenon_`/`PSN_` prefix.)

**Per-call resolution logic** (not just the table — the actual device/platform decision):

```c
if (BSInputDeviceManager::QUsingGamepad()) {
    ControlMap::GetButtonNameFromUserEvent(event, kGamepad, deviceIndex, &name);
} else {
    if (!ControlMap::GetButtonNameFromUserEvent(event, kKeyboard, deviceIndex, &name))
        ControlMap::GetButtonNameFromUserEvent(event, kMouse, deviceIndex, &name);
}
```

**This settles the PlayStation-vs-Xbox question definitively, not just by inference:** when a gamepad
is active, this code path *always* requests `kGamepad` and *never* branches on physical controller
brand — there is no PS-detection anywhere in this function or in `ControlMap::GetButtonNameFromUserEvent`
below it. The `PSN_*` glyph slots are real, populated, present in the font — they are simply **never
selected by native code on this runtime**, regardless of what physical pad is plugged in (matches
`PrismaPipboy`'s own observation: FO4 reads a PS pad as XInput). If you want PlayStation icons, you
have to build the `"PSN_" + <same button suffix>` name yourself and look it up against this same
table — the table supports it, the resolution path doesn't use it.

**UserEvent device-index suffix:** the input `userEvent` string can carry a colon-separated device
index (e.g. `"Activate:1"`) — parsed via a manual colon-scan + `atoi()` on the tail — for multi-pad
setups. No colon means device index `-1` (default/any).

**Glyph-miss fallback:** if the resolved name has no entry in the char table, the function falls back
to plain bracketed text, `"[%s]"` with the raw name (e.g. `"[Activate]"`) — this is why some prompts
render as bracketed text instead of an icon: it's not a bug, it's the documented fallback for names
the glyph font doesn't cover.

### `ControlMap::GetButtonNameFromUserEvent` (RVA `0x1b289a0`) — UserEvent → raw button ID

Walks `ControlMap`'s per-context binding tables (contexts 0 through `0x20`, each holding a per-device
array of `{userEvent, buttonID, ...}` bindings — this is the same context system that gates which
controls are live in menus vs. gameplay), looking for the entry whose event name matches the input, in
the CURRENT context first, then falling back through all other contexts if not found there. On a hit
(`buttonID != 0xff`), calls `BSInputDeviceManager::GetButtonNameFromID(device, buttonID, &outName)`.

### `BSInputDeviceManager::GetButtonNameFromID` (RVA `0x1b2bea0`) — thin dispatcher

Just forwards to the per-device-instance virtual `BSInputDevice::GetButtonNameFromID(buttonID, &out)`
for whichever concrete device object is registered at `param_1`'s `INPUT_DEVICE` slot. **Not chased
further this pass** — the concrete gamepad device subclass's own implementation (presumably a switch
over `RE::BS_BUTTON_CODE`'s gamepad range, `0x10000`+, already fully enumerated in
`CommonLibF4/include/RE/B/BS_BUTTON_CODE.h`) would be the next real target if the raw-code↔name
mapping itself is ever in question — not currently needed since the name table above is already the
actionable artifact.

## Practical takeaway

To render a button prompt without depending on further in-game log capture: take the `RE::
BS_BUTTON_CODE` your `ControlMap::GetMappedKey`-style call already returns (already a real,
fully-named CommonLibF4 enum — `kAButton`, `kBButton`, `kXButton`, `kYButton`, `kDPAD_Up/Down/Left/
Right`, `kLTrigger`/`kRTrigger`, `kStart`, `kBack`, `kLStick`/`kRStick`, `kLShoulder`/`kRShoulder`),
map it to the matching `Xenon_*` name by hand (a small fixed table, not decompiled further since the
CommonLibF4 enum names already make the correspondence obvious — e.g. `kAButton` → `"Xenon_A"`), then
look that name up in the glyph table above. No runtime probing required for the table itself; only
worth probing further if a specific `ControlMap::GetMappedKey` return value doesn't match the expected
`BS_BUTTON_CODE` range (i.e. to catch a real engine surprise, not to discover the table).

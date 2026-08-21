import { describe, expect, it } from "vitest";
import { parseReferences, scalarIdsIn } from "../../src/learning/grammar.js";

const slots = (src: string) => parseReferences(src)[0]?.slots ?? [];
const values = (src: string) => slots(src).map((s) => s.rawValue);
const runtimes = (src: string) => slots(src).map((s) => s.runtime);

describe("parseReferences — CommonLibF4 grammar", () => {
  it("fills every runtime slot from a scalar REL::ID", () => {
    const src = "REL::Relocation<uintptr_t> f{ REL::ID(1000120) };";
    expect(runtimes(src)).toEqual(["kOG", "kNG", "kAE"]);
    expect(values(src)).toEqual(["1000120", "1000120", "1000120"]);
    expect(parseReferences(src)[0].form).toBe("id_scalar");
  });

  it("reads a braced REL::VariantID declaration with an intervening name", () => {
    const src = "inline constexpr REL::VariantID EventIndex{ 1163657, 2663407 };";
    expect(parseReferences(src)).toHaveLength(1);
    expect(parseReferences(src)[0].form).toBe("variant");
    expect(values(src)).toEqual(["1163657", "2663407", "2663407"]);
  });

  it("pads a short list with its last value, per REL/VariantID.h", () => {
    // { og, ae } means kNG takes the AE value too — not that kAE is unset.
    expect(values("REL::VariantID X{ 10, 20 };")).toEqual(["10", "20", "20"]);
    expect(values("REL::VariantID X{ 10, 20, 30 };")).toEqual(["10", "20", "30"]);
  });

  it("treats 0 as the declared-unavailable sentinel, not as id zero", () => {
    const parsed = slots("inline constexpr REL::VariantID Singleton{ 484974, 0 };");
    expect(parsed.map((s) => s.declaredUnavailable)).toEqual([false, true, true]);
  });

  it("does not mistake std::array<REL::ID, N> for a reference", () => {
    const src = "inline constexpr std::array<REL::ID, 2> Actor{ REL::ID(1455516), REL::ID(985925) };";
    const refs = parseReferences(src);
    // Two independent vtables for one class, not one two-runtime literal.
    expect(refs).toHaveLength(2);
    expect(refs.every((r) => r.form === "id_scalar")).toBe(true);
    expect(refs.map((r) => r.rawValue)).toEqual(["1455516", "985925"]);
  });

  it("keeps nested REL::ID / REL::Offset args inside a VariantID as slots of one reference", () => {
    const src = "REL::VariantID v( REL::ID(11), REL::Offset(0x2200), REL::ID(33) );";
    const refs = parseReferences(src);
    expect(refs).toHaveLength(1);
    expect(refs[0].slots.map((s) => [s.runtime, s.method, s.rawValue])).toEqual([
      ["kOG", "id", "11"],
      ["kNG", "offset", "0x2200"],
      ["kAE", "id", "33"],
    ]);
  });

  it("finds both the paren and brace REL::Offset forms", () => {
    const src = [
      "REL::Relocation<RawArray*> a{ REL::Offset(0x5A66B58) };",
      "static REL::Relocation<GetTexture_t> g{ REL::Offset{ 0x27D61F0 } };",
    ].join("\n");
    expect(parseReferences(src).map((r) => r.rawValue)).toEqual(["0x5A66B58", "0x27D61F0"]);
  });

  it("keeps a vtable reference symbolic, class and index intact", () => {
    const refs = parseReferences("uintptr_t v{ RE::VTABLE::BSEffectShaderProperty[0] };");
    expect(refs).toHaveLength(1);
    expect(refs[0].kind).toBe("vtable_symbol");
    expect(refs[0].rawValue).toBe("BSEffectShaderProperty[0]");
    expect(refs[0].slots).toEqual([]);
  });

  it("finds the static-member vtable spelling too, normalised to the same name", () => {
    // Consumer code uses RE::Class::VTABLE[n] as often as RE::VTABLE::Class[n]; matching only the
    // namespace form reported "no vtable references" for repos built entirely on the other one.
    const refs = parseReferences("REL::Relocation<std::uintptr_t> v{ RE::Actor::VTABLE[0] };");
    expect(refs).toHaveLength(1);
    expect(refs[0].kind).toBe("vtable_symbol");
    expect(refs[0].rawValue).toBe("Actor[0]");
  });

  it("keeps a nested class qualified in the static-member form", () => {
    const refs = parseReferences("auto v = RE::BGSCharacterTint::PaletteEntry::VTABLE[0];");
    expect(refs.map((r) => r.rawValue)).toEqual(["BGSCharacterTint::PaletteEntry[0]"]);
  });

  it("does not double-report a class that appears in both spellings on one line", () => {
    const refs = parseReferences("a{ RE::VTABLE::Actor[0] }; b{ RE::Actor::VTABLE[0] };");
    expect(refs.filter((r) => r.kind === "vtable_symbol").map((r) => r.rawValue)).toEqual([
      "Actor[0]",
    ]);
  });

  it("does not match a longer identifier that merely starts with a construct name", () => {
    expect(parseReferences("const auto iddb = REL::IDDB::GetSingleton();")).toEqual([]);
  });

  it("numbers lines from the declaration start even when it wraps", () => {
    const src = ["// header", "REL::VariantID Wrapped{", "  10,", "  20", "};"].join("\n");
    expect(parseReferences(src)[0].lineNumber).toBe(2);
  });

  it("scalarIdsIn reports only unlabelled ids, since labelled slots carry no provenance evidence", () => {
    const src = ["REL::ID(555)", "REL::VariantID X{ 10, 20 }", "REL::Offset(0x30)"].join("\n");
    expect(scalarIdsIn(parseReferences(src))).toEqual([555]);
  });
});

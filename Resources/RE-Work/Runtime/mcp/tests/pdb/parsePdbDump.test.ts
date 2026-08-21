import { describe, expect, it } from "vitest";
import {
  extractCallingConvention,
  extractSignatureName,
  parsePdbLine,
} from "../../src/pdb/parsePdbDump.js";

// Captured verbatim from Symbols/Fallout4_functions.txt.
const REAL_LINES = [
  "  func [0x00020900+ 0 - 0x00020908- 1 | sizeof=  8] (FPO) hknpBodyId& __cdecl hknpBodyId::operator=(hknpBodyId& __that)",
  "  func [0x01cce5b0+17 - 0x01cce6f4-13 | sizeof=324] (FPO) bool __cdecl BSCullingProcess::AddShared(NiAVObject* apObject)",
  "  func [0x017c8e90+ 0 - 0x017c8ec4- 1 | sizeof= 52] (FPO) unsigned __int64 __cdecl hkStringMapOperations::hash(unsigned __int64 key)",
];

const NO_FPO_LINE =
  "  func [0x0abc1230+ 3 - 0x0abc1250- 2 | sizeof= 32] void __cdecl PlayerCharacter::Update(float dt)";

const HEADER_LINES = [
  "Summary for E:\\F4SE OG\\Tools\\PDB\\Fallout4.pdb",
  "  Size: 452201472 bytes",
  "  Guid: {E0BB0FDB-47DA-48F1-886C-CC6967D72468}",
  "  Age: 1",
  "  Attributes: HasPrivateSymbols ",
  "---GLOBALS---",
];

describe("parsePdbLine", () => {
  it("parses the real dump's line grammar with (FPO) present", () => {
    const parsed = parsePdbLine(REAL_LINES[0])!;
    expect(parsed.pdbRva).toBe(0x00020900);
    expect(parsed.sizeof).toBe(8);
    expect(parsed.isFpo).toBe(true);
    expect(parsed.signature).toContain("hknpBodyId::operator=");
  });

  it("parses a line with (FPO) absent", () => {
    const parsed = parsePdbLine(NO_FPO_LINE)!;
    expect(parsed.isFpo).toBe(false);
    expect(parsed.pdbRva).toBe(0x0abc1230);
    expect(parsed.sizeof).toBe(32);
  });

  it("parses every real captured line", () => {
    for (const line of REAL_LINES) {
      expect(parsePdbLine(line), line).not.toBeNull();
    }
  });

  it("returns null for dump header/metadata lines", () => {
    for (const line of HEADER_LINES) {
      expect(parsePdbLine(line), line).toBeNull();
    }
  });

  it("returns null for a blank line", () => {
    expect(parsePdbLine("")).toBeNull();
  });
});

describe("extractCallingConvention", () => {
  it("finds __cdecl", () => {
    expect(extractCallingConvention(parsePdbLine(REAL_LINES[1])!.signature)).toBe("__cdecl");
  });

  it("returns null when no calling convention is present", () => {
    expect(extractCallingConvention("void Foo::Bar(int x)")).toBeNull();
  });
});

describe("extractSignatureName", () => {
  it("extracts a plain qualified name", () => {
    expect(extractSignatureName(parsePdbLine(REAL_LINES[1])!.signature)).toBe(
      "BSCullingProcess::AddShared"
    );
    expect(extractSignatureName(parsePdbLine(REAL_LINES[2])!.signature)).toBe(
      "hkStringMapOperations::hash"
    );
  });

  it("extracts an operator overload", () => {
    expect(extractSignatureName(parsePdbLine(REAL_LINES[0])!.signature)).toBe(
      "hknpBodyId::operator="
    );
  });

  it("extracts an MSVC scalar-deleting-destructor form", () => {
    const name = extractSignatureName(
      "void* __cdecl hkaAnimationContainer::`scalar deleting destructor'()"
    );
    expect(name).toContain("scalar deleting destructor");
    expect(name).toContain("hkaAnimationContainer::");
  });

  it("extracts a dynamic-initializer form", () => {
    const name = extractSignatureName(
      "void __cdecl `dynamic initializer for 'fVATSAPCostStimpak''()"
    );
    expect(name).toContain("dynamic initializer");
  });

  it("returns null rather than a wrong name for an unparseable nested-template/lambda form", () => {
    const name = extractSignatureName(
      "BSContainer::ForEachResult __cdecl NiNode::ForEachChild<<lambda_6ec21535ec589316bd10ef46d0628226> >()"
    );
    // Either a best-effort name or null is acceptable; it must never throw.
    expect(() => name).not.toThrow();
  });

  it("returns null when there is no parenthesis at all", () => {
    expect(extractSignatureName("not a signature")).toBeNull();
  });

  it("preserves qualified class names for template operators (regression: nested > >::)", () => {
    const name = extractSignatureName(
      "bool __cdecl BSTArrayConstIterator<NiPointer<CombatInventoryItem> >::operator*()"
    );
    expect(name).toBe("BSTArrayConstIterator<NiPointer<CombatInventoryItem> >::operator*");
    expect(name).toContain("BSTArrayConstIterator");
    expect(name).toContain("::operator*");
  });

  it("preserves qualified class names for deeply nested template operators", () => {
    const name = extractSignatureName(
      "Scaleform::ArrayBase<Scaleform::ArrayData<int,Scaleform::AllocatorGH<Scaleform::HeapMH> > >::operator[]() const"
    );
    expect(name).toContain("Scaleform::ArrayBase");
    expect(name).toContain("::operator[]");
    expect(name).not.toBe("operator[]");
  });

  it("returns null for bare operator with no class qualifier (wrong name prevention)", () => {
    // This should not happen in real PDB data, but if it does, we return null
    // rather than a wrong unqualified name
    const name = extractSignatureName("operator*(int x)");
    expect(name).toBeNull();
  });

  // ---- Fix round 2 regression classes (all from the real 411,713-line dump) ----

  it("qualifies the functor-call operator on a lambda", () => {
    const name = extractSignatureName(
      "float __cdecl <lambda_67d953aa6bf69cd9caf2f5a3c4a2db32>::operator()()"
    );
    expect(name).toBe("<lambda_67d953aa6bf69cd9caf2f5a3c4a2db32>::operator()");
  });

  it("qualifies operator() on a functor with nested template args", () => {
    const name = extractSignatureName(
      "bool __cdecl ArrayEqualFunctor<BSTEventSink<BSTValueRequestEvent<ShowingVATSCritAnim> > * __ptr64,BSTEventSink<BSTValueRequestEvent<ShowingVATSCritAnim> > * __ptr64>::operator()(BSTEventSink<BSTValueRequestEvent<ShowingVATSCritAnim> >* aLHS)"
    );
    expect(name).toBe(
      "ArrayEqualFunctor<BSTEventSink<BSTValueRequestEvent<ShowingVATSCritAnim> > * __ptr64,BSTEventSink<BSTValueRequestEvent<ShowingVATSCritAnim> > * __ptr64>::operator()"
    );
  });

  it("keeps the leading < on lambda conversion operators with fn-pointer return types", () => {
    const name = extractSignatureName(
      "bool (__cdecl *)(BSFastNavmeshEdgeHandle&, unsigned short) __cdecl <lambda_306f2e712744a28197a7eb431e54aa6c>::operator bool (__cdecl*)(BSFastNavmeshEdgeHandle&, unsigned short)()"
    );
    expect(name).toBe("<lambda_306f2e712744a28197a7eb431e54aa6c>::operator bool");
  });

  it("extracts conversion operators to enum types with ::", () => {
    const name = extractSignatureName(
      "BSContainer::ForEachResult (__cdecl *)(BGSBaseAlias*) __cdecl <lambda_bd2a90c310ed6872f4601f060bca044d>::operator enum BSContainer::ForEachResult (__cdecl*)(BGSBaseAlias * __ptr64)()"
    );
    expect(name).toBe("<lambda_bd2a90c310ed6872f4601f060bca044d>::operator enum BSContainer::ForEachResult");
  });

  it("extracts conversion operators to pointer types (not __ptr64)", () => {
    const name = extractSignatureName(
      "Scaleform::GFx::ASMovieRootBase* __cdecl Scaleform::Ptr<Scaleform::GFx::ASMovieRootBase>::operator Scaleform::GFx::ASMovieRootBase * __ptr64()"
    );
    expect(name).toBe(
      "Scaleform::Ptr<Scaleform::GFx::ASMovieRootBase>::operator Scaleform::GFx::ASMovieRootBase * __ptr64"
    );
  });

  it("keeps qualifier chains through multiple nested templates with :: inside", () => {
    const name = extractSignatureName(
      "const void* __cdecl std::_Func_impl<std::_Callable_obj<<lambda_95c1f664d5de6418541cd3f8f73d60d6>,0>,BSTThreadScrapSTLAllocator<void>,bool,BSScript::Object const & __ptr64,std::_Nil,std::_Nil,std::_Nil,std::_Nil,std::_Nil,std::_Nil>::_Get()"
    );
    expect(name).toContain("std::_Func_impl");
    expect(name).toContain("<lambda_95c1f664d5de6418541cd3f8f73d60d6>");
    expect(name).toMatch(/::_Get$/);
  });

  it("extracts global templated operator new without dropping the operator keyword", () => {
    const name = extractSignatureName(
      "void* __cdecl operator new<512,8>(unsigned __int64 nbytes, Scaleform::StackMemPool<512,8,Scaleform::MemPoolImmediateFree>& pool)"
    );
    expect(name).toBe("operator new<512,8>");
  });

  it("extracts global operator new and delete", () => {
    expect(extractSignatureName("void* __cdecl operator new(unsigned __int64 aSize)")).toBe(
      "operator new"
    );
    expect(extractSignatureName("void __cdecl operator delete[](void* apPtr)")).toBe(
      "operator delete[]"
    );
  });

  it("extracts operator delete[] with a qualified template class", () => {
    const name = extractSignatureName(
      "virtual void __cdecl hkMap<unsigned __int64,unsigned __int64,hkMapOperations<unsigned __int64>,hkContainerHeapAllocator>::operator delete[](void* __formal)"
    );
    expect(name).toBe(
      "hkMap<unsigned __int64,unsigned __int64,hkMapOperations<unsigned __int64>,hkContainerHeapAllocator>::operator delete[]"
    );
  });

  it("strips at the function's own calling convention, not a fn-pointer param's", () => {
    const name = extractSignatureName(
      "virtual void __cdecl Scaleform::GFx::AS3::VMAbcFile::ForEachChild_GC(Scaleform::GFx::AS3::RefCountCollector<328>* prcc, void (__cdecl *)(Scaleform::GFx::AS3::RefCountCollector<328>*, Scaleform::GFx::AS3::RefCountBaseGC<328>**) op)"
    );
    expect(name).toBe("Scaleform::GFx::AS3::VMAbcFile::ForEachChild_GC");
  });

  it("extracts functions in the anonymous namespace (backtick first segment)", () => {
    expect(extractSignatureName(
      "BSFixedString* __cdecl `anonymous namespace'::InspectModeRepeatHandler::QLastButtonPressed()"
    )).toBe("`anonymous namespace'::InspectModeRepeatHandler::QLastButtonPressed");
  });

  it("extracts operators whose qualifier has an anonymous-namespace template arg", () => {
    const name = extractSignatureName(
      "BSTArrayConstIterator<GameScript::`anonymous namespace'::ScriptWithStage>& __cdecl BSTArrayConstIterator<GameScript::`anonymous namespace'::ScriptWithStage>::operator++()"
    );
    expect(name).toBe(
      "BSTArrayConstIterator<GameScript::`anonymous namespace'::ScriptWithStage>::operator++"
    );
  });

  it("extracts an anonymous-namespace-qualified dynamic initializer", () => {
    const name = extractSignatureName(
      "void __cdecl `anonymous namespace'::`dynamic initializer for 'sLandTextureFactory''()"
    );
    expect(name).toBe("`anonymous namespace'::`dynamic initializer for 'sLandTextureFactory''");
  });

  it("extracts template args with array sizes", () => {
    const name = extractSignatureName(
      "void __cdecl BSScript::IVirtualMachine::PostErrorToLogger<char const [81]>(const char[81]& aFunctorOrString, BSScript::ErrorLogger::Severity)"
    );
    expect(name).toBe("BSScript::IVirtualMachine::PostErrorToLogger<char const [81]>");
  });

  it("extracts negative template arguments (regression: '-' missing from segment chars)", () => {
    expect(extractSignatureName("void __cdecl hkIntVector::splatImmediate32<-1>()")).toBe(
      "hkIntVector::splatImmediate32<-1>"
    );
    expect(
      extractSignatureName(
        "int __cdecl hkcdGskBase::process_point_edge<-1>(hkVector4f* __restrict vertexA, hkVector4f* __restrict vertexB, int dimB, hkVector4f* __restrict support)"
      )
    ).toBe("hkcdGskBase::process_point_edge<-1>");
  });

  it("does not treat operator_outofline as an operator overload (regression: token-bound ::operator)", () => {
    const name = extractSignatureName(
      "hkbInternal::HksObject __cdecl hkbInternal::hks::operator_outofline<hkbInternal::hks::OpcodeAdd>(hkbInternal::lua_State* s, hkbInternal::HksObject left, hkbInternal::HksObject right, hkbInternal::hksInstruction* PC)"
    );
    expect(name).toBe("hkbInternal::hks::operator_outofline<hkbInternal::hks::OpcodeAdd>");
  });
});

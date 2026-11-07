#pragma once

/* The .pex object model, reader and writer now live in Radicle. This header
   keeps the fo4ide::papyrus spellings that the native host already uses. */

#include <fo4/pex/pex.hpp>

namespace fo4ide::papyrus {

using fo4::pex::PexDebugFunction;
using fo4::pex::PexFile;
using fo4::pex::PexFunction;
using fo4::pex::PexInstruction;
using fo4::pex::PexObject;
using fo4::pex::PexProperty;
using fo4::pex::PexPropertyGroup;
using fo4::pex::PexState;
using fo4::pex::PexStruct;
using fo4::pex::PexStructMember;
using fo4::pex::PexStructOrder;
using fo4::pex::PexTypedName;
using fo4::pex::PexUserFlag;
using fo4::pex::PexValue;
using fo4::pex::PexValueType;
using fo4::pex::PexVariable;

}  // namespace fo4ide::papyrus

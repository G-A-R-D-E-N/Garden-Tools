using Mutagen.Bethesda.Fallout4.Internals;
using Mutagen.Bethesda.Plugins;
using Mutagen.Bethesda.Plugins.Binary.Overlay;
using Mutagen.Bethesda.Plugins.Binary.Streams;
using Mutagen.Bethesda.Plugins.Binary.Translations;
using Noggog;
using System.Buffers.Binary;
using System.Diagnostics;
using Mutagen.Bethesda.Translations.Binary;
using static Mutagen.Bethesda.Fallout4.Condition;
using Mutagen.Bethesda.Plugins.Internals;

namespace Mutagen.Bethesda.Fallout4;

partial class ConditionFloatBinaryOverlay
{
    public partial IConditionDataGetter GetDataCustom(int location)
    {
        var functionIndex = BinaryPrimitives.ReadUInt16LittleEndian(_structData.Slice(location));
        if (functionIndex == ConditionBinaryCreateTranslation.EventFunctionIndex)
        {
            return GetEventDataBinaryOverlay.GetEventDataFactory(new OverlayStream(_structData.Slice(location), _package), _package);
        }
        else
        {
            return FunctionConditionDataBinaryOverlay.FunctionConditionDataFactory(new OverlayStream(_structData.Slice(location), _package), _package);
        }
    }

    partial void CustomFactoryEnd(OverlayStream stream, int finalPos, int offset)
    {
        stream.Position = offset;
        _structData = stream.RemainingMemory;
    }
}

partial class ConditionGlobalBinaryOverlay
{
    public partial IConditionDataGetter GetDataCustom(int location)
    {
        var functionIndex = BinaryPrimitives.ReadUInt16LittleEndian(_structData.Slice(location));
        if (functionIndex == ConditionBinaryCreateTranslation.EventFunctionIndex)
        {
            return GetEventDataBinaryOverlay.GetEventDataFactory(new OverlayStream(_structData.Slice(location), _package), _package);
        }
        else
        {
            return FunctionConditionDataBinaryOverlay.FunctionConditionDataFactory(new OverlayStream(_structData.Slice(location), _package), _package);
        }
    }

    partial void CustomFactoryEnd(OverlayStream stream, int finalPos, int offset)
    {
        stream.Position = offset;
        _structData = stream.RemainingMemory;
    }
}

using System;
using System.Collections.Generic;
using System.Linq;

namespace Radicle.Interop;

public enum PexValueType : byte
{
    None = 0,
    Identifier = 1,
    String = 2,
    Integer = 3,
    Float = 4,
    Bool = 5,
}

/// <summary>One decoded .pex value. <see cref="String"/> is a string-table index, or -1.</summary>
public readonly record struct PexValue(PexValueType Type, int String, int Integer, float Real, bool Boolean);

/// <summary>A name and type pair as string-table indexes; -1 is an empty name the table does not hold.</summary>
public readonly record struct PexTypedName(int Name, int Type);

/// <summary>One instruction: its opcode byte and its decoded operands in file order, the vararg count removed.</summary>
public sealed record PexInstruction(byte Opcode, int Line, IReadOnlyList<PexValue> Arguments);

/// <summary>One function. <see cref="State"/> is its state index, or -1 for a property handler.</summary>
public sealed record PexFunction(
    int State,
    int Name,
    int ReturnType,
    uint UserFlags,
    bool IsGlobal,
    bool IsNative,
    IReadOnlyList<PexTypedName> Parameters,
    IReadOnlyList<PexTypedName> Locals,
    IReadOnlyList<PexInstruction> Instructions);

public sealed record PexState(int Name, IReadOnlyList<int> Functions);

public sealed record PexVariable(int Name, int Type, uint UserFlags, bool IsConst, PexValue? DefaultValue);

/// <summary>One property. The handler fields are function indexes, or -1.</summary>
public sealed record PexProperty(int Name, int Type, uint UserFlags, byte Flags, int AutoVarName, int ReadFunction, int WriteFunction);

public sealed record PexStruct(int Name, IReadOnlyList<PexVariable> Members);

/// <summary>One object's code, every name a string-table index into <see cref="PexFile.StringTable"/>.</summary>
public sealed record PexObjectCode(
    int AutoStateName,
    IReadOnlyList<PexState> States,
    IReadOnlyList<PexFunction> Functions,
    IReadOnlyList<PexVariable> Variables,
    IReadOnlyList<PexProperty> Properties,
    IReadOnlyList<PexStruct> Structs);

public sealed partial class PexFile
{
    public unsafe PexObjectCode ReadCode(int objectIndex)
    {
        using var use = Use();
        Native.PexObjectCode raw = default;
        Call.Check(Native.RadicleNative.PexObjectCodeGet(use.Pointer, (nuint)objectIndex, &raw));
        try
        {
            var values = (Native.PexValue*)raw.Values;
            var names = (Native.PexTypedName*)raw.TypedNames;
            var instructions = (Native.PexInstruction*)raw.Instructions;

            var functions = new PexFunction[(int)raw.FunctionCount];
            for (var index = 0; index < functions.Length; ++index)
            {
                var function = ((Native.PexFunction*)raw.Functions)[index];
                var code = new PexInstruction[(int)function.InstructionCount];
                for (var at = 0; at < code.Length; ++at)
                {
                    var instruction = instructions[(int)function.FirstInstruction + at];
                    var arguments = new PexValue[(int)instruction.ArgumentCount];
                    for (var argument = 0; argument < arguments.Length; ++argument)
                    {
                        arguments[argument] = Value(values[(int)instruction.FirstArgument + argument]);
                    }
                    code[at] = new PexInstruction(instruction.Opcode, instruction.Line, arguments);
                }
                functions[index] = new PexFunction(Index(function.State), Index(function.Name), Index(function.ReturnType),
                    function.UserFlags, function.IsGlobal != 0, function.IsNative != 0,
                    Names(names, function.FirstParameter, function.ParameterCount),
                    Names(names, function.FirstLocal, function.LocalCount), code);
            }

            var states = new PexState[(int)raw.StateCount];
            for (var index = 0; index < states.Length; ++index)
            {
                var state = ((Native.PexState*)raw.States)[index];
                states[index] = new PexState(Index(state.Name),
                    Enumerable.Range((int)state.FirstFunction, (int)state.FunctionCount).ToArray());
            }

            var properties = new PexProperty[(int)raw.PropertyCount];
            for (var index = 0; index < properties.Length; ++index)
            {
                var property = ((Native.PexProperty*)raw.Properties)[index];
                properties[index] = new PexProperty(Index(property.Name), Index(property.Type), property.UserFlags,
                    property.Flags, Index(property.AutoVarName), Index(property.ReadFunction), Index(property.WriteFunction));
            }

            var structs = new PexStruct[(int)raw.StructCount];
            for (var index = 0; index < structs.Length; ++index)
            {
                var row = ((Native.PexStruct*)raw.Structs)[index];
                structs[index] = new PexStruct(Index(row.Name),
                    Variables((Native.PexVariable*)raw.StructMembers, row.FirstMember, row.MemberCount));
            }

            return new PexObjectCode(Index(raw.AutoStateName), states, functions,
                Variables((Native.PexVariable*)raw.Variables, 0, raw.VariableCount), properties, structs);
        }
        finally
        {
            Native.RadicleNative.PexFreeObjectCode(&raw);
        }
    }

    private static int Index(nuint value) => value == nuint.MaxValue ? -1 : checked((int)value);

    private static PexValue Value(Native.PexValue value) =>
        new((PexValueType)value.Type, Index(value.String), value.Integer, value.Real, value.Boolean != 0);

    private static unsafe PexTypedName[] Names(Native.PexTypedName* names, nuint first, nuint count)
    {
        var result = new PexTypedName[(int)count];
        for (var index = 0; index < result.Length; ++index)
        {
            var name = names[(int)first + index];
            result[index] = new PexTypedName(Index(name.Name), Index(name.Type));
        }
        return result;
    }

    private static unsafe PexVariable[] Variables(Native.PexVariable* rows, nuint first, nuint count)
    {
        var result = new PexVariable[(int)count];
        for (var index = 0; index < result.Length; ++index)
        {
            var row = rows[(int)first + index];
            result[index] = new PexVariable(Index(row.Name), Index(row.Type), row.UserFlags, row.IsConst != 0,
                row.HasDefault != 0 ? Value(row.DefaultValue) : null);
        }
        return result;
    }
}

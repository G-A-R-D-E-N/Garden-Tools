using System;
using System.Collections.Generic;
using System.Linq;

namespace FO4RecordEditor.Services.Papyrus;

internal static class PapyrusCreationKitFidelity
{
    private static readonly byte AssignOpcode = Opcode("assign");

    public static void Normalize(PexFile pex)
    {
        var changed = false;
        foreach (var function in Functions(pex))
        {
            foreach (var instruction in function.Instructions)
            {
                changed |= LowerGotoStateLiteral(instruction);
                changed |= FoldNegativeLiteral(instruction);
            }
        }

        if (changed) pex.RebuildStringTable();
        CanonicalizeGeneratedStrings(pex);
    }

    private static bool LowerGotoStateLiteral(PexInstruction instruction)
    {
        var argumentIndex = instruction.Mnemonic switch
        {
            "callmethod" => 3,
            "callparent" => 2,
            _ => -1,
        };
        if (argumentIndex < 0 || instruction.Args.Count <= argumentIndex) return false;
        if (instruction.Args[0].Type != PexValueType.Identifier
            || !instruction.Args[0].Str.Equals("GotoState", StringComparison.OrdinalIgnoreCase)) return false;

        var argument = instruction.Args[argumentIndex];
        if (argument.Type != PexValueType.String) return false;

        var lowered = argument.Str.ToLowerInvariant();
        if (lowered == argument.Str) return false;
        argument.Str = lowered;
        return true;
    }

    private static bool FoldNegativeLiteral(PexInstruction instruction)
    {
        if (instruction.Args.Count != 2) return false;
        var operand = instruction.Args[1];

        if (instruction.Mnemonic == "ineg" && operand.Type == PexValueType.Integer)
        {
            RewriteAsAssign(instruction);
            operand.Int = -operand.Int;
            return true;
        }

        if (instruction.Mnemonic == "fneg" && operand.Type == PexValueType.Float)
        {
            RewriteAsAssign(instruction);
            operand.Float = -operand.Float;
            return true;
        }

        return false;
    }

    private static void CanonicalizeGeneratedStrings(PexFile pex)
    {
        var canonical = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        var table = new List<string>();

        string Canonical(string? value)
        {
            value ??= "";
            if (canonical.TryGetValue(value, out var first)) return first;
            canonical[value] = value;
            table.Add(value);
            return value;
        }

        foreach (var value in pex.StringTable) Canonical(value);

        void Value(PexValue? value)
        {
            if (value?.Type is PexValueType.Identifier or PexValueType.String)
                value.Str = Canonical(value.Str);
        }

        void Function(PexFunction function)
        {
            function.ReturnType = Canonical(function.ReturnType);
            function.DocString = Canonical(function.DocString);
            foreach (var parameter in function.Params)
            {
                parameter.Name = Canonical(parameter.Name);
                parameter.Type = Canonical(parameter.Type);
            }
            foreach (var local in function.Locals)
            {
                local.Name = Canonical(local.Name);
                local.Type = Canonical(local.Type);
            }
            foreach (var instruction in function.Instructions)
                foreach (var argument in instruction.Args)
                    Value(argument);
        }

        foreach (var flag in pex.UserFlags) flag.Name = Canonical(flag.Name);

        foreach (var debugFunction in pex.DebugFunctions)
        {
            debugFunction.ObjectName = Canonical(debugFunction.ObjectName);
            debugFunction.StateName = Canonical(debugFunction.StateName);
            debugFunction.FunctionName = Canonical(debugFunction.FunctionName);
        }

        foreach (var group in pex.PropertyGroups)
        {
            group.ObjectName = Canonical(group.ObjectName);
            group.GroupName = Canonical(group.GroupName);
            group.DocString = Canonical(group.DocString);
            for (var index = 0; index < group.PropertyNames.Count; index++)
                group.PropertyNames[index] = Canonical(group.PropertyNames[index]);
        }

        foreach (var order in pex.StructOrders)
        {
            order.ObjectName = Canonical(order.ObjectName);
            order.OrderName = Canonical(order.OrderName);
            for (var index = 0; index < order.MemberNames.Count; index++)
                order.MemberNames[index] = Canonical(order.MemberNames[index]);
        }

        foreach (var obj in pex.Objects)
        {
            obj.Name = Canonical(obj.Name);
            obj.ParentClassName = Canonical(obj.ParentClassName);
            obj.DocString = Canonical(obj.DocString);
            obj.AutoStateName = Canonical(obj.AutoStateName);

            foreach (var structure in obj.Structs)
            {
                structure.Name = Canonical(structure.Name);
                foreach (var member in structure.Members)
                {
                    member.Name = Canonical(member.Name);
                    member.Type = Canonical(member.Type);
                    member.DocString = Canonical(member.DocString);
                    Value(member.DefaultValue);
                }
            }

            foreach (var variable in obj.Variables)
            {
                variable.Name = Canonical(variable.Name);
                variable.Type = Canonical(variable.Type);
                Value(variable.DefaultValue);
            }

            foreach (var property in obj.Properties)
            {
                property.Name = Canonical(property.Name);
                property.Type = Canonical(property.Type);
                property.DocString = Canonical(property.DocString);
                property.AutoVarName = Canonical(property.AutoVarName);
                if (property.ReadHandler != null) Function(property.ReadHandler);
                if (property.WriteHandler != null) Function(property.WriteHandler);
            }

            foreach (var state in obj.States)
            {
                state.Name = Canonical(state.Name);
                foreach (var function in state.Functions)
                {
                    function.Name = Canonical(function.Name);
                    Function(function);
                }
            }
        }

        pex.StringTable = table;
    }

    private static void RewriteAsAssign(PexInstruction instruction)
    {
        var metadata = PexFile.OpCodes[AssignOpcode];
        instruction.OpCode = AssignOpcode;
        instruction.Mnemonic = metadata.name;
        instruction.FixedArgCount = metadata.args;
        instruction.HasVarArgs = metadata.varargs;
    }

    private static byte Opcode(string name)
    {
        for (var index = 0; index < PexFile.OpCodes.Length; index++)
        {
            if (PexFile.OpCodes[index].name == name) return (byte)index;
        }
        throw new InvalidOperationException($"No PEX opcode named '{name}'.");
    }

    private static IEnumerable<PexFunction> Functions(PexFile pex)
    {
        foreach (var obj in pex.Objects)
        {
            foreach (var property in obj.Properties)
            {
                if (property.ReadHandler != null) yield return property.ReadHandler;
                if (property.WriteHandler != null) yield return property.WriteHandler;
            }

            foreach (var state in obj.States)
            {
                foreach (var function in state.Functions) yield return function;
            }
        }
    }
}

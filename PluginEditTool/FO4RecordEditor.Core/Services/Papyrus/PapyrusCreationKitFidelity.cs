using System;
using System.Collections.Generic;

namespace FO4RecordEditor.Services.Papyrus;

internal static class PapyrusCreationKitFidelity
{
    private static readonly byte AssignOpcode = Opcode("assign");

    public static void Normalize(PexFile pex, PapyrusScriptIndex index)
    {
        var stringsChanged = false;
        foreach (var entry in Functions(pex))
        {
            foreach (var instruction in entry.Function.Instructions)
            {
                stringsChanged |= CanonicalizeGotoStateLiteral(instruction, entry.Object, entry.Function, index);
                FoldNegativeLiteral(instruction);
            }
        }

        if (stringsChanged) pex.RebuildStringTable();
    }

    private static bool CanonicalizeGotoStateLiteral(
        PexInstruction instruction,
        PexObject obj,
        PexFunction function,
        PapyrusScriptIndex index)
    {
        if (instruction.Args.Count == 0
            || instruction.Args[0].Type != PexValueType.Identifier
            || !instruction.Args[0].Str.Equals("GotoState", StringComparison.OrdinalIgnoreCase))
            return false;

        PexValue argument;
        PapyrusScript? receiverScript;
        bool receiverIsSelf;
        switch (instruction.Mnemonic)
        {
            case "callmethod":
                if (instruction.Args.Count <= 3) return false;
                var receiverType = ReceiverType(instruction.Args[1], obj, function);
                receiverScript = receiverType == null ? null : index.Resolve(receiverType);
                receiverIsSelf = receiverType != null && receiverType.Equals(obj.Name, StringComparison.OrdinalIgnoreCase);
                argument = instruction.Args[3];
                break;
            case "callparent":
                if (instruction.Args.Count <= 2) return false;
                receiverScript = index.Resolve(obj.ParentClassName);
                receiverIsSelf = false;
                argument = instruction.Args[2];
                break;
            default:
                return false;
        }

        if (receiverScript == null
            || index.FindMember(receiverScript, "GotoState", out var owner) is not PapyrusCallableDecl
            || owner == null
            || !owner.Name.Equals("ScriptObject", StringComparison.OrdinalIgnoreCase)
            || argument.Type != PexValueType.String)
            return false;

        var canonical = DeclaredStateName(obj, argument.Str, receiverIsSelf);
        if (canonical == null || canonical == argument.Str) return false;
        argument.Str = canonical;
        return true;
    }

    private static string? DeclaredStateName(PexObject obj, string literal, bool receiverIsSelf)
    {
        if (!receiverIsSelf) return null;
        foreach (var state in obj.States)
        {
            if (state.Name.Length != 0 && state.Name.Equals(literal, StringComparison.OrdinalIgnoreCase))
                return state.Name;
        }
        return null;
    }

    private static string? ReceiverType(PexValue receiver, PexObject obj, PexFunction function)
    {
        if (receiver.Type != PexValueType.Identifier) return null;
        if (receiver.Str.Equals("self", StringComparison.OrdinalIgnoreCase)) return obj.Name;

        foreach (var parameter in function.Params)
            if (parameter.Name.Equals(receiver.Str, StringComparison.OrdinalIgnoreCase))
                return parameter.Type;
        foreach (var local in function.Locals)
            if (local.Name.Equals(receiver.Str, StringComparison.OrdinalIgnoreCase))
                return local.Type;
        foreach (var variable in obj.Variables)
            if (variable.Name.Equals(receiver.Str, StringComparison.OrdinalIgnoreCase))
                return variable.Type;

        return null;
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

    private static IEnumerable<(PexObject Object, PexFunction Function)> Functions(PexFile pex)
    {
        foreach (var obj in pex.Objects)
        {
            foreach (var property in obj.Properties)
            {
                if (property.ReadHandler != null) yield return (obj, property.ReadHandler);
                if (property.WriteHandler != null) yield return (obj, property.WriteHandler);
            }

            foreach (var state in obj.States)
            {
                foreach (var function in state.Functions) yield return (obj, function);
            }
        }
    }
}

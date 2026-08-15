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

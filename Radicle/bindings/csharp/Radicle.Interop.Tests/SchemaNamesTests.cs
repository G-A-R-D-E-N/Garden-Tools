using System;
using System.Linq;
using Radicle.Interop;
using Radicle.Interop.Native;
using Xunit;

namespace Radicle.Interop.Tests;

public class SchemaNamesTests
{
    [Fact]
    public void FlagRowsKeepTheirNamesAndSingleBitMasks()
    {
        var values = RecordFieldSchemaEx.Values("NPC_", "NPC_\\TemplateFlags");
        Assert.NotNull(values);
        Assert.Equal(13, values!.Count);
        Assert.Contains(new FieldSchemaValue("Traits", 1), values);
        Assert.Contains(new FieldSchemaValue("Factions", 4), values);
        Assert.Contains(new FieldSchemaValue("Keywords", 4096), values);
        Assert.Equal(4096L, RecordFieldSchemaEx.ValueOf("NPC_", "NPC_\\TemplateFlags", "keywords"));
        Assert.Equal("Factions", RecordFieldSchemaEx.NameOf("NPC_", "NPC_\\TemplateFlags", 4));
        Assert.Null(RecordFieldSchemaEx.ValueOf("NPC_", "NPC_\\TemplateFlags", "NoSuchFlag"));
        Assert.Null(RecordFieldSchemaEx.NameOf("NPC_", "NPC_\\TemplateFlags", 3));
    }

    [Fact]
    public void UnknownFieldIsNullAndAFieldWithoutNamesIsEmpty()
    {
        Assert.Null(RecordFieldSchemaEx.Values("NPC_", "NPC_\\NoSuchField"));
        Assert.Null(RecordFieldSchemaEx.ValueOf("NPC_", "NPC_\\NoSuchField", "Traits"));
        var plain = RecordFieldSchemaEx.Values("NPC_", "NPC_\\SpellCount");
        Assert.NotNull(plain);
        Assert.Empty(plain!);
    }

    [Fact]
    public void EveryNamedValueOfEveryFieldRoundTripsThroughTheLookups()
    {
        var checkedRows = 0;
        foreach (var entry in RecordFieldSchemaEx.For("NPC_").Where(e => e.Names.Count > 0))
        {
            var values = RecordFieldSchemaEx.Values("NPC_", entry.Path)!;
            Assert.Equal(entry.Names, values.Select(v => v.Name).ToArray());
            foreach (var value in values)
            {
                Assert.NotNull(RecordFieldSchemaEx.ValueOf("NPC_", entry.Path, value.Name));
                Assert.NotNull(RecordFieldSchemaEx.NameOf("NPC_", entry.Path, value.Value));
                ++checkedRows;
            }
        }
        Assert.True(checkedRows >= 13);
    }

    [Fact]
    public void RunOnRowsMatchTheEnumAndParseWithOrWithoutSpaces()
    {
        var rows = ConditionFunctions.RunOns();
        Assert.Equal(Enum.GetValues<ConditionRunOn>().Length, rows.Count);
        foreach (var row in rows)
        {
            Assert.True(Enum.IsDefined((ConditionRunOn)row.Value));
            Assert.Equal(row.Name.Replace(" ", string.Empty), ((ConditionRunOn)row.Value).ToString());
        }
        Assert.Equal(ConditionRunOn.Subject, (ConditionRunOn)rows[0].Value);
        Assert.Equal(ConditionRunOn.CombatTarget, ConditionFunctions.RunOn("Combat Target"));
        Assert.Equal(ConditionRunOn.MyKiller, ConditionFunctions.RunOn("mykiller"));
        Assert.Null(ConditionFunctions.RunOn("Nobody"));
        Assert.Equal(0u, (uint)ConditionRunOn.Subject);
        Assert.Equal(1u, (uint)ConditionRunOn.Target);
        Assert.Equal(2u, (uint)ConditionRunOn.Reference);
    }

    [Fact]
    public void ProcedureEnumFollowsTheNativeNameTable()
    {
        var names = PackageProcedures.Names;
        Assert.Equal(Enum.GetValues<PackageProcedure>().Length, names.Count);
        foreach (var procedure in Enum.GetValues<PackageProcedure>())
        {
            Assert.Equal(procedure.ToString(), names[(int)procedure]);
            Assert.Equal(procedure.ToString(), PackageProcedures.Name(procedure));
            Assert.Equal(procedure, PackageProcedures.Parse(procedure.ToString().ToUpperInvariant()));
        }
        Assert.Equal(PackageProcedure.Sandbox, PackageProcedures.Parse("sandbox"));
        Assert.Null(PackageProcedures.Parse("NoSuchProcedure"));
        Assert.Equal(IntPtr.Zero, RadicleNative.PackageProcedureName((uint)names.Count));
    }
}

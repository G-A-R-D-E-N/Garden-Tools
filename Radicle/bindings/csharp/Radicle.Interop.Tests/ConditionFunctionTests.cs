using System;
using Xunit;

namespace Radicle.Interop.Tests;

public class ConditionFunctionTests
{
    [Fact]
    public void NamesMapToTheCtdaFunctionIdsAndIgnoreAsciiCase()
    {
        Assert.Equal((ushort)58, ConditionFunctions.Id("GetStage"));
        Assert.Equal((ushort)59, ConditionFunctions.Id("GetStageDone"));
        Assert.Equal((ushort)69, ConditionFunctions.Id("GetIsRace"));
        Assert.Equal((ushort)72, ConditionFunctions.Id("getisid"));
    }

    [Fact]
    public void InfoReportsFormLinkAndQuestFirstParametersAndNullForAnUnknownId()
    {
        Assert.Equal(new ConditionFunctionInfo("GetStage", true, true), ConditionFunctions.Info(58));
        Assert.Equal(new ConditionFunctionInfo("GetIsRace", true, false), ConditionFunctions.Info(69));
        Assert.False(ConditionFunctions.Info(18)!.TakesFormParam);
        Assert.Null(ConditionFunctions.Info(65535));
    }

    [Fact]
    public void UnknownNamesAreNullAndBadArgumentsAreRefused()
    {
        Assert.Null(ConditionFunctions.Id("NoSuchFunction"));
        Assert.Throws<ArgumentNullException>(() => ConditionFunctions.Id(null!));
        Assert.Throws<ArgumentException>(() => ConditionFunctions.Id("Get\0Stage"));
    }
}

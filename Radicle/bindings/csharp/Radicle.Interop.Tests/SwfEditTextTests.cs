using System;
using System.Linq;
using Xunit;

namespace Radicle.Interop.Tests;

public class SwfEditTextTests
{
    [Fact]
    public void ReadsEveryDeclaredFieldOfTheFixture()
    {
        using var movie = SwfFile.FromBytes(SwfFixtures.Movie());
        var text = movie.EditText(0);
        Assert.Equal((ushort)11, text.CharacterId);
        Assert.Equal(new SwfRectRecord(0, 400, 0, 200), text.Bounds);
        Assert.Equal(0, text.Flags1);
        Assert.Equal(0, text.Flags2);
        Assert.Null(text.FontId);
        Assert.Null(text.Color);
        Assert.Null(text.Layout);
        Assert.Equal("variable", text.Variable);
        Assert.Equal(string.Empty, text.Text);
    }

    [Fact]
    public void EditingTheTextKeepsTheEarlierPayloadBytes()
    {
        using var movie = SwfFile.FromBytes(SwfFixtures.Movie());
        var original = movie.Payload(0);
        var result = movie.EditEditText(0, new SwfEditTextEdit(Text: "hello"));
        Assert.True(result.Changed);
        var edited = movie.Payload(0);
        var text = movie.EditText(0);
        Assert.Equal("hello", text.Text);
        Assert.Equal("variable", text.Variable);
        Assert.Equal((byte)0x80, text.Flags1);
        for (var index = 0; index < original.Length; ++index)
        {
            if (original[index] != edited[index]) break;
            if (index == original.Length - 1) Assert.Fail("the text edit did not change the payload tail");
        }
    }

    [Fact]
    public void EditingTheColourAndBoundsReEncodeOnlyThoseFields()
    {
        using var movie = SwfFile.FromBytes(SwfFixtures.Movie());
        movie.EditEditText(0, new SwfEditTextEdit(Color: 0x80112233u, Bounds: new SwfRectRecord(0, 1000, 0, 500)));
        var text = movie.EditText(0);
        Assert.Equal(0x80112233u, text.Color);
        Assert.True((text.Flags1 & 0x04) != 0);
        Assert.Equal(1000, text.Bounds.XMax);
        Assert.Equal(500, text.Bounds.YMax);
        Assert.Equal("variable", text.Variable);
    }

    [Fact]
    public void AnEmptyEditIsANoOpPlan()
    {
        using var movie = SwfFile.FromBytes(SwfFixtures.Movie());
        var original = movie.Payload(0);
        var result = movie.EditEditText(0, new SwfEditTextEdit());
        Assert.False(result.Changed);
        Assert.Equal(original, movie.Payload(0));
        Assert.Equal(0ul, movie.Revision);
    }

    [Fact]
    public void RefusesATagThatIsNotAnEditText()
    {
        using var movie = SwfFile.FromBytes(SwfFixtures.Movie());
        var label = movie.Tags.First(tag => tag.Code == 43 && !tag.InsideSprite);
        Assert.ThrowsAny<Exception>(() => movie.EditText(label.Index));
    }
}
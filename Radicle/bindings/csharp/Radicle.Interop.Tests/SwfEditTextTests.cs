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
    public void RefusesATagThatIsNotAnEditText()
    {
        using var movie = SwfFile.FromBytes(SwfFixtures.Movie());
        var label = movie.Tags.First(tag => tag.Code == 43 && !tag.InsideSprite);
        Assert.ThrowsAny<Exception>(() => movie.EditText(label.Index));
    }
}
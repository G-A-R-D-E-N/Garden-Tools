using OpenCommonwealth.Services.Hkx;
using Xunit;

namespace BehaviourStudio.Tests;

public sealed class SkeletonAuthoringProofTests
{
    [Fact]
    public void SkeletonAuthoringBuildsAnOrderedValidRig()
    {
        var rig = new SkeletonEdit();
        int root = rig.AddBone("Root");
        int spine = rig.AddBone("Spine", root);
        rig.AddBone("Head", spine);

        Assert.Equal(new[] { -1, 0, 1 }, rig.Skeleton.ParentIndices);
        Assert.True(SkeletonValidator.Valid(rig.Skeleton));
    }
}

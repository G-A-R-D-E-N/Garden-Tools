using Avalonia;
using BehaviourStudio.App;

namespace BehaviourStudio.UiSmoke;

public class HeadlessApp : Application
{
    public override void Initialize() => Ux.Install(this);
}

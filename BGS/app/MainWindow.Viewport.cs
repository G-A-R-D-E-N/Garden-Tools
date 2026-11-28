using System;
using System.Linq;
using Avalonia.Controls;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Threading;

namespace BehaviourStudio.App;

public partial class MainWindow : Window
{
    internal const string MeshModeShaded = "Shaded";
    internal const string MeshModeWireframe = "Wireframe";
    internal const string MeshModeOff = "Off";
    internal static readonly string[] MeshModes = { MeshModeShaded, MeshModeWireframe, MeshModeOff };

    internal string MeshModeName => _meshMode;
    internal bool MeshViewportOnGpu => _viewport is { GlReady: true };
    internal bool MeshWireframeFallbackActive => _meshMode == MeshModeShaded && !MeshViewportOnGpu;
    internal string MeshViewportStatusText => _meshViewportStatus?.Text ?? "";
    internal bool MeshWireframeToggleChecked => _meshWireframeToggle.IsChecked == true;
    internal bool MeshWireframeDrawn => _skeleton.MeshWireframe;
    internal bool MeshViewportLayerVisible => _viewport?.IsVisible == true;

    internal bool SelectMeshModeForTest(string mode)
    {
        int at = Array.IndexOf(MeshModes, mode);
        if (at < 0) return false;
        _meshModeCombo.SelectedIndex = at;
        return true;
    }

    private Control ViewportLayers()
    {
        _viewport = new Viewport3D
        {
            HorizontalAlignment = HorizontalAlignment.Stretch,
            VerticalAlignment = VerticalAlignment.Stretch,
        };
        _viewport.StateChanged += () => Dispatcher.UIThread.Post(SyncViewport);
        _viewport.Rendered += UpdateMeshViewportStatus;
        _skeleton.CameraChanged += () => _viewport?.SetScene(_skeleton.CameraState, ActiveViewportMesh());

        var layers = new Grid();
        layers.Children.Add(_viewport);
        layers.Children.Add(_skeleton);
        return layers;
    }

    private MeshGeometry? ActiveViewportMesh()
    {
        bool shaded = _meshMode == MeshModeShaded;
        return shaded && _viewport is { GlReady: true } ? _viewportMesh : null;
    }

    internal void SyncViewport()
    {
        if (_viewport == null) return;

        bool shaded = _meshMode == MeshModeShaded;
        bool gpu = shaded && _viewport.GlReady;

        _viewport.IsVisible = shaded;
        _viewport.SetScene(_skeleton.CameraState, gpu ? _viewportMesh : null);

        bool wireframe = _meshMode == MeshModeWireframe ||
                         (shaded && (_meshWireframeToggle.IsChecked == true || !gpu));
        _skeleton.MeshWireframe = wireframe;
        _skeleton.DrawBackdrop = !gpu;
        _skeleton.DrawGroundPlane = !gpu;
        _skeleton.InvalidateVisual();

        UpdateMeshViewportStatus();
    }

    private void SetViewportMesh(MeshGeometry? geometry)
    {
        _viewportMesh = geometry;
        if (geometry != null) geometry.Version = ++_viewportMeshVersion;
        SyncViewport();
    }

    private void UpdateViewportGeometry()
    {
        _viewportMesh ??= new MeshGeometry();
        _viewportMesh.Refresh(_meshParts);
        SetViewportMesh(_viewportMesh);
    }

    private void SetMeshMode(string mode)
    {
        if (Array.IndexOf(MeshModes, mode) < 0) mode = MeshModeShaded;
        bool changed = _meshMode != mode;
        _meshMode = mode;
        if (changed) Settings.TrySet("playback_mesh_mode", mode, out _);
        SyncViewport();
    }

    private string RendererName()
    {
        string renderer = _viewport?.Renderer ?? "";
        int line = renderer.IndexOf('\n');
        if (line > 0) renderer = renderer[..line];
        return renderer.Trim().Length > 0 ? renderer.Trim() : "OpenGL";
    }

    private void UpdateMeshViewportStatus()
    {
        if (_meshViewportStatus == null) return;

        string mode = _meshMode switch
        {
            MeshModeWireframe => "Wireframe: the 2D edges, no shaded surface",
            MeshModeOff => "Off: no mesh is drawn",
            _ => _viewport switch
            {
                { GlReady: true } => "Shaded (GL) on " + RendererName(),
                { LastError: { Length: > 0 } error } => "OpenGL failed: " + error,
                _ => "Shaded requested, but there is no OpenGL here: the 2D wireframe is drawn instead",
            },
        };

        string mesh = _viewportMesh == null
            ? "no mesh loaded"
            : $"{_viewportMesh.TriangleCount} tris, skin {_meshSkinMilliseconds:F1} ms";

        string gl = _viewport is { GlReady: true } ? "  |  " + _viewport.GlErrorNote : "";
        _meshViewportStatus.Text = mode + "  |  " + mesh + gl;
    }
}

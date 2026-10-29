using System;
using System.Diagnostics;
using System.Numerics;
using Avalonia.OpenGL;
using Avalonia.OpenGL.Controls;
using Silk.NET.OpenGL;

namespace BehaviourStudio.App;

internal sealed class Viewport3D : OpenGlControlBase
{
    private sealed class Scene
    {
        public ViewportCamera Camera;
        public MeshGeometry? Mesh;
        public int Version;
    }

    private const string VertexShaderDesktop = @"#version 330 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
uniform mat4 uViewProj;
uniform mat4 uModel;
out vec3 vNormal;
out vec3 vWorld;
void main()
{
    vec4 world = uModel * vec4(aPosition, 1.0);
    vWorld = world.xyz;
    vNormal = mat3(uModel) * aNormal;
    gl_Position = uViewProj * world;
}
";

    private const string FragmentShaderDesktop = @"#version 330 core
in vec3 vNormal;
in vec3 vWorld;
uniform vec3 uKeyDirection;
uniform vec3 uFillDirection;
uniform vec3 uBaseColour;
out vec4 oColour;
void main()
{
    vec3 normal = normalize(vNormal);
    if (!gl_FrontFacing) normal = -normal;
    float key = max(dot(normal, normalize(uKeyDirection)), 0.0);
    float fill = max(dot(normal, normalize(uFillDirection)), 0.0) * 0.35;
    vec3 lit = uBaseColour * (0.26 + 0.74 * key + fill);
    oColour = vec4(lit, 1.0);
}
";

    private const string LineVertexShaderDesktop = @"#version 330 core
layout(location = 0) in vec3 aPosition;
uniform mat4 uViewProj;
uniform mat4 uModel;
void main()
{
    gl_Position = uViewProj * uModel * vec4(aPosition, 1.0);
}
";

    private const string LineFragmentShaderDesktop = @"#version 330 core
uniform vec3 uBaseColour;
out vec4 oColour;
void main()
{
    oColour = vec4(uBaseColour, 1.0);
}
";

    private const string VertexShaderEs = @"#version 300 es
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
uniform mat4 uViewProj;
uniform mat4 uModel;
out vec3 vNormal;
out vec3 vWorld;
void main()
{
    vec4 world = uModel * vec4(aPosition, 1.0);
    vWorld = world.xyz;
    vNormal = mat3(uModel) * aNormal;
    gl_Position = uViewProj * world;
}
";

    private const string FragmentShaderEs = @"#version 300 es
precision mediump float;
in vec3 vNormal;
in vec3 vWorld;
uniform vec3 uKeyDirection;
uniform vec3 uFillDirection;
uniform vec3 uBaseColour;
out vec4 oColour;
void main()
{
    vec3 normal = normalize(vNormal);
    if (!gl_FrontFacing) normal = -normal;
    float key = max(dot(normal, normalize(uKeyDirection)), 0.0);
    float fill = max(dot(normal, normalize(uFillDirection)), 0.0) * 0.35;
    vec3 lit = uBaseColour * (0.26 + 0.74 * key + fill);
    oColour = vec4(lit, 1.0);
}
";

    private const string LineVertexShaderEs = @"#version 300 es
layout(location = 0) in vec3 aPosition;
uniform mat4 uViewProj;
uniform mat4 uModel;
void main()
{
    gl_Position = uViewProj * uModel * vec4(aPosition, 1.0);
}
";

    private const string LineFragmentShaderEs = @"#version 300 es
precision mediump float;
uniform vec3 uBaseColour;
out vec4 oColour;
void main()
{
    oColour = vec4(uBaseColour, 1.0);
}
";

    private static readonly Vector3 KeyDirection = Vector3.Normalize(new Vector3(0.35f, -0.55f, 0.75f));
    private static readonly Vector3 FillDirection = Vector3.Normalize(new Vector3(-0.55f, 0.35f, 0.30f));
    private static readonly Vector3 SurfaceColour = new(0.72f, 0.70f, 0.68f);
    private static readonly Vector3 GridColour = new(0.28f, 0.29f, 0.31f);

    private GL? _gl;
    private readonly ViewportGlState _state = new();
    private float[] _packed = Array.Empty<float>();
    private float[] _gridPack = Array.Empty<float>();
    private uint[] _indices = Array.Empty<uint>();

    private readonly Avalonia.Threading.DispatcherTimer _pump = new() { Interval = TimeSpan.FromMilliseconds(100) };
    private volatile Scene _scene = new();
    private readonly Stopwatch _clock = new();
    private readonly Stopwatch _uploadClock = new();
    private bool _firstDrawChecked;

    public Viewport3D()
    {
        ClipToBounds = true;
        IsHitTestVisible = false;
        _pump.Tick += (_, _) =>
        {
            if (GlReady && IsVisible)
            {
                Request();
                Rendered?.Invoke();
            }
        };
    }

    protected override void OnAttachedToVisualTree(Avalonia.VisualTreeAttachmentEventArgs e)
    {
        base.OnAttachedToVisualTree(e);
        _pump.Start();
    }

    protected override void OnDetachedFromVisualTree(Avalonia.VisualTreeAttachmentEventArgs e)
    {
        _pump.Stop();
        base.OnDetachedFromVisualTree(e);
    }

    public bool GlReady { get; private set; }
    public string? LastError { get; private set; }
    public string Renderer { get; private set; } = "not started";
    public double LastDrawMilliseconds { get; private set; }
    public double LastUploadMilliseconds { get; private set; }
    public uint InitGlError { get; private set; }
    public uint FirstDrawGlError { get; private set; }
    public bool FirstDrawChecked => _firstDrawChecked;

    internal ViewportGlState State => _state;

    public string GlErrorNote
    {
        get
        {
            if (InitGlError != 0) return $"glGetError 0x{InitGlError:X} after init";
            if (FirstDrawGlError != 0) return $"glGetError 0x{FirstDrawGlError:X} on the first draw";
            return _firstDrawChecked ? "glGetError clean at init and on the first draw" : "glGetError clean at init";
        }
    }

    public event Action? StateChanged;
    public event Action? Rendered;

    public void SetScene(ViewportCamera camera, MeshGeometry? mesh)
    {
        _scene = new Scene { Camera = camera, Mesh = mesh, Version = mesh?.Version ?? -1 };
        Request();
    }
    private void Request() => RequestNextFrameRendering();

    protected override unsafe void OnOpenGlInit(GlInterface gl)
    {
        _gl = GL.GetApi(gl.GetProcAddress);
        if (_gl == null)
        {
            Fail("the OpenGL entry points could not be loaded");
            return;
        }

        try
        {
            BuildContext(_gl);
            GlReady = true;
            LastError = null;
            InitGlError = ReadError(_gl);
            StateChanged?.Invoke();
        }
        catch (Exception error)
        {
            _state.Reset();
            Fail("the shaded viewport could not be set up: " + error.Message);
        }
    }

    private unsafe void BuildContext(GL api)
    {
        _state.Reset();
        _firstDrawChecked = false;
        InitGlError = 0;
        FirstDrawGlError = 0;

        string version = SafeVersion(api);
        bool es = version.Contains("OpenGL ES", StringComparison.OrdinalIgnoreCase) ||
                  version.Contains("GLES", StringComparison.OrdinalIgnoreCase);

        _state.MeshProgram = Build(ref _state.MeshProgram, es ? VertexShaderEs : VertexShaderDesktop,
                                    es ? FragmentShaderEs : FragmentShaderDesktop);
        _state.LineProgram = Build(ref _state.LineProgram, es ? LineVertexShaderEs : LineVertexShaderDesktop,
                                    es ? LineFragmentShaderEs : LineFragmentShaderDesktop);

        _state.MeshViewProj = api.GetUniformLocation(_state.MeshProgram, "uViewProj");
        _state.MeshModel = api.GetUniformLocation(_state.MeshProgram, "uModel");
        _state.MeshKey = api.GetUniformLocation(_state.MeshProgram, "uKeyDirection");
        _state.MeshFill = api.GetUniformLocation(_state.MeshProgram, "uFillDirection");
        _state.MeshBase = api.GetUniformLocation(_state.MeshProgram, "uBaseColour");
        _state.LineViewProj = api.GetUniformLocation(_state.LineProgram, "uViewProj");
        _state.LineModel = api.GetUniformLocation(_state.LineProgram, "uModel");
        _state.LineBase = api.GetUniformLocation(_state.LineProgram, "uBaseColour");

        _state.MeshVao = api.GenVertexArray();
        _state.MeshVbo = api.GenBuffer();
        _state.MeshEbo = api.GenBuffer();
        _state.GridVao = api.GenVertexArray();
        _state.GridVbo = api.GenBuffer();

        api.BindVertexArray(_state.MeshVao);
        api.BindBuffer(BufferTargetARB.ArrayBuffer, _state.MeshVbo);
        api.EnableVertexAttribArray(0);
        api.VertexAttribPointer(0, 3, VertexAttribPointerType.Float, false,
                                (uint)(MeshGeometry.FloatsPerVertex * sizeof(float)), (void*)0);
        api.EnableVertexAttribArray(1);
        api.VertexAttribPointer(1, 3, VertexAttribPointerType.Float, false,
                                (uint)(MeshGeometry.FloatsPerVertex * sizeof(float)), (void*)(3 * sizeof(float)));
        api.BindBuffer(BufferTargetARB.ElementArrayBuffer, _state.MeshEbo);

        api.BindVertexArray(_state.GridVao);
        api.BindBuffer(BufferTargetARB.ArrayBuffer, _state.GridVbo);
        api.EnableVertexAttribArray(0);
        api.VertexAttribPointer(0, 3, VertexAttribPointerType.Float, false, 3 * sizeof(float), (void*)0);
        api.BindVertexArray(0);

        Renderer = version;
    }

    protected override void OnOpenGlDeinit(GlInterface gl)
    {
        if (_gl != null)
        {
            _gl.DeleteBuffer(_state.MeshVbo);
            _gl.DeleteBuffer(_state.MeshEbo);
            _gl.DeleteBuffer(_state.GridVbo);
            _gl.DeleteVertexArray(_state.MeshVao);
            _gl.DeleteVertexArray(_state.GridVao);
            _gl.DeleteProgram(_state.MeshProgram);
            _gl.DeleteProgram(_state.LineProgram);
        }
        _gl?.Dispose();
        _gl = null;
        GlReady = false;
        _firstDrawChecked = false;
        _state.Reset();
    }

    protected override void OnOpenGlLost()
    {
        GlReady = false;
        _gl = null;
        _firstDrawChecked = false;
        _state.Reset();
        StateChanged?.Invoke();
    }

    protected override unsafe void OnOpenGlRender(GlInterface gl, int framebuffer)
    {
        if (_gl == null || !GlReady) return;
        var api = _gl;
        var scene = _scene;
        var state = _state;

        try
        {
            _clock.Restart();
            api.BindFramebuffer(FramebufferTarget.Framebuffer, (uint)framebuffer);

            double width = Math.Max(1, Bounds.Width);
            double height = Math.Max(1, Bounds.Height);
            double scaling = (VisualRoot as Avalonia.Controls.TopLevel)?.RenderScaling ?? 1;

            api.Viewport(0, 0, (uint)Math.Round(width * scaling), (uint)Math.Round(height * scaling));
            api.Enable(EnableCap.DepthTest);
            api.DepthFunc(DepthFunction.Lequal);
            api.Disable(EnableCap.CullFace);
            api.ClearColor(0.133f, 0.133f, 0.133f, 1f);
            api.Clear(ClearBufferMask.ColorBufferBit | ClearBufferMask.DepthBufferBit);

            var viewProjection = scene.Camera.ViewProjection(width, height);
            EnsureGrid(api, scene.Camera.Fit);

            api.UseProgram(state.LineProgram);
            Uniform(api, state.LineViewProj, viewProjection);
            Uniform(api, state.LineModel, Matrix4x4.Identity);
            api.Uniform3(state.LineBase, GridColour.X, GridColour.Y, GridColour.Z);
            api.BindVertexArray(state.GridVao);
            api.DrawArrays(PrimitiveType.Lines, 0, (uint)state.GridVertices);

            var mesh = scene.Mesh;
            if (mesh != null && mesh.TriangleCount > 0)
            {
                if (scene.Version != state.UploadedVersion)
                {
                    _uploadClock.Restart();
                    UploadMesh(api, mesh);
                    LastUploadMilliseconds = _uploadClock.Elapsed.TotalMilliseconds;
                    state.UploadedVersion = scene.Version;
                }

                api.UseProgram(state.MeshProgram);
                Uniform(api, state.MeshViewProj, viewProjection);
                Uniform(api, state.MeshModel, Matrix4x4.Identity);
                api.Uniform3(state.MeshKey, KeyDirection.X, KeyDirection.Y, KeyDirection.Z);
                api.Uniform3(state.MeshFill, FillDirection.X, FillDirection.Y, FillDirection.Z);
                api.Uniform3(state.MeshBase, SurfaceColour.X, SurfaceColour.Y, SurfaceColour.Z);
                api.BindVertexArray(state.MeshVao);
                api.DrawElements(PrimitiveType.Triangles, state.MeshIndexCount, DrawElementsType.UnsignedInt, (void*)0);
            }

            api.BindVertexArray(0);
            if (!_firstDrawChecked)
            {
                FirstDrawGlError = ReadError(api);
                _firstDrawChecked = true;
            }
            LastDrawMilliseconds = _clock.Elapsed.TotalMilliseconds;
        }
        catch (Exception error)
        {
            Fail(error.Message);
        }
    }

    private void UploadMesh(GL api, MeshGeometry mesh)
    {
        var indices = mesh.PackSnapshot(ref _packed);

        api.BindVertexArray(_state.MeshVao);
        api.BindBuffer(BufferTargetARB.ArrayBuffer, _state.MeshVbo);
        if (_packed.Length == _state.UploadedVertexFloats)
        {
            api.BufferSubData<float>(BufferTargetARB.ArrayBuffer, 0, _packed);
        }
        else
        {
            api.BufferData<float>(BufferTargetARB.ArrayBuffer, _packed, BufferUsageARB.DynamicDraw);
            _state.UploadedVertexFloats = _packed.Length;
        }

        if (ReferenceEquals(indices, _state.UploadedIndices)) return;

        if (_indices.Length != indices.Length) _indices = new uint[indices.Length];
        for (int i = 0; i < _indices.Length; i++)
        {
            int index = indices[i];
            _indices[i] = index < 0 ? 0u : (uint)index;
        }
        api.BindBuffer(BufferTargetARB.ElementArrayBuffer, _state.MeshEbo);
        api.BufferData<uint>(BufferTargetARB.ElementArrayBuffer, _indices, BufferUsageARB.DynamicDraw);
        _state.MeshIndexCount = (uint)_indices.Length;
        _state.UploadedIndices = indices;
    }

    private void EnsureGrid(GL api, double fit)
    {
        float extent = Math.Max(1f, (float)(fit * 1.4));
        if (Math.Abs(extent - _state.GridExtent) < extent * 0.05f && _state.GridVertices > 0) return;

        const int divisions = 10;
        int segments = (divisions + 1) * 2;
        int floats = segments * 2 * 3;
        if (_gridPack.Length < floats) _gridPack = new float[floats];

        float step = extent * 2 / divisions;
        int at = 0;
        for (int i = 0; i <= divisions; i++)
        {
            float offset = -extent + step * i;
            _gridPack[at++] = offset;
            _gridPack[at++] = -extent;
            _gridPack[at++] = 0;
            _gridPack[at++] = offset;
            _gridPack[at++] = extent;
            _gridPack[at++] = 0;
            _gridPack[at++] = -extent;
            _gridPack[at++] = offset;
            _gridPack[at++] = 0;
            _gridPack[at++] = extent;
            _gridPack[at++] = offset;
            _gridPack[at++] = 0;
        }

        api.BindVertexArray(_state.GridVao);
        api.BindBuffer(BufferTargetARB.ArrayBuffer, _state.GridVbo);
        api.BufferData<float>(BufferTargetARB.ArrayBuffer, _gridPack.AsSpan(0, at), BufferUsageARB.StaticDraw);
        _state.GridVertices = segments * 2;
        _state.GridExtent = extent;
    }

    private static unsafe void Uniform(GL api, int location, Matrix4x4 matrix)
    {
        if (location < 0) return;
        Span<float> values = stackalloc float[16]
        {
            matrix.M11, matrix.M12, matrix.M13, matrix.M14,
            matrix.M21, matrix.M22, matrix.M23, matrix.M24,
            matrix.M31, matrix.M32, matrix.M33, matrix.M34,
            matrix.M41, matrix.M42, matrix.M43, matrix.M44,
        };
        fixed (float* value = values) api.UniformMatrix4(location, 1, false, value);
    }

    private void Fail(string message)
    {
        LastError = message;
        GlReady = false;
        StateChanged?.Invoke();
    }

    private static uint ReadError(GL api)
    {
        try { return (uint)api.GetError(); }
        catch (Exception) { return 0; }
    }

    private static string SafeVersion(GL api)
    {
        try { return api.GetStringS(StringName.Version) ?? ""; }
        catch (Exception) { return ""; }
    }

    private uint Build(ref uint program, string vertexSource, string fragmentSource)
    {
        program = ViewportGlState.EnsureProgram(program, () => Link(vertexSource, fragmentSource));
        return program;
    }

    private uint Link(string vertexSource, string fragmentSource)
    {
        var api = _gl ?? throw new InvalidOperationException("no OpenGL context");
        uint vertex = Compile(api, ShaderType.VertexShader, vertexSource);
        uint fragment = Compile(api, ShaderType.FragmentShader, fragmentSource);
        uint program = api.CreateProgram();
        try
        {
            api.AttachShader(program, vertex);
            api.AttachShader(program, fragment);
            api.LinkProgram(program);
            api.GetProgram(program, ProgramPropertyARB.LinkStatus, out int linked);
            if (linked == 0)
            {
                api.DeleteProgram(program);
                throw new InvalidOperationException("shader link failed: " + api.GetProgramInfoLog(program));
            }
            return program;
        }
        finally
        {
            api.DeleteShader(vertex);
            api.DeleteShader(fragment);
        }
    }

    private static uint Compile(GL api, ShaderType type, string source)
    {
        uint shader = api.CreateShader(type);
        api.ShaderSource(shader, source);
        api.CompileShader(shader);
        api.GetShader(shader, ShaderParameterName.CompileStatus, out int compiled);
        if (compiled == 0) throw new InvalidOperationException("shader compile failed: " + api.GetShaderInfoLog(shader));
        return shader;
    }
}

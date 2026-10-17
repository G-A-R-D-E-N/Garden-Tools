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

    private const string VertexShaderDesktop = @"
#version 330 core
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

    private const string FragmentShaderDesktop = @"
#version 330 core
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

    private const string LineVertexShaderDesktop = @"
#version 330 core
layout(location = 0) in vec3 aPosition;
uniform mat4 uViewProj;
uniform mat4 uModel;
void main()
{
    gl_Position = uViewProj * uModel * vec4(aPosition, 1.0);
}
";

    private const string LineFragmentShaderDesktop = @"
#version 330 core
uniform vec3 uBaseColour;
out vec4 oColour;
void main()
{
    oColour = vec4(uBaseColour, 1.0);
}
";

    private const string VertexShaderEs = @"
#version 300 es
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

    private const string FragmentShaderEs = @"
#version 300 es
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

    private const string LineVertexShaderEs = @"
#version 300 es
layout(location = 0) in vec3 aPosition;
uniform mat4 uViewProj;
uniform mat4 uModel;
void main()
{
    gl_Position = uViewProj * uModel * vec4(aPosition, 1.0);
}
";

    private const string LineFragmentShaderEs = @"
#version 300 es
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
    private uint _meshProgram;
    private uint _meshVao;
    private uint _meshVbo;
    private uint _meshEbo;
    private int _meshViewProj = -1;
    private int _meshModel = -1;
    private int _meshKey = -1;
    private int _meshFill = -1;
    private int _meshBase = -1;

    private uint _lineProgram;
    private uint _gridVao;
    private uint _gridVbo;
    private int _lineViewProj = -1;
    private int _lineModel = -1;
    private int _lineBase = -1;
    private int _gridVertices;
    private float _gridExtent;

    private uint _meshIndexCount;
    private int _uploadedVersion = -1;

    private readonly Avalonia.Threading.DispatcherTimer _pump = new() { Interval = TimeSpan.FromMilliseconds(100) };
    private Scene _scene = new();
    private readonly Stopwatch _clock = new();

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

        string version = SafeVersion(_gl);
        bool es = version.Contains("OpenGL ES", StringComparison.OrdinalIgnoreCase) ||
                  version.Contains("GLES", StringComparison.OrdinalIgnoreCase);

        _meshProgram = Build(_meshProgram, es ? VertexShaderEs : VertexShaderDesktop,
                             es ? FragmentShaderEs : FragmentShaderDesktop);
        _lineProgram = Build(_lineProgram, es ? LineVertexShaderEs : LineVertexShaderDesktop,
                             es ? LineFragmentShaderEs : LineFragmentShaderDesktop);

        _meshViewProj = _gl.GetUniformLocation(_meshProgram, "uViewProj");
        _meshModel = _gl.GetUniformLocation(_meshProgram, "uModel");
        _meshKey = _gl.GetUniformLocation(_meshProgram, "uKeyDirection");
        _meshFill = _gl.GetUniformLocation(_meshProgram, "uFillDirection");
        _meshBase = _gl.GetUniformLocation(_meshProgram, "uBaseColour");
        _lineViewProj = _gl.GetUniformLocation(_lineProgram, "uViewProj");
        _lineModel = _gl.GetUniformLocation(_lineProgram, "uModel");
        _lineBase = _gl.GetUniformLocation(_lineProgram, "uBaseColour");

        _meshVao = _gl.GenVertexArray();
        _meshVbo = _gl.GenBuffer();
        _meshEbo = _gl.GenBuffer();
        _gridVao = _gl.GenVertexArray();
        _gridVbo = _gl.GenBuffer();

        _gl.BindVertexArray(_meshVao);
        _gl.BindBuffer(BufferTargetARB.ArrayBuffer, _meshVbo);
        _gl.EnableVertexAttribArray(0);
        _gl.VertexAttribPointer(0, 3, VertexAttribPointerType.Float, false,
                                (uint)(MeshGeometry.FloatsPerVertex * sizeof(float)), (void*)0);
        _gl.EnableVertexAttribArray(1);
        _gl.VertexAttribPointer(1, 3, VertexAttribPointerType.Float, false,
                                (uint)(MeshGeometry.FloatsPerVertex * sizeof(float)), (void*)(3 * sizeof(float)));
        _gl.BindBuffer(BufferTargetARB.ElementArrayBuffer, _meshEbo);

        _gl.BindVertexArray(_gridVao);
        _gl.BindBuffer(BufferTargetARB.ArrayBuffer, _gridVbo);
        _gl.EnableVertexAttribArray(0);
        _gl.VertexAttribPointer(0, 3, VertexAttribPointerType.Float, false, 3 * sizeof(float), (void*)0);
        _gl.BindVertexArray(0);

        _uploadedVersion = -1;
        _gridExtent = 0;
        Renderer = version;
        GlReady = true;
        LastError = null;
        StateChanged?.Invoke();
    }

    protected override void OnOpenGlDeinit(GlInterface gl)
    {
        if (_gl != null)
        {
            _gl.DeleteBuffer(_meshVbo);
            _gl.DeleteBuffer(_meshEbo);
            _gl.DeleteBuffer(_gridVbo);
            _gl.DeleteVertexArray(_meshVao);
            _gl.DeleteVertexArray(_gridVao);
            _gl.DeleteProgram(_meshProgram);
            _gl.DeleteProgram(_lineProgram);
        }
        _gl?.Dispose();
        _gl = null;
        GlReady = false;
        _uploadedVersion = -1;
        _gridExtent = 0;
    }

    protected override void OnOpenGlLost()
    {
        GlReady = false;
        _gl = null;
        StateChanged?.Invoke();
    }

    protected override unsafe void OnOpenGlRender(GlInterface gl, int framebuffer)
    {
        if (_gl == null || !GlReady) return;
        var api = _gl;
        var scene = _scene;

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

            api.UseProgram(_lineProgram);
            Uniform(api, _lineViewProj, viewProjection);
            Uniform(api, _lineModel, Matrix4x4.Identity);
            api.Uniform3(_lineBase, GridColour.X, GridColour.Y, GridColour.Z);
            api.BindVertexArray(_gridVao);
            api.DrawArrays(PrimitiveType.Lines, 0, (uint)_gridVertices);

            var mesh = scene.Mesh;
            if (mesh != null && mesh.TriangleCount > 0)
            {
                if (scene.Version != _uploadedVersion)
                {
                    var upload = Stopwatch.StartNew();
                    UploadMesh(api, mesh);
                    LastUploadMilliseconds = upload.Elapsed.TotalMilliseconds;
                    _uploadedVersion = scene.Version;
                }

                api.UseProgram(_meshProgram);
                Uniform(api, _meshViewProj, viewProjection);
                Uniform(api, _meshModel, Matrix4x4.Identity);
                api.Uniform3(_meshKey, KeyDirection.X, KeyDirection.Y, KeyDirection.Z);
                api.Uniform3(_meshFill, FillDirection.X, FillDirection.Y, FillDirection.Z);
                api.Uniform3(_meshBase, SurfaceColour.X, SurfaceColour.Y, SurfaceColour.Z);
                api.BindVertexArray(_meshVao);
                api.DrawElements(PrimitiveType.Triangles, _meshIndexCount, DrawElementsType.UnsignedInt, (void*)0);
            }

            api.BindVertexArray(0);
            LastDrawMilliseconds = _clock.Elapsed.TotalMilliseconds;
        }
        catch (Exception error)
        {
            Fail(error.Message);
        }
    }

    private void UploadMesh(GL api, MeshGeometry mesh)
    {
        var packed = mesh.Pack();
        api.BindVertexArray(_meshVao);
        api.BindBuffer(BufferTargetARB.ArrayBuffer, _meshVbo);
        api.BufferData<float>(BufferTargetARB.ArrayBuffer, packed, BufferUsageARB.DynamicDraw);

        var indices = new uint[mesh.Indices.Length];
        for (int i = 0; i < indices.Length; i++)
        {
            int index = mesh.Indices[i];
            indices[i] = index < 0 ? 0u : (uint)index;
        }
        api.BindBuffer(BufferTargetARB.ElementArrayBuffer, _meshEbo);
        api.BufferData<uint>(BufferTargetARB.ElementArrayBuffer, indices, BufferUsageARB.DynamicDraw);
        _meshIndexCount = (uint)indices.Length;
    }

    private void EnsureGrid(GL api, double fit)
    {
        float extent = Math.Max(1f, (float)(fit * 1.4));
        if (Math.Abs(extent - _gridExtent) < extent * 0.05f && _gridVertices > 0) return;

        const int divisions = 10;
        var lines = new System.Collections.Generic.List<Vector3>((divisions + 1) * 4);
        float step = extent * 2 / divisions;
        for (int i = 0; i <= divisions; i++)
        {
            float at = -extent + step * i;
            lines.Add(new Vector3(at, -extent, 0));
            lines.Add(new Vector3(at, extent, 0));
            lines.Add(new Vector3(-extent, at, 0));
            lines.Add(new Vector3(extent, at, 0));
        }

        var packed = new float[lines.Count * 3];
        for (int i = 0; i < lines.Count; i++)
        {
            packed[i * 3] = lines[i].X;
            packed[i * 3 + 1] = lines[i].Y;
            packed[i * 3 + 2] = lines[i].Z;
        }

        api.BindVertexArray(_gridVao);
        api.BindBuffer(BufferTargetARB.ArrayBuffer, _gridVbo);
        api.BufferData<float>(BufferTargetARB.ArrayBuffer, packed, BufferUsageARB.StaticDraw);
        _gridVertices = lines.Count;
        _gridExtent = extent;
    }

    private static unsafe void Uniform(GL api, int location, Matrix4x4 matrix)
    {
        if (location < 0) return;
        var values = new float[16]
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

    private static string SafeVersion(GL api)
    {
        try { return api.GetStringS(StringName.Version) ?? ""; }
        catch (Exception) { return ""; }
    }

    private uint Build(uint existing, string vertexSource, string fragmentSource)
    {
        if (existing != 0) return existing;
        var api = _gl ?? throw new InvalidOperationException("no OpenGL context");
        uint vertex = Compile(api, ShaderType.VertexShader, vertexSource);
        uint fragment = Compile(api, ShaderType.FragmentShader, fragmentSource);
        uint program = api.CreateProgram();
        api.AttachShader(program, vertex);
        api.AttachShader(program, fragment);
        api.LinkProgram(program);
        api.GetProgram(program, ProgramPropertyARB.LinkStatus, out int linked);
        if (linked == 0) throw new InvalidOperationException("shader link failed: " + api.GetProgramInfoLog(program));
        api.DeleteShader(vertex);
        api.DeleteShader(fragment);
        return program;
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

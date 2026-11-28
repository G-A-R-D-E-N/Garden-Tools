using System;
using System.Collections.Generic;
using Flash.Events;

namespace Flash.External
{
    public static class ExternalInterface
    {
        private static readonly object Gate = new object();
        private static readonly Dictionary<string, Delegate> Callbacks = new Dictionary<string, Delegate>();

        public static Func<string, object?[], object?>? Host { get; set; }

        public static bool Available => Host != null;

        public static string ObjectID { get; set; } = string.Empty;

        public static object? Call(string functionName, params object?[]? args)
        {
            Func<string, object?[], object?>? host = Host;
            return host == null ? null : host(functionName, Ops.Rest(args));
        }

        public static void AddCallback(string functionName, Delegate closure)
        {
            lock (Gate)
            {
                Callbacks[functionName] = closure;
            }
        }

        public static void addCallback(string functionName, Delegate closure) => AddCallback(functionName, closure);

        public static bool HasCallback(string functionName)
        {
            lock (Gate)
            {
                return Callbacks.ContainsKey(functionName);
            }
        }

        public static object? InvokeCallback(string functionName, params object?[]? args)
        {
            Delegate? closure;
            lock (Gate)
            {
                Callbacks.TryGetValue(functionName, out closure);
            }
            if (closure == null)
            {
                throw new InvalidOperationException("No ExternalInterface callback named " + functionName + ".");
            }
            return Ops.Invoke(closure, Ops.Rest(args));
        }

        public static void Reset()
        {
            lock (Gate)
            {
                Callbacks.Clear();
            }
            Host = null;
            ObjectID = string.Empty;
        }
    }
}

namespace Flash.Net
{
    public class URLRequest
    {
        public URLRequest(string? url = null)
        {
            Url = url;
        }

        public string? Url { get; set; }

        public string Method { get; set; } = "GET";

        public string ContentType { get; set; } = "application/x-www-form-urlencoded";

        public object? Data { get; set; }
    }

    public class URLLoader : EventDispatcher
    {
        public URLLoader(URLRequest? request = null)
        {
            Request = request;
        }

        public URLRequest? Request { get; }

        public string DataFormat { get; set; } = "text";

        public object? Data { get; set; }

        public uint BytesLoaded { get; set; }

        public uint BytesTotal { get; set; }
    }

    public static class Globals
    {
        public static Action<URLRequest, string?>? Handler { get; set; }

        public static void NavigateToUrl(URLRequest request, string? window = null) => Handler?.Invoke(request, window);
    }
}

namespace Flash.System
{
    public class ApplicationDomain
    {
        private readonly object _gate = new object();
        private readonly Dictionary<string, Type> _definitions = new Dictionary<string, Type>();

        public ApplicationDomain(ApplicationDomain? parentDomain = null)
        {
            ParentDomain = parentDomain;
        }

        public static ApplicationDomain CurrentDomain { get; set; } = new ApplicationDomain();

        public ApplicationDomain? ParentDomain { get; }

        public void SetDefinition(string name, Type type)
        {
            lock (_gate)
            {
                _definitions[name] = type;
            }
        }

        public bool HasDefinition(string name)
        {
            lock (_gate)
            {
                if (_definitions.ContainsKey(name))
                {
                    return true;
                }
            }
            return ParentDomain != null && ParentDomain.HasDefinition(name);
        }

        public Type GetDefinition(string name)
        {
            lock (_gate)
            {
                if (_definitions.TryGetValue(name, out Type? type))
                {
                    return type;
                }
            }
            if (ParentDomain != null)
            {
                return ParentDomain.GetDefinition(name);
            }
            throw new ArgumentException("Definition " + name + " could not be found.");
        }
    }

    public static class Capabilities
    {
        public static string Language { get; set; } = "en";

        public static string Os { get; set; } = "Windows";

        public static string Version { get; set; } = "WIN 11,0,0,0";

        public static string PlayerType { get; set; } = "StandAlone";

        public static double ScreenResolutionX { get; set; } = 1280;

        public static double ScreenResolutionY { get; set; } = 720;

        public static bool IsDebugger { get; set; }
    }

    public static class Globals
    {
        public static Action<string, string>? Handler { get; set; }

        public static void Fscommand(string command, string args = "") => Handler?.Invoke(command, args);
    }
}

namespace Flash.Ui
{
    public static class Keyboard
    {
        public const uint BACKSPACE = 8;
        public const uint TAB = 9;
        public const uint ENTER = 13;
        public const uint SHIFT = 16;
        public const uint CONTROL = 17;
        public const uint CAPS_LOCK = 20;
        public const uint ESCAPE = 27;
        public const uint SPACE = 32;
        public const uint PAGE_UP = 33;
        public const uint PAGE_DOWN = 34;
        public const uint END = 35;
        public const uint HOME = 36;
        public const uint LEFT = 37;
        public const uint UP = 38;
        public const uint RIGHT = 39;
        public const uint DOWN = 40;
        public const uint INSERT = 45;
        public const uint DELETE = 46;
        public const uint NUMBER_0 = 48;
        public const uint NUMBER_1 = 49;
        public const uint NUMBER_2 = 50;
        public const uint NUMBER_3 = 51;
        public const uint NUMBER_4 = 52;
        public const uint NUMBER_5 = 53;
        public const uint NUMBER_6 = 54;
        public const uint NUMBER_7 = 55;
        public const uint NUMBER_8 = 56;
        public const uint NUMBER_9 = 57;
        public const uint F1 = 112;
        public const uint F2 = 113;
        public const uint F3 = 114;
        public const uint F4 = 115;
        public const uint F5 = 116;
        public const uint F6 = 117;
        public const uint F7 = 118;
        public const uint F8 = 119;
        public const uint F9 = 120;
        public const uint F10 = 121;
        public const uint F11 = 122;
        public const uint F12 = 123;
    }

    public static class Mouse
    {
        public static bool IsHidden { get; private set; }

        public static string Cursor { get; set; } = "auto";

        public static void Hide() => IsHidden = true;

        public static void Show() => IsHidden = false;
    }
}

namespace Flash.Media
{
    public class SoundTransform
    {
        public SoundTransform(double vol = 1, double panning = 0)
        {
            Volume = vol;
            Pan = panning;
        }

        public double Volume { get; set; }

        public double Pan { get; set; }
    }

    public class SoundChannel : EventDispatcher
    {
        public double Position { get; set; }

        public SoundTransform SoundTransform { get; set; } = new SoundTransform();

        public bool Stopped { get; private set; }

        public void Stop() => Stopped = true;
    }

    public class Sound : EventDispatcher
    {
        public string? Url { get; set; }

        public double Length { get; set; }

        public uint BytesLoaded { get; set; }

        public uint BytesTotal { get; set; }

        public SoundChannel Play(double startTime = 0, int loops = 0, SoundTransform? sndTransform = null)
        {
            SoundChannel channel = new SoundChannel();
            channel.Position = startTime;
            if (sndTransform != null)
            {
                channel.SoundTransform = sndTransform;
            }
            return channel;
        }
    }
}

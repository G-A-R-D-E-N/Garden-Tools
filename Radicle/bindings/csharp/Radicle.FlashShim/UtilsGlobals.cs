using System;

namespace Flash.Utils;

public static class Globals
{
    public static int GetTimer() => (int)Scheduler.Now;

    public static uint SetTimeout(Delegate closure, double delay, params object?[]? args)
    {
        object?[] bound = Ops.Rest(args);
        return (uint)Scheduler.Schedule(() => Ops.Invoke(closure, bound), delay, false);
    }

    public static void ClearTimeout(double id) => Scheduler.Cancel((int)id);

    public static uint SetInterval(Delegate closure, double delay, params object?[]? args)
    {
        object?[] bound = Ops.Rest(args);
        return (uint)Scheduler.Schedule(() => Ops.Invoke(closure, bound), delay, true);
    }

    public static void ClearInterval(double id) => Scheduler.Cancel((int)id);

    public static void RegisterDefinition(string name, Type type) =>
        Flash.System.ApplicationDomain.CurrentDomain.SetDefinition(name, type);

    public static Type GetDefinitionByName(string name) =>
        Flash.System.ApplicationDomain.CurrentDomain.GetDefinition(name);

    public static string GetQualifiedClassName(object? value)
    {
        if (value == null)
        {
            return "null";
        }
        if (value is Undefined)
        {
            return "void";
        }
        return Qualify(value as Type ?? value.GetType());
    }

    private static string Qualify(Type type)
    {
        if (type == typeof(string))
        {
            return "String";
        }
        if (type == typeof(bool))
        {
            return "Boolean";
        }
        if (type == typeof(int) || type == typeof(uint))
        {
            return type == typeof(int) ? "int" : "uint";
        }
        if (type == typeof(double) || type == typeof(float) || type == typeof(long))
        {
            return "Number";
        }
        if (type == typeof(AsObject))
        {
            return "Object";
        }
        if (type == typeof(AsArray))
        {
            return "Array";
        }
        if (type == typeof(AsMath))
        {
            return "Math";
        }
        if (type.IsGenericType && type.GetGenericTypeDefinition() == typeof(AsVector<>))
        {
            return "__AS3__.vec::Vector.<" + Qualify(type.GetGenericArguments()[0]) + ">";
        }
        string ns = type.Namespace ?? string.Empty;
        if (ns.StartsWith("Flash.", StringComparison.Ordinal) || ns.StartsWith("Scaleform.", StringComparison.Ordinal))
        {
            return ns.ToLowerInvariant() + "::" + type.Name;
        }
        return ns.Length == 0 ? type.Name : ns + "::" + type.Name;
    }
}

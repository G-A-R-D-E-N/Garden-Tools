using System;

namespace FO4RecordEditor.Services;

[AttributeUsage(AttributeTargets.Class)]
public sealed class RpcExposeAttribute : Attribute
{
}

[AttributeUsage(AttributeTargets.Method)]
public sealed class RpcHiddenAttribute : Attribute
{
}

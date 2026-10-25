using System;
using System.Globalization;
using System.Text;
using OpenCommonwealth.Services.Hkx;

namespace BehaviourStudio.Tools.Export;

// Constraint: one writer for every exported document, so indentation, escaping and float text cannot drift between formats.
public sealed class ExportJson
{
    private readonly StringBuilder _text = new();
    private int _depth;
    private bool _empty = true;
    private bool _named;

    public ExportJson Object()
    {
        Value();
        _text.Append('{');
        _depth++;
        _empty = true;
        return this;
    }

    public ExportJson EndObject() => Close('}');

    public ExportJson Array()
    {
        Value();
        _text.Append('[');
        _depth++;
        _empty = true;
        return this;
    }

    public ExportJson EndArray() => Close(']');

    public ExportJson Name(string name)
    {
        Value();
        Quote(name);
        _text.Append(':');
        _named = true;
        return this;
    }

    public ExportJson Text(string value)
    {
        Value();
        Quote(value);
        return this;
    }

    public ExportJson Number(int value)
    {
        Value();
        _text.Append(value.ToString(CultureInfo.InvariantCulture));
        return this;
    }

    public ExportJson Number(uint value)
    {
        Value();
        _text.Append(value.ToString(CultureInfo.InvariantCulture));
        return this;
    }

    public ExportJson Number(long value)
    {
        Value();
        _text.Append(value.ToString(CultureInfo.InvariantCulture));
        return this;
    }

    // Constraint: a non-finite float has no JSON number, so it fails the document instead of being written as null or zero.
    public ExportJson Number(float value)
    {
        if (!float.IsFinite(value))
            throw new InvalidOperationException("this document holds a value that is not a finite number");

        Value();
        _text.Append(HkxNumber.Text(value));
        return this;
    }

    public ExportJson Boolean(bool value)
    {
        Value();
        _text.Append(value ? "true" : "false");
        return this;
    }

    public ExportJson Null()
    {
        Value();
        _text.Append("null");
        return this;
    }

    public ExportJson Vector(System.Numerics.Vector3 value) =>
        Array().Number(value.X).Number(value.Y).Number(value.Z).EndArray();

    public ExportJson Quaternion(System.Numerics.Quaternion value) =>
        Array().Number(value.X).Number(value.Y).Number(value.Z).Number(value.W).EndArray();

    public string Finish() => _text.Append('\n').ToString();

    private ExportJson Close(char bracket)
    {
        _depth--;
        if (!_empty)
        {
            _text.Append('\n');
            Indent();
        }

        _text.Append(bracket);
        _empty = false;
        _named = false;
        return this;
    }

    private void Value()
    {
        if (_named)
        {
            _named = false;
            return;
        }

        if (_text.Length == 0)
        {
            _empty = false;
            return;
        }

        if (!_empty) _text.Append(',');
        _text.Append('\n');
        Indent();
        _empty = false;
    }

    private void Indent() => _text.Append(' ', _depth * 2);

    private void Quote(string value)
    {
        _text.Append('"');
        foreach (char c in value)
        {
            switch (c)
            {
                case '"': _text.Append("\\\""); break;
                case '\\': _text.Append("\\\\"); break;
                case '\n': _text.Append("\\n"); break;
                case '\r': _text.Append("\\r"); break;
                case '\t': _text.Append("\\t"); break;
                default:
                    if (c < 0x20 || c > 0x7E)
                        _text.Append("\\u").Append(((int)c).ToString("x4", CultureInfo.InvariantCulture));
                    else _text.Append(c);
                    break;
            }
        }

        _text.Append('"');
    }
}

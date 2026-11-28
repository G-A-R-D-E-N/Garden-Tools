#include "decompile_write.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace fo4::abc::detail {

namespace {

struct Row {
  const char* as3;
  const char* cs;
};

/* Invariant: a null C# name is a known AS3 type with no C# form. */
const Row kTypes[] = {
    {"*", "dynamic"}, {"void", "void"}, {"int", "int"}, {"uint", "uint"},
    {"Number", "double"}, {"String", "string"}, {"Boolean", "bool"}, {"Object", "Flash.AsObject"},
    {"Array", "Flash.AsArray"}, {"Function", "dynamic"}, {"Class", "System.Type"},
    {"Date", "System.DateTime"}, {"Error", "System.Exception"},
    {"RegExp", "System.Text.RegularExpressions.Regex"}, {"Math", "Flash.AsMath"},
    {"__AS3__.vec::Vector", "Flash.AsVector"}, {"XML", nullptr}, {"XMLList", nullptr},
    {"QName", nullptr}, {"Namespace", nullptr},
    {"flash.display::DisplayObject", "Flash.Display.DisplayObject"},
    {"flash.display::DisplayObjectContainer", "Flash.Display.DisplayObjectContainer"},
    {"flash.display::InteractiveObject", "Flash.Display.InteractiveObject"},
    {"flash.display::MovieClip", "Flash.Display.MovieClip"},
    {"flash.display::Sprite", "Flash.Display.Sprite"},
    {"flash.display::Stage", "Flash.Display.Stage"},
    {"flash.display::Shape", "Flash.Display.Shape"},
    {"flash.display::Bitmap", "Flash.Display.Bitmap"},
    {"flash.display::BitmapData", "Flash.Display.BitmapData"},
    {"flash.display::SimpleButton", "Flash.Display.SimpleButton"},
    {"flash.display::Loader", "Flash.Display.Loader"},
    {"flash.display::LoaderInfo", "Flash.Display.LoaderInfo"},
    {"flash.display::Graphics", "Flash.Display.Graphics"},
    {"flash.display::FrameLabel", "Flash.Display.FrameLabel"},
    {"flash.display::Scene", "Flash.Display.Scene"},
    {"flash.display::BlendMode", "Flash.Display.BlendMode"},
    {"flash.display::StageAlign", "Flash.Display.StageAlign"},
    {"flash.display::StageScaleMode", "Flash.Display.StageScaleMode"},
    {"flash.display::StageQuality", "Flash.Display.StageQuality"},
    {"flash.display::StageDisplayState", "Flash.Display.StageDisplayState"},
    {"flash.text::TextField", "Flash.Text.TextField"},
    {"flash.text::TextFormat", "Flash.Text.TextFormat"},
    {"flash.text::TextFieldAutoSize", "Flash.Text.TextFieldAutoSize"},
    {"flash.text::TextFieldType", "Flash.Text.TextFieldType"},
    {"flash.text::TextFormatAlign", "Flash.Text.TextFormatAlign"},
    {"flash.text::TextLineMetrics", "Flash.Text.TextLineMetrics"},
    {"flash.text::StyleSheet", "Flash.Text.StyleSheet"},
    {"flash.text::AntiAliasType", "Flash.Text.AntiAliasType"},
    {"flash.text::GridFitType", "Flash.Text.GridFitType"},
    {"flash.events::Event", "Flash.Events.Event"},
    {"flash.events::MouseEvent", "Flash.Events.MouseEvent"},
    {"flash.events::KeyboardEvent", "Flash.Events.KeyboardEvent"},
    {"flash.events::FocusEvent", "Flash.Events.FocusEvent"},
    {"flash.events::TextEvent", "Flash.Events.TextEvent"},
    {"flash.events::TimerEvent", "Flash.Events.TimerEvent"},
    {"flash.events::IOErrorEvent", "Flash.Events.IOErrorEvent"},
    {"flash.events::ErrorEvent", "Flash.Events.ErrorEvent"},
    {"flash.events::ProgressEvent", "Flash.Events.ProgressEvent"},
    {"flash.events::FullScreenEvent", "Flash.Events.FullScreenEvent"},
    {"flash.events::EventDispatcher", "Flash.Events.EventDispatcher"},
    {"flash.events::IEventDispatcher", "Flash.Events.IEventDispatcher"},
    {"flash.events::EventPhase", "Flash.Events.EventPhase"},
    {"flash.geom::Point", "Flash.Geom.Point"},
    {"flash.geom::Rectangle", "Flash.Geom.Rectangle"},
    {"flash.geom::Matrix", "Flash.Geom.Matrix"},
    {"flash.geom::ColorTransform", "Flash.Geom.ColorTransform"},
    {"flash.geom::Transform", "Flash.Geom.Transform"},
    {"flash.geom::Vector3D", "Flash.Geom.Vector3D"},
    {"flash.utils::Timer", "Flash.Utils.Timer"},
    {"flash.utils::Dictionary", "Flash.Utils.Dictionary"},
    {"flash.utils::ByteArray", "Flash.Utils.ByteArray"},
    {"flash.net::URLRequest", "Flash.Net.URLRequest"},
    {"flash.net::URLLoader", "Flash.Net.URLLoader"},
    {"flash.system::ApplicationDomain", "Flash.System.ApplicationDomain"},
    {"flash.system::Capabilities", "Flash.System.Capabilities"},
    {"flash.external::ExternalInterface", "Flash.External.ExternalInterface"},
    {"flash.ui::Keyboard", "Flash.Ui.Keyboard"},
    {"flash.ui::Mouse", "Flash.Ui.Mouse"},
    {"flash.filters::BitmapFilter", "Flash.Filters.BitmapFilter"},
    {"flash.filters::BlurFilter", "Flash.Filters.BlurFilter"},
    {"flash.filters::DropShadowFilter", "Flash.Filters.DropShadowFilter"},
    {"flash.filters::GlowFilter", "Flash.Filters.GlowFilter"},
    {"flash.filters::ColorMatrixFilter", "Flash.Filters.ColorMatrixFilter"},
    {"flash.media::Sound", "Flash.Media.Sound"},
    {"flash.media::SoundChannel", "Flash.Media.SoundChannel"},
    {"flash.media::SoundTransform", "Flash.Media.SoundTransform"},
    {"scaleform.gfx::Extensions", "Scaleform.Gfx.Extensions"},
    {"scaleform.gfx::TextFieldEx", "Scaleform.Gfx.TextFieldEx"},
    {"scaleform.gfx::InteractiveObjectEx", "Scaleform.Gfx.InteractiveObjectEx"},
    {"scaleform.gfx::DisplayObjectEx", "Scaleform.Gfx.DisplayObjectEx"},
    {"scaleform.gfx::FocusEventEx", "Scaleform.Gfx.FocusEventEx"},
    {"scaleform.gfx::MouseEventEx", "Scaleform.Gfx.MouseEventEx"},
    {"scaleform.gfx::KeyboardEventEx", "Scaleform.Gfx.KeyboardEventEx"},
    {"scaleform.gfx::SelectionEx", "Scaleform.Gfx.SelectionEx"},
    {"scaleform.gfx::IMEEx", "Scaleform.Gfx.IMEEx"},
};

const Row kGlobals[] = {
    {"trace", "Flash.Globals.Trace"}, {"parseInt", "Flash.Globals.ParseInt"},
    {"parseFloat", "Flash.Globals.ParseFloat"}, {"isNaN", "double.IsNaN"},
    {"isFinite", "double.IsFinite"},
    {"flash.utils::getTimer", "Flash.Utils.Globals.GetTimer"},
    {"flash.utils::setTimeout", "Flash.Utils.Globals.SetTimeout"},
    {"flash.utils::clearTimeout", "Flash.Utils.Globals.ClearTimeout"},
    {"flash.utils::setInterval", "Flash.Utils.Globals.SetInterval"},
    {"flash.utils::clearInterval", "Flash.Utils.Globals.ClearInterval"},
    {"flash.utils::getDefinitionByName", "Flash.Utils.Globals.GetDefinitionByName"},
    {"flash.utils::getQualifiedClassName", "Flash.Utils.Globals.GetQualifiedClassName"},
    {"flash.net::navigateToURL", "Flash.Net.Globals.NavigateToUrl"},
    {"flash.system::fscommand", "Flash.System.Globals.Fscommand"},
};

/* Invariant: keyed by the AS3 member name alone, so a row renames that member on every receiver. */
const Row kMembers[] = {
    {"x", "X"}, {"y", "Y"}, {"width", "Width"}, {"height", "Height"}, {"scaleX", "ScaleX"},
    {"scaleY", "ScaleY"}, {"rotation", "Rotation"}, {"alpha", "Alpha"}, {"visible", "Visible"},
    {"name", "Name"}, {"parent", "Parent"}, {"stage", "Stage"}, {"root", "Root"},
    {"mouseX", "MouseX"}, {"mouseY", "MouseY"}, {"transform", "Transform"}, {"filters", "Filters"},
    {"mask", "Mask"}, {"blendMode", "BlendMode"}, {"cacheAsBitmap", "CacheAsBitmap"},
    {"loaderInfo", "LoaderInfo"}, {"getBounds", "GetBounds"}, {"getRect", "GetRect"},
    {"localToGlobal", "LocalToGlobal"}, {"globalToLocal", "GlobalToLocal"},
    {"hitTestObject", "HitTestObject"}, {"hitTestPoint", "HitTestPoint"},
    {"mouseEnabled", "MouseEnabled"}, {"mouseChildren", "MouseChildren"},
    {"tabEnabled", "TabEnabled"}, {"tabIndex", "TabIndex"}, {"focusRect", "FocusRect"},
    {"doubleClickEnabled", "DoubleClickEnabled"}, {"buttonMode", "ButtonMode"},
    {"useHandCursor", "UseHandCursor"}, {"graphics", "Graphics"}, {"hitArea", "HitArea"},
    {"startDrag", "StartDrag"}, {"stopDrag", "StopDrag"}, {"enabled", "Enabled"},
    {"numChildren", "NumChildren"}, {"addChild", "AddChild"}, {"addChildAt", "AddChildAt"},
    {"removeChild", "RemoveChild"}, {"removeChildAt", "RemoveChildAt"},
    {"removeChildren", "RemoveChildren"}, {"getChildAt", "GetChildAt"},
    {"getChildByName", "GetChildByName"}, {"getChildIndex", "GetChildIndex"},
    {"setChildIndex", "SetChildIndex"}, {"swapChildren", "SwapChildren"}, {"contains", "Contains"},
    {"gotoAndPlay", "GotoAndPlay"}, {"gotoAndStop", "GotoAndStop"}, {"play", "Play"},
    {"stop", "Stop"}, {"nextFrame", "NextFrame"}, {"prevFrame", "PrevFrame"},
    {"currentFrame", "CurrentFrame"}, {"totalFrames", "TotalFrames"},
    {"currentLabel", "CurrentLabel"}, {"framesLoaded", "FramesLoaded"},
    {"text", "Text"}, {"htmlText", "HtmlText"}, {"textColor", "TextColor"},
    {"textWidth", "TextWidth"}, {"textHeight", "TextHeight"}, {"multiline", "Multiline"},
    {"wordWrap", "WordWrap"}, {"selectable", "Selectable"}, {"autoSize", "AutoSize"},
    {"defaultTextFormat", "DefaultTextFormat"}, {"setTextFormat", "SetTextFormat"},
    {"getTextFormat", "GetTextFormat"}, {"maxChars", "MaxChars"}, {"numLines", "NumLines"},
    {"scrollV", "ScrollV"}, {"maxScrollV", "MaxScrollV"}, {"embedFonts", "EmbedFonts"},
    {"appendText", "AppendText"}, {"replaceText", "ReplaceText"}, {"antiAliasType", "AntiAliasType"},
    {"font", "Font"}, {"size", "Size"}, {"color", "Color"}, {"bold", "Bold"}, {"italic", "Italic"},
    {"underline", "Underline"}, {"align", "Align"}, {"leading", "Leading"},
    {"letterSpacing", "LetterSpacing"}, {"type", "Type"}, {"target", "Target"},
    {"currentTarget", "CurrentTarget"}, {"bubbles", "Bubbles"}, {"cancelable", "Cancelable"},
    {"eventPhase", "EventPhase"}, {"stopPropagation", "StopPropagation"},
    {"stopImmediatePropagation", "StopImmediatePropagation"}, {"preventDefault", "PreventDefault"},
    {"isDefaultPrevented", "IsDefaultPrevented"}, {"clone", "Clone"}, {"localX", "LocalX"},
    {"localY", "LocalY"}, {"stageX", "StageX"}, {"stageY", "StageY"}, {"ctrlKey", "CtrlKey"},
    {"shiftKey", "ShiftKey"}, {"altKey", "AltKey"}, {"buttonDown", "ButtonDown"}, {"delta", "Delta"},
    {"relatedObject", "RelatedObject"}, {"keyCode", "KeyCode"}, {"charCode", "CharCode"},
    {"keyLocation", "KeyLocation"}, {"addEventListener", "AddEventListener"},
    {"removeEventListener", "RemoveEventListener"}, {"dispatchEvent", "DispatchEvent"},
    {"hasEventListener", "HasEventListener"}, {"willTrigger", "WillTrigger"},
    {"left", "Left"}, {"top", "Top"}, {"right", "Right"}, {"bottom", "Bottom"},
    {"topLeft", "TopLeft"}, {"bottomRight", "BottomRight"}, {"containsPoint", "ContainsPoint"},
    {"containsRect", "ContainsRect"}, {"intersects", "Intersects"}, {"intersection", "Intersection"},
    {"union", "Union"}, {"inflate", "Inflate"}, {"isEmpty", "IsEmpty"}, {"offset", "Offset"},
    {"normalize", "Normalize"}, {"delay", "Delay"}, {"repeatCount", "RepeatCount"},
    {"running", "Running"}, {"start", "Start"}, {"reset", "Reset"}, {"currentCount", "CurrentCount"},
    {"length", "Length"}, {"push", "Push"}, {"pop", "Pop"}, {"shift", "Shift"},
    {"unshift", "Unshift"}, {"splice", "Splice"}, {"slice", "Slice"}, {"concat", "Concat"},
    {"join", "Join"}, {"reverse", "Reverse"}, {"sort", "Sort"}, {"sortOn", "SortOn"},
    {"indexOf", "IndexOf"}, {"lastIndexOf", "LastIndexOf"}, {"forEach", "ForEach"}, {"map", "Map"},
    {"filter", "Filter"}, {"some", "Some"}, {"every", "Every"}, {"charAt", "CharAt"},
    {"charCodeAt", "CharCodeAt"}, {"substr", "Substr"}, {"substring", "Substring"},
    {"toLowerCase", "ToLowerCase"}, {"toUpperCase", "ToUpperCase"}, {"split", "Split"},
    {"replace", "Replace"}, {"match", "Match"}, {"search", "Search"},
    {"fromCharCode", "FromCharCode"}, {"hasOwnProperty", "HasOwnProperty"},
    {"toString", "ToString"}, {"valueOf", "ValueOf"}, {"call", "Call"}, {"apply", "Apply"},
    {"abs", "Abs"}, {"ceil", "Ceil"}, {"floor", "Floor"}, {"round", "Round"}, {"max", "Max"},
    {"min", "Min"}, {"pow", "Pow"}, {"sqrt", "Sqrt"}, {"sin", "Sin"}, {"cos", "Cos"},
    {"atan2", "Atan2"}, {"random", "Random"}, {"noInvisibleAdvance", "NoInvisibleAdvance"},
    {"setVerticalAlign", "SetVerticalAlign"}, {"setVerticalAutoSize", "SetVerticalAutoSize"},
    {"appendHtml", "AppendHtml"}, {"setImageSubstitutions", "SetImageSubstitutions"},
    {"controllerIdx", "ControllerIdx"},
};

const char* const kKeywords[] = {
    "abstract", "as", "base", "bool", "break", "byte", "case", "catch", "char", "checked", "class",
    "const", "continue", "decimal", "default", "delegate", "do", "double", "else", "enum", "event",
    "explicit", "extern", "false", "finally", "fixed", "float", "for", "foreach", "goto", "if",
    "implicit", "in", "int", "interface", "internal", "is", "lock", "long", "namespace", "new",
    "null", "object", "operator", "out", "override", "params", "private", "protected", "public",
    "readonly", "ref", "return", "sbyte", "sealed", "short", "sizeof", "stackalloc", "static",
    "string", "struct", "switch", "this", "throw", "true", "try", "typeof", "uint", "ulong",
    "unchecked", "unsafe", "ushort", "using", "virtual", "void", "volatile", "while",
};

struct Entry {
  const char* cs;
  bool type;
};

const std::map<std::string, Entry>& name_table() {
  static const std::map<std::string, Entry> table = [] {
    std::map<std::string, Entry> built;
    for (const Row& row : kTypes) {
      built[row.as3] = Entry{row.cs, true};
    }
    for (const Row& row : kGlobals) {
      built[row.as3] = Entry{row.cs, false};
    }
    return built;
  }();
  return table;
}

const std::map<std::string, const char*>& member_table() {
  static const std::map<std::string, const char*> table = [] {
    std::map<std::string, const char*> built;
    for (const Row& row : kMembers) {
      built[row.as3] = row.cs;
    }
    return built;
  }();
  return table;
}

bool table_hit(const std::string& name, CsName& out) {
  const auto found = name_table().find(name);
  if (found == name_table().end()) {
    return false;
  }
  out.type = found->second.type;
  if (found->second.cs == nullptr) {
    out.status = CsNameStatus::Unsupported;
    out.text = name;
  } else {
    out.status = CsNameStatus::Mapped;
    out.text = found->second.cs;
  }
  return true;
}

CsName resolve_local(const std::string& local) {
  CsName out;
  if (table_hit(local, out)) {
    return out;
  }
  const auto member = member_table().find(local);
  if (member != member_table().end()) {
    out.status = CsNameStatus::Mapped;
    out.text = member->second;
    return out;
  }
  out.status = CsNameStatus::Passthrough;
  out.text = cs_identifier(local);
  return out;
}

std::vector<std::string> split(const std::string& text, char separator) {
  std::vector<std::string> parts;
  std::size_t start = 0;
  for (std::size_t i = 0; i <= text.size(); ++i) {
    if (i == text.size() || text[i] == separator) {
      parts.push_back(text.substr(start, i - start));
      start = i + 1;
    }
  }
  return parts;
}

std::string one_type(const std::string& name, const PackageSet& packages,
                     std::vector<std::string>& unmapped) {
  const CsName resolved = cs_resolve_name(name, packages);
  if (resolved.status == CsNameStatus::Unmapped || resolved.status == CsNameStatus::Unsupported) {
    unmapped.push_back(name);
    return "/* unmapped " + name + " */ dynamic";
  }
  return resolved.text;
}

}  // namespace

bool is_flash_namespace(const std::string& ns) {
  return ns == "flash" || ns.rfind("flash.", 0) == 0 || ns == "scaleform" || ns.rfind("scaleform.", 0) == 0;
}

PackageSet package_namespaces(const AbcFile& file) {
  PackageSet out;
  for (const AbcNamespace& ns : file.pool.namespaces) {
    if (ns.kind == AbcNamespaceKind::Package || ns.kind == AbcNamespaceKind::PackageInternal) {
      const std::string name = file.string_at(ns.name);
      if (!name.empty()) {
        out.insert(name);
      }
    }
  }
  return out;
}

std::string cs_identifier(const std::string& name) {
  std::string out;
  for (const char c : name) {
    const unsigned char byte = static_cast<unsigned char>(c);
    const bool word = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                      c == '_' || byte >= 0x80;
    if (c == '$') {
      out += "_dollar_";
    } else {
      out += word ? c : '_';
    }
  }
  if (out.empty()) {
    return "_";
  }
  if (out[0] >= '0' && out[0] <= '9') {
    out = "_" + out;
  }
  for (const char* keyword : kKeywords) {
    if (out == keyword) {
      return "@" + out;
    }
  }
  return out;
}

std::string cs_dotted(const std::string& ns) {
  std::string out;
  for (const std::string& part : split(ns, '.')) {
    if (!out.empty()) {
      out += ".";
    }
    out += cs_identifier(part);
  }
  return out;
}

CsName cs_resolve_name(const std::string& as3, const PackageSet& packages) {
  CsName out;
  if (as3.empty()) {
    out.text = std::string{};
    return out;
  }
  if (table_hit(as3, out)) {
    return out;
  }
  const std::size_t at = as3.rfind("::");
  if (at == std::string::npos) {
    return resolve_local(as3);
  }
  const std::string ns = as3.substr(0, at);
  const std::string local = as3.substr(at + 2);
  if (ns.find('|') != std::string::npos) {
    for (const std::string& candidate : split(ns, '|')) {
      CsName hit;
      if (table_hit(candidate + "::" + local, hit)) {
        return hit;
      }
    }
    return resolve_local(local);
  }
  if (is_flash_namespace(ns)) {
    out.status = CsNameStatus::Unmapped;
    out.text = as3;
    return out;
  }
  if (packages.count(ns) != 0) {
    out.status = CsNameStatus::Passthrough;
    const auto member = member_table().find(local);
    out.text = cs_dotted(ns) + "." + (member != member_table().end() ? member->second : cs_identifier(local));
    return out;
  }
  return resolve_local(local);
}

CsName cs_resolve_member(const std::string& as3) {
  const std::size_t at = as3.rfind("::");
  CsName out;
  if (at == std::string::npos) {
    return resolve_local(as3);
  }
  const std::string ns = as3.substr(0, at);
  if (ns.find('|') == std::string::npos && is_flash_namespace(ns)) {
    out.status = CsNameStatus::Unmapped;
    out.text = as3;
    return out;
  }
  const std::string local = as3.substr(at + 2);
  const auto member = member_table().find(local);
  if (member != member_table().end()) {
    out.status = CsNameStatus::Mapped;
    out.text = member->second;
    return out;
  }
  out.text = cs_identifier(local);
  return out;
}

std::string cs_type_text(const std::string& as3, const PackageSet& packages,
                         std::vector<std::string>& unmapped) {
  const std::size_t open = as3.find('<');
  if (open == std::string::npos || as3.empty() || as3.back() != '>') {
    return one_type(as3, packages, unmapped);
  }
  const std::string inner = as3.substr(open + 1, as3.size() - open - 2);
  std::vector<std::string> args;
  std::size_t depth = 0;
  std::size_t start = 0;
  for (std::size_t i = 0; i <= inner.size(); ++i) {
    if (i < inner.size() && inner[i] == '<') {
      ++depth;
    } else if (i < inner.size() && inner[i] == '>') {
      depth = depth == 0 ? 0 : depth - 1;
    } else if (i == inner.size() || (inner[i] == ',' && depth == 0)) {
      args.push_back(inner.substr(start, i - start));
      start = i + 1;
    }
  }
  std::string out = one_type(as3.substr(0, open), packages, unmapped) + "<";
  for (std::size_t i = 0; i < args.size(); ++i) {
    out += (i == 0 ? "" : ", ") + cs_type_text(args[i], packages, unmapped);
  }
  return out + ">";
}

std::string cs_literal(const std::string& as3_literal) {
  if (as3_literal == "undefined" || as3_literal == "null") {
    return "null";
  }
  if (as3_literal == "NaN") {
    return "double.NaN";
  }
  if (as3_literal == "Infinity") {
    return "double.PositiveInfinity";
  }
  if (as3_literal == "-Infinity") {
    return "double.NegativeInfinity";
  }
  if (as3_literal.empty() || as3_literal[0] != '"') {
    return as3_literal;
  }
  std::string out;
  for (std::size_t i = 0; i < as3_literal.size(); ++i) {
    const char c = as3_literal[i];
    if (c != '\\' || i + 1 >= as3_literal.size()) {
      out += c;
      continue;
    }
    if (as3_literal[i + 1] == 'x' && i + 3 < as3_literal.size()) {
      out += "\\u00" + as3_literal.substr(i + 2, 2);
      i += 3;
      continue;
    }
    out += c;
    out += as3_literal[++i];
  }
  return out;
}

const char* cs_cast_helper(const std::string& type) {
  if (type == "int") {
    return "Flash.Ops.ToInt";
  }
  if (type == "uint") {
    return "Flash.Ops.ToUInt";
  }
  if (type == "Number") {
    return "Flash.Ops.ToNumber";
  }
  if (type == "Boolean") {
    return "Flash.Ops.ToBool";
  }
  if (type == "String") {
    return "Flash.Ops.ToString";
  }
  return nullptr;
}

}  // namespace fo4::abc::detail

namespace fo4::abc {

std::vector<std::string> cs_unmapped_names(const AbcFile& file) {
  std::set<std::string> found;
  const detail::PackageSet none;
  for (const AbcMultiname& multiname : file.pool.multinames) {
    if (multiname.kind != AbcMultinameKind::QName && multiname.kind != AbcMultinameKind::QNameA) {
      continue;
    }
    const std::string ns = file.namespace_at(multiname.ns);
    if (!detail::is_flash_namespace(ns)) {
      continue;
    }
    const std::string full = ns + "::" + file.string_at(multiname.name);
    if (detail::cs_resolve_name(full, none).status == detail::CsNameStatus::Unmapped) {
      found.insert(full);
    }
  }
  return std::vector<std::string>(found.begin(), found.end());
}

std::vector<std::string> cs_emitted_names() {
  std::set<std::string> found;
  const auto shim_owned = [](const std::string& name) {
    return name.rfind("Flash.", 0) == 0 || name.rfind("Scaleform.", 0) == 0;
  };
  for (const detail::Row& row : detail::kTypes) {
    if (row.cs != nullptr && shim_owned(row.cs)) {
      found.insert(std::string("type ") + row.cs);
    }
  }
  for (const detail::Row& row : detail::kGlobals) {
    if (shim_owned(row.cs)) {
      found.insert(std::string("global ") + row.cs);
    }
  }
  for (const char* type : {"int", "uint", "Number", "Boolean", "String"}) {
    found.insert(std::string("helper ") + detail::cs_cast_helper(type));
  }
  for (const detail::Row& row : detail::kMembers) {
    found.insert(std::string("member ") + row.cs);
  }
  return std::vector<std::string>(found.begin(), found.end());
}

}  // namespace fo4::abc

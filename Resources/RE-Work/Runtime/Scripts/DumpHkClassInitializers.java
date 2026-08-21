// Dumps every Havok class's field layout by reading the startup code that builds it.
//
// The hkClass objects are all zero in the file; they are filled in at load time by one
// _dynamic_initializer_for__<Name>Class__ per class, whose single call to hkClass::hkClass carries
// the name, parent, object size, member table and member count as arguments. So the class list is a
// function-name walk and the metadata comes out of that call, not out of the data blocks. A scan of
// the data blocks finds nothing, which is the trap this script exists to avoid.
//
// The member tables themselves are static and read straight off the file, at a stride of 0x28:
//   +0x00 name, +0x08 class, +0x10 enum, +0x18 type, +0x19 subtype,
//   +0x1a cArraySize, +0x1c flags, +0x1e offset, +0x20 attributes
// confirmed against hkbVariableBounds, whose two members are known to sit at 0 and 4.
//
// Output is flushed per class, so a run that dies partway still leaves everything it had reached.
//
// Usage: -postScript DumpHkClassInitializers.java <outputFile> [namePrefixFilter]
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolIterator;

import java.io.FileWriter;
import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

public class DumpHkClassInitializers extends GhidraScript {

    private static final String PREFIX = "_dynamic_initializer_for__";
    private static final int MEMBER_STRIDE = 0x28;

    private static final String[] TYPES = {
        "void", "bool", "char", "int8", "uint8", "int16", "uint16", "int32", "uint32",
        "int64", "uint64", "real", "vector4", "quaternion", "matrix3", "rotation",
        "qstransform", "matrix4", "transform", "zero", "pointer", "functionpointer",
        "array", "inplacearray", "enum", "struct", "simplearray", "homogeneousarray",
        "variant", "cstring", "ulong", "flags", "half", "stringptr", "relarray"
    };

    private static final Pattern CALL = Pattern.compile("hkClass::hkClass\\s*\\(");

    private PrintWriter out;
    private DecompInterface decompiler;

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 1) { println("need an output file"); return; }
        String filter = args.length > 1 ? args[1] : "";

        out = new PrintWriter(new FileWriter(args[0]), true);
        decompiler = new DecompInterface();
        decompiler.openProgram(currentProgram);

        List<Function> initializers = collect(filter);
        line("# Havok class field layouts, read from the startup initializers in "
             + currentProgram.getName());
        line("# " + initializers.size() + " initializers found"
             + (filter.isEmpty() ? "" : " matching '" + filter + "'"));
        line("#");
        line("# member line: +offset  name  type[ of subtype][ xN]");
        line("# hkbVariableBounds is the check: 8 bytes, min at +0 and max at +4.");
        line("");

        int done = 0, failed = 0, seen = 0;
        for (Function f : initializers) {
            if (monitor.isCancelled()) break;
            try {
                if (dump(f)) done++; else failed++;
            } catch (Exception e) {
                line("# " + f.getName() + ": " + e);
                failed++;
            }
            if (++seen % 100 == 0) println("... " + seen + " of " + initializers.size());
        }

        line("");
        line("# " + done + " classes read, " + failed + " initializers could not be parsed");
        decompiler.dispose();
        out.close();
    }

    private List<Function> collect(String filter) {
        List<Function> found = new ArrayList<>();
        FunctionIterator it = currentProgram.getFunctionManager().getFunctions(true);
        while (it.hasNext()) {
            Function f = it.next();
            String name = f.getName();
            if (!name.startsWith(PREFIX) || !name.endsWith("Class__")) continue;
            if (!filter.isEmpty() && !name.contains(filter)) continue;
            found.add(f);
        }
        found.sort((a, b) -> a.getName().compareTo(b.getName()));
        return found;
    }

    private boolean dump(Function f) throws Exception {
        DecompileResults res = decompiler.decompileFunction(f, 30, monitor);
        if (res == null || !res.decompileCompleted()) return false;

        String c = res.getDecompiledFunction().getC();
        Matcher m = CALL.matcher(c);
        if (!m.find()) return false;

        List<String> args = split(c, m.end());
        if (args.size() < 10) return false;

        String name = unquote(args.get(1));
        String parent = symbolName(args.get(2));
        long size = number(args.get(3));
        String membersSymbol = symbolName(args.get(8));
        long count = number(args.get(9));
        if (name == null) return false;

        line(String.format("class %s : %s  size=%d  members=%d",
                name, parent == null ? "-" : parent, size, count));

        if (count <= 0) return true;
        Address members = findSymbol(membersSymbol);
        if (members == null) {
            line("    (member table " + membersSymbol + " not found)");
            return true;
        }
        for (int i = 0; i < count; i++) member(members.add((long) i * MEMBER_STRIDE));
        return true;
    }

    private void member(Address at) throws Exception {
        String name = stringAt(getLong(at));
        int type = getByte(at.add(0x18)) & 0xFF;
        int subtype = getByte(at.add(0x19)) & 0xFF;
        int arraySize = getShort(at.add(0x1a));
        int offset = getShort(at.add(0x1e)) & 0xFFFF;

        line(String.format("    +%-5d %-44s %s%s%s",
                offset,
                name == null ? "?" : name,
                type < TYPES.length ? TYPES[type] : ("type" + type),
                subtype != 0 && subtype < TYPES.length ? " of " + TYPES[subtype] : "",
                arraySize != 0 ? " x" + arraySize : ""));
    }

    // The decompiler prints casts, so an argument arrives as something like
    // "(hkClassMember *)&hkbVariableBoundsClass_Members". Splitting has to respect nesting, because
    // a cast's own parentheses would otherwise end the argument early.
    private List<String> split(String text, int from) {
        List<String> args = new ArrayList<>();
        StringBuilder current = new StringBuilder();
        int depth = 0;
        boolean inString = false;

        for (int i = from; i < text.length(); i++) {
            char ch = text.charAt(i);
            if (inString) {
                current.append(ch);
                if (ch == '"' && text.charAt(i - 1) != '\\') inString = false;
                continue;
            }
            switch (ch) {
                case '"': inString = true; current.append(ch); break;
                case '(': case '[': depth++; current.append(ch); break;
                case ']': depth--; current.append(ch); break;
                case ')':
                    if (depth == 0) { args.add(current.toString().trim()); return args; }
                    depth--; current.append(ch); break;
                case ',':
                    if (depth == 0) { args.add(current.toString().trim()); current.setLength(0); }
                    else current.append(ch);
                    break;
                case '\n': current.append(' '); break;
                default: current.append(ch);
            }
        }
        return Collections.emptyList();
    }

    private String strip(String arg) {
        String s = arg.trim();
        while (s.startsWith("(")) {
            int close = s.indexOf(')');
            if (close < 0) break;
            s = s.substring(close + 1).trim();
        }
        return s;
    }

    private String unquote(String arg) {
        String s = strip(arg);
        if (s.startsWith("\"") && s.endsWith("\"") && s.length() > 1) {
            return s.substring(1, s.length() - 1);
        }
        return null;
    }

    private String symbolName(String arg) {
        String s = strip(arg);
        if (!s.startsWith("&")) return null;
        return s.substring(1).trim();
    }

    private long number(String arg) {
        String s = strip(arg);
        try {
            if (s.startsWith("0x")) return Long.parseLong(s.substring(2), 16);
            return Long.parseLong(s);
        } catch (NumberFormatException e) {
            return -1;
        }
    }

    // Member tables are named two ways, flat as hkbVariableBoundsClass_Members and namespaced as
    // hkbClipGenerator::Members. A lookup by the whole string only finds the flat ones, which
    // silently lost the member layout of a fifth of the classes, including the ones that matter most.
    private Address findSymbol(String name) {
        if (name == null) return null;

        String[] parts = name.split("::");
        if (parts.length == 1) return firstNonExternal(name, null);

        // Ghidra reports only the immediate parent, so hkbStateMachine::StateInfo::Members has to be
        // matched on "StateInfo" rather than on the whole path.
        return firstNonExternal(parts[parts.length - 1], parts[parts.length - 2]);
    }

    private Address firstNonExternal(String name, String namespace) {
        SymbolIterator it = currentProgram.getSymbolTable().getSymbols(name);
        while (it.hasNext()) {
            Symbol s = it.next();
            if (s.isExternal()) continue;
            if (namespace != null && !namespace.equals(s.getParentNamespace().getName())) continue;
            return s.getAddress();
        }
        return null;
    }

    private String stringAt(long value) {
        if (value == 0) return null;
        try {
            Address at = toAddr(value);
            if (!currentProgram.getMemory().contains(at)) return null;
            StringBuilder sb = new StringBuilder();
            for (int i = 0; i < 128; i++) {
                byte b = getByte(at.add(i));
                if (b == 0) return sb.length() == 0 ? null : sb.toString();
                if (b < 0x20 || b > 0x7e) return null;
                sb.append((char) b);
            }
        } catch (Exception ignored) {
        }
        return null;
    }

    private void line(String text) {
        out.println(text);
    }
}

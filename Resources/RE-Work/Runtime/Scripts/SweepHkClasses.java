// Dumps every Havok class description in the binary: name, parent, object size, and every member's
// name, type and byte offset.
//
// The class objects themselves are all zeroes on disk, because they are built at startup, so the
// only place the description exists statically is in the arguments of the constructor call inside
// each _dynamic_initializer_for__<Class>Class__ function. The arguments are read from the printed
// C: the pointer ones arrive in registers loaded by a LEA, so the pcode varnode holds a register and
// not the address, while the decompiler has already folded that and prints the symbol.
//
// Member tables ARE static and read straight from memory, with the layout confirmed against
// hkbVariableBounds, whose two members are known to sit at offsets 0 and 4.
//
// Anything that does not resolve is printed as UNRESOLVED with the reason, so partial coverage is
// visible rather than silently looking complete.
//
// Usage: -postScript SweepHkClasses.java <outputFile>
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;

import java.io.PrintWriter;
import java.util.Iterator;

public class SweepHkClasses extends GhidraScript {

    private static final int MEMBER_STRIDE = 0x28;
    private static final int MEMBER_NAME = 0x00;
    private static final int MEMBER_TYPE = 0x18;
    private static final int MEMBER_SUBTYPE = 0x19;
    private static final int MEMBER_ARRAY_SIZE = 0x1a;
    private static final int MEMBER_FLAGS = 0x1c;
    private static final int MEMBER_OFFSET = 0x1e;

    // hkClass::hkClass(this, name, parent, objectSize, interfaces, numInterfaces, enums, numEnums,
    //                  members, numMembers, defaults, attributes, flags, describedVersion)
    // Numbered from one including the leading this, so each is used as ARG_X - 1 into the list.
    private static final int ARG_NAME = 2;
    private static final int ARG_PARENT = 3;
    private static final int ARG_OBJECT_SIZE = 4;
    private static final int ARG_MEMBERS = 9;
    private static final int ARG_NUM_MEMBERS = 10;

    private static final String[] TYPES = {
        "void", "bool", "char", "int8", "uint8", "int16", "uint16", "int32", "uint32",
        "int64", "uint64", "real", "vector4", "quaternion", "matrix3", "rotation",
        "qstransform", "matrix4", "transform", "zero", "pointer", "functionpointer",
        "array", "inplacearray", "enum", "struct", "simplearray", "homogeneousarray",
        "variant", "cstring", "ulong", "flags", "half", "stringptr", "relarray"
    };

    private int resolved = 0;
    private int unresolved = 0;

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        PrintWriter out = new PrintWriter(args[0]);

        DecompInterface decompiler = new DecompInterface();
        decompiler.openProgram(currentProgram);

        out.println("# Havok class descriptions from " + currentProgram.getName());
        out.println("# Read from the constructor arguments in each _dynamic_initializer_for__*Class__.");
        out.println("# Member layout confirmed against hkbVariableBounds: min at +0, max at +4.");
        out.println();

        FunctionIterator functions = currentProgram.getFunctionManager().getFunctions(true);
        while (functions.hasNext() && !monitor.isCancelled()) {
            Function f = functions.next();
            String name = f.getName();
            if (!name.startsWith("_dynamic_initializer_for__") || !name.contains("Class__")) continue;
            sweep(out, decompiler, f, name);
            out.flush();
        }

        decompiler.dispose();
        out.println();
        out.println("# " + resolved + " classes resolved, " + unresolved + " unresolved");
        out.close();
        println(resolved + " classes resolved, " + unresolved + " unresolved");
    }

    private void sweep(PrintWriter out, DecompInterface decompiler, Function f, String symbol) {
        String shortName = symbol.replace("_dynamic_initializer_for__", "").replaceAll("__$", "");
        try {
            DecompileResults res = decompiler.decompileFunction(f, 30, monitor);
            if (res == null || !res.decompileCompleted()) {
                fail(out, shortName, "decompile failed");
                return;
            }

            // Read the arguments from the printed call rather than from pcode. The pointer
            // arguments arrive as registers loaded by a LEA, so the pcode varnode is not the
            // address; the decompiler has already folded that and prints the symbol.
            String c = res.getDecompiledFunction().getC().replaceAll("\\s+", " ");
            java.util.regex.Matcher m = CONSTRUCTOR.matcher(c);
            if (!m.find()) { fail(out, shortName, "no hkClass constructor call in the decompile"); return; }

            String[] argv = splitArgs(m.group(1));
            if (argv.length <= ARG_NUM_MEMBERS - 1) {
                fail(out, shortName, "constructor call has only " + argv.length + " arguments");
                return;
            }

            String className = quoted(argv[ARG_NAME - 1]);
            if (className == null) { fail(out, shortName, "class name is not a literal string"); return; }

            Long objectSize = number(argv[ARG_OBJECT_SIZE - 1]);
            Long numMembers = number(argv[ARG_NUM_MEMBERS - 1]);
            if (objectSize == null || numMembers == null) {
                fail(out, className, "object size or member count is not a constant");
                return;
            }

            String parent = symbolName(argv[ARG_PARENT - 1]);
            if (parent != null) parent = parent.replaceAll("Class$", "");
            Address members = symbolAddress(argv[ARG_MEMBERS - 1]);

            out.println(String.format("class %s : %s  size=%d  members=%d",
                    className, parent == null ? "-" : parent, objectSize, numMembers));
            resolved++;

            if (numMembers == 0) return;
            if (members == null) {
                out.println("    UNRESOLVED member table pointer");
                return;
            }
            for (int i = 0; i < numMembers; i++) dumpMember(out, members.add((long) i * MEMBER_STRIDE));
        } catch (Exception e) {
            fail(out, shortName, e.toString());
        }
    }

    private void dumpMember(PrintWriter out, Address m) {
        try {
            String name = stringAt(pointer(m.add(MEMBER_NAME)));
            int type = getByte(m.add(MEMBER_TYPE)) & 0xFF;
            int subtype = getByte(m.add(MEMBER_SUBTYPE)) & 0xFF;
            int arraySize = getShort(m.add(MEMBER_ARRAY_SIZE));
            int offset = getShort(m.add(MEMBER_OFFSET)) & 0xFFFF;

            out.println(String.format("    +%-5d %-44s %-16s%s%s",
                    offset,
                    name == null ? "(unnamed)" : name,
                    type < TYPES.length ? TYPES[type] : "type" + type,
                    subtype != 0 && subtype < TYPES.length ? " of " + TYPES[subtype] : "",
                    arraySize != 0 ? " [" + arraySize + "]" : ""));
        } catch (Exception e) {
            out.println("    UNRESOLVED member: " + e);
        }
    }

    private void fail(PrintWriter out, String name, String why) {
        out.println("UNRESOLVED " + name + ": " + why);
        unresolved++;
    }

    private static final java.util.regex.Pattern CONSTRUCTOR =
        java.util.regex.Pattern.compile("hkClass::hkClass\\s*\\((.*?)\\)\\s*;");

    /// Splits an argument list on the commas that are actually separators, so a cast or a string
    /// containing one does not break the split.
    private String[] splitArgs(String text) {
        java.util.List<String> parts = new java.util.ArrayList<>();
        int depth = 0;
        boolean inString = false;
        StringBuilder current = new StringBuilder();

        for (int i = 0; i < text.length(); i++) {
            char ch = text.charAt(i);
            if (inString) {
                current.append(ch);
                if (ch == '"' && text.charAt(i - 1) != '\\') inString = false;
                continue;
            }
            switch (ch) {
                case '"': inString = true; current.append(ch); break;
                case '(': case '<': depth++; current.append(ch); break;
                case ')': case '>': depth--; current.append(ch); break;
                case ',':
                    if (depth == 0) { parts.add(current.toString().trim()); current.setLength(0); }
                    else current.append(ch);
                    break;
                default: current.append(ch);
            }
        }
        if (current.length() > 0) parts.add(current.toString().trim());
        return parts.toArray(new String[0]);
    }

    private String quoted(String arg) {
        int open = arg.indexOf('"');
        int close = arg.lastIndexOf('"');
        return open >= 0 && close > open ? arg.substring(open + 1, close) : null;
    }

    /// The last token of an argument, once any cast and address-of are stripped.
    private String bare(String arg) {
        String s = arg.replaceAll("\\([^)]*\\)", "").trim();
        while (s.startsWith("&") || s.startsWith("*")) s = s.substring(1).trim();
        return s;
    }

    private Long number(String arg) {
        String s = bare(arg);
        try {
            if (s.startsWith("0x") || s.startsWith("0X")) return Long.parseLong(s.substring(2), 16);
            return Long.parseLong(s);
        } catch (NumberFormatException e) {
            return null;
        }
    }

    private String symbolName(String arg) {
        String s = bare(arg);
        if (s.isEmpty() || s.equals("0") || s.startsWith("0x")) return null;
        return s;
    }

    private Address symbolAddress(String arg) {
        String name = symbolName(arg);
        if (name == null) return null;

        var it = currentProgram.getSymbolTable().getSymbols(name);
        while (it.hasNext()) {
            var s = it.next();
            if (!s.isExternal()) return s.getAddress();
        }
        // Ghidra prints some member tables as Class::Members; the symbol is the last component.
        int scope = name.lastIndexOf("::");
        if (scope < 0) return null;

        it = currentProgram.getSymbolTable().getSymbols(name.substring(scope + 2));
        while (it.hasNext()) {
            var s = it.next();
            if (!s.isExternal() && s.getParentNamespace() != null
                    && name.startsWith(s.getParentNamespace().getName())) {
                return s.getAddress();
            }
        }
        return null;
    }

    private Address pointer(Address at) throws Exception {
        if (at == null) return null;
        long value = getLong(at);
        if (value == 0) return null;
        Address target = toAddr(value);
        return currentProgram.getMemory().contains(target) ? target : null;
    }

    private String stringAt(Address at) {
        if (at == null) return null;
        try {
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
}

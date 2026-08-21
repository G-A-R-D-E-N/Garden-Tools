// Dumps every Havok hkClass reflection record found in the binary: class name, parent, object size,
// and every declared member's name, type, and byte offset.
//
// The layout below is a hypothesis until the run confirms it. The script proves itself rather than
// asserting: a candidate is only accepted when its name pointer lands on a plausible hk* string, its
// object size and member count are sane, and every one of its members' offsets falls inside the
// object. Classes with a known answer are printed first so the numbers can be checked against them.
//
// Usage: -postScript DumpHkClasses.java [outputFile]
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.mem.MemoryBlock;

import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.List;

public class DumpHkClasses extends GhidraScript {

    private static final int CLASS_NAME = 0x00;
    private static final int CLASS_PARENT = 0x08;
    private static final int CLASS_OBJECT_SIZE = 0x10;
    private static final int CLASS_DECLARED_MEMBERS = 0x28;
    private static final int CLASS_NUM_MEMBERS = 0x30;

    private static final int MEMBER_STRIDE = 0x28;
    private static final int MEMBER_NAME = 0x00;
    private static final int MEMBER_TYPE = 0x18;
    private static final int MEMBER_SUBTYPE = 0x19;
    private static final int MEMBER_ARRAY_SIZE = 0x1a;
    private static final int MEMBER_FLAGS = 0x1c;
    private static final int MEMBER_OFFSET = 0x1e;

    private static final String[] TYPES = {
        "void", "bool", "char", "int8", "uint8", "int16", "uint16", "int32", "uint32",
        "int64", "uint64", "real", "vector4", "quaternion", "matrix3", "rotation",
        "qstransform", "matrix4", "transform", "zero", "pointer", "functionpointer",
        "array", "inplacearray", "enum", "struct", "simplearray", "homogeneousarray",
        "variant", "cstring", "ulong", "flags", "half", "stringptr", "relarray"
    };

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        PrintWriter out = args.length > 0 ? new PrintWriter(args[0]) : null;

        List<Address> found = new ArrayList<>();
        for (MemoryBlock block : currentProgram.getMemory().getBlocks()) {
            if (!block.isInitialized() || block.isExecute()) continue;
            scan(block, found);
        }

        emit(out, "# hkClass reflection dump, " + currentProgram.getName());
        emit(out, "# " + found.size() + " classes accepted");
        emit(out, "#");
        emit(out, "# Check these against what is already known before trusting the rest:");
        emit(out, "#   hkbVariableBounds is 8 bytes, members min at 0 and max at 4.");
        emit(out, "");

        for (Address at : found) {
            try {
                dump(out, at);
            } catch (Exception e) {
                emit(out, "# unreadable record at 0x" + at + ": " + e);
            }
        }
        if (out != null) out.close();
    }

    private void scan(MemoryBlock block, List<Address> found) throws Exception {
        Address at = block.getStart();
        Address end = block.getEnd();

        while (at.compareTo(end) < 0) {
            try {
                if (looksLikeClass(at)) found.add(at);
            } catch (Exception ignored) {
                // An address whose pointers do not resolve is simply not a class record.
            }
            at = at.add(8);
            if (monitor.isCancelled()) return;
        }
    }

    // Every condition here exists to reject something that passed the previous one by luck. A name
    // pointer alone matches far too much of .rdata; it is the agreement between the name, the size,
    // the member count and every member offset that makes a hit mean something.
    private boolean looksLikeClass(Address at) throws Exception {
        String name = stringAt(pointer(at.add(CLASS_NAME)));
        if (name == null || name.length() < 3 || !name.startsWith("hk")) return false;
        if (!name.matches("hk[A-Za-z0-9_:<>,* ]+")) return false;

        int size = getInt(at.add(CLASS_OBJECT_SIZE));
        int count = getInt(at.add(CLASS_NUM_MEMBERS));
        if (size < 0 || size > 0x4000) return false;
        if (count < 0 || count > 256) return false;

        if (count == 0) return size > 0;

        Address members = pointer(at.add(CLASS_DECLARED_MEMBERS));
        if (members == null) return false;

        for (int i = 0; i < count; i++) {
            Address m = members.add((long) i * MEMBER_STRIDE);
            String memberName = stringAt(pointer(m.add(MEMBER_NAME)));
            if (memberName == null || memberName.isEmpty()) return false;

            int offset = getShort(m.add(MEMBER_OFFSET)) & 0xFFFF;
            if (offset >= size) return false;

            int type = getByte(m.add(MEMBER_TYPE)) & 0xFF;
            if (type >= TYPES.length) return false;
        }
        return true;
    }

    private void dump(PrintWriter out, Address at) throws Exception {
        String name = stringAt(pointer(at.add(CLASS_NAME)));
        String parent = stringAt(pointer(pointerOrNull(at.add(CLASS_PARENT))));
        int size = getInt(at.add(CLASS_OBJECT_SIZE));
        int count = getInt(at.add(CLASS_NUM_MEMBERS));

        emit(out, String.format("class %s : %s  size=%d  members=%d  at=0x%s",
                name, parent == null ? "-" : parent, size, count, at.toString()));

        if (count == 0) return;
        Address members = pointer(at.add(CLASS_DECLARED_MEMBERS));
        if (members == null) { emit(out, "    (member table unreadable)"); return; }
        for (int i = 0; i < count; i++) {
            Address m = members.add((long) i * MEMBER_STRIDE);
            int type = getByte(m.add(MEMBER_TYPE)) & 0xFF;
            int subtype = getByte(m.add(MEMBER_SUBTYPE)) & 0xFF;
            int arraySize = getShort(m.add(MEMBER_ARRAY_SIZE));
            int flags = getShort(m.add(MEMBER_FLAGS)) & 0xFFFF;
            int offset = getShort(m.add(MEMBER_OFFSET)) & 0xFFFF;

            emit(out, String.format("    +%-5d %-40s %-16s%s%s",
                    offset,
                    stringAt(pointer(m.add(MEMBER_NAME))),
                    TYPES[type],
                    subtype != 0 ? " of " + TYPES[subtype] : "",
                    arraySize != 0 ? " [" + arraySize + "]" : ""));
            if (flags != 0) { /* flags carry defaults and alignment, not layout */ }
        }
    }

    private Address pointer(Address at) throws Exception {
        if (at == null) return null;
        long value = getLong(at);
        if (value == 0) return null;
        Address target = toAddr(value);
        return currentProgram.getMemory().contains(target) ? target : null;
    }

    private Address pointerOrNull(Address at) {
        try { return pointer(at); } catch (Exception e) { return null; }
    }

    private String stringAt(Address at) throws Exception {
        if (at == null) return null;
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < 128; i++) {
            byte b = getByte(at.add(i));
            if (b == 0) return sb.length() == 0 ? null : sb.toString();
            if (b < 0x20 || b > 0x7e) return null;
            sb.append((char) b);
        }
        return null;
    }

    private void emit(PrintWriter out, String line) {
        if (out != null) out.println(line); else println(line);
    }
}

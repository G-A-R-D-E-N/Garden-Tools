// Prints the raw bytes behind named data symbols, as bytes and as 8-byte words with any word that
// resolves to a string shown as that string. Used to derive a struct layout from a record whose
// contents are already known, rather than assuming the layout and hoping.
//
// Usage: -postScript DumpSymbolBytes.java <symbolName> <byteCount> [<symbolName> <byteCount> ...]
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolIterator;

public class DumpSymbolBytes extends GhidraScript {

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        for (int i = 0; i + 1 < args.length; i += 2) {
            dump(args[i], Integer.parseInt(args[i + 1]));
        }
    }

    private void dump(String name, int count) throws Exception {
        Address at = findSymbol(name);
        if (at == null) { println("### " + name + ": no symbol"); return; }
        println("### " + name + " @ " + at + " (" + count + " bytes)");

        for (int off = 0; off + 8 <= count; off += 8) {
            long word = getLong(at.add(off));
            String text = stringAt(word);
            println(String.format("  +0x%02x  %016x  %-12d%s",
                    off, word, (int) word, text == null ? "" : "  -> \"" + text + "\""));
        }
    }

    private Address findSymbol(String name) {
        SymbolIterator it = currentProgram.getSymbolTable().getSymbols(name);
        while (it.hasNext()) {
            Symbol s = it.next();
            if (!s.isExternal()) return s.getAddress();
        }
        return null;
    }

    private String stringAt(long value) {
        if (value == 0) return null;
        try {
            Address at = toAddr(value);
            if (!currentProgram.getMemory().contains(at)) return null;
            StringBuilder sb = new StringBuilder();
            for (int i = 0; i < 64; i++) {
                byte b = getByte(at.add(i));
                if (b == 0) return sb.length() < 2 ? null : sb.toString();
                if (b < 0x20 || b > 0x7e) return null;
                sb.append((char) b);
            }
        } catch (Exception ignored) {
        }
        return null;
    }
}

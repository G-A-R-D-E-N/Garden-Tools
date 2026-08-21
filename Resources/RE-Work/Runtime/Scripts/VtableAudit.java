// Audit the 163 project's VTABLE_<Class> slot names against CommonLibF4's OWN authoritative
// vtable order (parsed from the header // NN slot comments into commonlib_vtable_slots.csv:
// class,slot,expected_method). For each class, walk VTABLE_<Class>, and for each slot the header
// names, compare the function's short name (case-insensitive) to the header's expected method.
// Reports every mismatch -- these are places the doodlum RTTI-matcher mis-applied CommonLibF4's
// names into the Ghidra DB (e.g. the 4-slot hknpShape shift). Output: audit report CSV +
// a machine-readable fix CSV (address,expected_qualified_name) for the confirmed mismatches.
//
// Usage: -postScript VtableAudit.java <slotsCsv> <reportCsv> <fixCsv>
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolIterator;
import ghidra.program.model.symbol.SymbolTable;

import java.io.BufferedReader;
import java.io.FileReader;
import java.io.PrintWriter;
import java.util.*;

public class VtableAudit extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] a = getScriptArgs();
        String slotsCsv = a.length > 0 ? a[0] : "E:\\F4SE OG\\ReverseEngineering\\Organized\\commonlib_vtable_slots.csv";
        String reportCsv = a.length > 1 ? a[1] : "E:\\F4SE OG\\ReverseEngineering\\Organized\\vtable_audit_report.csv";
        String fixCsv = a.length > 2 ? a[2] : "E:\\F4SE OG\\ReverseEngineering\\Organized\\vtable_audit_fixes.csv";

        // class -> (slot -> expectedMethod)
        Map<String, TreeMap<Integer, String>> want = new LinkedHashMap<>();
        try (BufferedReader r = new BufferedReader(new FileReader(slotsCsv))) {
            r.readLine();
            String line;
            while ((line = r.readLine()) != null) {
                String[] f = line.split(",", -1);
                if (f.length < 3) continue;
                want.computeIfAbsent(f[0].trim(), k -> new TreeMap<>())
                    .put(Integer.parseInt(f[1].trim()), f[2].trim());
            }
        }

        SymbolTable st = currentProgram.getSymbolTable();
        Memory mem = currentProgram.getMemory();
        Address imageBase = currentProgram.getImageBase();

        // Pre-index VTABLE_ symbols by exact class name for fast lookup.
        Map<String, Address> vtByClass = new HashMap<>();
        SymbolIterator si = st.getAllSymbols(true);
        while (si.hasNext()) {
            Symbol s = si.next();
            String n = s.getName();
            if (n.startsWith("VTABLE_")) {
                String cls = n.substring("VTABLE_".length());
                vtByClass.putIfAbsent(cls, s.getAddress());
            }
        }

        PrintWriter rep = new PrintWriter(new java.io.FileWriter(reportCsv));
        rep.println("class,slot,expected_method,actual_name,verdict,address");
        PrintWriter fix = new PrintWriter(new java.io.FileWriter(fixCsv));
        fix.println("address,expected_qualified_name");

        int classesChecked = 0, classesNoVtable = 0, slotsChecked = 0, match = 0, mismatch = 0, shiftClasses = 0;

        for (Map.Entry<String, TreeMap<Integer, String>> e : want.entrySet()) {
            String cls = e.getKey();
            Address vaddr = vtByClass.get(cls);
            if (vaddr == null) { classesNoVtable++; continue; }
            classesChecked++;
            int clsMismatch = 0;
            List<String[]> clsRows = new ArrayList<>();  // slot,expected,actual,verdict,addr
            for (Map.Entry<Integer, String> se : e.getValue().entrySet()) {
                int slot = se.getKey();
                String expected = se.getValue();
                Address slotAddr = vaddr.add((long) slot * 8);
                long ptr;
                try { ptr = mem.getLong(slotAddr); } catch (Exception ex) { continue; }
                if (ptr == 0) continue;
                Address target = imageBase.getNewAddress(ptr);
                Function f = getFunctionAt(target);
                if (f == null) continue;
                slotsChecked++;
                String actualShort = f.getName();  // short name
                String verdict;
                if (actualShort.equalsIgnoreCase(expected)) { verdict = "match"; match++; }
                else { verdict = "MISMATCH"; mismatch++; clsMismatch++; }
                String rvaHex = "0x" + Long.toHexString(target.getOffset());
                clsRows.add(new String[]{ Integer.toString(slot), expected, f.getName(true), verdict, rvaHex });
            }
            // Only emit rows for classes that HAVE at least one mismatch (keep report focused).
            if (clsMismatch > 0) {
                shiftClasses++;
                for (String[] row : clsRows) {
                    rep.println(cls + "," + row[0] + "," + row[1] + ",\"" + row[2] + "\"," + row[3] + "," + row[4]);
                    if (row[3].equals("MISMATCH")) {
                        fix.println(row[4] + "," + cls + "::" + row[1]);
                    }
                }
            }
        }
        rep.close();
        fix.close();
        println("=== VTABLE AUDIT ===");
        println("  classes with header slots     : " + want.size());
        println("  classes found in DB (VTABLE_) : " + classesChecked);
        println("  classes with NO VTABLE_ symbol: " + classesNoVtable);
        println("  slots checked                 : " + slotsChecked);
        println("  slot matches                  : " + match);
        println("  slot MISMATCHES               : " + mismatch);
        println("  classes containing a mismatch : " + shiftClasses);
        println("  report: " + reportCsv);
        println("  fixes : " + fixCsv);
    }
}

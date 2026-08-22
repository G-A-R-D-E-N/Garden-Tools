// Apply the community-sourced, version-exact 1.10.163 label CSV
// (E:\F4SE OG\ReverseEngineering\Symbols\F4_1_10_163_labels_decoded.csv, format "0xADDR,name" per line - addresses
// are already absolute VAs, e.g. base 0x140000000 + RVA) onto this program.
//
// Precedence rule (per task spec): a "real" existing name (anything not the Ghidra-default
// FUN_/sub_/DAT_/LAB_/etc. prefix - i.e. the CommonLibF4 RTTI naming pass's ~14k names) is NEVER
// silently overwritten. If the CSV's name for that address disagrees with the existing real name,
// the disagreement is logged (address, old name, new name) for manual review - it may indicate a
// vtable-slot mismatch in the RTTI matcher. Only addresses that are currently default-named get the
// CSV name applied.
//
// Uses Ghidra's own demangler (DemanglerUtil/DemangledObject.applyTo) for MSVC-mangled names so
// functions get proper namespace + signature, exactly like PDB import does - not just a bare label.
// Falls back to a plain sanitized label/rename when the string isn't demanglable (a handful of the
// CSV's names are already plain, e.g. "_tls_end").
//
// Entire run executes inside GhidraScript's single ambient transaction (headless auto-wraps the
// whole script execution in one transaction) - no manual per-symbol commits, for performance across
// ~698k rows.
import ghidra.app.script.GhidraScript;
import ghidra.app.util.demangler.DemangledObject;
import ghidra.app.util.demangler.DemanglerOptions;
import ghidra.app.util.demangler.DemanglerUtil;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.symbol.SourceType;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolTable;

import java.io.BufferedReader;
import java.io.FileReader;
import java.io.PrintWriter;
import java.util.List;

public class ApplyDecodedLabels extends GhidraScript {

    private static boolean isDefaultName(String n) {
        return n == null || n.isEmpty()
            || n.startsWith("FUN_") || n.startsWith("sub_") || n.startsWith("DAT_")
            || n.startsWith("s_") || n.startsWith("u_") || n.startsWith("ptr_") || n.startsWith("PTR_")
            || n.startsWith("off_") || n.startsWith("ARRAY_") || n.startsWith("STRUCT_")
            || n.startsWith("LAB_") || n.startsWith("switchdata") || n.startsWith("caseD_")
            || n.startsWith("AddrTable") || n.startsWith("JTable");
    }

    // Best-effort sanitize of a raw (non-demanglable) name into a legal Ghidra symbol name.
    private static String sanitize(String raw) {
        String s = raw.replaceAll("[^A-Za-z0-9_:<>~,\\.\\$]", "_");
        if (s.length() > 2000) s = s.substring(0, 2000);
        return s;
    }

    @Override
    public void run() throws Exception {
        String[] scriptArgs = getScriptArgs();
        String csvPath = scriptArgs.length > 0 ? scriptArgs[0]
            : "E:\\F4SE OG\\ReverseEngineering\\Symbols\\F4_1_10_163_labels_decoded.csv";
        String reportPath = scriptArgs.length > 1 ? scriptArgs[1]
            : "E:\\F4SE OG\\ReverseEngineering\\Projects\\fwgh_proj_1.10.163_unpacked\\label_apply_disagreements.csv";

        FunctionManager fm = currentProgram.getFunctionManager();
        SymbolTable st = currentProgram.getSymbolTable();
        DemanglerOptions demOpts = new DemanglerOptions();
        demOpts.setApplySignature(true);

        long total = 0, applied = 0, appliedDemangled = 0, appliedPlain = 0;
        long alreadyMatchingOrSkippedSame = 0, disagreements = 0, outOfRange = 0, errors = 0;

        PrintWriter report = new PrintWriter(new java.io.FileWriter(reportPath));
        report.println("address,existing_name,decoded_name,note");

        try (BufferedReader br = new BufferedReader(new FileReader(csvPath))) {
            String line;
            while ((line = br.readLine()) != null) {
                if (monitor.isCancelled()) break;
                total++;
                if (total % 50000 == 0) {
                    println("... processed " + total + " (applied=" + applied + ", disagreements="
                        + disagreements + ")");
                }
                int comma = line.indexOf(',');
                if (comma < 0) continue;
                String addrStr = line.substring(0, comma).trim();
                String rawName = line.substring(comma + 1).trim();
                if (rawName.isEmpty()) continue;

                long addrVal;
                try {
                    addrVal = Long.parseUnsignedLong(addrStr.replace("0x", "").replace("0X", ""), 16);
                } catch (NumberFormatException e) {
                    errors++;
                    continue;
                }

                Address addr;
                try {
                    addr = toAddr(addrVal);
                } catch (Exception e) {
                    outOfRange++;
                    continue;
                }
                if (addr == null || !currentProgram.getMemory().contains(addr)) {
                    outOfRange++;
                    continue;
                }

                // Try to demangle - gives proper namespace+signature for functions, proper
                // type+namespace for data (DemangledVariable), when the string is MSVC-mangled.
                DemangledObject demangled = null;
                try {
                    demangled = DemanglerUtil.demangle(currentProgram, rawName);
                } catch (Exception e) {
                    demangled = null;
                }
                String decodedDisplayName = demangled != null ? demangled.toString() : rawName;

                Function fn = fm.getFunctionAt(addr);
                if (fn != null) {
                    String existing = fn.getName(true);
                    if (isDefaultName(fn.getName())) {
                        // unnamed -> apply
                        boolean ok = false;
                        if (demangled != null) {
                            try {
                                ok = demangled.applyTo(currentProgram, addr, demOpts, monitor);
                            } catch (Exception e) {
                                ok = false;
                            }
                        }
                        if (!ok) {
                            try {
                                fn.setName(sanitize(rawName), SourceType.USER_DEFINED);
                                ok = true;
                                appliedPlain++;
                            } catch (Exception e) {
                                errors++;
                            }
                        } else {
                            appliedDemangled++;
                        }
                        if (ok) applied++;
                    } else {
                        // already has a real name - compare, log disagreement, never overwrite
                        if (existing.equalsIgnoreCase(decodedDisplayName)
                                || existing.equalsIgnoreCase(rawName)) {
                            alreadyMatchingOrSkippedSame++;
                        } else {
                            disagreements++;
                            report.println(addrStr + ",\"" + existing + "\",\"" + decodedDisplayName
                                + "\",function-name-conflict");
                        }
                    }
                    continue;
                }

                // No function here - check defined data, else fall back to a plain label.
                Data data = currentProgram.getListing().getDataAt(addr);
                Symbol primary = st.getPrimarySymbol(addr);
                String existingSymName = primary != null ? primary.getName(true) : null;

                if (existingSymName != null && !isDefaultName(primary.getName())) {
                    if (existingSymName.equalsIgnoreCase(decodedDisplayName)
                            || existingSymName.equalsIgnoreCase(rawName)) {
                        alreadyMatchingOrSkippedSame++;
                    } else {
                        disagreements++;
                        report.println(addrStr + ",\"" + existingSymName + "\",\"" + decodedDisplayName
                            + "\",data-or-label-name-conflict");
                    }
                    continue;
                }

                // unnamed data/label address -> apply
                boolean ok = false;
                if (demangled != null) {
                    try {
                        ok = demangled.applyTo(currentProgram, addr, demOpts, monitor);
                    } catch (Exception e) {
                        ok = false;
                    }
                    if (ok) appliedDemangled++;
                }
                if (!ok) {
                    try {
                        st.createLabel(addr, sanitize(rawName), SourceType.USER_DEFINED);
                        ok = true;
                        appliedPlain++;
                    } catch (Exception e) {
                        errors++;
                    }
                }
                if (ok) applied++;
            }
        }
        report.close();

        println("=== APPLY DECODED LABELS RESULT ===");
        println("  csv rows processed         : " + total);
        println("  applied (demangled)        : " + appliedDemangled);
        println("  applied (plain fallback)   : " + appliedPlain);
        println("  applied (total)            : " + applied);
        println("  already matching / same    : " + alreadyMatchingOrSkippedSame);
        println("  disagreements (logged)     : " + disagreements);
        println("  out of range / no memory   : " + outOfRange);
        println("  parse/apply errors         : " + errors);
        println("  disagreement report        : " + reportPath);
    }
}

// Apply the 150 decompile-VERIFIED CommonLib misname corrections from
// ReverseEngineering\Organized\confirmed_misnames_to_apply.csv (07-DECOMPILE-CANDIDATES.md) onto
// the 1.10.163 project. Unlike ApplyDecodedLabels.java's conservative "never overwrite a real
// name" rule (appropriate for 698k UNVERIFIED rows), these 150 addresses were individually
// decompile-verified by independent review against the actual C body -- so this script
// deliberately DOES overwrite the existing (confirmed-wrong) name.
//
// For each row: look up the address's ORIGINAL mangled name from F4_1_10_163_labels_decoded.csv
// (same source ApplyDecodedLabels.java uses) and demangle it via Ghidra's own demangler, exactly
// like symbol import does -- this applies the full namespace+signature, not just a bare rename.
// Falls back to a plain rename using the qualified name extracted from the CSV's already-demangled
// "claimed_real_name" column if the mangled lookup/demangle fails for any reason.
//
// Entire run executes inside GhidraScript's single ambient transaction.
//
// Usage: -postScript ApplyConfirmedMisnames.java [confirmedCsvPath] [labelsCsvPath] [reportPath]
import ghidra.app.script.GhidraScript;
import ghidra.app.util.demangler.DemangledObject;
import ghidra.app.util.demangler.DemanglerOptions;
import ghidra.app.util.demangler.DemanglerUtil;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.symbol.SourceType;

import java.io.BufferedReader;
import java.io.FileReader;
import java.io.PrintWriter;
import java.util.HashMap;
import java.util.Map;

public class ApplyConfirmedMisnames extends GhidraScript {

    // Same minimal RFC4180-ish CSV splitter as DecompileCandidates.java.
    private static String[] splitCsvLine(String line) {
        java.util.List<String> fields = new java.util.ArrayList<>();
        StringBuilder cur = new StringBuilder();
        boolean inQuotes = false;
        for (int i = 0; i < line.length(); i++) {
            char c = line.charAt(i);
            if (inQuotes) {
                if (c == '"') {
                    if (i + 1 < line.length() && line.charAt(i + 1) == '"') { cur.append('"'); i++; }
                    else inQuotes = false;
                } else cur.append(c);
            } else {
                if (c == '"') inQuotes = true;
                else if (c == ',') { fields.add(cur.toString()); cur.setLength(0); }
                else cur.append(c);
            }
        }
        fields.add(cur.toString());
        return fields.toArray(new String[0]);
    }

    // Extract "Class::Method" from a demangled declaration string like
    // "public virtual void __thiscall MagicItem::Copy(class TESForm * __ptr64) __ptr64 " by
    // finding the calling-convention keyword, then walking to the first top-level '(' (tracking
    // <...> depth so template args don't confuse it) -- same technique as
    // organize_functions.py's extract_name_from_155_line.
    private static String extractQualifiedName(String decl) {
        String[] ccs = {"__thiscall", "__cdecl", "__stdcall", "__fastcall", "__vectorcall"};
        int start = -1;
        for (String cc : ccs) {
            int idx = decl.indexOf(cc);
            if (idx >= 0) { start = idx + cc.length(); break; }
        }
        String text = start >= 0 ? decl.substring(start).trim() : decl.trim();
        int depth = 0;
        for (int i = 0; i < text.length(); i++) {
            char c = text.charAt(i);
            if (c == '<') depth++;
            else if (c == '>') depth = Math.max(0, depth - 1);
            else if (c == '(' && depth == 0) return text.substring(0, i).trim();
        }
        return text.trim();
    }

    private static String shortNameOf(String qualifiedName) {
        return qualifiedName.contains("::") ? qualifiedName.substring(qualifiedName.lastIndexOf("::") + 2) : qualifiedName;
    }

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        String confirmedCsvPath = (args.length > 0 && !args[0].isEmpty())
            ? args[0] : "E:\\F4SE OG\\ReverseEngineering\\Organized\\confirmed_misnames_to_apply.csv";
        String labelsCsvPath = (args.length > 1 && !args[1].isEmpty())
            ? args[1] : "E:\\F4SE OG\\ReverseEngineering\\Symbols\\F4_1_10_163_labels_decoded.csv";
        String reportPath = (args.length > 2 && !args[2].isEmpty())
            ? args[2] : "E:\\F4SE OG\\ReverseEngineering\\Organized\\applied_misname_corrections.csv";

        // Load address -> raw mangled name for the whole 698k-row labels CSV once (same source
        // ApplyDecodedLabels.java reads), so each of our 150 addresses can be re-demangled with
        // full signature/namespace fidelity instead of just a bare rename.
        println("Loading labels CSV for mangled-name lookup...");
        Map<Long, String> mangledByAddr = new HashMap<>();
        try (BufferedReader br = new BufferedReader(new FileReader(labelsCsvPath))) {
            String line;
            while ((line = br.readLine()) != null) {
                int comma = line.indexOf(',');
                if (comma < 0) continue;
                String addrStr = line.substring(0, comma).trim();
                String rawName = line.substring(comma + 1).trim();
                if (rawName.isEmpty()) continue;
                try {
                    long v = Long.parseUnsignedLong(addrStr.replace("0x", "").replace("0X", ""), 16);
                    mangledByAddr.put(v, rawName);
                } catch (Exception e) { /* skip */ }
            }
        }
        println("Loaded " + mangledByAddr.size() + " address->mangled-name entries.");

        FunctionManager fm = currentProgram.getFunctionManager();
        DemanglerOptions demOpts = new DemanglerOptions();
        demOpts.setApplySignature(true);

        PrintWriter report = new PrintWriter(new java.io.FileWriter(reportPath));
        report.println("address,old_name,new_name,method,note");

        int total = 0, appliedDemangled = 0, appliedPlain = 0, failed = 0, noFunc = 0;

        try (BufferedReader r = new BufferedReader(new FileReader(confirmedCsvPath))) {
            String header = r.readLine(); // address,old_name,claimed_real_name,evidence
            String line;
            while ((line = r.readLine()) != null) {
                if (line.isEmpty()) continue;
                String[] fields = splitCsvLine(line);
                if (fields.length < 3) continue;
                String addrStr = fields[0].trim();
                String oldName = fields[1];
                String claimedRealName = fields[2];
                total++;

                long addrVal;
                try {
                    addrVal = Long.parseUnsignedLong(addrStr.replace("0x", "").replace("0X", ""), 16);
                } catch (Exception e) {
                    report.println(addrStr + ",\"" + oldName + "\",,error,bad-address-format");
                    failed++;
                    continue;
                }

                Address addr = toAddr(addrVal);
                Function fn = fm.getFunctionContaining(addr);
                if (fn == null) {
                    report.println(addrStr + ",\"" + oldName + "\",,error,no-function-at-address");
                    noFunc++;
                    continue;
                }

                String currentName = fn.getName(true);
                // Idempotency: if a prior (partial/buggy) run of this script already renamed this
                // address to the expected FULL qualified target, treat it as already-done. MUST
                // compare the full "Class::Method" name, not just the trailing method name --
                // comparing only the method suffix was a real bug in an earlier version of this
                // script: 12 addresses whose OLD (wrong) class happened to share the same trailing
                // method name as the correct class (e.g. "ImageSpaceEffectGetHit::Render" vs the
                // correct "ImageSpaceEffect::Render" -- both end in "::Render") were wrongly
                // marked already-correct while the class-level rename had never actually happened.
                String expectedFullName = extractQualifiedName(claimedRealName);
                if (currentName.equals(expectedFullName)) {
                    report.println(addrStr + ",\"" + oldName + "\",\"" + currentName + "\",already-correct,skipped-idempotent");
                    continue;
                }
                // Safety check: only proceed if the function's CURRENT name still matches either
                // the original wrong name we verified against, or the expected corrected name with
                // a different namespace path Ghidra chose to display it under (still proceed to
                // force the exact target in that case) -- anything else means something unrelated
                // renamed this address since the decompile pass ran, so don't blindly overwrite it.
                if (!currentName.equals(oldName) && !currentName.endsWith("::" + shortNameOf(expectedFullName))) {
                    report.println(addrStr + ",\"" + oldName + "\",,skipped,current-name-changed-since-verification(now:" + currentName + ")");
                    failed++;
                    continue;
                }

                // Step 1 (best-effort, for signature/type fidelity): re-demangle the ORIGINAL
                // mangled community-label string and applyTo() -- this attaches full parameter
                // types etc. like symbol import does. NOTE: applyTo() respects Ghidra's symbol
                // source-type precedence and will SILENTLY SKIP the rename if the existing name
                // already has equal/higher priority (e.g. USER_DEFINED from the CommonLib RTTI
                // pass) -- confirmed empirically (a first run of this script "applied" 150/150 by
                // this path's return value, but only 12/150 actually changed fn.getName()). So
                // applyTo()'s return value is NOT trusted as proof of a rename; it's just used to
                // get the signature/type benefit when it works.
                String mangled = mangledByAddr.get(addrVal);
                boolean demangleApplied = false;
                if (mangled != null) {
                    DemangledObject demangled = null;
                    try { demangled = DemanglerUtil.demangle(currentProgram, mangled); }
                    catch (Exception e) { demangled = null; }
                    if (demangled != null) {
                        try { demangleApplied = demangled.applyTo(currentProgram, addr, demOpts, monitor); }
                        catch (Exception e) { demangleApplied = false; }
                    }
                }

                // Step 2 (ALWAYS run, regardless of step 1's result): explicitly force the name +
                // namespace via setName(..., USER_DEFINED)/setParentNamespace, which -- unlike
                // applyTo() -- unconditionally takes effect. This is what actually guarantees the
                // rename happens; step 1 is signature-typing only.
                String qname = extractQualifiedName(claimedRealName);
                boolean renamed = false;
                if (!qname.isEmpty()) {
                    try {
                        String shortName = qname.contains("::") ? qname.substring(qname.lastIndexOf("::") + 2) : qname;
                        if (qname.contains("::")) {
                            String nsPath = qname.substring(0, qname.lastIndexOf("::"));
                            ghidra.program.model.symbol.Namespace ns = getNamespace(nsPath, currentProgram.getGlobalNamespace());
                            if (ns == null) {
                                ns = currentProgram.getSymbolTable().createNameSpace(
                                    currentProgram.getGlobalNamespace(), nsPath, SourceType.USER_DEFINED);
                            }
                            fn.setParentNamespace(ns);
                        }
                        fn.setName(shortName, SourceType.USER_DEFINED);
                        renamed = true;
                    } catch (Exception e) {
                        renamed = false;
                    }
                }

                String finalName = fn.getName(true);
                boolean actuallyChanged = !finalName.equals(oldName);
                if (renamed && actuallyChanged) {
                    String method = demangleApplied ? "demangled+forced-rename" : "forced-rename-only";
                    if (demangleApplied) appliedDemangled++; else appliedPlain++;
                    report.println(addrStr + ",\"" + oldName + "\",\"" + finalName + "\"," + method + ",applied");
                } else {
                    report.println(addrStr + ",\"" + oldName + "\",\"" + finalName + "\",error,"
                        + (renamed ? "setName-succeeded-but-name-unchanged" : "setName-failed"));
                    failed++;
                }
            }
        }
        report.close();

        println("=== APPLY CONFIRMED MISNAMES RESULT ===");
        println("  total rows                : " + total);
        println("  applied (demangled)       : " + appliedDemangled);
        println("  applied (plain rename)    : " + appliedPlain);
        println("  failed / skipped          : " + failed);
        println("  no function at address    : " + noFunc);
        println("  report                    : " + reportPath);
    }

    // Best-effort namespace path resolver (handles "A::B::C" nested namespaces), falling back to
    // creating each missing level. Kept simple/best-effort -- worst case a rename lands one level
    // shallower than ideal namespace nesting, still far better than the wrong name it replaces.
    private ghidra.program.model.symbol.Namespace getNamespace(String path, ghidra.program.model.symbol.Namespace root) {
        ghidra.program.model.symbol.Namespace current = root;
        for (String part : path.split("::")) {
            if (part.isEmpty()) continue;
            ghidra.program.model.symbol.Namespace next = currentProgram.getSymbolTable().getNamespace(part, current);
            if (next == null) {
                try {
                    next = currentProgram.getSymbolTable().createNameSpace(current, part, SourceType.USER_DEFINED);
                } catch (Exception e) {
                    next = currentProgram.getSymbolTable().getNamespace(part, current);
                }
            }
            if (next == null) return null;
            current = next;
        }
        return current;
    }
}

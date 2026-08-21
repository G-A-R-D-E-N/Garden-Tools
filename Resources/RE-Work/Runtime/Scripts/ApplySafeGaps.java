// Apply the "safe naming-gap" fills: addresses whose CURRENT name is a generic CommonLib/Ghidra
// placeholder (Class::FuncN, _NNN, thunk_FUN_..., empty Class::) getting filled in with the real
// 1.10.155-PDB-cross-referenced name. Input is safe_gaps_apply.csv, which has ALREADY been filtered
// (by organize_functions.py's companion split) to only the entries where the decoded name's class
// MATCHES the placeholder's class (pure method-name fill of a vtable slot whose class the RTTI
// matcher already trusts) plus the class-less placeholders (_NNN/thunk_FUN) and the handful of data
// symbols. The riskier "class reassignment" entries (decoded name attributes the slot to a DIFFERENT
// class -- the base-vs-derived / sibling-class ambiguity that caused the misname-pass reverts) are
// deliberately held out in safe_gaps_class_reassigned_deferred.csv and NOT applied here.
//
// Handles BOTH functions and data (the 5 static-member-data addresses) -- unlike
// ApplyConfirmedMisnames.java which was functions-only.
//
// Usage: -postScript ApplySafeGaps.java [applyCsvPath] [labelsCsvPath] [reportPath]
import ghidra.app.script.GhidraScript;
import ghidra.app.util.demangler.DemangledObject;
import ghidra.app.util.demangler.DemanglerOptions;
import ghidra.app.util.demangler.DemanglerUtil;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.symbol.SourceType;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolTable;

import java.io.BufferedReader;
import java.io.FileReader;
import java.io.PrintWriter;
import java.util.HashMap;
import java.util.Map;

public class ApplySafeGaps extends GhidraScript {

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

    // Extract "A::B::Method" from a demangled decl, tracking <...> depth so template args' commas/
    // parens don't confuse the arg-list boundary. Works for both function decls (have a '(') and
    // data decls (no '(' -- returns the trailing "Type Namespace::name", handled by caller).
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

    // For data decls with no calling convention and no '(' (e.g.
    // "public static class BSReadWriteLock TESForm::AllFormsMapLock"), the qualified name is the
    // LAST whitespace-separated token that contains "::". Falls back to extractQualifiedName.
    private static String extractDataName(String decl) {
        String[] toks = decl.trim().split("\\s+");
        for (int i = toks.length - 1; i >= 0; i--) {
            if (toks[i].contains("::")) return toks[i];
        }
        return extractQualifiedName(decl);
    }

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        String applyCsvPath = (args.length > 0 && !args[0].isEmpty())
            ? args[0] : "E:\\F4SE OG\\ReverseEngineering\\Organized\\safe_gaps_apply.csv";
        String labelsCsvPath = (args.length > 1 && !args[1].isEmpty())
            ? args[1] : "E:\\F4SE OG\\ReverseEngineering\\Symbols\\F4_1_10_163_labels_decoded.csv";
        String reportPath = (args.length > 2 && !args[2].isEmpty())
            ? args[2] : "E:\\F4SE OG\\ReverseEngineering\\Organized\\applied_safe_gaps.csv";

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
                } catch (Exception e) { }
            }
        }
        println("Loaded " + mangledByAddr.size() + " address->mangled-name entries.");

        FunctionManager fm = currentProgram.getFunctionManager();
        SymbolTable st = currentProgram.getSymbolTable();
        DemanglerOptions demOpts = new DemanglerOptions();
        demOpts.setApplySignature(true);

        PrintWriter report = new PrintWriter(new java.io.FileWriter(reportPath));
        report.println("address,old_name,new_name,kind,note");

        int total = 0, renamedFn = 0, renamedData = 0, unchanged = 0, noTarget = 0;

        try (BufferedReader r = new BufferedReader(new FileReader(applyCsvPath))) {
            r.readLine(); // header
            String line;
            while ((line = r.readLine()) != null) {
                if (line.isEmpty()) continue;
                String[] f = splitCsvLine(line);
                if (f.length < 3) continue;
                String addrStr = f[0].trim();
                String oldName = f[1];
                String decodedDecl = f[2];
                total++;
                if (total % 500 == 0) println("... processed " + total);

                long addrVal;
                try {
                    addrVal = Long.parseUnsignedLong(addrStr.replace("0x", "").replace("0X", ""), 16);
                } catch (Exception e) {
                    report.println(addrStr + ",\"" + oldName + "\",,error,bad-address"); noTarget++; continue;
                }
                Address addr = toAddr(addrVal);

                // Best-effort: demangle the original mangled string and applyTo (full signature/type).
                String mangled = mangledByAddr.get(addrVal);
                if (mangled != null) {
                    try {
                        DemangledObject dem = DemanglerUtil.demangle(currentProgram, mangled);
                        if (dem != null) dem.applyTo(currentProgram, addr, demOpts, monitor);
                    } catch (Exception e) { }
                }

                // A data DECL (static member: no '(' and no calling convention) can still land at an
                // address Ghidra happens to have a function at -- route by the DECL shape, not by
                // whether a function object exists, so the right name extractor is used.
                boolean declIsData = decodedDecl.indexOf('(') < 0
                    && decodedDecl.indexOf("__thiscall") < 0 && decodedDecl.indexOf("__cdecl") < 0
                    && decodedDecl.indexOf("__stdcall") < 0 && decodedDecl.indexOf("__fastcall") < 0;

                Function fn = fm.getFunctionContaining(addr);
                if (fn != null && !declIsData) {
                    // Function: force the exact name+namespace (applyTo silently no-ops when the
                    // existing placeholder has equal/higher source priority).
                    String qname = extractQualifiedName(decodedDecl);
                    // Un-nameable guard: giant STL functor instantiations
                    // (std::_Func_impl<std::_Callable_obj<lambda...>,...>::_Move) have a ~500-char
                    // template "namespace" full of <>, chars Ghidra can't make a namespace from.
                    // These are compiler-generated internals with zero RE value -- skip cleanly
                    // rather than half-apply a namespace-less short name that reads worse than the
                    // existing (complete, if ugly) sanitized placeholder.
                    String ns = qname.contains("::") ? qname.substring(0, qname.lastIndexOf("::")) : "";
                    if (isUnnameable(ns) || isUnnameable(qname.contains("::") ? qname.substring(qname.lastIndexOf("::") + 2) : qname)) {
                        report.println(addrStr + ",\"" + oldName + "\",\"" + fn.getName(true) + "\",function,skipped-unnameable-template");
                        unchanged++;
                        continue;
                    }
                    boolean ok = forceRename(fn, qname);
                    String finalName = fn.getName(true);
                    if (ok && !finalName.equals(oldName)) {
                        report.println(addrStr + ",\"" + oldName + "\",\"" + finalName + "\",function,filled");
                        renamedFn++;
                    } else {
                        report.println(addrStr + ",\"" + oldName + "\",\"" + finalName + "\",function," + (ok ? "unchanged" : "rename-failed"));
                        unchanged++;
                    }
                    continue;
                }

                // Data / label path (the handful of static-member addresses).
                String dname = extractDataName(decodedDecl);
                Symbol prim = st.getPrimarySymbol(addr);
                String before = prim != null ? prim.getName(true) : "(none)";
                boolean ok = false;
                try {
                    if (dname.contains("::")) {
                        String nsPath = dname.substring(0, dname.lastIndexOf("::"));
                        String shortName = dname.substring(dname.lastIndexOf("::") + 2);
                        ghidra.program.model.symbol.Namespace ns = getNamespace(nsPath, currentProgram.getGlobalNamespace());
                        if (ns == null) ns = st.createNameSpace(currentProgram.getGlobalNamespace(), nsPath, SourceType.USER_DEFINED);
                        Symbol s = st.createLabel(addr, shortName, ns, SourceType.USER_DEFINED);
                        s.setPrimary();
                        ok = true;
                    } else {
                        Symbol s = st.createLabel(addr, dname, SourceType.USER_DEFINED);
                        s.setPrimary();
                        ok = true;
                    }
                } catch (Exception e) { ok = false; }
                String after = st.getPrimarySymbol(addr) != null ? st.getPrimarySymbol(addr).getName(true) : "(none)";
                if (ok && !after.equals(before)) {
                    report.println(addrStr + ",\"" + before + "\",\"" + after + "\",data,filled");
                    renamedData++;
                } else {
                    report.println(addrStr + ",\"" + before + "\",\"" + after + "\",data," + (ok ? "unchanged" : "rename-failed"));
                    noTarget++;
                }
            }
        }
        report.close();

        println("=== APPLY SAFE GAPS RESULT ===");
        println("  total rows        : " + total);
        println("  renamed functions : " + renamedFn);
        println("  renamed data      : " + renamedData);
        println("  unchanged         : " + unchanged);
        println("  no target/failed  : " + noTarget);
        println("  report            : " + reportPath);
    }

    // A symbol/namespace token Ghidra will reject: contains whitespace, template/pointer punctuation,
    // or is absurdly long (giant STL template instantiations). Cheap pre-check so we skip cleanly
    // instead of letting setName/createNameSpace throw.
    private static boolean isUnnameable(String s) {
        if (s == null) return true;
        if (s.length() > 200) return true;
        for (int i = 0; i < s.length(); i++) {
            char c = s.charAt(i);
            if (c == ' ' || c == '<' || c == '>' || c == ',' || c == '*' || c == '&' || c == '(' || c == ')' || c == '`' || c == '\'') return true;
        }
        return false;
    }

    private boolean forceRename(Function fn, String qname) {
        if (qname == null || qname.isEmpty()) return false;
        try {
            String shortName = qname.contains("::") ? qname.substring(qname.lastIndexOf("::") + 2) : qname;
            if (qname.contains("::")) {
                String nsPath = qname.substring(0, qname.lastIndexOf("::"));
                ghidra.program.model.symbol.Namespace ns = getNamespace(nsPath, currentProgram.getGlobalNamespace());
                if (ns == null) ns = currentProgram.getSymbolTable().createNameSpace(
                    currentProgram.getGlobalNamespace(), nsPath, SourceType.USER_DEFINED);
                fn.setParentNamespace(ns);
            }
            fn.setName(shortName, SourceType.USER_DEFINED);
            return true;
        } catch (Exception e) {
            return false;
        }
    }

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

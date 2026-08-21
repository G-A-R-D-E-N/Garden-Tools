import sys
from pathlib import Path
import pyghidra

INSTALL   = r"E:\F4SE OG\ReverseEngineering\Ghidra\ghidra_12.1_PUBLIC"
PROJ_LOC  = r"E:\F4SE OG\ReverseEngineering\Projects\fwgh_proj_OLD_packed_reference_only\BethesdaGhidraScripts"
PROJ_NAME = "BethesdaGhidraScripts"
SCRIPT    = Path(r"E:\F4SE OG\docs\Knowledge Materials\Reference\BethesdaGhidraScripts\ghidrascripts\CommonLibImport_F4_OG.py")

print("Starting pyghidra against", INSTALL, flush=True)
pyghidra.start(install_dir=INSTALL)

import java.lang
from ghidra.util.task import ConsoleTaskMonitor
monitor = ConsoleTaskMonitor()

with pyghidra.open_project(PROJ_LOC, PROJ_NAME, create=False) as project:
    root = project.getProjectData().getRootFolder()
    folder = root.getFolder("f4").getFolder("og")
    df = folder.getFile("Fallout4.exe")
    if df is None:
        print("ERROR: program /f4/og/Fallout4.exe not found")
        sys.exit(1)
    consumer = java.lang.Object()
    program = df.getDomainObject(consumer, True, False, monitor)
    try:
        print("Running CommonLibImport_F4_OG.py (applies names+types; takes several min)...", flush=True)
        out, err = pyghidra.ghidra_script(SCRIPT, project, program,
                                          echo_stdout=False, echo_stderr=True)
        if err:
            print("SCRIPT STDERR (tail):")
            print("\n".join(err.splitlines()[-25:]))
        program.save("CommonLib f4 og naming", monitor)
        print("Saved program.", flush=True)

        fm = program.getFunctionManager()
        total = unnamed = scoped = 0
        for f in fm.getFunctions(True):
            total += 1
            n = f.getName()
            if n.startswith("FUN_") or n.startswith("sub_"):
                unnamed += 1
            elif "::" in f.getName(True):
                scoped += 1
        print("NAMING RESULT: total=%d  unnamed=%d  scoped(Class::Meth)=%d" % (total, unnamed, scoped), flush=True)
    finally:
        program.release(consumer)
print("DONE", flush=True)

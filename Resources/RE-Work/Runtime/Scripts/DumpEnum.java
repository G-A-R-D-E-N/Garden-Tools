import ghidra.app.script.GhidraScript;
import ghidra.program.model.data.*;

public class DumpEnum extends GhidraScript {
    @Override
    public void run() throws Exception {
        DataTypeManager dtm = currentProgram.getDataTypeManager();
        for (String arg : getScriptArgs()) {
            boolean found = false;
            java.util.Iterator<DataType> it = dtm.getAllDataTypes();
            while (it.hasNext()) {
                DataType d = it.next();
                if (d.getName().toLowerCase().contains(arg.toLowerCase())) {
                    println("### TYPE " + d.getCategoryPath() + "/" + d.getName()
                            + "  (class=" + d.getClass().getSimpleName() + ", len=" + d.getLength() + ")");
                    dump(d);
                    found = true;
                }
            }
            if (!found) println("### no type matching: " + arg);
        }
    }
    private void dump(DataType dt) {
        if (dt instanceof ghidra.program.model.data.Enum) {
            ghidra.program.model.data.Enum e = (ghidra.program.model.data.Enum) dt;
            String[] names = e.getNames();
            println("enum " + e.getName() + " : " + e.getLength() + " bytes, " + names.length + " enumerators");
            for (int i = 0; i < names.length; i++) {
                println("  " + names[i] + " = " + e.getValue(names[i]));
            }
            if (names.length == 0) println("  (EMPTY -- no enumerators recorded in this data type manager)");
        } else if (dt instanceof Composite) {
            Composite c = (Composite) dt;
            println("  composite " + c.getName() + " : " + c.getLength() + " bytes");
            for (DataTypeComponent comp : c.getComponents()) {
                println("    +0x" + Integer.toHexString(comp.getOffset()) + " " + comp.getFieldName()
                        + " : " + comp.getDataType().getName() + " len=" + comp.getLength());
            }
        } else {
            println("  (type: " + dt.getClass().getSimpleName() + " name=" + dt.getName() + " length=" + dt.getLength() + ")");
        }
    }
}

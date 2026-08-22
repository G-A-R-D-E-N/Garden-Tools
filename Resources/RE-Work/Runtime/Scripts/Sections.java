import ghidra.app.script.GhidraScript;
import ghidra.program.model.mem.MemoryBlock;
public class Sections extends GhidraScript {
    public void run() throws Exception {
        long ib = currentProgram.getImageBase().getOffset();
        for (MemoryBlock b : currentProgram.getMemory().getBlocks()) {
            println(String.format("%-10s RVA 0x%x - 0x%x  r=%b w=%b x=%b", b.getName(),
                b.getStart().getOffset()-ib, b.getEnd().getOffset()-ib,
                b.isRead(), b.isWrite(), b.isExecute()));
        }
    }
}

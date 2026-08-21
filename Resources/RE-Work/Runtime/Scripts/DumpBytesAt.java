// Dump raw bytes + disassembled instructions starting at an RVA, for N bytes.
// Usage: -postScript DumpBytesAt.java 0xRVA byteCount
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.mem.Memory;

public class DumpBytesAt extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        long imageBase = currentProgram.getImageBase().getOffset();
        long rva = Long.parseLong(args[0].replace("0x", "").replace("0X", ""), 16);
        int count = args.length > 1 ? Integer.parseInt(args[1]) : 16;
        Address start = toAddr(imageBase + rva);

        Memory mem = currentProgram.getMemory();
        byte[] buf = new byte[count];
        mem.getBytes(start, buf);
        StringBuilder hex = new StringBuilder();
        for (byte b : buf) hex.append(String.format("%02X ", b));
        println("### BYTES @ RVA 0x" + Long.toHexString(rva) + ": " + hex.toString().trim());

        Listing listing = currentProgram.getListing();
        Address end = start.add(count - 1);
        InstructionIterator it = listing.getInstructions(start, true);
        long cumulative = 0;
        while (it.hasNext()) {
            Instruction ins = it.next();
            long insRva = ins.getAddress().getOffset() - imageBase;
            if (insRva >= rva + count) break;
            println("  0x" + Long.toHexString(insRva) + " (+0x" + Long.toHexString(insRva - rva) + "): "
                    + ins.toString() + "  [len=" + ins.getLength() + "]");
            cumulative = (insRva - rva) + ins.getLength();
        }
        println("### CUMULATIVE INSTRUCTION BYTES COVERED: 0x" + Long.toHexString(cumulative));
    }
}

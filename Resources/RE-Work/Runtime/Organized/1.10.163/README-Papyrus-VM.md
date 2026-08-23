# Papyrus VM — first decompile pass

**Binary:** Fallout4.exe 1.10.163 OG, unpacked Ghidra program

**Scope:** the first P1/P2 anchors from [08-PAPYRUS-VM-QUESTS-ROADMAP.md](../../08-PAPYRUS-VM-QUESTS-ROADMAP.md). Addresses below are OG RVAs, not portable AE addresses.

## `BSScript::Internal::VirtualMachine::Update`

**RVA:** `0x27317c0`

**Signature recovered by Ghidra:** `void Update(VirtualMachine*, float deltaSeconds)`

This is the VM frame/update coordinator. It lazily initializes seven static processing weights, multiplying each by the incoming delta before passing it to the individual queues:

| weight | default bits | default value | consumer |
|---|---:|---:|---|
| `fMessageProcessingWeightC` | `0x3ee66666` | `0.45` | `ProcessMessageQueue` |
| `fResetProcessingWeightC` | `0x3d4ccccd` | `0.05` | `ProcessResetQueue` |
| `fArrayCleanupProcessingWeightC` | `0x3c23d70a` | `0.01` | `ProcessArrayCleanup` |
| `fStructCleanupProcessingWeightC` | `0x3c23d70a` | `0.01` | `ProcessStructCleanup` |
| `fObjectCleanupProcessingWeightC` | `0x3ca3d70a` | `0.02` | `ProcessObjectCleanup` |
| `fUnneededObjectCleanupProcessingWeightC` | `0x3d75c28f` | `0.06` | `ProcessUnneededObjectCleanup` |
| `fResumeProcessingWeightC` | `0x3ecccccd` | `0.4` | `ProcessResumeQueue` |

The exact settings/override source for these globals is not yet pinned; the defaults are direct decompile evidence.

### Frame order

1. Lock the VM state at `vm+0xbdc4`.
2. Consult the VM state at `vm+0xbdf4` and the stack/task state. If thawing, it clears the state after finding the next live task and logs `VM is thawing...`; if all stacks are frozen, it logs `VM is frozen` and sets state `2`; otherwise it marks the VM freezing with `VM is freezing...` and sets state `1`.
3. Run `ProcessResetQueue(delta * 0.05)`.
4. Run `ProcessMessageQueue(delta * 0.45, &messageCountA, &messageCountB)`.
5. Run `ProcessResumeQueue(delta * 0.4, &suspendedCount)`.
6. Run array, struct, unneeded-object, and ordinary-object cleanup with their respective weighted budgets.
7. Lock the VM's loaded-type/object table at `vm+0xc0`; remove loaded entries whose state is below `3`, release their smart pointers, compact the array at `vm+0x228`, and free it when empty.
8. Unlock and publish `BSScript::StatsEvent` through the event source at `vm+0x28` when work was performed. The event carries running stacks, suspended stacks, function-message count, and detached-object count.

The decompile shows that freeze/thaw state is an explicit scheduler state machine, not merely a per-stack flag. The method also performs object/type cleanup in the same frame coordinator, so a VM tick is broader than “execute scripts.”

## `BSScript::Internal::VirtualMachine::UpdateTasklets`

**RVA:** `0x2731ec0`

This helper enforces a time budget around tasklet execution:

- Converts the supplied budget through `BSPrecisionTimer::fFrequencyMS`.
- Uses `vm+0x240` as the owner thread ID and `vm+0x244` as a recursion/active-update counter. A different thread waits with `Sleep`, retrying until it acquires the update gate.
- Copies the heap task array at `vm+0x82a0` into a temporary scrap array, then clears the source count at `vm+0x82b0` so tasks added during execution are deferred to a later pass.
- Calls `CodeTasklet::Process` once for each copied tasklet.
- Repeats while the copied array was non-empty and the precision timer has not exceeded the converted budget.

This is a snapshot-and-defer scheduler: newly queued tasklets cannot mutate the current iteration's task list. The inter-thread gate is explicit and protects the whole copied batch.

## `BSScript::Internal::CodeTasklet::Process`

**RVA:** `0x276e9e0`

The tasklet starts with `SetupFromStack`, handles a pending return state (`this+0x28 == 3`) through `CompleteFunctionCall`, and then enters the instruction loop. The loop:

1. Computes the bit position from the current instruction pointer and the bit-reader base (`this+0x48`).
2. Stores the current bit offset at `stack-frame+0x20`.
3. Stops and calls `VirtualMachine::StackIsFrozen` if the owning stack's freeze state at `stack+0x6c` is `1`.
4. Reads a **6-bit opcode** from `BSBitReader`, advancing the reader by six bits.
5. Dispatches opcode `1` through `0x2e` to arithmetic, comparisons, jumps, calls/returns, strings, arrays, and structs.
6. Treats `0x17`–`0x19` and `0x1c`/`0x1d` as call-family opcodes, with `HandleCall` deciding whether the tasklet continues or suspends.
7. Logs an error for an unknown opcode through the tasklet's `ErrorLogger`.

Confirmed opcode anchors from the dispatch table:

| opcode | handler |
|---:|---|
| `0x01` | `HandleIAdd` |
| `0x02` | `HandleFAdd` |
| `0x03`/`0x04` | integer/float subtract |
| `0x05`/`0x06` | integer/float multiply |
| `0x07`/`0x08` | integer/float divide |
| `0x09` | integer modulo |
| `0x0a`–`0x0e` | not, negate, assign, cast |
| `0x0f`–`0x13` | equality and ordered comparisons |
| `0x14`–`0x16` | jump, conditional jump true/false |
| `0x17`–`0x19`, `0x1c`, `0x1d` | call variants |
| `0x1a` | return |
| `0x1b` | string concatenation |
| `0x1e`–`0x23` | array create/length/get/set/find |
| `0x24` | type test |
| `0x25`–`0x29` | struct create/get/set/find |
| `0x2a`–`0x2e` | array add/insert/remove/clear |

This pins the instruction stream's dispatch width, but not yet the operand encoding consumed by each handler. Operand formats are the next P1 target.

## `BSScript::CompiledScriptLoader::GetClass`

**RVA:** `0x271db70`

`GetClass(char* scriptName, UnlinkedTypes::Object& out)` begins by recording the thread's error and fatal counts, clearing the output object, and asking the configured `IStore` at `loader+0x10` to locate the script. A missing store or failed store lookup posts a cached error and returns `false`.

For a found script it calls the following readers in order:

1. `ReadHeader`
2. `ReadStringTable`
3. `ReadDebuggingInfo`
4. `ReadUserFlagInfo`
5. `FindObject`

It then clears the loader's readable string table and resets the store. Success additionally requires that the thread error and fatal counters are unchanged from entry. This means a nominally successful reader chain is still rejected if a reader logged an error.

## `ReadHeader` facts

**RVA:** `0x271dde0`

The header reader uses the store's typed read virtual at slot `+0x20` and its error-string accessor at `+0x40`.

Confirmed behavior:

- Reads a 4-byte header value first and recognizes the signed value `-0x05a83f22` (`0xFA5780DE`) as the special header path.
- Reads two one-byte version components into loader bytes `+0x30` and `+0x31`.
- Rejects versions above major `3`, and rejects minor versions above `9` when major is `3`.
- Reads a 2-byte value. If it equals `2`, reads an additional 8-byte value into the output object's field at `+0x08`; other values generate an error.
- Initializes unknown filename, username, and computer-name fields when the header path fails.
- If the loader flag byte at `+0x32` bit `0` is set, reads three stored strings into the output object. Any read error posts `Error reading script header from store.` and returns false.

The exact semantic names of the header's 2-byte and 8-byte fields remain open. The next step is to decompile `BSStorage` read/write implementations and compare this reader against real `.pex` bytes.

## Linker anchor

`BSScript::LinkerProcessor::Process` is at **RVA `0x2799b60`**. It clears/rebuilds the processor's pending name arrays, extracts the requested script name, and calls `LoadAndLinkClass` for each discovered class. It rejects a missing loader immediately and posts an error. After processing it checks each loaded class through the callback path and returns false when the error/fatal counters changed.

The companion `LoadAndLinkClass` at `0x279a000` and the conversion methods (`LinkParent`, `ConvertObjectVariables`, `ConvertProperties`, `ConvertState`, `ConvertFunction`, and type-check methods) are the next P2 decompile batch. `LinkerProcessor::ForceReload` at `0x2764910` is a separate four-byte state setter and should not be confused with the full link pass.

## Confidence and open questions

**Confirmed:** OG RVAs, VM update order, seven default weights, task snapshot/defer behavior, 6-bit opcode dispatch, loader reader order, error-counter success gate, header version rejection rules.

**Open:** operand bit layouts, exact `.pex` header field semantics, string-table encoding, cache/store search order, `LoadAndLinkClass` failure matrix, native registration table layout, save/load serialization, and AE 1.11.240 address/layout parity.

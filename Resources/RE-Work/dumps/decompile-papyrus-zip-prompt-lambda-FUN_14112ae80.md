# Papyrus zip prompt callback — `lambda_71ce1f43`

**Binary:** `CreationKit.exe` 1.11.137.0<br>
**Callback/operator:** `FUN_14112ae80` (`0x14112ae80`, RVA `0x112ae80`)<br>
**RTTI type:** `.?AV<lambda_71ce1f4350638474a0f15e4607c39b12>@@`<br>
**Type-descriptor getter:** `FUN_14112af60` returns `0x143ca7930`

This is the `std::function<bool(void)>` callback supplied to
`FUN_14063ab40` by the Papyrus source-zip startup loop. It is called only after
the `[ZipInstall]` stamp comparison says that a zip is new or updated, and
before `CoCreateInstance`/Shell zip extraction.

## Decompiled body

```c
// Closure layout recovered from the generated lambda helpers:
//   lambda +0x08 = captured const char* zip path
bool lambda_71ce1f43::operator()() const
{
    char message[0x104];

    sprintf_s(message, sizeof(message),
        "Script source zip \"%s\" is new or has been updated - unpack?",
        this->capturedZipPath /* this + 0x08 */);

    // 0x34 = MB_YESNO | MB_ICONQUESTION.
    // IDYES is 6; IDNO is 7.
    return MessageBoxA(
        NULL,
        message,
        "Script Source Extraction",
        0x34) == 6;
}
```

### Instruction-level anchors

```asm
14112ae80  sub  rsp,138h
14112ae87  mov  r9,[rcx+8]             ; captured zip path
14112ae8b  lea  r8,[14310bf48]         ; prompt format string
14112ae92  lea  rcx,[rsp+20h]          ; 0x104-byte message buffer
14112ae97  mov  edx,104h
14112ae9c  call  sprintf_s
14112aea1  mov  r9d,34h                 ; MessageBoxA flags
14112aea7  lea  r8,[14310bf90]          ; "Script Source Extraction"
14112aeae  lea  rdx,[rsp+20h]
14112aeb3  xor  ecx,ecx                ; hWnd = NULL
14112aeb5  call  MessageBoxA
14112aebb  cmp  eax,6                  ; IDYES
14112aebe  sete al
14112aec8  ret
```

The generated closure helpers immediately following the operator include the
copy/accessors and `FUN_14112af60`, which returns the lambda RTTI descriptor.
This labels the previously anonymous callback without relying on a guessed
vtable slot.

## Effect on `FUN_14063ab40`

At `0x14063ac5b` the extractor verifies that its callback object is non-null,
then calls its boolean operator at `0x14063ac68`. The COM block is entered only
when the returned `AL` is nonzero:

```c
if (stamp_is_new_or_missing) {
    if (!callback) std::_Xbad_function_call();
    if (callback()) {
        // CoCreateInstance(Shell.Application)
        // NameSpace(zip), NameSpace(destination), Items(), CopyHere()
    }
    // common cleanup and ZipInstall stamp write follow
}
```

Clicking **No** therefore skips the COM calls cleanly for that zip and returns
the extractor's normal success value; it is not a COM error path. The common
post-branch writes the refreshed `[ZipInstall]` struct when the surrounding
path/stamp work succeeds, so the prompt is the intended user-facing gate.

## Settings/CLI conclusion

`FUN_14112ae80` reads only its captured path and the return value from
`MessageBoxA`. It has:

- no `GetPrivateProfile*` call;
- no RIP-relative read of a `CreationKit.ini` descriptor/value;
- no command-line or batch-bit read; and
- no Papyrus setting test in the caller-side gate (`FUN_141129aa0` / the
  Papyrus initialization chain).

The 1,629-key entries such as `bMustConfirmScriptSync:Papyrus`,
`bCopyScriptToSource:Papyrus`, and `sScriptSourceFolder:Papyrus` are not read by
this extraction gate. `-Switch` batch flags also cannot disable it: this path is
startup Papyrus initialization, not a batch dispatcher branch.

The clean supported bypass is to answer **No** in the prompt. A pre-seeded
`[ZipInstall]` stamp can avoid reaching the callback only if it matches the
runtime tuple; the surrounding decompile documents that the expected tuple is
read from uninitialized stack on the fresh-extraction path, so pre-seeding is
not a reliable configuration-level workaround. A noninteractive always-skip
behavior would require an external UI answer or a binary patch to the callback;
no INI key or batch flag provides it in this build.

## Sources

- `ghidra-dump/decompile-zipinstall-0x63ab40.log`
- `ghidra-dump/decompile-ziparraycopy-0x1126280.log`
- direct disassembly of `0x14112ae80` and the helper tail through `0x14112af60`
- `20-WINE-PAPYRUS-ZIP-CRASH.md`

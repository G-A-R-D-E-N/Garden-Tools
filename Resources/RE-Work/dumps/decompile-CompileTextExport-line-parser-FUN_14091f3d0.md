# `FUN_14091f3d0` — TextExport line parser

**Binary:** Fallout 4 CreationKit.exe 1.11.137.0, image base `0x140000000`.

This is the line reader called by `FUN_140916980` at `0x140916e94`. Its input
is the parser state object at `rcx`; the first qword is the byte-stream object,
and `+0x10` is the growable line buffer. The relevant result fields are:

```text
+0x1c  parsed StringID, or -1 when the first field is invalid
+0x20  category index, initialized to 17 (Misc)
+0x28  pointer to the fourth field (text), or null until three tabs exist
+0x30  line-reader state/continuation flag
```

## Annotated body

```asm
14091f3d0  mov    rbx,rcx
14091f3f6  mov    dword ptr [rcx+1c],-1       ; invalid StringID sentinel
14091f400  mov    dword ptr [rcx+20],11h     ; category = Misc (index 17)
14091f407  mov    qword ptr [rcx+28],0       ; no text field yet
14091f40b  cmp    byte ptr [rcx+30],0
14091f40f  jne    14091f738                 ; continue/finalize existing line

; Read one byte from the source stream into the temporary character slot.
14091f425  mov    rdi,[rcx]
14091f435  call   14008031e                 ; stream read, count = 1
14091f442  test   eax,eax
14091f444  jne    14091f49a                 ; EOF/error path

; A CR or LF terminates the record.  Otherwise append the byte to +0x10.
14091f451  movzx  edx,byte ptr [rsp+70]
14091f456  cmp    dl,0dh
14091f459  je     14091f465
14091f45d  cmp    dl,0ah
14091f460  jne    14091f4a1
14091f462  inc    dword ptr [rbx+18]
14091f465  ...

; The first three tabs become NUL delimiters.  The second-tab continuation
; pointer is kept in r13; the fourth-field pointer is saved at +0x28.
14091f4c4  cmp    dl,09h
14091f4c9  cmp    ebp,3
14091f4ce  xor    dl,dl
14091f4d0  inc    ebp
14091f4db  lea    r13,[buffer + current_position + 1] ; after tab 2
14091f4fc  mov    [rbx+28],rcx                       ; after tab 3
14091f500  mov    [buffer + current_position],dl

; Parse the complete first field as hexadecimal.  The IAT target is
; api-ms-win-crt-convert-l1-1-0!_strtoui64, with base 16.
14091f681  mov    rcx,[rbx+10]                  ; line start
14091f698  call   qword ptr [148235930]         ; _strtoui64(line, &end, 16)
14091f6a1  ...
14091f6b3  mov    edi,0ffffffffh                ; no digits / zero / -1 reject
14091f6d4  mov    [rbx+1c],edi                 ; low 32 bits are retained

; Resolve the third field (r13) against 18 strings using _stricmp.  If none
; matches, the initial index 17 (Misc) is retained and a warning is emitted.
14091f6d7  lea    rbp,[143b9bef0]              ; category pointer table
14091f6e1  cmp    esi,12h
14091f6eb  mov    rdx,[rbp + index*8]
14091f6f0  call   qword ptr [148236448]         ; _stricmp(r13, category)
14091f6fd  mov    r15d,esi                       ; matched category index
14091f72e  mov    [rbx+20],r15d

14091f738  cmp    dword ptr [rbx+1c],-1
14091f74d  cmp    qword ptr [rbx+28],0
14091f754  mov    al,1                           ; valid record
14091f781  xor    al,al                           ; otherwise invalid/EOF
```

The elided append/grow loop is the code at `0x14091f500`–`0x14091f66e`: it
preserves bytes after the third tab, grows the buffer when the current length
reaches its capacity, and bounds the growth at `0x100000` bytes before the
caller logs `Buffer exceeding reasonable size.`

## Producer-side proof of field order

The CK's text exporter helper `FUN_14091bd80`, called by the `-ExportText`
worker at `0x1409168fa`, writes the same record in four operations:

```asm
; r12d = StringID returned by the source string object's virtual getter
14091bf88  lea    r8,[143015950]                ; "0x%08X\\t"
14091bf8f  mov    r9d,r12d
14091bfa3  call   14000415b                       ; field 1: StringID

; r14d = owning form ID
14091bfe7  lea    r8,[143015950]                ; "0x%08X\\t"
14091bff6  mov    r9d,r14d
14091bffe  call   14000415b                       ; field 2: FormID

14091c039  lea    r8,[142f96bf4]                ; "%s\\t"
14091c048  mov    r9,[r15]                      ; category name
14091c050  call   14000415b                       ; field 3: category

; The string object's text getter and bounded copy supply field 4.
14091c092  call   qword ptr [rsi+20]
14091c0a9  call   14006a299
14091c0ca  call   140044adf                       ; field 4: text

14091c0f2  lea    rdx,[142f90828]               ; embedded "\\n"
14091c0fd  call   140044adf
```

Thus the generated line is:

```text
0x<8-hex-StringID>\\t0x<8-hex-FormID>\\t<Category>\\t<Text>\\n
```

The parser does not validate the FormID field; it only needs the second tab to
reach the category pointer. The first three tabs are structural delimiters and
are replaced in-place with NULs. Tabs in `<Text>` after the third delimiter are
ordinary text bytes. There is no quote or backslash escape layer in this
reader; CR/LF ends the record.

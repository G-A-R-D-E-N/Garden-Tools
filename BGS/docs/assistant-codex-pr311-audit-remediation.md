# PR 311 Codex assistant audit remediation

## Scope

This document records the proof-first remediation applied after the review of PR 311. It supplements `assistant-codex-subscription-auth.md` and supersedes that document where the earlier text describes API-key acceptance, reactive-only tool isolation, inherited provider selection, or incomplete process ownership.

No Fallout or Havok semantics are changed by this work. `commonlib_NonVR` was not modified.

## Verified findings

The PR initially had five concrete gaps:

1. `account/read` accepted any non-empty account type as signed in, including `apiKey`, even though this feature is specifically for ChatGPT subscription usage.
2. `thread/start` did not pin `modelProvider`, allowing user configuration to influence provider selection.
3. Codex built-in tools were primarily stopped after `item/started`; that was too late to be the primary isolation boundary.
4. Dynamic BGS tool requests were not bound to the active thread and turn and could execute concurrently.
5. failed/restarted and auth-only App Server connections did not have sufficiently explicit ownership and cleanup proof.

Windows npm installation was also not covered by executable discovery.

## Current subscription boundary

`CodexAccountClient` now preserves the reported auth mode but treats only `chatgpt` as a usable signed-in BGS account. `apiKey` and other Codex authentication modes cannot reach `thread/start` or `turn/start`.

BGS does not read or persist Codex credentials. Browser and device-code authentication remain Codex-owned.

## Provider boundary

Both App Server launch configuration and `thread/start` pin the provider to `openai`.

The App Server process is launched with session overrides that disable inherited MCP configuration, shell tools, unified exec, and web search before a BGS thread exists.

## Proactive thread isolation

Before every new BGS Codex thread, BGS calls the official `config/read` App Server method against the BGS scratch working directory. The request must succeed and return an object-shaped effective config. If the effective `mcp_servers` value is present, it must be an object with bounded server names. Any failure or malformed shape stops the request before `thread/start`.

Every discovered MCP server is explicitly represented in the new thread config as disabled.

The thread config mirrors the disable set used by Codex's own temporary structured-request implementation and disables:

- apps
- code mode and code-mode-only
- context management
- current-time reminder
- deferred executor
- fanout
- goals
- hooks
- image generation
- memories
- multi-agent modes
- plugins
- permission-request tool
- shell snapshot and shell tool
- standalone web search
- token budget tool
- tool suggestions
- unified exec
- image viewing
- skill orchestration and skill instructions
- experimental request-user-input tool
- update-plan tool
- web search
- every effective MCP server by name

Threads remain read-only, approval-never, ephemeral, and rooted at the BGS scratch directory. The existing interruption of unexpected `commandExecution`, `fileChange`, `mcpToolCall`, and `webSearch` items remains defense in depth rather than the primary control.

## BGS tool boundary

Dynamic `item/tool/call` requests now execute through a serialized gate. A request is refused unless its `threadId` and `turnId` exactly match the currently active BGS turn. The six-call budget is checked and incremented under the session state lock.

This preserves the existing `AssistantTools` mutation contract: mutation tools create previews only, explicit BGS approval is still required, stale approval fails closed, and Save remains explicit.

## Connection ownership

`CodexAssistantConnection.ConnectAsync` disposes any failed or exited prior transport before replacement. Partial connection attempts are cleaned up on failure.

Connections created by `MainWindow` are also bound to `Window.Closed`, covering the auth-only case where App Server can be started before a chat session owns it.

## Windows Codex discovery

The official npm launcher resolves platform packages to a native `vendor/<target>/bin/codex.exe`. BGS follows that native layout for Windows x64 and arm64 instead of executing `codex.cmd`, `.bat`, or `.ps1` through a shell.

Standalone `codex.exe` discovery remains supported.

## Deterministic proof added

The test suite now covers:

- API-key auth cannot create a BGS Codex thread or turn
- OpenAI provider pinning
- proactive process isolation arguments
- `config/read` occurs before `thread/start`
- full thread disable profile
- inherited MCP names are individually disabled
- `config/read` failure stops before thread creation
- malformed MCP configuration stops before thread creation
- wrong-thread and wrong-turn tool calls are rejected
- post-completion tool calls are rejected
- serialized tool execution and bounded tool budget
- repeated connect uses one transport
- reconnect disposes the exited transport before replacement
- failed handshake disposes the partial transport
- an auth-only connection is disposable by its owner
- window close disposes the window-owned Codex connection
- Windows npm shim discovery resolves to the native Codex executable without shell execution

No automated test needs a live ChatGPT account or paid model request.

## Remaining release gate

Release readiness still requires the current branch head to pass both Linux and Windows BGS CI, including build, native regression harness, xUnit, UI smoke, publish, and executable verification.

The real browser/device-code click-through and a real ChatGPT-subscription prompt/tool/mutation acceptance pass remain manual validation. Do not describe this change as release-ready until those gates are recorded.

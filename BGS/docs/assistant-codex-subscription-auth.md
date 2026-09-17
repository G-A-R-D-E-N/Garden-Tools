# BGS Codex App Server subscription authentication proof

## Status

Proof-first slice for replacing the assistant's OpenAI-compatible API-key provider with the official
Codex CLI / Codex App Server, so a user with a supported ChatGPT subscription can use the BGS
Assistant without an OpenAI API key and without separate API billing.

Source work follows the Ponytail proof-first/reuse-first workflow: https://github.com/dietrichgebert/ponytail

No Fallout/Havok semantics are changed or reinterpreted by this work, so no
`PRISMA-USER-INTERFACE-FRAMEWORK/commonlib_NonVR` cross-check is required. That repository was not
read, modified, or depended on.

## 1. Current BGS behavior (baseline, reproduced from source)

Baseline commit: `d33f9e1d` (origin/main, `feat(bgs): add read-only MCP and approved in-app assistant (#307)`).

| Concern | Current source | Observed behavior |
|---|---|---|
| Provider construction | `BGS/app/AssistantProvider.cs:26` `AssistantProvider.TryCreate` | Requires a `BaseUrl`, a `Model`, and a named credential environment variable; reads the key with `Environment.GetEnvironmentVariable` (`AssistantProvider.cs:54`). |
| Credential gate | `AssistantProvider.cs:54-59` | Returns `set <name> to connect to the assistant provider` and no client when the variable is empty. Default variable name is `BGS_AI_API_KEY` (`AssistantProvider.cs:20`). |
| IChatClient dependency | `AssistantProvider.cs:67`, `AssistantSession.cs:32`, `AssistantUi.cs:269` | Every path depends on `Microsoft.Extensions.AI.IChatClient` built from `OpenAIClient(new ApiKeyCredential(key), ...)`. |
| Settings | `AssistantProvider.cs:15-21` | Persists `assistant.base_url`, `assistant.model`, `assistant.api_key_env`. |
| Provider display | `AssistantUi.cs:352-357` | Hard-codes `OpenAI-compatible · <model> · <local|remote> · <base url>`. |
| Session/tool loop | `AssistantSession.cs:90-154` | BGS owns the loop: bounded system instructions, bounded editor context, one tool call at a time, 6 iterations maximum, history limited to 8 turns. |
| Approval | `AssistantTools.cs:85-102`, `AssistantUi.cs:335-350` | `bgs.set_clip_animation` only builds a preview; applying it requires the explicit "Apply approved edit" action, which rechecks document revision (`AssistantInteractionTests.cs:16-46`). |
| Test seam | `AssistantUi.cs:269 SetClientForTest`, `AssistantInteractionTests.cs:157 FakeChatClient` | Deterministic fake `IChatClient`; no live provider call in CI. |

### Why this does not satisfy a ChatGPT subscription user

1. The only constructed provider is an OpenAI-compatible HTTP endpoint authenticated by an API key.
   A ChatGPT/Plus/Pro/Business subscription does not issue an API key, so the subscription cannot be
   used at all: the UI shows `Disconnected: set BGS_AI_API_KEY ...` and no model call is possible.
2. Using an API key is billed separately at API rates, which is exactly what the task excludes for
   the subscription path.
3. BGS has no authentication surface: no signed-in/signed-out state, no login/logout action, no
   account/plan metadata. Authentication state does not belong to BGS, so it must be delegated.

## 2. Codex CLI / App Server version tested

- Installed binary: `C:\ci\home\AppData\Local\Programs\OpenAI\Codex\bin\codex.exe`
- `codex --version` -> `codex-cli 0.154.0`
- `initialize` response `userAgent` -> `bgs-probe/0.154.0 (Windows 10.0.26200; x86_64) unknown (bgs-probe; 0.0.1)`
- `codex app-server --help` -> `app-server [experimental] Run the app server or related tooling`, with
  `--listen <URL>` supporting `stdio://` (default), `unix://`, `ws://IP:PORT`, `off`.

Protocol artifacts were generated from this exact binary and are the pinned ground truth for the
implementation:

```text
codex app-server generate-json-schema --experimental --out <dir>
codex app-server generate-ts --experimental --out <dir>
```

Both are documented by OpenAI as version-specific ("Each output is specific to the Codex version you
ran"). They were generated into a temporary directory outside the repository and are **not** committed.

## 3. Official references

- App Server guide: https://developers.openai.com/codex/app-server
  - JSON-RPC 2.0, with `"jsonrpc":"2.0"` omitted on the wire.
  - `stdio` transport is newline-delimited JSON (JSONL).
  - `initialize` then `initialized` handshake; other requests before the handshake are rejected.
  - `dynamicTools on thread/start and the corresponding item/tool/call request or response flow are
    experimental APIs.`
  - `dynamicTools on thread/start is an experimental field (requires capabilities.experimentalApi = true).`
  - Server requests include `item/tool/call`; `thread/shellCommand` "runs outside the sandbox with
    full access", `process/spawn` starts "outside Codex's sandbox", and `fs/*` operates on absolute
    filesystem paths — none of these are used by BGS.
- Authentication guide: https://developers.openai.com/codex/auth
  - Codex supports "Sign in with ChatGPT for subscription access" or an API key.
  - `codex login` browser flow; device code is the headless path (`codex login --device-auth`).
  - Codex caches and automatically refreshes ChatGPT tokens.
- Repo/source: https://github.com/openai/codex (app-server implementation `codex-rs/app-server`).

## 4. Exact authentication lifecycle (verified)

Verified live against 0.154.0 over `stdio`:

```
--> {"id":1,"method":"initialize","params":{"clientInfo":{"name":"bgs-probe","version":"0.0.1"},"capabilities":{"experimentalApi":true}}}
<-- {"id":1,"result":{"userAgent":"bgs-probe/0.154.0 (Windows 10.0.26200; x86_64) unknown (bgs-probe; 0.0.1)","codexHome":"...","platformFamily":"windows","platformOs":"windows"}}
--> {"method":"initialized","params":{}}
--> {"id":2,"method":"account/read","params":{}}
<-- {"id":2,"result":{"account":{"type":"chatgpt","email":"<redacted>","planType":"prolite"},"requiresOpenaiAuth":true}}
```

Unrelated notifications (for example `remoteControl/status/changed`) were interleaved **before** the
`account/read` response, so responses must be matched by `id` and unknown notifications tolerated.

Method/notification surface used by BGS (official names, not guessed):

```text
request   initialize                      params { clientInfo, capabilities? }
notify    initialized                     params {} (no id)
request   account/read                    params { refreshToken?: bool }
          -> result { account: null | {type:"chatgpt", email: string|null, planType} | {type:"apiKey"} | {type:"amazonBedrock", ...}, requiresOpenaiAuth: bool }
request   account/login/start             params { type: "chatgpt", appBrand?, useHostedLoginSuccessPage? }
          -> result { type:"chatgpt", loginId: string, authUrl: string }
          params { type: "chatgptDeviceCode" }
          -> result { type:"chatgptDeviceCode", loginId: string, userCode: string, verificationUrl: string }
request   account/login/cancel            params { loginId } -> result { status: "canceled"|"notFound" }
request   account/logout                  params {} -> result {}
notify    account/login/completed         params { loginId: string|null, success: bool, error: string|null }
notify    account/updated                 params { authMode: string|null, planType: string|null }
request   model/list                      params {} -> result { data: [{ id, model, displayName, isDefault, hidden, ... }] }
```

`AuthMode` values (from the generated schema, `AccountUpdatedNotification.json`) include `apikey`,
`chatgpt`, `chatgptAuthTokens`, `agentIdentity`, `personalAccessToken`, `bedrockApiKey`, `null`.
`PlanType` is an enum (`free`, `go`, `plus`, `pro`, `prolite`, `team`, `business`, `enterprise`,
`edu`, ... `unknown`); BGS displays exactly the value the protocol returns and never invents a plan
label.

The device-code response fields and the `account/login/cancel` request/response shapes above are
taken from the pinned generated schema
(`LoginAccountResponse.json`, `CancelLoginAccountParams.json`, `CancelLoginAccountResponse.json`).
Device-code start was **not** exercised live in this session, to avoid creating an unused
authorization request against the user's account; the browser (`type: "chatgpt"`) flow shape is
documented by OpenAI and matches the schema.

BGS never reads, parses, copies, or stores `auth.json`, tokens, refresh tokens, OAuth codes, or
browser callback data. Codex owns OAuth, credential persistence, and token refresh.

## 5. Exact thread / turn lifecycle (verified)

Verified live: `thread/start` then `turn/start` then streaming notifications, with a trivial prompt.

```text
--> {"id":3,"method":"thread/start","params":{"cwd":"<bgs scratch>","sandbox":"read-only",
        "approvalPolicy":"never","ephemeral":true,"model":"<model>",
        "baseInstructions":"...","dynamicTools":[{"type":"function","name":"bgs_inspect_behavior",
        "description":"...","inputSchema":{...}}]}}
<-- {"id":3,"result":{"thread":{"id":"...","ephemeral":true,...},
        "model":"...","sandbox":{"type":"readOnly","networkAccess":false},
        "approvalPolicy":"never","cwd":"...","modelProvider":"openai",...}}

--> {"id":4,"method":"turn/start","params":{"threadId":"...","input":[{"type":"text","text":"..."}]}}
<-- {"id":4,"result":{"turn":{"id":"...","status":"inProgress","items":[],"itemsView":"notLoaded"}}}
<-- notify thread/status/changed
<-- notify turn/started
<-- notify item/started    {item:{type:"userMessage",...}}
<-- notify item/completed  {item:{type:"userMessage",...}}
<-- notify item/started    {item:{type:"agentMessage",...}}
<-- notify item/agentMessage/delta {threadId,turnId,itemId,delta}
<-- notify item/completed  {item:{type:"agentMessage",text:"ready",phase:"final_answer",...}}
<-- notify turn/completed  {threadId, turn:{id, items:[...], status:"completed"}}
```

Interleaved during the turn: `hook/started`, `hook/completed`, `mcpServer/startupStatus/updated`,
`thread/tokenUsage/updated`, `account/rateLimits/updated`. BGS tolerates all of them and acts only on
the methods it owns.

Cancellation is `turn/interrupt` with `{threadId, turnId}`; the turn then ends with status
`"interrupted"` (documented, and the schema for `TurnInterruptResponse` is empty `{}`).

`ephemeral: true` was verified live (response `thread.ephemeral == true`), so BGS conversations are
not added to the user's stored Codex thread list, matching the existing contract that BGS keeps the
visible transcript in memory only.

## 6. Tool integration findings

- **Mechanism exists and is official:** client-owned tools are registered per thread via
  `thread/start.dynamicTools` and invoked by the server as the `item/tool/call` server request with
  `DynamicToolCallParams { threadId, turnId, callId, namespace, tool, arguments }`. The client replies
  with `DynamicToolCallResponse { contentItems: [{type:"inputText", text}], success }`.
- **It is experimental:** verified live — without the capability the server replies
  `{"error":{"code":-32600,"message":"thread/start.dynamicTools requires experimentalApi capability"}}`,
  and with `capabilities.experimentalApi = true` the thread is created. BGS therefore sends the
  explicit opt-in and capability-gates the feature; when the server rejects it, the session falls back
  to plain ChatGPT-authenticated conversation **without** enabling any Codex built-in tool as a
  substitute.
- **Naming constraint:** verified live — `dynamic tool name must match ^[a-zA-Z0-9_-]+$ to match
  Responses API: bgs.inspect_behavior`. BGS tool names contain dots, so the adapter maps
  `bgs.inspect_behavior` <-> `bgs_inspect_behavior` in one place and dispatches by the original BGS
  name. The mapping is one-to-one and validated in both directions.
- **Approval is not delegated:** a dynamic tool call is answered from the existing
  `AssistantTools` catalog. `bgs.set_clip_animation` still only creates a preview there; applying it
  still requires the explicit BGS approval action and the existing stale-revision recheck. Codex
  cannot approve its own mutation, and BGS never treats model text as approval.
- **No built-in tool fallback:** BGS does not call `command/exec`, `thread/shellCommand`,
  `process/spawn`, `fs/*`, or `applyPatchApproval`, and never sends `dangerFullAccess` or
  `workspaceWrite` sandbox policies.

## 7. Security boundaries

1. `codex app-server` is treated as a local child process with an untrusted protocol boundary:
   line framing with a maximum line length, oversized lines discarded, malformed JSON ignored
   without terminating the connection, JSON-RPC request ids matched exactly, unknown methods and
   notifications ignored, stdout kept protocol-clean, stderr volume capped and never surfaced raw.
2. Process spawned without a shell, without a visible window, with redirected stdin/stdout/stderr,
   and never inheriting a credential from BGS.
3. Every thread is created with `sandbox: "read-only"`, `approvalPolicy: "never"`,
   `ephemeral: true`, and `cwd` set to a BGS-owned, empty scratch directory rather than the user's
   project. The verified effective sandbox is `{"type":"readOnly","networkAccess":false}`.
4. The app-server is launched with `-c mcp_servers={}` so the user's globally configured MCP servers
   are not inherited by BGS's Codex process. This override was verified accepted live (the process
   started, `config/read` succeeded, and `thread/start` succeeded with it).
5. BGS fails a turn closed if Codex attempts a built-in `commandExecution` or `fileChange` item:
   the adapter interrupts the turn and reports a bounded policy error instead of presenting the
   result. BGS never applies Codex-authored file changes to project files.
6. Bounded context is unchanged: only the existing typed `AssistantContext` snapshot, the user
   prompt, and bounded BGS tool results are sent. No raw HKX/XML/project dump is sent.
7. Errors shown to the user are bounded and sanitized; account email is never displayed, and no
   token, OAuth code, credential path, or raw auth payload is logged or stored.
8. Manual Save stays explicit, unsaved editor state stays distinct from disk state, and mutation
   approval stays in the existing BGS UI path.

### Residual risk (documented, not hidden)

The Codex CLI reads the user's global `~/.codex/config.toml`. On the verification machine that file
contains `sandbox_mode = "danger-full-access"`, plus configured apps/plugins. BGS overrides the
effective sandbox at thread level (`read-only`) and disables inherited MCP servers, but a read-only
sandbox still permits read operations. BGS therefore cannot claim the process is fully enclosed; it
claims, and tests, that (a) BGS requests read-only, approval-never, ephemeral threads, (b) BGS
disables inherited MCP servers, (c) BGS interrupts turns that attempt built-in command/file items,
and (d) BGS never grants write authority. Full process enclosure would need an official Codex
isolation flag that does not exist in 0.154.0; that gap is recorded here rather than papered over.

## 8. Failure modes covered

`codex` executable missing; process start failure; handshake failure/timeout; signed-out account;
login start failure; login cancelled or failed by notification; account updated to signed-out;
logout; malformed protocol line; unknown notification; oversized line; process exit mid-request;
request timeout; cancellation via `turn/interrupt`; turn failure status; tool-call refusal for an
unknown or schema-invalid tool; exceeded per-turn tool budget; disallowed built-in item;
clean shutdown.

## 9. Test plan

Deterministic fake-transport tests (no live account, no paid request, no `auth.json`) cover:

- Codex discovery (missing executable, override path, version/userAgent parsing).
- Protocol framing: initialize handshake, request-id correlation, interleaved notifications,
  malformed JSON, unknown notification, oversized line, bounded error text.
- Auth lifecycle: signed-out, signed-in ChatGPT, plan parsing, login start (browser + device code),
  login completed success/failure, account updated, logout.
- Conversation: thread start with sandbox/approval/ephemeral assertions, turn start, response
  assembly from deltas and completed items, cancellation, new-chat isolation, process death.
- Tools: known tool dispatch, unknown tool refused, invalid schema refused, per-turn budget,
  mutation cannot auto-approve, disallowed built-in item interrupts the turn.
- Settings: no credential, token, or endpoint key is persisted; deprecated keys are not written.
- Regression: opening the assistant drawer starts no turn; existing mutation approval and
  stale-approval fail-closed behavior unchanged.

## 10. Live end-to-end evidence (this session, Codex 0.154.0)

The real subscription-authenticated path was exercised against the installed CLI with no API key
configured anywhere:

```text
account/read            -> {"account":{"type":"chatgpt","email":"<redacted>","planType":"prolite"},"requiresOpenaiAuth":true}
model/list              -> {"data":[{"id":"gpt-6-astra", ...}]}
thread/start (read-only, approvalPolicy never, ephemeral true, baseInstructions, dynamicTools)
                        -> {"thread":{"id":"...","ephemeral":true,...},"sandbox":{"type":"readOnly","networkAccess":false},"approvalPolicy":"never"}
turn/start              -> turn/started, item/started(userMessage), item/completed,
                           item/started(agentMessage), item/agentMessage/delta("ready"),
                           item/completed(agentMessage, phase final_answer), turn/completed(status completed)
```

Interleaved notifications observed and tolerated: `hook/started`, `hook/completed`,
`mcpServer/startupStatus/updated`, `thread/tokenUsage/updated`, `account/rateLimits/updated`,
`remoteControl/status/changed`.

Not exercised live: browser/device-code login start, login completion, logout, and `turn/interrupt`
(their shapes come from the pinned generated schema and the official App Server guide). Logout was
deliberately not fired so the developer's own signed-in session was left untouched.

## 11. Regression gate results

Commands were run from the `BGS/` directory with the locked restores above.

```text
dotnet build app/BehaviourStudio.csproj --configuration Release --no-restore
  exit 0 -- Build succeeded, 0 Error(s)

dotnet run --project tools/symrm/symrm.csproj --configuration Release --no-restore -- test
  exit 0 -- 1798 checks, 0 failed

dotnet test tools/tests/BehaviourGraph.Tests.csproj --configuration Release --no-restore
  exit 0 -- Failed: 0, Passed: 692, Skipped: 0, Total: 692
            (49 of these are the Codex subscription/auth/protocol/tool tests)

dotnet run --project tools/uismoke/uismoke.csproj --configuration Release --no-restore -- --assistant
  exit 0 -- every check green, including the full Chat/Assistant-settings/approval lifecycle:
            assistant starts closed
            Chat entry point exists once
            Assistant settings entry point exists once
            assistant advertises the ChatGPT subscription boundary without a request
            assistant starts signed out and offers sign in
            Assistant settings opens the drawer
            Assistant settings preserves active activity
            Assistant settings exposes ChatGPT sign in
            opening Assistant settings makes no provider request
            Chat opens the assistant drawer
            Chat preserves active activity
            opening Chat makes no provider request
            opening Chat leaves the account signed out
            send reaches the configured provider
            conversation shows the user and assistant messages
            completed request returns to ready
            closing assistant restores the shell
            window-owned Codex connection starts
            closing window disposes the Codex connection
            model can invoke the real clip preview tool
            preview waits for explicit approval
            approval uses the normal editor commit
            approved animation reaches the active document
```

The full `uismoke` run (no `--assistant`) was re-attempted at `da200a74`. Its non-assistant checks
still stall at the Java-setup-control check (`The window has no Java setup control`) and never
advance to the Workspace or Ragdoll lifecycle sections. The assistant assertions that the same
binary did advance through are the entire `AssistantLifecycleSmoke` enumerated above, and every one
of those is green. The stall is in the unrelated core-shell smoke and predates the assistant work;
no assistant assertion fails in the full run. The full uismoke gate is therefore not claimed here;
the assistant-relevant gate, which is the entire assistant surface that the production user can
touch, is fully green at `da200a74`.

## 12. PR-readiness summary at `da200a74`

Production code change for this UX pass: none. The Chat and Assistant settings buttons, the Codex
detection text, the Signed out / Connected + plan + model account text, the Sign in with ChatGPT /
Use device code / Sign out controls, and the explicit Apply approved edit / Reject approval card
already existed in `BGS/app/AssistantUi.cs` and were wired to the production shell by
`BGS/app/MainWindow.Assistant.cs`. The only file edited by this UX pass is this documentation file,
to drop the stale "uismoke incomplete" framing now that the assistant smoke is green end-to-end.

Final user flow (production shell):

1. Launch `BGS/app/BehaviourStudio.csproj`. The Bridge tab is the first tab. The ChatGPT group
   on the Bridge shows two equally-weighted buttons: `Chat` (primary) and `Assistant settings`
   (secondary). No model request is made until a prompt is sent.
2. Click `Assistant settings` to open the drawer. The header reads `ChatGPT via Codex · Codex
   CLI detected` or `ChatGPT via Codex · Codex CLI not detected`. The account row starts at
   `Signed out`. The auth row shows `Sign in with ChatGPT` and `Use device code`.
3. Click `Sign in with ChatGPT`. BGS calls `account/login/start {type:"chatgpt"}`. Codex opens
   the browser login flow via `OpenUrl`. The status line flips to `Waiting for browser sign-in
   to finish...`. Codex owns the browser interaction, the token, refresh, and persistence; BGS
   never receives the credential. On `account/login/completed` success the account row flips
   to `Connected · ChatGPT <PlanType>` and `Model: <model>` when the server supplies one.
4. Click `Chat` to switch the same drawer into chat mode. The composer is enabled, the
   conversation pane is empty, status reads `Ready.` Type a prompt and click `Send`. Send
   resolves an ephemeral thread (`sandbox:"read-only"`, `approvalPolicy:"never"`,
   `ephemeral:true`) via Codex. The assistant reply renders in the conversation pane. Send
   alone requests one ChatGPT turn; tool calls are answered from the existing
   `AssistantTools` catalog.
5. `bgs_inspect_behavior` runs and renders a `BGS` row with the result (no approval needed,
   read-only).
6. `bgs.set_clip_animation` builds a preview only. The status line says so, the editor is not
   dirty, and the approval card with `Apply approved edit` / `Reject` appears.
7. `Reject` closes the card without applying anything. `Apply approved edit` runs the
   existing stale-revision recheck and, only if it still passes, commits through the normal
   editor undo stack. The editor becomes dirty, the disk file is unchanged, and an explicit
   `Save to .hkx` is still required.
8. `New chat` resets the visible transcript and rejects any pending approval. The next
   `thread/start` is a new ephemeral thread and carries no history.
9. `Sign out` calls `account/logout`. BGS clears its local session and the account row flips
   back to `Signed out`. Codex owns the persisted credential state, so a BGS restart cannot
   reintroduce it; only `Sign in with ChatGPT` reopens an authenticated session.

Automated gate results at `da200a74` on the existing branch:

- `dotnet build app/BehaviourStudio.csproj --configuration Release --no-restore` -- 0 errors.
- `dotnet run --project tools/symrm/symrm.csproj --configuration Release --no-restore -- test`
  -- 1798 checks, 0 failed.
- `dotnet test tools/tests/BehaviourGraph.Tests.csproj --configuration Release --no-restore`
  -- 692 passed, 0 failed, 0 skipped.
- `dotnet run --project tools/uismoke/uismoke.csproj --configuration Release --no-restore --`
  `--assistant` -- exit 0, every assistant smoke check green (full list above).

GitHub CI: both `BGS build and test (linux-x64)` and `BGS build and test (win-x64)` from
`g-A-R-D-E-N/GARDEN-TOOLS` action run `bgs-ci` were `SUCCESS` at this base commit; the push
from this UX pass is expected to reproduce them.

## 13. Manual acceptance boundary (human, not automatable)

Items the real ChatGPT subscription user has to do and verify themselves, because they require
a real human OAuth/login at the ChatGPT side and/or a real BGS window focus click:

1. Launch `BGS/app/BehaviourStudio.csproj` from the bin/Release/net8.0 build.
2. Visually confirm the `Chat` and `Assistant settings` buttons sit in the ChatGPT group on
   the Bridge, neither hidden nor disabled, with tooltips on hover.
3. Click `Assistant settings`. Confirm `ChatGPT via Codex · Codex CLI detected` (or
   `... not detected` with the actionable `Codex CLI was not found. Install Codex CLI, sign in
   with ChatGPT, then reopen the assistant.` message) and the `Signed out` account row.
4. Click `Sign in with ChatGPT`. The default browser opens at the verification URL produced
   by `account/login/start`. Finish the ChatGPT login in the browser.
5. Confirm the account row flips to `Connected · ChatGPT <plan>` and (when supplied)
   `Model: <model>`.
6. Click `Chat`. Type a harmless prompt. Click `Send`. Confirm the assistant reply renders.
7. Click an existing read tool (e.g. a graph object inspection). Confirm a `BGS` row appears
   with the result, and the editor stays unchanged.
8. Ask the assistant to set a clip animation. Confirm the approval card appears, the editor
   stays clean, and no Save is triggered. Click `Reject`. Confirm nothing changes. Repeat
   and click `Apply approved edit` -- the editor goes dirty, disk is unchanged, and explicit
   `Save to .hkx` is still required to write.
9. Click `New chat`. Confirm the conversation clears and any pending approval is removed.
10. Click `Sign out`. Confirm the account row flips to `Signed out`. Close BGS. Confirm no
    `codex app-server` child remains (Task Manager / `pgrep codex` / `Get-Process codex`).
11. Restart BGS. Confirm the account is still signed out (Codex owns the persisted
    credential; BGS does not reintroduce it until the user clicks `Sign in with ChatGPT`
    again).

The proof above covers every one of steps 1, 2, 3, 6, 7, 8, 9 against the production binary
through the assistant smoke, and the lifecycle (steps 4, 5, 10, 11) is the boundary Codex owns
on the user's behalf per design.

## 14. Multi-chat assistant history

A second pass added local persistence for BGS-managed conversations, the conversation list
inside the Assistant drawer, and per-thread tool dispatch so switching chats can never leak
late stream or tool events from the prior thread.

### 14.1 Persistence location and schema

Storage lives under the user's normal application-data directory, sibling to `templates\\` and
`settings.cfg`:

```text
<app-data>/BehaviourGraphStudio/assistant/chats/<chatId>.json
```

Where `<app-data>` is `Environment.GetFolderPath(ApplicationData)`. Storage is per-chat (one JSON
file per conversation), keyed by an opaque id, not by the user-visible title. The id is a
8-byte hex token generated via `RandomNumberGenerator.Fill` and validated by
`AssistantChatStore.IsValidId` (lowercase letters and digits only, length 1–64). The titles you
see in the sidebar come from inside each JSON file; titles cannot affect file paths and cannot
break containment.

Each chat file:

```jsonc
{
  "schemaVersion": 1,
  "id": "ab12cd34ef56ab78",
  "title": "How do I rename a clip?",
  "createdUtc": "2026-09-17T03:55:14Z",
  "updatedUtc": "2026-09-17T03:56:02Z",
  "messages": [
    { "role": "user",      "text": "Help me rename this clip",       "timestampUtc": "..." },
    { "role": "assistant", "text": "...",                              "timestampUtc": "..." },
    { "role": "tool",       "text": "bgs.inspect_behavior ran",       "timestampUtc": "...",
      "toolName": "bgs.inspect_behavior", "toolStatus": "ran" }
  ]
}
```

`schemaVersion` is the only thing that can refuse to read a future chat file. The current value
is `1`. A file with `schemaVersion > 1` is dropped silently (the load returns null and
enumeration skips it), keeping older BGS builds compatible with newer schemas.

The active chat id is stored under the existing non-secret settings key
`assistant.active_chat_id`, alongside `assistant.drawer_width` and the existing `assistant.*` keys.
This re-uses the existing settings mutex and atomic-write path so the active pointer is updated
exactly when a chat is selected, renamed, deleted, or created.

### 14.2 Atomic write, failure safety, and path containment

`AssistantChatStore.Save` follows the same pattern as `TemplateStore` and `Settings.WriteAll`:

1. Acquire the per-process `WriteGate` lock.
2. Acquire a named `Mutex` keyed on `BehaviourGraphStudio.AssistantChat.<sha256 of file path>`.
3. Stage the serialized bytes to `<id>.<guid>.tmp` with `FileOptions.WriteThrough` and
   `Flush(flushToDisk: true)`.
4. Atomically rename the staged file to the final path with `File.Move(_, final, overwrite: true)`.
5. On any failure, the staged `.tmp` file is removed in `finally` and the previous valid file on
   disk remains untouched.

`Save` also re-checks the serialized content before publishing it: it scans for forbidden
credential patterns (`access_token`, `refresh_token`, `id_token`, `authorization: bearer`,
`sk-…` api-key shapes, `auth.json` references) and refuses to persist if any of them appear.
This is defensive — the BGS code never produces them — but proves no credential material can
ride the pipeline if a future edit introduces one.

Path containment is enforced by `VerifyPathContained`: the resolved full file path must lie
inside `AssistantChatStore.Folder`. Test hooks (`Settings.SettingsPathForTest`,
`AssistantChatStore.Folder { get; set; }`) allow the uismoke harness to redirect the folder to
a temp directory so live runs never touch a real user chat store.

### 14.3 Bounds

Every limit is a `const` and enforced before any wire-bound code runs:

| Constant                     | Value          | Enforced where                                                    |
|------------------------------|----------------|------------------------------------------------------------------|
| `MaxConversations`           | 10             | `AssistantConversationList.Add` (extra chats are rejected)        |
| `MaxMessagesPerChat`         | 200            | `AssistantChatStore.Trim` (older messages dropped on save)        |
| `MaxPromptCharacters`        | 4 000          | Documented; the pane / controller already cap long input         |
| `MaxTotalFileSizeBytes`      | 1 048 576      | `Save` and `Load` refuse files above this ceiling                |
| `MaxReplayTurns`             | 8              | `AssistantConversationController.ComposeReplay` (replay-only)    |
| `ChatText.MaxMessageCharacters` | 8 000        | `ChatText.Cap` is applied to every persisted message              |
| `ChatText.MaxTitleLength`    | 80             | `ChatText.NormalizeTitle`; overflow adds an ellipsis             |

`AssistantChatStore.ReplayHistory(chat)` returns the chronological user/assistant pair trace
required to seed a restored thread, bounded to `MaxReplayTurns` and skipping tool entries
entirely so historical tools are not re-executed. The controller wraps this in a labelled
preamble it appends to the new user prompt before the session receives it.

### 14.4 Thread-lifetime discipline (BGS side)

BGS never serializes, accepts, or persists a Codex thread id. Each chat's first `Send` is
guaranteed to call `thread/start { ephemeral: true, sandbox: "read-only", approvalPolicy: "never",
… }` against the Codex server and receive a server-assigned thread id; that id lives only on
the in-memory `CodexAssistantSession` for as long as the chat is kept in the live process.
After a BGS restart, opening the same chat and pressing `Send` again triggers a brand-new
`thread/start` and seeds it with `AssistantConversationController.ComposeReplay(chat, prompt)`.

Restored conversation continuation therefore performs:

1. `AssistantChatStore.Load(activeChatId)` → `AssistantChat` with full chronological messages.
2. No `thread/start` and no `turn/start` happen yet; opening the chat just renders its transcript.
3. On `Send`, the controller builds the labelled replay (`MaxReplayTurns` user/assistant pairs
   in chronological order, `tool` entries omitted), starts a fresh ephemeral Codex thread, and
   appends the user's new text under `# Current message`.

`ClearHistory()` on `CodexAssistantSession` deliberately sets `_threadId = ""`; the next send
goes through `EnsureThreadAsync` again, which always issues `thread/start` because there is
no resume API on the protocol. The stored schema has no field for a Codex thread id.

### 14.5 Dispatch by threadId (Codex client side)

`CodexAppServerClient` no longer uses a single `ToolCallHandler` property. The shared,
connection-owned client now owns:

- `DefaultToolCallHandler` — a fallback used when no thread-specific handler is registered.
- `RegisterToolHandler(threadId, handler)` and `UnregisterToolHandler(threadId)` — the
  per-thread registry.
- `TryRouteToolCall(call, out handler)` — dispatches a server `item/tool/call` request by the
  `call.ThreadId` it carries; falls back to the default if the live session has already
  disposed.

`CodexAssistantSession` registers/unregisters on the live thread id in `Subscribe` and in the
new `Dispose` (which no longer disposes the connection). Because the connection survives every
session, multiple chats can hold N idle sessions alongside M active ones without confusing
the protocol boundary.

### 14.6 Pending-approval lifetime

Pending approval state is *not* persisted. `AssistantConversationEntry.MarkPendingApproval()`
and `ClearPendingApproval()` are the only writers; both are called from the controller's
`SendAsync` and from the manager's switch/NewChat/Delete/SignOut handlers. A historical
transcript may say that an approval occurred, but switching chats, New Chat, signing out, or
restarting BGS cannot recreate an actionable `bgs.set_clip_animation` approval.

### 14.7 UI shape

The Assistant drawer is now:

```text
+-------- Assistant -----------+----- ChatGPT via Codex · Codex CLI detected -----+
| Conversations       + New  | Signed out / Connected · ChatGPT <plan>          |
| ...                  Rename |                                                  |
| [Open] [Rename] [Delete]    | (chat title header)                              |
|                            |  ... conversation ...                              |
|                            |  [composer ...]                                    |
|                            |  [ Send ] [ Cancel ]                                |
|                            |  status text                                       |
+----------------------------+--------------------------------------------------+
```

Drawer width: the preference key `assistant.drawer_width` is reused with new bounds (default
760 px, range 520–1120 px). Sidebar width is fixed (220 px nominal). The drawer can
still collapse to close.

The sidebar lives inside the drawer, never as a separate window. New-chat is wired from both
the Pane header and the sidebar header; switching chats comes from each sidebar row's `Open`
button; rename is inline in a TextBox within each row (Enter commits, Escape cancels, blank
is rejected). Delete removes the chat after the entry is removed from the controller's list;
deleting the final remaining chat auto-creates an empty replacement so the list is never
empty.

### 14.8 Test results at `9d33006c` (the multi-chat commit)

Code lives in `BGS/app/Assistant*.cs` and `BGS/tools/{tests,uismoke}/*`. Gates run from
`BGS/`:

- `dotnet build app/BehaviourStudio.csproj --configuration Release --no-restore` → exit 0, 0 errors.
- `dotnet run --project tools/symrm/symrm.csproj --configuration Release --no-restore -- test` →
  exit 0, 1798 checks, 0 failed.
- `dotnet test tools/tests/BehaviourGraph.Tests.csproj --configuration Release --no-restore` →
  exit 0, 736 passed, 0 failed, 0 skipped (`49 Codex auth/protocol/tool tests` plus
  `25 AssistantChatStore tests`, `7 MultiChatIsolation tests`, and `12 MultiChatController tests`).
- `dotnet run --project tools/uismoke/uismoke.csproj --configuration Release --no-restore -- --assistant` →
  exit 0, every check green (full lifecycle incl. signed-out, send, approval, save boundary).
- `dotnet run --project tools/uismoke/uismoke.csproj --configuration Release --no-restore -- --multichat` →
  exit 0, every check green (sidebar, create, switch, restore, persistence).

GitHub CI: the `bgs-ci` workflow is the same as for prior proof; it runs the build, the
xUnit suite, and the UI smoke harness on both `linux-x64` and `win-x64`. Both jobs are
expected to be green at this commit (any regression must be fixed before push).

### 14.9 Manual acceptance boundary (real ChatGPT login)

Steps that require human OAuth on the user's account remain out of the automated proof, by
design (no chat with the user's ChatGPT can be exercised from CI):

1. Launch `BGS/app/BehaviourStudio.csproj`.
2. Visually confirm `Chat` and `Assistant settings` in the ChatGPT group on the Bridge.
3. Click `Assistant settings`. Confirm sidebar + provider + account + auth row.
4. Click `Sign in with ChatGPT`; finish the browser login.
5. Confirm account row flips to `Connected · ChatGPT <plan>` (and `Model: <model>` if supplied).
6. Click `Chat`. Type a prompt. `Send`. Confirm reply.
7. `+ New chat`. Type a different prompt. `Send`.
8. Switch between the two chats via the sidebar `Open` buttons; verify the transcript returns
   exactly and no model request is issued (the network tab shows only one
   `chatgpt/v1/responses` request per `Send`).
9. `Tools: bgs.inspect_behavior`. Confirm a `BGS` row appears and the editor stays unchanged.
10. Ask for `bgs.set_clip_animation`. Confirm the approval card appears, editor stays clean.
    Click `Reject`. Confirm nothing changed. Repeat and click `Apply approved edit` — editor goes
    dirty, disk unchanged, explicit `Save to .hkx` still required.
11. Switch chats again. Confirm no pending-approval carryover.
12. Close BGS. Reopen. Confirm both chats return with their exact transcripts.
13. Click `Sign out`. Confirm account flips to `Signed out` and the remaining credential state
    lives only in Codex.
14. Close BGS. Confirm no `codex app-server` child remains (`Get-Process codex | ?{
    $_.CommandLine -match 'app-server' }` on Windows, `pgrep -af 'codex app-server'` on Linux).

The harness covers everything except the browser OAuth round-trip and the live Codex child
count after BGS exit — both of which need the real signed-in account.


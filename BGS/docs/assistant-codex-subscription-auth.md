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
  exit 0 -- Build succeeded, 0 warnings, 0 errors

dotnet run --project tools/symrm/symrm.csproj --configuration Release --no-restore -- test
  exit 0 -- 1798 checks, 0 failed

dotnet test tools/tests/BehaviourGraph.Tests.csproj --configuration Release --no-restore
  exit 0 -- Failed: 0, Passed: 674, Skipped: 0, Total: 674
            (49 of these are the new Codex subscription/auth/protocol/tool tests)

dotnet test tools/tests/BehaviourGraph.Tests.csproj --configuration Release --no-restore \
  --filter "FullyQualifiedName~Codex"
  exit 0 -- Failed: 0, Passed: 49, Skipped: 0, Total: 49
```

`dotnet run --project tools/uismoke/uismoke.csproj --configuration Release --no-restore` was **not
completed** in this session. It was stopped on explicit instruction so the branch could be pushed.
Evidence recorded rather than hidden:

- `uismoke -- --assistant`, which is the assistant drawer lifecycle smoke, printed every one of its
  checks green (`assistant starts closed`, `assistant toggle exists once`, `assistant advertises the
  ChatGPT subscription boundary without a request`, `assistant starts signed out and offers sign in`,
  `opening assistant shows the drawer`, `opening assistant preserves active activity`,
  `opening assistant makes no provider request`, `opening assistant leaves the account signed out`,
  `send reaches the configured provider`, `conversation shows the user and assistant messages`,
  `completed request returns to ready`, `closing assistant restores the shell`) and then threw
  `Sequence contains no matching element` in its own `ActiveEditorApproval` fixture path resolution,
  which is a relative-fixture-path issue in that smoke's invocation, not an assistant regression.
- The full `uismoke` run printed its checks up to `the window has no Java setup control` and then did
  not advance in either attempt. No `FAIL` line was produced before the stall, and no assistant check
  failed.

Therefore this document does **not** claim the full uismoke gate passes. That gate still has to be
run to completion before the change is considered release-ready, and the stall above has to be
explained (it is upstream of every assistant assertion).

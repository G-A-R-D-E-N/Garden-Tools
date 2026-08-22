# Player controller (movement, input, camera) -- fully catalogued

RE pass on the player controller: `PlayerControls` and its 17 input handlers, the
`IMovementPlayerControls`/`IMovementPlayerControlsFilter` filter chain, `PlayerCamera` and its 13
camera states, `PlayerCharacter`'s movement surface, and `MovementControllerNPC`. Built from
CommonLibF4 header cross-referencing plus Ghidra decompiles against 1.10.163 (OG). Raw dump this
doc is built from: `Organized/1.10.163/player_controller_functions.csv` (663 rows).

Also on the OpenCommonwealth GitLab wiki (`Player-Controller-Notes` page) as a dev-facing version
without local paths.

## Architecture

Three pieces:

1. **Input handlers.** `PlayerControls` owns an array of `PlayerInputHandler*` and an array of
   `HeldStateHandler*`. 17 handler classes register at startup (`PlayerControls::RegisterHandlers`,
   RVA `0xf44cc0`), one per action (move, look, jump, sneak, run, sprint, toggle run, activate,
   attack/block, auto move, toggle POV, melee throw, grab rotation, ready weapon). Each one gets raw
   `ButtonEvent`/`ThumbstickEvent`/`MouseMoveEvent` through `BSInputEventUser` and writes into a
   shared `PlayerControlsData`.

2. **Movement filtering.** `PlayerControls::GetControllerOutput` (RVA `0xf44b50`) packages handler
   input into `PlayerControlsMovementData`. That goes through `IMovementPlayerControlsFilter`,
   which `PlayerCharacter` implements with 9 virtuals (`FilterControllerOutput`,
   `ShouldPlayLargeDeltas`, `IsInCombatStance`, `DampenRotationInput`, `DampenInput`,
   `IsInThirdPerson`, `FilterDampenedOutput`, `ForceWalkBeforeRun`,
   `GetNormalizedAngleAccelerationMultiplier`). This is where camera state, mount, dialogue, and
   race data actually change the movement output before animation/physics see it.

3. **Camera.** `PlayerCamera` runs a 13-state machine off `TESCameraState`
   (`Begin`/`End`/`Update`/`GetRotation`/`GetTranslation`/`SaveGame`/`LoadGame`/`Revert`). Also has
   its own Havok physics proxy (body id + shape) kept in sync every frame
   (`PlayerCamera::UpdateCamera`, RVA `0x1249690`), which is how the camera collides with geometry.

## Class reference

Has a CommonLibF4 header: `PlayerControls`, `PlayerInputHandler`, `PlayerControlsData`,
`PlayerControlsMovementData`, `IMovementPlayerControls`, `IMovementPlayerControlsFilter`,
`IMovementInterface`, `MovementData`, `PlayerCharacter`, `PlayerCamera`, `ThirdPersonState`,
`PowerArmor`, `SneakHandler`, `ReadyWeaponHandler`, `LookHandler`, `JumpHandler`, `AutoMoveHandler`,
`HeldStateHandler`, `AttackBlockHandler`, `MeleeThrowHandler`.

No header, forward-declared only in `PlayerControls.h`. Vtables mapped via
`Scripts/FindDumpVtable.java`, every constructor decompiled:

| Class | Base | Size | Own fields |
|---|---|---|---|
| `MovementHandler` | `PlayerInputHandler` | 0x20 | none |
| `ToggleRunHandler` | `PlayerInputHandler` | ~0x28 | 2-byte flag @ 0x20 |
| `ActivateHandler` | `HeldStateHandler` | ~0x38 | `BSTSmartPointer<BSInputEnableLayer>` @ 0x28, 4 bools @ 0x30-0x33, bool @ 0x34 |
| `RunHandler` | `HeldStateHandler` | ~0x30 | bool (always-run toggle) @ 0x28 |
| `SprintHandler` | `HeldStateHandler` | 0x28 | none |
| `TogglePOVHandler` | `HeldStateHandler` | ~0x38 | `BSTSmartPointer<BSInputEnableLayer>` @ 0x28, 2 bools @ 0x30-0x31 |
| `GrabRotationHandler` | `HeldStateHandler` | ~0x30 | byte (grab-type counter) @ 0x28 |
| `LoopingActivateHandler` | not a handler, `IHandlerFunctor<Actor, BSFixedString>` | -- | callback invoked by name through `ActivateHandler`'s dispatch table |

`HeldStateHandler`'s own fields (`heldStateActive`, `triggerReleaseEvent`) get cleared as one
2-byte write at 0x20 in every subclass constructor above -- that's "base fields", not something new.

13 camera states, all real names confirmed by grepping the
`Class::Class(Class*, TESCamera*, uint)` constructor signature and by decompiling
`PlayerCamera::SetCameraRoot`, which builds the whole array:

| `CameraStates` enum | Real class | Size | Ctor RVA |
|---|---|---|---|
| kFirstPerson | `FirstPersonState` | 0x88 | `0x124f3d0` |
| kAutoVanity | `AutoVanityState` | 0x30 | `0x124f320` |
| kVATS | `VATSCameraState` | 0x60 | `0x12564b0` |
| kFree | `FreeCameraState` | 0x58 | `0x124f450` |
| kIronSights | `IronSightsState` | 0x30 | `0x124f590` |
| kPCTransition | `PlayerCameraTransitionState` | 0x50 | `0x124f5c0` |
| kTween | `TweenMenuCameraState` | 0x58 | `0x1255cf0` |
| kAnimated | none | -- | -- |
| k3rdPerson | `ThirdPersonState` (has a header) | 0x138 | `0x1250ab0` |
| kFurniture | `FurnitureCameraState` | 0x158 | -- |
| kMount | `HorseCameraState` | 0x148 | `0x12476c0` |
| kBleedout | `BleedoutCameraState` | 0x1a0 | `0x124f350` |
| kDialogue | `DialogueCameraState` | 0x220 | `0x838fd0` |

`kAnimated` genuinely has no class. `PlayerCamera::SetCameraRoot` builds all 13 array slots
(`param_1 + 0xE0` through `+0x148`, 8 bytes per slot) in one function, and it just never writes
slot 7 -- goes straight from `TweenMenuCameraState` at slot 6 to `ThirdPersonState` at slot 8. Not a
naming gap, confirmed by decompile.

Inheritance is mixed, not all flat off `TESCameraState`:
- `HorseCameraState`, `DialogueCameraState`, and `BleedoutCameraState` all call
  `ThirdPersonState::ThirdPersonState` as their first line. `BleedoutCameraState`'s own fields
  start at 0x138 (a `NiMatrix3`, then a couple of floats, then some flag bytes).
- `FreeCameraState` inlines the base constructor itself (writes `camera` at 0x18 and `id` at 0x20,
  matching `TESCameraState`'s own header exactly) -- direct `TESCameraState` subclass. Own fields
  start at 0x28: a `NiPoint3`, then 4 float pairs (probably per-axis accel/max-speed for the fly
  cam).
- `FirstPersonState`, `AutoVanityState`, `VATSCameraState`, `IronSightsState`,
  `PlayerCameraTransitionState` get built inline in `SetCameraRoot` the same direct-`TESCameraState`
  way.
- `FurnitureCameraState` and `TweenMenuCameraState` are called as real separate constructors (not
  inlined) -- size confirmed, field-level layout not pulled apart in this pass.

## Decompiled algorithms

- **`MovementHandler::HandleEvent(ButtonEvent)`** RVA `0xf49c20`: Forward/Back/StrafeLeft/
  StrafeRight by UserEvent string match, sets move vector X/Y to +/-1. Literal WASD-to-vector.
- **`ActivateHandler::HandleNormalActivate`** RVA `0xf4aac0`: E-key decision tree -- grab/
  telekinesis vs activate-pick-ref vs companion command, plus the power-armor exit-clear check.
  This is the interception point 04-ROADMAP.md's open item #1 wanted.
- **`ActivateHandler::HandleLoopingActivate`** RVA `0xf4a7b0`: held loot/search interaction,
  pushes/pops an input-disable layer ("Activation Handler Disable") around the hold.
- **`TogglePOVHandler::HandleEvent`** RVA `0xf4a270`: first/third toggle, gated on workshop mode,
  scope overlay, dialogue/workshop menu. Manages its own "Toggle POV Input Layer".
- **`RunHandler::HandleEvent`** RVA `0xf4a0c0`: toggles always-run on a debounced hold-state flip.
- **`SprintHandler::HandleEvent`** RVA `0xf4a200`: stops sneaking, checks
  GetWantSprinting/GetMobilityCrippled, calls `PlayerCharacter::ToggleControllerSprint`.
- **`GrabRotationHandler::HandleEvent`** RVA `0xf49540`: cycles grab type (none/normal/telekinesis)
  on a Sprint tap, else rotates the grabbed object scaled by held input.
- **`PlayerCharacter::FilterControllerOutput`** RVA `0xec1700`: core per-frame movement filter.
  Zeroes rotation if camera disallows it, freezes movement during dialogue, snaps mount turning to
  cardinal-ish angles, computes the grab-facing-camera turn-in-place blend.
- **`PlayerCharacter::DampenInput`** RVA `0xec1b40`: accel/decel dampening from the race's
  `BGSMovementType` if present, else hardcoded per-mode fallbacks.
- **`PlayerControls::HandleMovementInput`** RVA `0xf44870`: builds the move-mode bitmask fed to
  `ActorState::DoSetMoveMode` -- run/walk, sneak, 8-direction locomotion blend from
  camera-heading-vs-movement angle, gamepad flag, reverse-direction anim trigger.
- **`PlayerCamera::UpdateCamera`** RVA `0x1249690`: main per-frame update -- dialogue cleanup,
  auto-vanity trigger, per-state update loop, base TESCamera update, Havok keyframe sync for the
  camera's physics body.
- **`PlayerCharacter::CreateMovementController`** RVA `0xebb7a0`: after the base movement
  controller is made, sets it move-to-high + controls-driven, registers `PlayerCharacter` itself as
  the filter callback.

## Every function on the 7 undocumented handlers

Every one has `Func1` at vtable slot 0 (standard destructor thunk) and `ShouldHandleEvent`
(input-enable-layer gate, checked before `HandleEvent` runs). Neither is on `PlayerInputHandler.h`
today even though every subclass has them -- that header's missing at least `ShouldHandleEvent`
alongside the `PerFrameUpdate` it lists.

- **MovementHandler** (6): ctor, dtor, `Func1`, `HandleEvent(ButtonEvent)` (decompiled),
  `HandleEvent(ThumbstickEvent)` (RVA `0xf49d60`, gamepad equivalent, same move vector, not
  separately decompiled), `ShouldHandleEvent`.
- **ActivateHandler** (9): ctor, dtor (releases the disable-layer pointer), `Func1`, `DisableInput`,
  `HandleEvent` (dispatches to `HandleNormalActivate`), `HandleLoopingActivate` (decompiled),
  `HandleNormalActivate` (decompiled), `ReenableInput`, `ShouldHandleEvent`.
- **RunHandler** (5): ctor, dtor, `Func1`, `HandleEvent` (decompiled), `ShouldHandleEvent`.
- **SprintHandler** (5): ctor, dtor, `Func1`, `HandleEvent` (decompiled), `ShouldHandleEvent`.
- **ToggleRunHandler** (5): ctor, dtor, `Func1`, `HandleEvent` (RVA `0xf4a510`, toggles run/walk as
  the default speed -- the always-on version of `RunHandler`'s held-button behavior),
  `ShouldHandleEvent`.
- **TogglePOVHandler** (6): ctor, dtor (releases disable layer), `Func1`, `DebounceTogglePOV`
  (helper called from `HandleEvent`), `HandleEvent` (decompiled), `ShouldHandleEvent`.
- **GrabRotationHandler** (5): ctor, dtor, `Func1`, `HandleEvent` (decompiled), `ShouldHandleEvent`.
- **LoopingActivateHandler** (4, not a `PlayerInputHandler`): ctor (no `PlayerControlsData&`,
  different hierarchy), dtor, `Func1`, `executeHandler` (the actual callback body).

## Every function on the 7 undocumented camera states

Shared lifecycle on all of these: `Begin`/`End`/`Update`/`GetRotation`/`GetTranslation`/`SaveGame`/
`LoadGame`/`Revert`, per `TESCameraState`'s own header. `Func1` is always the destructor thunk,
ctor always `(TESCamera*, uint)`.

**FirstPersonState** (30): ctor/dtor/`Func1` + 8 lifecycle methods + `ShouldHandleEvent` +
`HandleEvent` + `Init3D`/`Clear3D` (attach/detach arm-weapon and eye nodes) +
`GetCameraPitchFromNode` + `GetGeometryOffset` + sitting group (`UpdateSittingRotation`,
`Set3rdPersonSittingValues`, `QSittingRotation`, `SetSittingPitch`, `SetSittingRotation`) +
pitch-offset spring (`GetInitialPitchOffsetMult`, `CalculatePitchOffsetMult`, `UpdatePitchOffset`,
`CalculatePitchOffsetChaseValue`) + override flags (`QCameraOverride`, `SetCameraOverride`,
`SetCameraPitchOverride`) + `GetDampeningOffset`.

**AutoVanityState** (9): ctor/dtor/`Func1` + `Begin`/`End`/`Update`/`GetRotation`/`GetTranslation`
x2. Takes over 3rd person and slowly orbits after idle -- the trigger
`PlayerCamera::UpdateCamera` checks for.

**VATSCameraState** (10): ctor/dtor/`Func1` + `Begin`/`End`/`Update`/`GetRotation`/`GetTranslation`
+ `SetCameraRotation`/`SetCameraTranslation` (direct placement for VATS shot sequencing).

**FreeCameraState** (17): ctor/dtor/`Func1` + full 8-method lifecycle + `SetRotation`/
`SetTranslation` + `ShouldHandleEvent` + `HandleEvent(ButtonEvent)`/`HandleEvent(ThumbstickEvent)`
(independent fly-cam input, separate from the normal `PlayerControls` chain) + `UpdateMovement`
(the actual fly integration step).

**FurnitureCameraState** (15): ctor/dtor/`Func1` + `Begin`/`End`/`Update`/`SaveGame`/`LoadGame`/
`Revert` + `GetRotation` + `GetTranslation` x2 (plain, and with a rotation matrix) + `SetFurniture`
(assigns the reference to frame around) + `QIgnoreFurnitureEnter`/`SetIgnoreFurnitureEnter`
(suppresses the transition for scripted scenes).

**BleedoutCameraState** (10): ctor/dtor/`Func1` + `Begin`/`End`/`Update` + `GetRotationMtx`
(matrix instead of quaternion) + `GetTargetPos` + `UpdateRotation`/`UpdateTranslation` (per-frame
pose, called from `Update`).

**DialogueCameraState** (46, biggest by far): ctor/dtor/`Func1` + 8 lifecycle + input
(`ShouldHandleEvent`, `HandleEvent` x3 for Button/Thumbstick/MouseMove) + participant tracking
(`QSpeaker`/`SetSpeaker`, `QListener`/`SetListener`, `QPrimary`/`SetPrimary`, `QTarget`) + direct
placement (`SetCameraRotation`, `SetCameraTranslation`, `SetCameraTarget`,
`SetCameraLocationAndTarget`) + opening flags (`SetPlayOpening`, `SetEnteredFrom1stP`/
`QEnteredFrom1stP`, `SetAllow1stP`/`QAllow1stP`) + event camera playback (`PlayEventCamera` x2,
`QEventCamRef`) + auto shot selection (`UseNextCamera`, `FindNewCameraList`, `UpdateCamera`) + shot
lifetime (`ClearCurrentCamera`, `ClearData`, `CheckClearData` -- the per-frame cleanup
`PlayerCamera::UpdateCamera` calls) + `ForceBackToLastSpeaker` + `GetCurrentPlaybackTimer` +
`StartCameraImagespaceMod` + `UpdateNumCameraShootActions` + `UpdatePlayerInput` +
`UpdateActorsAndAction`.

## MovementControllerNPC

83 functions. `PlayerCharacter::CreateMovementController` (RVA `0xebb7a0`) wires the player into
this; shared with NPCs, so most of it is generic actor movement.

Frame-to-frame logic, decompiled: `ProcessMessages` (RVA `0xf298f0`) is the per-tick pump -- swaps
the pending message queue and warp-to-location queue under a spin lock, then runs each message
through `ProcessMessage` (RVA `0xf2b2a0`), which just forwards to a global
`ActorMovementMessageMap<16>` singleton keyed by message type (calls `Actor::ProcessMessage` on the
owning actor for anything that isn't a warp). So the actual per-message movement behavior lives in
that message map's handlers, not in `MovementControllerNPC` itself -- not chased further this pass.

Everything else, by role: lifecycle (ctor, dtor, `Init`, `AnyMessages`, `SaveGame`, `LoadGame`,
`FinishLoadGame`); idle integration (`SelectIdle`, `PlayIdle`, `StopIdle`, `FlushIdle`,
`IsWaitingForIdle`, `IsPlayingPathingInterruptibleIdle`); driven-mode flags, same ones
`CreateMovementController` sets (`SetMotionDriven`/`GetMotionDriven`, `SetAnimationDriven`/
`GetAnimationDriven`, `SetAnimationDrivenAllowRotation`/`GetAllowRotation`,
`SetAnimationDrivenMotionFeedbackAllowed`/`GetAnimationDrivenMotionFeedbackAllowed`,
`GetGraphDriven`, `SetAIDriven`/`GetAIDriven`, `SetControlsDriven`/`GetControlsDriven`,
`SetNodeFollowing`, `SetDirectControl`, `OnMoveToHigh`/`OnMoveToNotHigh`); movement state
(`SetSprinting`/`GetSprinting`, `SetCrippled`, `SetFlying`, `SetMovementDirection`,
`SetMovementSpeed`, `SetMovementRotationSpeed`, `RampToMovementDirection`, `RampToMovementSpeed`,
`RampToRotationSpeed`); direct-control and planner overrides (`ClearDirectControl`,
`SetPlannerDirectControl`, `SetTargetDirection`, `SetTargetSpeed`, `SetTargetAngle`,
`ClearPlannerDirectControl`, `SetStaticPathingState`, and the `DoSetXxx` family implementing all of
the above); message queue (`HasNewPathMessage`, `ClearPendingMessagesOfType`, `PostWarpToMessage`,
`PostMessageA`, `SendMessageA`); output (`GetLastAnimDeltas`, `GetRemainingDeltas`,
`GetGraphMovementDelta`, `GetGraphRotationDelta`).

## Everything closed out

- `kAnimated` -- confirmed no class, slot left empty in `PlayerCamera::SetCameraRoot`.
- Handler struct layout -- confirmed for all 7 (table above).
- Camera state inheritance and size -- confirmed for all 13 slots; field-level layout pulled for
  `BleedoutCameraState` and `FreeCameraState` specifically, sizes only for
  `FurnitureCameraState`/`TweenMenuCameraState`.
- `MovementControllerNPC` frame-to-frame logic -- confirmed it's a message-queue pump forwarding to
  `ActorMovementMessageMap`, not its own state machine.


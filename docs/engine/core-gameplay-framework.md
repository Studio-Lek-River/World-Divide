# Gameplay framework and Enhanced Input

## What it is

The gameplay framework is a fixed set of actor classes with fixed roles and fixed places in a networked game.
The rules live in `AGameModeBase`, shared match state in `AGameStateBase`, each player's will in `APlayerController`, each player's public data in `APlayerState`, the controlled body in `APawn` and the 2D overlay in `AHUD`.
The engine spawns them from the classes set on the game mode.
Enhanced Input maps keys to data-asset actions through mapping contexts that are added per local player.

## Key concepts

### Framework classes

- `UGameInstance`: one per running game, survives map travel. Holds cross-map state; extend with `UGameInstanceSubsystem`.
- `AGameModeBase`: **server only**.
  - Joining: `InitGame`, `PostLogin`, `HandleStartingNewPlayer`, `Logout`.
  - Spawning players: `ChoosePlayerStart`, `RestartPlayer`, `SpawnDefaultPawnFor`, `GetDefaultPawnClassForController`.
  - Class slots: `DefaultPawnClass`, `PlayerControllerClass`, `PlayerStateClass`, `GameStateClass`, `HUDClass`.
  - `AGameMode` adds a match-state machine (`MatchState`, `ReadyToStartMatch`, `StartMatch`).
- `AGameStateBase`: server and all clients. Replicated match state; `PlayerArray` lists every `APlayerState`. `AGameState` pairs with `AGameMode`.
- `APlayerController`: the server has one per player; each client has only its own.
  - Input (`SetupInputComponent()`), camera (`PlayerCameraManager`), UI and cursor (`bShowMouseCursor`, `SetInputMode(FInputModeGameAndUI())`), client/server RPCs.
  - `IsLocalController()`, `GetLocalPlayer()`, `OnPossess`/`OnUnPossess`, `GetHUD()`.
- `APlayerState`: server and all clients, one per player. Replicated per-player data everyone may see (name, team, score, faction). Survives pawn death.
- `APawn`: possessable body. `SetupPlayerInputComponent()`, `GetController()`. `ACharacter` adds a capsule, skeletal mesh and `UCharacterMovementComponent`. An RTS camera can be a pawn.
- `AController` / `AAIController`: the possessing side. AI controllers exist on the server only.
- `AHUD`: one per local player controller, on the owning machine only. `DrawHUD()` for canvas drawing; usually it just creates UMG/Slate widgets.

### Where things exist

| Class | Server | Owning client | Other clients |
|---|---|---|---|
| `AGameModeBase` | yes | no | no |
| `AGameStateBase` | yes | yes | yes |
| `APlayerController` | all players' | own only | no |
| `APlayerState` | all | all | all |
| `APawn` | all | if relevant | if relevant |
| `AHUD` and widgets | listen host's own only | own | no |

### Net mode and role

- `GetNetMode()`: `NM_Standalone`, `NM_DedicatedServer`, `NM_ListenServer`, `NM_Client`.
- `HasAuthority()`: true on the side that owns the actor (the server for replicated actors).
- `GetLocalRole()`: `ROLE_Authority`, `ROLE_AutonomousProxy` (the owning client's pawn/controller), `ROLE_SimulatedProxy`.
- Replication setup: `bReplicates`, `UPROPERTY(Replicated)`, `GetLifetimeReplicatedProps` + `DOREPLIFETIME`.

### Getters

- `UWorld::GetAuthGameMode()` (null on clients), `UWorld::GetGameState()`, `UWorld::GetFirstPlayerController()`.
- `UGameplayStatics::GetPlayerController(World, Index)`.
- `Controller->GetPlayerState<T>()`, `Controller->GetPawn()`.
- Game mode choice: per map (World Settings override) or `GlobalDefaultGameMode` in `[/Script/EngineSettings.GameMapsSettings]`; `GameModeClassAliases` for `?game=` URLs.

### Enhanced Input

- `UInputAction` (IA): one action and its `ValueType` (`EInputActionValueType::Boolean`, `Axis1D`, `Axis2D`, `Axis3D`).
- `UInputMappingContext` (IMC): maps keys to actions (`MapKey(Action, Key)`) with per-mapping triggers and modifiers.
- Triggers decide when an action fires: `UInputTriggerDown`, `UInputTriggerPressed`, `UInputTriggerReleased`, `UInputTriggerHold`, ...
- Modifiers change the value: `UInputModifierNegate`, `UInputModifierSwizzleAxis`, `UInputModifierDeadZone`, ...
- `UEnhancedInputLocalPlayerSubsystem` (per local player): `AddMappingContext(Context, Priority)`, `RemoveMappingContext(Context)`, `ClearAllMappings()`. Higher priority wins when contexts map the same key.
- `UEnhancedInputComponent::BindAction(Action, ETriggerEvent::Triggered, this, &AMyPC::Handler)`; the handler takes `const FInputActionValue&` and reads `Value.Get<FVector2D>()` (or `bool`, `float`, `FVector`).
- `ETriggerEvent`:
  - `Started`: evaluation began (key went down).
  - `Ongoing`: still evaluating (a hold not yet long enough).
  - `Triggered`: conditions met; fires every tick while they stay met.
  - `Completed`: stopped after having triggered.
  - `Canceled`: stopped before triggering.
- Config: in `[/Script/Engine.InputSettings]` (`DefaultInput.ini`) set `DefaultPlayerInputClass = /Script/EnhancedInput.EnhancedPlayerInput` and `DefaultInputComponentClass = /Script/EnhancedInput.EnhancedInputComponent`.

## Rules and pitfalls

1. Never read `GetAuthGameMode()` from client code; it is null there. Put shared state on the game state or player state.
2. Put per-player data that others must see on `APlayerState`, not the pawn (pawns die) and not the controller (other clients do not have it).
3. UI, HUD and input exist only on the owning machine. Guard with `IsLocalController()`; a listen host also has server-side controllers of remote players.
4. Gameplay decisions run where `HasAuthority()` is true; clients send requests through `Server` RPCs.
5. Add mapping contexts once the local player exists (in `BeginPlay` of a local controller, or in `SetupInputComponent`/`OnPossess` after checking `GetLocalPlayer()`). `ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(LP)` is null for a server-side controller of a remote player.
6. Bind with `ETriggerEvent::Triggered` for continuous input and `Started`/`Completed` for press/release. Binding `Triggered` with a `Down` trigger fires every frame the button is held.
7. Do not mix the legacy `BindAxis`/`BindAction(FName)` input with Enhanced Input in new code.
8. `Cast<UEnhancedInputComponent>(InputComponent)` fails if `DefaultInputComponentClass` is not set; use `CastChecked` so the mistake shows.
9. Do not draw shipping UI per frame with `DrawHUD` canvas calls; use widgets and update them on change.
10. Do not keep references to another player's `APlayerController` on a client; it does not exist there. Use `APlayerState`.
11. `BeginPlay` of the player controller can run before its pawn, player state or HUD exist on a client; react in `OnRep_Pawn`, `OnRep_PlayerState` or when the object arrives (`OnPossess` runs on the server only) instead of assuming order.

```cpp
if (ULocalPlayer* LP = GetLocalPlayer())
    if (auto* Input = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(LP))
        Input->AddMappingContext(MappingContext, /*Priority*/ 0);
```

## APIs, macros and commands

| Name | Use |
|---|---|
| `AGameModeBase::InitGame`, `PostLogin`, `Logout`, `HandleStartingNewPlayer`, `RestartPlayer` | Server-side join and spawn flow |
| `AGameStateBase::PlayerArray` | All player states, everywhere |
| `APlayerController::SetupInputComponent`, `IsLocalController`, `GetLocalPlayer`, `GetHUD` | Local player hooks |
| `APawn::SetupPlayerInputComponent` | Pawn-side input binding |
| `AHUD::DrawHUD`, `ClientSetHUD` | HUD |
| `UWorld::GetAuthGameMode`, `GetGameState`, `GetFirstPlayerController` | Framework getters |
| `GetNetMode`, `HasAuthority`, `GetLocalRole` | Where am I running |
| `UInputAction`, `UInputMappingContext::MapKey` | Define actions and key maps |
| `UEnhancedInputLocalPlayerSubsystem::AddMappingContext`, `RemoveMappingContext`, `ClearAllMappings` | Activate contexts |
| `UEnhancedInputComponent::BindAction`, `ETriggerEvent`, `FInputActionValue` | Bind handlers |
| `showdebug enhancedinput` | Console: live view of contexts, actions and values |

## In World Divide

- `ARTSGameMode`, `ARTSGameState`, `ARTSPlayerState`, `ARTSPlayerController`, `ARTSHUD` and `ARTSCameraPawn` live in `Source/WorldDivide/Core` and `Player`; the menu and lobby have their own game modes and controllers in `Menu/`.
- Input actions and `IMC_RTS` are built in code (`NewObject<UInputAction>`, `MapKey`) in `Player/RTSPlayerController.cpp`, not as assets.
- Multiplayer is listen-server (test `WorldDivide.Smoke.ListenServer`). Replication rules: [PERFORMANCE_GUIDELINES.md](../tech/PERFORMANCE_GUIDELINES.md) rules 27–28. Project docs win over this note on conflict.

## Sources

- https://dev.epicgames.com/documentation/en-us/unreal-engine/gameplay-framework-in-unreal-engine
- https://dev.epicgames.com/documentation/en-us/unreal-engine/game-mode-and-game-state-in-unreal-engine
- https://dev.epicgames.com/documentation/en-us/unreal-engine/player-controllers-in-unreal-engine
- https://dev.epicgames.com/documentation/en-us/unreal-engine/pawn-in-unreal-engine
- https://dev.epicgames.com/documentation/en-us/unreal-engine/enhanced-input-in-unreal-engine
- Engine source: `Runtime/Engine/Classes/GameFramework/` (`GameModeBase.h`, `GameStateBase.h`, `PlayerController.h`, `PlayerState.h`, `HUD.h`), `Engine/Plugins/EnhancedInput/Source/EnhancedInput/Public/`
- Epic course 115.01 (UE 5.5), paraphrased

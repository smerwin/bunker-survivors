// Simulation: bunker, defenders, zerg waves, economy, upgrades and abilities (port of client/js/game.js),
// plus the runtime-built world (lights, fog, ground) and code-driven effects.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BSTypes.h"
#include "BSFx.h"
#include "BSGame.generated.h"

class ABSModelActor;
class UBSAssets;
class UStaticMeshComponent;
class UMaterialInstanceDynamic;
class UPointLightComponent;
class UStaticMesh;
class UMaterialInterface;

enum class EBSMode : uint8 { Lane, Arena };

struct FBSInfantryUnit { const FBSInfantryDef* Def = nullptr; float Hp = 0, MaxHp = 0, Cd = 0; };

struct FBSBunker
{
	float Hp = BSDefs::BunkerHp, MaxHp = BSDefs::BunkerHp, Armor = BSDefs::BunkerArmor;
	int32 Slots = BSDefs::BunkerSlots;
	TArray<FBSInfantryUnit> Units;
	ABSModelActor* Actor = nullptr;
	bool bDead = false;
	float LastShot = -1, Flash = 0;
};

struct FBSTank
{
	enum EState : uint8 { Incoming, Unloading, Leaving, Ready } State = Incoming;
	float X = 0, Y = 0, Hp = BSDefs::TankHp, MaxHp = BSDefs::TankHp, Armor = BSDefs::TankArmor, Cd = 0, T = 0, MedT = 0, Flash = 0;
	ABSModelActor* Actor = nullptr;
	ABSModelActor* Medivac = nullptr;
	FVector Med;
	bool bDead = false;
};

struct FBSScv
{
	float X = 0, Y = 0, Hp = BSDefs::ScvHp, MaxHp = BSDefs::ScvHp, Flash = 0, Yaw = 0;
	FVector2D Idle;
	FString Anim;
	ABSModelActor* Actor = nullptr;
	bool bDead = false;
};

// A soldier standing outside the bunker (ejected, or recruited while it was full).
struct FBSTrooper
{
	const FBSInfantryDef* Def = nullptr;
	float X = 0, Y = 0, Yaw = -PI / 2, Hp = 0, MaxHp = 0, Cd = 0, Flash = 0, StandAt = 0;
	FVector2D Spot = FVector2D::ZeroVector;
	int32 Team = 0;                           // fire team (0 / 1): the teams take turns to bound forward
	float RetreatUntil = 0;                   // falling back level with the bunker after heavy losses
	FString Anim;
	ABSModelActor* Actor = nullptr;
	bool bBoarding = false, bDead = false;
};

// Missile turret (anti-air) or sensor tower (radar).
struct FBSStructure
{
	bool bSam = true, bReady = false, bDead = false;
	float X = 0, Y = 0, Yaw = PI / 2, Hp = 0, MaxHp = 0, Radius = 1, Cd = 0, T = 0, Flash = 0;
	ABSModelActor* Actor = nullptr;
};

struct FBSTarget
{
	enum EKind : uint8 { None, Bunker, Tank, Scv, Trooper, Structure } Kind = None;
	TWeakPtr<FBSTank> TankRef;
	TWeakPtr<FBSScv> ScvRef;
	TWeakPtr<FBSTrooper> TrooperRef;
	TWeakPtr<FBSStructure> StructureRef;
	float X = 0, Y = 0, Radius = 0;
	bool bStructure = false, bArmored = false, bLight = false;
	bool IsValid(const FBSBunker& B) const
	{
		switch (Kind)
		{
		case Bunker: return !B.bDead;
		case Tank: { auto P = TankRef.Pin(); return P && !P->bDead && P->State == FBSTank::Ready; }
		case Scv: { auto P = ScvRef.Pin(); return P && !P->bDead; }
		case Trooper: { auto P = TrooperRef.Pin(); return P && !P->bDead; }
		case Structure: { auto P = StructureRef.Pin(); return P && !P->bDead; }
		default: return false;
		}
	}
	bool Same(const FBSTarget& O) const { return Kind == O.Kind && TankRef == O.TankRef && ScvRef == O.ScvRef && TrooperRef == O.TrooperRef && StructureRef == O.StructureRef; }
};

struct FBSEnemy
{
	int32 Id = 0;
	const FBSZergDef* Def = nullptr;
	float X = 0, Y = 0, Z = 0, Yaw = 0, Hp = 0, MaxHp = 0;
	float Retarget = 0, Cd = 0, HitAt = -1, SlowUntil = 0, Radius = 0, Life = 0, BurrowT = 0, Sx = 0, Sy = 0, Flash = 0;
	bool bPendingHit = false, bBurrowed = false, bBurrowing = false, bAttacking = false;
	TArray<FVector2D> Route;                  // lane terrain: waypoints through the Y (or up a ramp to a ledge)
	bool bHighRoute = false, bPerched = false;
	FBSTarget Target, HitTarget;
	FString Anim, ModelKey, DeathKey;
	ABSModelActor* Actor = nullptr;
	bool Alive() const { return Hp > 0; }
};

// Air strike banshee: flies in to a hover post, fires on ground zerg below, then leaves.
struct FBSBanshee
{
	enum EState : uint8 { In, Hover, Out } State = In;
	FVector2D Post = FVector2D::ZeroVector;
	float X = 0, Y = 0, Z = 0, Yaw = 0, Cd = 0, T = 0, StandAt = 0;
	ABSModelActor* Actor = nullptr;
};

enum class EBSProjKind : uint8 { Shell, Grenade, Rocket, Acid, Spine, Bullet, Glaive, Spines, Brood, Yamato, Sam };

struct FBSProjectile
{
	EBSProjKind Kind = EBSProjKind::Shell;
	bool bFriendly = true;
	FVector From = FVector::ZeroVector, To = FVector::ZeroVector;   // sim metres (x, y, z)
	float T = 0, Dur = 0.3f, Dmg = 0, DmgArmored = 0, Arc = 0;
	TWeakPtr<FBSEnemy> TargetEnemy;
	FBSTarget TargetDef;
	TWeakPtr<FBSEnemy> Source;
	const FBSZergDef* ZDef = nullptr;
	const FBSInfantryDef* IDef = nullptr;
	UStaticMeshComponent* Visual = nullptr;
	UMaterialInstanceDynamic* Mid = nullptr;
	ABSModelActor* Model = nullptr;           // SC2 projectile mesh
	int32 Trail = 0;                          // SC2 trail effect handle
	FString ImpactFx;
	int32 Bounces = 0;                        // mutalisk glaive: remaining bounces
	TArray<FBSTarget> AlreadyHit;
};

// Reinforcements: a medivac flies in, lands behind the bunker, and the troops walk inside.
struct FBSWalker { const FBSInfantryDef* Def = nullptr; float X = 0, Y = 0, Yaw = 0, Delay = 0; ABSModelActor* Actor = nullptr; bool bWalking = false; };
struct FBSDrop
{
	enum EState : uint8 { Incoming, Unloading, Leaving } State = Incoming;
	FVector Med = FVector::ZeroVector;
	float T = 0, LandX = 0, LandY = 0;
	TArray<const FBSInfantryDef*> Cargo;
	ABSModelActor* Medivac = nullptr;
};

enum class EBSFxKind : uint8 { Tracer, Flash, Blast, Flame, Spines };

struct FBSFx
{
	EBSFxKind Kind = EBSFxKind::Blast;
	FVector A = FVector::ZeroVector, B = FVector::ZeroVector;      // sim metres
	float T = 0, Dur = 0.3f, Radius = 1, Intensity = 4;
	FLinearColor Color = FLinearColor::White;
	UStaticMeshComponent* Comp = nullptr;
	UMaterialInstanceDynamic* Mid = nullptr;
};

// A camera-facing sprite particle (sim metres). Sparks stretch along their velocity; flames use noise breakup.
struct FBSParticle
{
	FVector P = FVector::ZeroVector, V = FVector::ZeroVector;
	float Life = 0.2f, MaxLife = 0.2f, Delay = 0;
	float Size0 = 0.1f, Size1 = 0.1f, Stretch = 1, Drag = 0, Gravity = 0, Noise = 0, Seed = 0, Inten = 8;
	FLinearColor C0 = FLinearColor::White, C1 = FLinearColor::White, C2 = FLinearColor::White;
	bool bFlame = false;
};

struct FBSFlash { ABSModelActor* A = nullptr; FVector P = FVector::ZeroVector; float Yaw = 0, Delay = 0, Life = 0.1f; bool bShown = false; };
struct FBSFloater { FVector Pos; FString Text; FLinearColor Color; float T = 0; };
struct FBSCorpse { ABSModelActor* Actor = nullptr; float T = 0, Hold = 3.5f, X = 0, Y = 0, Z = 0, Yaw = 0; bool bSink = true, bVanish = false; };
struct FBSSpawn { float T = 0; FString Type; float Ang = 0; };

UCLASS()
class ABSGame : public AActor
{
	GENERATED_BODY()
public:
	ABSGame();
	virtual void Tick(float Dt) override;

	void Init(UBSAssets* InAssets, EBSMode InMode);
	void SetupAttract();                 // staged scene behind the start screen (no simulation)
	virtual void Destroyed() override;
	void BuildWorld(EBSMode InMode);

	// Player actions. Each returns true on success; failures post a toast.
	bool AddInfantry(const FString& Type, bool bFree = false, bool bInstant = false);
	void BeginOpening();                 // the starting fireteam walks into the bunker
	int32 PendingInfantry() const;
	bool AddScv(bool bFree = false);
	bool DropTank();
	bool CallAirStrike();
	bool UseStim();
	bool EmergencyRepair();
	bool BuyUpgrade(const FString& Id);
	bool FocusAt(float SimX, float SimY);
	bool Eject(int32 Index);              // send a bunker trooper out to stand behind it
	bool Board(int32 Index);              // walk an outside trooper back in (toggles)
	bool BuildStructure(bool bSam);       // missile turret or sensor tower
	bool TroopsFull() const { return Bunker.Units.Num() + Squad.Num() + PendingInfantry() >= Bunker.Slots + BSDefs::SquadMax; }
	int32 SamCount() const { int32 N = 0; for (auto& S : Structures) N += S->bSam; return N; }
	bool HasSensor() const { for (auto& S : Structures) if (!S->bSam) return true; return false; }
	bool SensorUp() const { for (auto& S : Structures) if (!S->bSam && S->bReady) return true; return false; }
	void Toast(const FString& Msg);

	// State read by the HUD and camera.
	EBSMode Mode = EBSMode::Lane;
	bool bStarted = false, bPaused = false, bOver = false, bHard = false;
	float Time = 0, Minerals = 150, Income = 2, Score = 0, Shake = 0;
	int32 Kills = 0, Wave = 0;
	float WaveTimer = 8, WaveLen = 30;
	int32 FinalScore = 0, Best = 0; bool bNewBest = false;
	FBSBunker Bunker;
	TArray<TSharedPtr<FBSTank>> Tanks;
	TArray<TSharedPtr<FBSScv>> Scvs;
	TArray<TSharedPtr<FBSEnemy>> Enemies;
	TArray<FBSBanshee> Banshees;
	TArray<TSharedPtr<FBSTrooper>> Squad;
	TArray<TSharedPtr<FBSStructure>> Structures;
	TArray<FBSFloater> Floaters;
	TWeakPtr<FBSEnemy> Focus; float FocusUntil = 0;
	struct FUp { bool bStim = false, bNeosteel = false, bMedivac = false, bHiSec = false, bRails = false; int32 Weapons = 0, Vehicle = 0, Plating = 0; } Up;
	struct FAb { float StimUntil = 0, StimCd = 0, StrikeCd = 0, RepairCd = 0, YamatoCd = 0; } Ab;
	bool UseYamato();
	int32 TankCost() const { return BSDefs::TankCost + BSDefs::TankCostStep * FMath::Max(0, Tanks.Num() - 1); }
	FString LastToast; int32 ToastSerial = 0;
	FString WaveBanner; float WaveBannerT = -10; int32 WaveSerial = 0;
	int32 TankMax = 2;
	float TimeScale = 1;
	bool bAutoPlay = false;
	// Automated test games: bot personality and per-wave telemetry (JSON lines).
	FString Bot = TEXT("balanced");
	FString TelemetryPath;
	FString EndReason;
	void WriteTelemetry(const FString& Kind);
	struct FTele { int32 PeakAlive = 0, Spawned = 0, KillsAtStart = 0, Purchases = 0, AbilityUses = 0; float MinHpPct = 1, Spent = 0, IdleTime = 0, Damage = 0, MineralSamples = 0, MineralSum = 0, WaveStart = 0; } Tele;
	int32 GamePeakAlive = 0; float FirstDamageAt = -1, TotalDamage = 0, TotalSpent = 0; int32 TotalPurchases = 0;
	UBSAssets* GetAssets() const { return Assets; }
	int32 CostOfUpgrade(const FString& Id) const;
	bool UpgradeAvailable(const FString& Id) const;
	int32 UpgradeLevel(const FString& Id) const;
	FString DebugState() const;
	void DebugSpawn(const FString& Type, float X, float Y) { const FVector2D At(X, Y); SpawnEnemy(Type, 0, &At); }

	// StarCraft II particle effects (BSFx.cpp). Pos in sim metres, Yaw is the sim facing. Returns a handle (0 if missing).
	int32 PlayFx(const FString& Key, const FVector& Pos, float Yaw = 0, float Scale = 1, float Delay = 0);
	void MoveFx(int32 Handle, const FVector& Pos, float Yaw);
	void StopFx(int32 Handle);

private:
	void Step(float Dt);
	void StartWave();
	TSharedPtr<FBSEnemy> SpawnEnemy(const FString& Type, float Ang, const FVector2D* At = nullptr);
	void PlayEnemyAnim(FBSEnemy& E, const FString& Kind);
	float EnemyCd(const FBSEnemy& E) const { return E.Def->Cd / (bHard ? 1.05f : 1.f); }
	void BuildTargets();
	void DamageDefender(const FBSTarget& T, float Dmg);
	void HurtEnemy(const TSharedPtr<FBSEnemy>& E, float Dmg);
	void KillEnemy(const TSharedPtr<FBSEnemy>& E, bool bSuicide);
	void BanelingBlast(const FBSEnemy& E, bool bFromDeath);
	TArray<TSharedPtr<FBSEnemy>> EnemiesInRange(float X, float Y, float Range, TFunctionRef<bool(const FBSEnemy&)> Pred) const;
	void Corpse(const FString& Key, float X, float Y, float Z, float Yaw, float Hold, float Scale = 1.f, bool bTerran = false);
	void Floater(float X, float Y, float Z, const FString& Text, const FLinearColor& C);
	void GameOver();
	void UpdateEnemies(float Dt);
	void UpdateBunker(float Dt);
	void UpdateTanks(float Dt);
	void UpdateScvs(float Dt);
	void UpdateBanshees(float Dt);
	void UpdateProjectiles(float Dt);
	void UpdateReinforcements(float Dt);
	void UpdateSquad(float Dt);
	void BuildTerrain();                      // the lane chokepoint's ground mesh (BSTerrain heights)
	bool StepGround(float& X, float& Y, float Mx, float My) const;
	void UpdateStructures(float Dt);
	TSharedPtr<FBSTrooper> JoinSquad(const FBSInfantryDef* Def, float Hp, float X, float Y, ABSModelActor* Actor);
	void KillTrooper(const FBSInfantryDef* Def, float X, float Y, float Yaw, ABSModelActor* Actor, bool bStim);
	void DestroyStructure(const TSharedPtr<FBSStructure>& S);
	FVector2D RoundBunker(float X, float Y, float Gx, float Gy) const;
	void FireInfantry(const FBSInfantryDef* D, const FVector& Muzzle, const TSharedPtr<FBSEnemy>& Target);
	void LaunchGlaive(const FVector& From, const FBSTarget& To, float Dmg, int32 Bounces, const TArray<FBSTarget>& Hit, const FBSZergDef* D);
	void AddProjectileModel(FBSProjectile& P, const TCHAR* Model, const TCHAR* Trail, float TrailScale = 1.f);
	void MuzzleMesh(const FVector& At, float Yaw, float Delay);
	FVector BunkerSlit(float Ang) const;          // the bunker's gun slit (Ref_Weapon bones) facing a direction
	TArray<FBSFlash> Flashes;
	ABSModelActor* StationMedivac = nullptr;   // Medivac on Station: hovers over the bunker
	FString StationAnim;
	float StationYaw = PI / 2, StationAnimT = 10;
	void YamatoImpact(const FVector& At, float Dmg);
	void UpdateVisuals(float Dt);
	void AutoPlay();
	void AutoPlayImpl();
	void PlaySound(const FString& Key, float Vol = 0.6f, float MinGap = 0.07f);

	// Pools.
	ABSModelActor* AcquireModel(const FString& Key);
	void ReleaseModel(ABSModelActor* A);
	UStaticMeshComponent* AcquireMesh(UStaticMesh* Mesh, UMaterialInterface* Mat, UMaterialInstanceDynamic*& OutMid);
	void ReleaseMesh(UStaticMeshComponent* C);
	void AddFx(EBSFxKind Kind, const FVector& A, const FVector& B, float Dur, float Radius, const FLinearColor& C, float Intensity = 4);
	void AddProjectileVisual(FBSProjectile& P, const FLinearColor& C, float Size);

	// Particles.
	FBSParticle& Emit();
	void EmitGunfire(const FVector& Muzzle, const FVector& Hit);
	void EmitFlame(const FVector& From, const FVector& To);
	void EmitExplosion(const FVector& At, float Radius, const FLinearColor& Hot, const FLinearColor& Cool, int32 Count);
	void EmitSparks(const FVector& At, const FVector& Dir, int32 Count, float Speed, float Spread, const FLinearColor& C, float Delay = 0);
	void UpdateParticles(float Dt);
	TArray<FBSParticle> Particles;
	void UpdateFx(float Dt);
	int32 FxBatch(const FString& Texture, int32 Blend);
	void SpawnSprite(int32 Handle, FBSFxInstance& I, int32 Emitter, const FBSFxEmitter& E, float T);
	TMap<int32, FBSFxInstance> FxInstances;
	TArray<FBSSprite> FxSprites;
	int32 NextFx = 1;
	UPROPERTY() TArray<TObjectPtr<class UInstancedStaticMeshComponent>> FxBatches;
	TMap<FString, int32> FxBatchIndex;
	UPROPERTY() TObjectPtr<class UInstancedStaticMeshComponent> Sprites;

	UPROPERTY() TObjectPtr<UBSAssets> Assets;
	UPROPERTY() TArray<TObjectPtr<ABSModelActor>> AllModels;
	TMap<FString, TArray<ABSModelActor*>> FreeModels;
	TMap<UStaticMesh*, TArray<UStaticMeshComponent*>> FreeMeshes;
	TMap<UStaticMeshComponent*, UMaterialInstanceDynamic*> MeshMids;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> AllMeshes;
	UPROPERTY() TObjectPtr<UPointLightComponent> MuzzleLight;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Ground;
	UPROPERTY() TObjectPtr<class UProceduralMeshComponent> Terrain;

	TArray<FBSTarget> Targets;
	TArray<FBSProjectile> Projectiles;
	TArray<FBSWalker> Walkers;
	TArray<FBSDrop> Drops;
	TArray<FBSFx> Fx;
	TArray<FBSCorpse> Corpses;
	TArray<FBSSpawn> SpawnQueue;
	float SpawnTimer = 0, Acc = 0, MuzzleFlash = 0, AutoT = 0;
	int32 NextId = 1;
	TArray<FVector2D> TankSlots, ScvIdle, SquadSpots, SamSlots, StrikePosts;
	TArray<FVector> SquadDeaths;              // where (x, y) and when (z) outside troopers fell, for morale
	int32 BoundTeam = 0, NextTeam = 0; float BoundT = 0;
	FVector2D SensorSlot = FVector2D::ZeroVector;
	TMap<FString, float> LastSound;
	FLinearColor TerranColor = FLinearColor(0.08f, 0.32f, 1.0f), ZergColor = FLinearColor(0.55f, 0.08f, 0.7f);
};

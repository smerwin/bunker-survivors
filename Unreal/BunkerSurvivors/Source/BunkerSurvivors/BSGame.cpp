#include "BSGame.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "BSAssets.h"
#include "BSModelActor.h"
#include "BSTerrain.h"
#include "ProceduralMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/SkeletalMesh.h"
#include "Components/PointLightComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/SkyLightComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/ConfigCacheIni.h"

namespace
{
	float Rnd(float A, float B) { return FMath::FRandRange(A, B); }
	float Dist2(float Ax, float Ay, float Bx, float By) { const float Dx = Ax - Bx, Dy = Ay - By; return Dx * Dx + Dy * Dy; }
	FVector W(const FVector& Sim) { return BSSpace::ToWorld(Sim.X, Sim.Y, Sim.Z); }
	const TArray<FString> WalkNames = { TEXT("walk"), TEXT("walk 01"), TEXT("fly") };
	const TArray<FString> StandNames = { TEXT("stand"), TEXT("stand 01") };
	// Extra yaw applied to the sieged tank's turret bone on top of its aim (SC2's model rests it in line with the hull).
	constexpr float TankTurretOffsetDeg = 0.f;
	// Brood queens: who Spawn Broodling goes for first (higher first; the bunker and buildings are never targets), and
	// what it does to a tank or mech (troopers and SCVs die outright).
	int32 BroodPriority(FBSTarget::EKind K)
	{
		switch (K) { case FBSTarget::Tank: return 4; case FBSTarget::Mech: return 3; case FBSTarget::Trooper: return 2; case FBSTarget::Scv: return 1; default: return 0; }
	}
	constexpr float BroodSpellDmg = 120.f;
}

ABSGame::ABSGame()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

// ---- World -------------------------------------------------------------------------------------------
void ABSGame::BuildWorld(EBSMode InMode)
{
	auto MakeComp = [this](auto* C) { C->SetupAttachment(GetRootComponent()); C->RegisterComponent(); return C; };

	UDirectionalLightComponent* Sun = MakeComp(NewObject<UDirectionalLightComponent>(this, TEXT("Sun")));
	Sun->SetMobility(EComponentMobility::Movable);
	Sun->SetWorldRotation(FRotator(-52, -35, 0));
	Sun->SetIntensity(3.6f);
	Sun->SetLightColor(FLinearColor(1.0f, 0.9f, 0.78f));
	Sun->SetDynamicShadowCascades(2);
	Sun->DynamicShadowDistanceMovableLight = 8000;
	Sun->SetCastShadows(true);
	Sun->ForwardShadingPriority = 1;

	// A cool, shadowless rim light from the zerg side; lower forward-shading priority than the sun.
	UDirectionalLightComponent* Fill = MakeComp(NewObject<UDirectionalLightComponent>(this, TEXT("Fill")));
	Fill->SetMobility(EComponentMobility::Movable);
	Fill->SetWorldRotation(FRotator(-25, 150, 0));
	Fill->SetIntensity(0.7f);
	Fill->SetLightColor(FLinearColor(0.45f, 0.55f, 1.0f));
	Fill->SetCastShadows(false);
	Fill->ForwardShadingPriority = 0;

	USkyLightComponent* Sky = MakeComp(NewObject<USkyLightComponent>(this, TEXT("Sky")));
	Sky->SetMobility(EComponentMobility::Movable);
	Sky->SourceType = ESkyLightSourceType::SLS_CapturedScene;
	Sky->SetIntensity(0.55f);
	Sky->SetLowerHemisphereColor(FLinearColor(0.08f, 0.06f, 0.1f));
	Sky->bLowerHemisphereIsBlack = false;

	UExponentialHeightFogComponent* Fog = MakeComp(NewObject<UExponentialHeightFogComponent>(this, TEXT("Fog")));
	Fog->SetFogDensity(0.035f);
	Fog->SetFogHeightFalloff(0.2f);
	Fog->SetStartDistance(InMode == EBSMode::Lane ? 2600.f : 2800.f);
	Fog->SetFogInscatteringColor(FLinearColor(0.035f, 0.025f, 0.05f));
	Fog->SetFogMaxOpacity(0.97f);

	UPostProcessComponent* Post = MakeComp(NewObject<UPostProcessComponent>(this, TEXT("Post")));
	Post->bUnbound = true;
	FPostProcessSettings& P = Post->Settings;
	P.bOverride_AutoExposureMethod = true; P.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;
	P.bOverride_AutoExposureApplyPhysicalCameraExposure = true; P.AutoExposureApplyPhysicalCameraExposure = false;
	P.bOverride_AutoExposureBias = true; P.AutoExposureBias = 0.0f;
	P.bOverride_BloomIntensity = true; P.BloomIntensity = 0.75f;
	P.bOverride_VignetteIntensity = true; P.VignetteIntensity = 0.55f;
	P.bOverride_ColorSaturation = true; P.ColorSaturation = FVector4(1.08f, 1.0f, 1.12f, 1.0f);
	P.bOverride_ColorContrast = true; P.ColorContrast = FVector4(1.08f, 1.08f, 1.08f, 1.0f);
	P.bOverride_SceneFringeIntensity = true; P.SceneFringeIntensity = 0.6f;

	Ground = MakeComp(NewObject<UStaticMeshComponent>(this, TEXT("Ground")));
	Ground->SetStaticMesh(Assets->Plane);
	Ground->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Ground->SetCastShadow(false);
	const float Size = InMode == EBSMode::Lane ? 90.f : 110.f;        // metres
	const FVector Centre = InMode == EBSMode::Lane ? BSSpace::ToWorld(0, 14, 0) : FVector::ZeroVector;
	Ground->SetWorldLocationAndRotation(Centre, FRotator::ZeroRotator);
	Ground->SetWorldScale3D(FVector(Size, Size, 1));
	if (Assets->GroundMaterial)
	{
		UMaterialInstanceDynamic* Mid = Ground->CreateAndSetMaterialInstanceDynamicFromMaterial(0, Assets->GroundMaterial);
		Mid->SetScalarParameterValue(TEXT("Tiling"), Size / 7.f);
	}
	BSTerrain::SetMode(InMode == EBSMode::Lane && Assets->TerrainMaterial);
	if (BSTerrain::Active())
	{
		BuildTerrain();
		Ground->SetVisibility(false);           // the terrain mesh is the ground; past its edges the fog takes over
	}

	Sprites = MakeComp(NewObject<UInstancedStaticMeshComponent>(this, TEXT("Sprites")));
	Sprites->SetStaticMesh(Assets->Plane);
	Sprites->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Sprites->SetCastShadow(false);
	Sprites->SetNumCustomDataFloats(6);
	if (Assets->ParticleMaterial) Sprites->SetMaterial(0, Assets->ParticleMaterial);

	MuzzleLight = MakeComp(NewObject<UPointLightComponent>(this, TEXT("Muzzle")));
	MuzzleLight->SetMobility(EComponentMobility::Movable);
	MuzzleLight->SetLightColor(FLinearColor(1.0f, 0.75f, 0.4f));
	MuzzleLight->SetAttenuationRadius(900);
	MuzzleLight->SetIntensity(0);
	MuzzleLight->SetCastShadows(false);
}

// The lane chokepoint: a height grid over BSTerrain with cliff faces textured by projection (see M_Terrain).
void ABSGame::BuildTerrain()
{
	Terrain = NewObject<UProceduralMeshComponent>(this, TEXT("Terrain"));
	Terrain->SetupAttachment(GetRootComponent());
	Terrain->RegisterComponent();
	Terrain->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Terrain->SetWorldLocation(FVector::ZeroVector);
	const float X0 = -30, Y0 = -20, Step = 0.3f;
	const int32 Nx = 201, Ny = 267;                        // to x = 30, y = 59.8
	TArray<float> Hg; Hg.SetNumUninitialized(Nx * Ny);
	for (int32 j = 0; j < Ny; j++) for (int32 i = 0; i < Nx; i++) Hg[j * Nx + i] = BSTerrain::Height(X0 + i * Step, Y0 + j * Step);
	TArray<FVector> V, N; TArray<FVector2D> Uv0, Uv1, Uv2, Uv3; TArray<FLinearColor> Col; TArray<int32> Tri; TArray<FProcMeshTangent> Tan;
	V.Reserve(Nx * Ny); N.Reserve(Nx * Ny); Uv0.Reserve(Nx * Ny); Uv1.Reserve(Nx * Ny); Uv2.Reserve(Nx * Ny); Col.Reserve(Nx * Ny);
	auto Smooth = [](float A, float B, float X) { const float T = FMath::Clamp((X - A) / (B - A), 0.f, 1.f); return T * T * (3 - 2 * T); };
	for (int32 j = 0; j < Ny; j++)
		for (int32 i = 0; i < Nx; i++)
		{
			const float X = X0 + i * Step, Y = Y0 + j * Step, Hh = Hg[j * Nx + i];
			const float Hx = (Hg[j * Nx + FMath::Min(i + 1, Nx - 1)] - Hg[j * Nx + FMath::Max(i - 1, 0)]) / (2 * Step);
			const float Hy = (Hg[FMath::Min(j + 1, Ny - 1) * Nx + i] - Hg[FMath::Max(j - 1, 0) * Nx + i]) / (2 * Step);
			const FVector Ns = FVector(-Hx, -Hy, 1).GetSafeNormal();                          // sim space
			V.Add(BSSpace::ToWorld(X, Y, Hh));
			N.Add(FVector(Ns.Y, Ns.X, Ns.Z));                                                // world axes (sim x/y swap)
			// Low ground at the foot of a cliff sits in shadow, which outlines the channel; plateau tops are a touch lighter.
			const float Sdf = BSTerrain::LowSdf(X, Y);
			const float Ao = Sdf < 0 ? 1 - 0.38f * FMath::Max(0.f, 1 + Sdf / 1.6f) : 1;
			const float Bright = Ao * (1 + 0.16f * Smooth(BSTerrain::H * 0.7f, BSTerrain::H, Hh)) / 1.25f;
			const float Wx = FMath::Abs(Ns.X) / (FMath::Abs(Ns.X) + FMath::Abs(Ns.Y) + 1e-4f);
			const float Cliff = Smooth(0.12f, 0.38f, 1 - Ns.Z);
			const float Vv = FMath::Clamp(1 - Hh / BSTerrain::H, 0.03f, 0.97f) * 0.92f;
			Uv0.Add(FVector2D(X / 6, Y / 6));
			Uv1.Add(FVector2D(Y * 0.22f, Vv));
			Uv2.Add(FVector2D(X * 0.22f, Vv));
			Col.Add(FLinearColor(Bright, 0, Wx, Cliff));
		}
	Tri.Reserve((Nx - 1) * (Ny - 1) * 6);
	for (int32 j = 0; j < Ny - 1; j++)
		for (int32 i = 0; i < Nx - 1; i++)
		{
			const int32 A = j * Nx + i, B = A + 1, C = A + Nx, D = C + 1;
			Tri.Append({ A, B, D, A, D, C });                    // front faces up (Unreal winds front faces clockwise)
		}
	Terrain->CreateMeshSection_LinearColor(0, V, Tri, N, Uv0, Uv1, Uv2, Uv3, Col, Tan, false, false);
	Terrain->SetMaterial(0, Assets->TerrainMaterial);
	Terrain->SetCastShadow(true);
}

// One step for a ground unit: straight on if it can, otherwise the nearest heading off the cliff face, so nothing
// hangs on a corner (port of Game.stepGround). Returns false when boxed in.
bool ABSGame::StepGround(float& X, float& Y, float Mx, float My) const
{
	if (BSTerrain::Walkable(X + Mx, Y + My)) { X += Mx; Y += My; return true; }
	for (const float A : { 0.5f, -0.5f, 1.0f, -1.0f, PI / 2, -PI / 2, 2.1f, -2.1f })
	{
		const float C = FMath::Cos(A), S = FMath::Sin(A), K = FMath::Max(0.35f, C), Rx = (Mx * C - My * S) * K, Ry = (Mx * S + My * C) * K;
		if (BSTerrain::Walkable(X + Rx, Y + Ry)) { X += Rx; Y += Ry; return true; }
	}
	// Wedged on a slope: step down it, back towards the low ground.
	const float E = 0.2f, Gx = BSTerrain::Height(X + E, Y) - BSTerrain::Height(X - E, Y), Gy = BSTerrain::Height(X, Y + E) - BSTerrain::Height(X, Y - E), Gl = FMath::Sqrt(Gx * Gx + Gy * Gy);
	if (Gl > 1e-4f) { const float L = FMath::Sqrt(Mx * Mx + My * My); X -= Gx / Gl * L; Y -= Gy / Gl * L; return true; }
	return false;
}

void ABSGame::Init(UBSAssets* InAssets, EBSMode InMode)
{
	Assets = InAssets;
	Mode = InMode;
	bHard = Mode == EBSMode::Arena;
	WaveLen = 30.f;
	if (Mode == EBSMode::Lane)
	{
		TankSlots = { FVector2D(-3.4f, -0.4f), FVector2D(3.4f, -0.4f), FVector2D(-5.4f, -2.8f), FVector2D(5.4f, -2.8f) };
		ScvIdle = { FVector2D(-2.6f, -2.9f), FVector2D(2.6f, -2.9f), FVector2D(-4.4f, -2.4f), FVector2D(4.4f, -2.4f) };
		TankMax = BSDefs::TankMaxLane;
		SamSlots = { FVector2D(-5.6f, 1.2f), FVector2D(5.6f, 1.2f), FVector2D(-3.6f, -4.6f) };
		SensorSlot = FVector2D(3.6f, -4.6f);
		StrikePosts = { FVector2D(-3.4f, 3.6f), FVector2D(3.4f, 3.6f), FVector2D(-1.4f, 6.4f), FVector2D(1.4f, 6.4f) };   // in view: covering both ledges and the lane
		// High-tech walkers: goliaths at the bunker's shoulders; the thors and the odin are too big to stand in the lane
		// without walling off the view, so they fire long-range from behind the bunker.
		MechSlots.Add(TEXT("goliath"), { FVector2D(-3.9f, 1.6f), FVector2D(3.9f, 1.6f), FVector2D(-2.3f, 2.3f), FVector2D(2.3f, 2.3f) });
		MechSlots.Add(TEXT("thor"), { FVector2D(-5.0f, -5.8f), FVector2D(5.0f, -5.8f) });
		MechSlots.Add(TEXT("odin"), { FVector2D(0, -10.5f) });
		// Forward base on the plateau east of the stem (see UpdateExpansion): the way up, the command center's site and the
		// SCV's spot beside it, the Odin, goliaths over the arm, and three ranks of troopers on the cliff edge over the stem.
		FwdRoute = BSTerrain::LedgeRoute(1);
		FwdPost = FVector2D(8.0f, 5.0f); FwdCc = FVector2D(9.8f, 2.6f); FwdScv = FVector2D(8.3f, 3.3f); FwdOdin = FVector2D(10.8f, 6.6f);
		FwdGoliath = { FVector2D(7.4f, 8.4f), FVector2D(8.4f, 9.2f), FVector2D(9.3f, 8.2f), FVector2D(8.4f, 7.4f) };
		const float Ranks[3][3] = { { 5.6f, 3.0f, 5 }, { 6.35f, 3.0f, 6 }, { 7.1f, 3.75f, 5 } };
		for (const auto& Rk : Ranks) for (int32 i = 0; i < (int32)Rk[2]; i++) FwdTroopers.Add(FVector2D(Rk[0], Rk[1] + i * 0.75f));
	}
	else
	{
		TankSlots = { FVector2D(-3.0f, -2.6f), FVector2D(3.0f, -2.6f), FVector2D(-3.0f, 2.6f), FVector2D(3.0f, 2.6f) };
		ScvIdle = { FVector2D(-2.6f, -2.6f), FVector2D(2.6f, 2.6f), FVector2D(-2.6f, 2.6f), FVector2D(2.6f, -2.6f) };
		TankMax = BSDefs::TankMaxArena;
		SamSlots = { FVector2D(0, 4), FVector2D(-4, 0), FVector2D(4, 0) };
		SensorSlot = FVector2D(-4.2f, -4.2f);
		MechSlots.Add(TEXT("goliath"), { FVector2D(0, 2.7f), FVector2D(2.7f, 0), FVector2D(-2.7f, 0), FVector2D(0, -2.7f) });
		MechSlots.Add(TEXT("thor"), { FVector2D(-4.6f, -3.6f), FVector2D(4.6f, 3.6f) });
		MechSlots.Add(TEXT("odin"), { FVector2D(-5.2f, 3.2f) });
		for (const float A : { 45.f, 135.f, 225.f, 315.f }) StrikePosts.Add(FVector2D(FMath::Cos(FMath::DegreesToRadians(A)) * 7, FMath::Sin(FMath::DegreesToRadians(A)) * 7));
	}
	// Ranks of eight behind the bunker (the side away from the lane), stepping round the tanks, SCVs, turrets and sensor
	// tower, as deep as it takes to fit the whole squad.
	TArray<FVector2D> Taken = TankSlots; Taken.Append(ScvIdle); Taken.Append(SamSlots); Taken.Add(SensorSlot);
	for (const auto& Pair : MechSlots) Taken.Append(Pair.Value);
	for (float Y = -2.3f; SquadSpots.Num() < BSDefs::SquadMax; Y -= 0.7f)
		for (int32 i = 0; i < 8 && SquadSpots.Num() < BSDefs::SquadMax; i++)
		{
			const FVector2D P(-2.625f + i * 0.75f, Y);
			if (!Taken.ContainsByPredicate([&P](const FVector2D& T) { return FVector2D::Distance(T, P) < 1.0f; })) SquadSpots.Add(P);
		}
	BuildWorld(Mode);
	InitCreep();
	Bunker.Actor = AcquireModel(TEXT("bunker"));
	if (Bunker.Actor) { Bunker.Actor->SetSim(0, 0, 0, PI / 2); Bunker.Actor->SetTeamColor(TerranColor); }
	AddScv(true);
	GConfig->GetInt(TEXT("BunkerSurvivors"), *(FString(TEXT("Best.")) + (Mode == EBSMode::Lane ? TEXT("Lane") : TEXT("Arena"))), Best, GGameUserSettingsIni);
}

void ABSGame::SetupAttract()
{
	// A dug-in position mid-fight: a sieged tank and SCV beside the bunker, the swarm gathering up the lane.
	TSharedPtr<FBSTank> T = MakeShared<FBSTank>();
	T->X = TankSlots[0].X; T->Y = TankSlots[0].Y; T->State = FBSTank::Ready;
	T->Actor = AcquireModel(TEXT("siegetank"));
	if (T->Actor)
	{
		T->Actor->SetTeamColor(TerranColor); T->Actor->SetSim(T->X, T->Y, 0, PI / 2); T->Actor->Play(TEXT("stand"), true);
		T->Actor->EnableTurret(TEXT("Ref_TurretZ"), TankTurretOffsetDeg);
		T->Actor->SetTurretAim(FMath::Atan2(14.5f - T->Y, -2.2f - T->X));     // tracking the ultralisk
	}
	Tanks.Add(T);
	struct FPose { const TCHAR* Key; float X, Y; const TCHAR* Anim; };
	const FPose Poses[] = {
		{ TEXT("zergling"), -1.5f, 7.f, TEXT("stand") }, { TEXT("zergling"), 0.8f, 7.6f, TEXT("fidget") }, { TEXT("zergling"), 2.4f, 8.4f, TEXT("stand 01") },
		{ TEXT("zergling"), -3.0f, 8.8f, TEXT("stand") }, { TEXT("hydralisk"), -0.4f, 10.5f, TEXT("stand") }, { TEXT("roach"), 2.8f, 11.2f, TEXT("stand") },
		{ TEXT("ultralisk"), -2.2f, 14.5f, TEXT("stand") }, { TEXT("baneling"), 1.2f, 9.6f, TEXT("stand") }, { TEXT("zerglingDeath"), 0.2f, 4.6f, TEXT("death") },
	};
	for (const FPose& P : Poses)
	{
		ABSModelActor* A = AcquireModel(P.Key);
		if (!A) continue;
		A->SetTeamColor(ZergColor);
		A->SetSim(P.X, P.Y, 0, FMath::Atan2(-P.Y, -P.X) + Rnd(-0.3f, 0.3f));
		if (FString(P.Anim) == TEXT("death")) { A->PlayRandomWithPrefix(TEXT("death"), false); A->SetRate(4.f); }
		else A->Play(P.Anim, true, Rnd(0.8f, 1.1f));
	}
}

void ABSGame::Destroyed()
{
	for (ABSModelActor* A : AllModels) if (IsValid(A)) A->Destroy();
	AllModels.Reset();
	Super::Destroyed();
}

// ---- Pools --------------------------------------------------------------------------------------------
ABSModelActor* ABSGame::AcquireModel(const FString& Key)
{
	const FBSModel* M = Assets->Model(Key);
	if (!M) { UE_LOG(LogTemp, Warning, TEXT("Missing model %s"), *Key); return nullptr; }
	TArray<ABSModelActor*>& Free = FreeModels.FindOrAdd(Key);
	ABSModelActor* A = Free.Num() ? Free.Pop() : nullptr;
	if (!A)
	{
		FActorSpawnParameters Sp; Sp.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		A = GetWorld()->SpawnActor<ABSModelActor>(FVector(0, 0, -100000), FRotator::ZeroRotator, Sp);
		A->SetModel(M);
		AllModels.Add(A);
	}
	A->Activate();
	return A;
}

void ABSGame::ReleaseModel(ABSModelActor* A)
{
	if (!A) return;
	A->Deactivate();
	if (A->GetModel()) FreeModels.FindOrAdd(A->GetModel()->Key).Add(A);
}

UStaticMeshComponent* ABSGame::AcquireMesh(UStaticMesh* Mesh, UMaterialInterface* Mat, UMaterialInstanceDynamic*& OutMid)
{
	// Each effect mesh is only ever used with one material, so the pool is keyed by mesh.
	TArray<UStaticMeshComponent*>& Free = FreeMeshes.FindOrAdd(Mesh);
	UStaticMeshComponent* C = Free.Num() ? Free.Pop() : nullptr;
	if (!C)
	{
		C = NewObject<UStaticMeshComponent>(this);
		C->SetStaticMesh(Mesh);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCastShadow(false);
		C->SetupAttachment(GetRootComponent());
		C->RegisterComponent();
		MeshMids.Add(C, C->CreateAndSetMaterialInstanceDynamicFromMaterial(0, Mat));
		AllMeshes.Add(C);
	}
	C->SetVisibility(true);
	OutMid = MeshMids[C];
	return C;
}

void ABSGame::ReleaseMesh(UStaticMeshComponent* C)
{
	if (!C) return;
	C->SetVisibility(false);
	FreeMeshes.FindOrAdd(C->GetStaticMesh()).Add(C);
}

void ABSGame::AddFx(EBSFxKind Kind, const FVector& A, const FVector& B, float Dur, float Radius, const FLinearColor& C, float Intensity)
{
	FBSFx F; F.Kind = Kind; F.A = A; F.B = B; F.Dur = Dur; F.Radius = Radius; F.Color = C; F.Intensity = Intensity;
	UStaticMesh* Mesh = Assets->Sphere;
	UMaterialInterface* Mat = Assets->FxMaterial;
	switch (Kind)
	{
	case EBSFxKind::Tracer: case EBSFxKind::Spines: Mesh = Assets->Cylinder; break;
	case EBSFxKind::Blast: Mesh = Assets->Plane; Mat = Assets->FxRingMaterial; break;
	case EBSFxKind::Flame: Mesh = Assets->Cone; break;
	default: break;
	}
	if (!Mesh || !Mat) return;
	F.Comp = AcquireMesh(Mesh, Mat, F.Mid);
	F.Mid->SetVectorParameterValue(TEXT("Color"), C);
	F.Mid->SetScalarParameterValue(TEXT("Intensity"), Intensity);
	F.Mid->SetScalarParameterValue(TEXT("Fade"), 1);
	Fx.Add(F);
}

void ABSGame::AddProjectileVisual(FBSProjectile& P, const FLinearColor& C, float Size)
{
	if (!Assets->Sphere || !Assets->FxMaterial) return;
	P.Visual = AcquireMesh(Assets->Sphere, Assets->FxMaterial, P.Mid);
	P.Mid->SetVectorParameterValue(TEXT("Color"), C);
	P.Mid->SetScalarParameterValue(TEXT("Intensity"), 8);
	P.Mid->SetScalarParameterValue(TEXT("Fade"), 1);
	P.Visual->SetWorldScale3D(FVector(Size));
	P.Visual->SetWorldLocation(W(P.From));
}

// ---- Particles -------------------------------------------------------------------------------------------
FBSParticle& ABSGame::Emit()
{
	if (Particles.Num() >= 1600) Particles.RemoveAtSwap(FMath::RandRange(0, Particles.Num() - 1), EAllowShrinking::No);
	FBSParticle& P = Particles.AddDefaulted_GetRef();
	P.Seed = FMath::FRand();
	return P;
}

void ABSGame::EmitSparks(const FVector& At, const FVector& Dir, int32 Count, float Speed, float Spread, const FLinearColor& C, float Delay)
{
	for (int32 i = 0; i < Count; i++)
	{
		FBSParticle& P = Emit();
		P.P = At; P.Delay = Delay;
		P.V = (Dir + FMath::VRand() * Spread).GetSafeNormal() * Speed * Rnd(0.5f, 1.2f) + FVector(0, 0, Rnd(0.5f, 2.f));
		P.Life = P.MaxLife = Rnd(0.12f, 0.28f);
		P.Size0 = Rnd(0.08f, 0.12f); P.Size1 = 0.03f; P.Stretch = 5;
		P.Drag = 2.5f; P.Gravity = -9.8f; P.Inten = 12;
		P.C0 = FLinearColor(1, 0.95f, 0.75f); P.C1 = C; P.C2 = C * FLinearColor(0.8f, 0.4f, 0.2f);
	}
}

void ABSGame::EmitGunfire(const FVector& Muzzle, const FVector& Hit)
{
	const FVector Dir = (Hit - Muzzle).GetSafeNormal();
	const float Dist = FVector::Dist(Hit, Muzzle);
	// Muzzle flash: a hot core plus a short forward-stretched blade.
	{
		FBSParticle& P = Emit();
		P.P = Muzzle; P.Life = P.MaxLife = 0.06f; P.Size0 = Rnd(0.7f, 0.95f); P.Size1 = 0.35f; P.Inten = 12; P.Noise = 0.45f;
		P.C0 = FLinearColor(1, 1, 0.85f); P.C1 = FLinearColor(1, 0.75f, 0.35f); P.C2 = FLinearColor(1, 0.45f, 0.1f);
	}
	{
		FBSParticle& P = Emit();
		P.P = Muzzle + Dir * 0.45f; P.V = Dir * 0.01f; P.Life = P.MaxLife = 0.05f; P.Size0 = 0.42f; P.Size1 = 0.22f; P.Stretch = 3.2f; P.Inten = 12;
		P.C0 = FLinearColor(1, 0.95f, 0.7f); P.C1 = FLinearColor(1, 0.65f, 0.25f); P.C2 = P.C1;
	}
	// A few hot casings / sparks kicked out of the slit.
	EmitSparks(Muzzle, Dir, 3, 7.f, 0.45f, FLinearColor(1, 0.7f, 0.3f));
	// Gauss slug: a fast bright streak that travels to the target.
	{
		const float Speed = 85.f;
		FBSParticle& P = Emit();
		P.P = Muzzle + Dir * 0.4f; P.V = Dir * Speed; P.Life = P.MaxLife = FMath::Max(0.02f, (Dist - 0.4f) / Speed);
		P.Size0 = P.Size1 = 0.14f; P.Stretch = 12; P.Inten = 12;
		P.C0 = FLinearColor(1, 0.97f, 0.8f); P.C1 = FLinearColor(1, 0.85f, 0.5f); P.C2 = P.C1;
	}
	// Impact: sparks thrown back off the carapace when the slug lands.
	const float Delay = Dist / 85.f;
	EmitSparks(Hit, -Dir + FVector(0, 0, 0.6f), 4, 5.f, 0.9f, FLinearColor(1, 0.6f, 0.2f), Delay);
	FBSParticle& F = Emit();
	F.P = Hit; F.Delay = Delay; F.Life = F.MaxLife = 0.08f; F.Size0 = 0.6f; F.Size1 = 0.2f; F.Inten = 8;
	F.C0 = FLinearColor(1, 0.85f, 0.55f); F.C1 = FLinearColor(1, 0.5f, 0.15f); F.C2 = F.C1;
}

void ABSGame::EmitFlame(const FVector& From, const FVector& To)
{
	const FVector Dir = (To - From).GetSafeNormal();
	const float Dist = FMath::Max(0.5f, FVector::Dist(To, From));
	// Roiling flame puffs, emitted over ~0.35 s: they start small and white-hot at the nozzle, swell and cool to deep red.
	for (int32 i = 0; i < 30; i++)
	{
		FBSParticle& P = Emit();
		const float Speed = Rnd(8.f, 12.f);
		P.P = From + FMath::VRand() * 0.05f; P.Delay = i * 0.012f;
		P.V = (Dir + FMath::VRand() * 0.12f).GetSafeNormal() * Speed + FVector(0, 0, Rnd(0.2f, 1.f));
		P.Life = P.MaxLife = FMath::Clamp(Dist / Speed * Rnd(1.0f, 1.5f), 0.15f, 0.6f);
		P.Size0 = Rnd(0.2f, 0.32f); P.Size1 = Rnd(1.2f, 1.7f);
		P.Drag = 1.6f; P.Gravity = 3.f; P.Noise = 1.f; P.Inten = 3.6f; P.bFlame = true;
		P.C0 = FLinearColor(1, 0.92f, 0.65f); P.C1 = FLinearColor(1, 0.42f, 0.07f); P.C2 = FLinearColor(0.55f, 0.07f, 0.02f);
	}
	// Bright blue-white core right at the igniter.
	for (int32 i = 0; i < 6; i++)
	{
		FBSParticle& P = Emit();
		P.P = From; P.Delay = i * 0.05f; P.V = Dir * 3.f;
		P.Life = P.MaxLife = 0.1f; P.Size0 = 0.35f; P.Size1 = 0.55f; P.Inten = 5; P.Noise = 0.5f;
		P.C0 = FLinearColor(0.7f, 0.85f, 1); P.C1 = FLinearColor(1, 0.85f, 0.5f); P.C2 = P.C1;
	}
	// Embers drifting up from the burning target.
	for (int32 i = 0; i < 8; i++)
	{
		FBSParticle& P = Emit();
		P.P = To + FVector(Rnd(-0.6f, 0.6f), Rnd(-0.6f, 0.6f), Rnd(0, 0.4f)); P.Delay = Dist / 11.f + Rnd(0, 0.3f);
		P.V = FVector(Rnd(-0.6f, 0.6f), Rnd(-0.6f, 0.6f), Rnd(1.f, 2.5f));
		P.Life = P.MaxLife = Rnd(0.5f, 1.0f); P.Size0 = 0.1f; P.Size1 = 0.04f; P.Stretch = 2; P.Drag = 0.8f; P.Gravity = 0.8f; P.Inten = 9;
		P.C0 = FLinearColor(1, 0.8f, 0.4f); P.C1 = FLinearColor(1, 0.4f, 0.08f); P.C2 = FLinearColor(0.6f, 0.1f, 0.02f);
	}
}

void ABSGame::EmitExplosion(const FVector& At, float Radius, const FLinearColor& Hot, const FLinearColor& Cool, int32 Count)
{
	for (int32 i = 0; i < Count; i++)
	{
		FBSParticle& P = Emit();
		FVector D = FMath::VRand(); D.Z = FMath::Abs(D.Z) * 0.8f + 0.2f;
		P.P = At; P.V = D * Radius * Rnd(2.f, 4.f);
		P.Life = P.MaxLife = Rnd(0.35f, 0.7f); P.Size0 = Radius * 0.3f; P.Size1 = Radius * Rnd(0.7f, 1.0f);
		P.Drag = 3.f; P.Gravity = 1.5f; P.Noise = 1.f; P.Inten = 1.8f; P.bFlame = true;
		P.C0 = Hot; P.C1 = FMath::Lerp(Hot, Cool, 0.5f); P.C2 = Cool * 0.6f;
	}
	EmitSparks(At, FVector(0, 0, 1), Count, Radius * 7.f, 1.2f, Hot);
}

void ABSGame::UpdateParticles(float Dt)
{
	if (!Sprites) return;
	FVector CamLoc = FVector::ZeroVector;
	if (APlayerCameraManager* Cam = UGameplayStatics::GetPlayerCameraManager(this, 0)) CamLoc = Cam->GetCameraLocation();

	TArray<FTransform> Xf;
	Xf.Reserve(Particles.Num());
	TArray<float> Data;
	Data.Reserve(Particles.Num() * 6);
	for (int32 i = Particles.Num() - 1; i >= 0; i--)
	{
		FBSParticle& P = Particles[i];
		if (P.Delay > 0) { P.Delay -= Dt; continue; }
		P.Life -= Dt;
		if (P.Life <= 0) { Particles.RemoveAtSwap(i, EAllowShrinking::No); continue; }
		P.V *= FMath::Exp(-P.Drag * Dt);
		P.V.Z += P.Gravity * Dt;
		P.P += P.V * Dt;
	}
	for (const FBSParticle& P : Particles)
	{
		const bool bLive = P.Delay <= 0;
		const float T = 1.f - FMath::Clamp(P.Life / P.MaxLife, 0.f, 1.f);
		const FVector W = BSSpace::ToWorld(P.P.X, P.P.Y, P.P.Z);
		const FVector ToCam = (CamLoc - W).GetSafeNormal();
		const FVector Vw(P.V.Y, P.V.X, P.V.Z);
		FVector Along = Vw - ToCam * FVector::DotProduct(Vw, ToCam);
		const float Speed = Along.Size();
		if (Speed < 1e-3f) Along = FVector::CrossProduct(ToCam, FVector::UpVector);
		const FRotator Rot = FRotationMatrix::MakeFromZX(ToCam, Along.GetSafeNormal()).Rotator();
		const float Size = FMath::Lerp(P.Size0, P.Size1, T);
		const float Stretch = P.Stretch > 1 ? FMath::Max(1.f, P.Stretch * FMath::Min(1.f, Speed / 8.f)) : 1.f;
		Xf.Add(FTransform(Rot, W, bLive ? FVector(Size * Stretch, Size, 1) : FVector(0.0001f)));
		const FLinearColor C = T < 0.5f ? FMath::Lerp(P.C0, P.C1, T * 2) : FMath::Lerp(P.C1, P.C2, (T - 0.5f) * 2);
		// Flames bloom in quickly then fade; sparks just fade.
		const float A = P.bFlame ? FMath::Min(1.f, T * 6.f) * (1 - T) * 1.6f : (1 - T * T);
		Data.Append({ C.R, C.G, C.B, bLive ? P.Inten * A : 0.f, P.Noise, P.Seed });
	}
	const int32 N = Xf.Num();
	while (Sprites->GetInstanceCount() < N) Sprites->AddInstance(FTransform(FRotator::ZeroRotator, FVector::ZeroVector, FVector(0.0001f)), true);
	for (int32 i = N; i < Sprites->GetInstanceCount(); i++) { Xf.Add(FTransform(FRotator::ZeroRotator, FVector(0, 0, -100000), FVector(0.0001f))); Data.Append({ 0, 0, 0, 0, 0, 0 }); }
	if (Xf.Num()) Sprites->BatchUpdateInstancesTransforms(0, Xf, true, false, true);
	for (int32 i = 0; i < Xf.Num(); i++) Sprites->SetCustomData(i, TArrayView<const float>(Data.GetData() + i * 6, 6), false);
	Sprites->MarkRenderStateDirty();
}

void ABSGame::PlaySound(const FString& Key, float Vol, float MinGap)
{
	const float Now = GetWorld()->GetRealTimeSeconds();
	float& Last = LastSound.FindOrAdd(Key, -10.f);
	if (Now - Last < MinGap) return;
	Last = Now;
	if (USoundBase* S = Assets->Sound(Key)) UGameplayStatics::PlaySound2D(this, S, Vol, Rnd(0.96f, 1.04f));
}

void ABSGame::Toast(const FString& Msg) { LastToast = Msg; ToastSerial++; }
void ABSGame::Floater(float X, float Y, float Z, const FString& Text, const FLinearColor& C) { Floaters.Add({ FVector(X, Y, Z), Text, C, 0 }); }

// ---- Player actions -------------------------------------------------------------------------------------
bool ABSGame::AddInfantry(const FString& Type, bool bFree, bool bInstant)
{
	const FBSInfantryDef* D = BSDefs::FindInfantry(Type);
	if (!D) return false;
	if (TroopsFull()) { Toast(TEXT("No room for more troops")); return false; }
	if (!bFree) { const int32 Cost = TroopCost(D); if (Minerals < Cost) { Toast(TEXT("Not enough minerals")); return false; } Minerals -= Cost; }
	if (bInstant || !Assets->Model(TEXT("medivac")))
	{
		if (Bunker.Units.Num() >= Bunker.Slots) { JoinSquad(D, TrooperHp(D), BSDefs::BunkerHalfSize + 0.3f, 0.4f, nullptr); return true; }
		Bunker.Units.Add({ D, TrooperHp(D), TrooperHp(D), Rnd(0, 0.3f) });
		if (Bunker.Actor) Bunker.Actor->Play(TEXT("stand work"), true);
		return true;
	}
	// Ride along with a medivac that is still on its way in, or call a new one.
	for (FBSDrop& Dr : Drops) if (Dr.State == FBSDrop::Incoming && Dr.Cargo.Num() < 4) { Dr.Cargo.Add(D); return true; }
	FBSDrop Dr;
	Dr.LandX = (FMath::RandBool() ? 1 : -1) * Rnd(2.2f, 2.7f); Dr.LandY = Rnd(2.6f, 3.2f);
	Dr.Med = FVector(Dr.LandX, Mode == EBSMode::Lane ? -18.f : -30.f, 6.f);
	Dr.Cargo.Add(D);
	Dr.Medivac = AcquireModel(TEXT("medivac"));
	if (Dr.Medivac) { Dr.Medivac->SetTeamColor(TerranColor); Dr.Medivac->SetSim(Dr.Med.X, Dr.Med.Y, Dr.Med.Z, PI / 2); Dr.Medivac->Play(WalkNames, true); }
	Drops.Add(Dr);
	Toast(TEXT("Reinforcements inbound"));
	return true;
}

int32 ABSGame::PendingInfantry() const
{
	int32 N = Walkers.Num();
	for (const FBSDrop& Dr : Drops) N += Dr.Cargo.Num();
	return N;
}

void ABSGame::BeginOpening()
{
	// Three marines double-time it into the bunker while the first wave gathers.
	const FBSInfantryDef* M = BSDefs::FindInfantry(TEXT("marine"));
	for (int32 i = 0; i < 3; i++)
	{
		FBSWalker W; W.Def = M; W.X = -3.3f + i * 0.25f; W.Y = 3.0f + i * 0.9f; W.Yaw = -PI / 4; W.Delay = 0.5f + i * 0.45f;
		W.Actor = AcquireModel(TEXT("marine"));
		if (W.Actor) { W.Actor->SetTeamColor(TerranColor); W.Actor->SetSim(W.X, W.Y, 0, W.Yaw); W.Actor->Play(StandNames, true); }
		if (!W.Actor) { Bunker.Units.Add({ M, M->Hp, M->Hp, Rnd(0, 0.3f) }); continue; }
		Walkers.Add(W);
	}
	// Hyperion perks (bots play with none): War Chest, Reinforced Bunker, and a Veteran Fireteam firebat comes along.
	if (bAutoPlay) return;
	Minerals += 50 * BSMeta::Perk(TEXT("minerals"));
	Bunker.MaxHp = Bunker.Hp = BSDefs::BunkerHp + 50 * BSMeta::Perk(TEXT("hull"));
	if (BSMeta::Perk(TEXT("firebat"))) AddInfantry(TEXT("firebat"), true);
}

void ABSGame::UpdateReinforcements(float Dt)
{
	for (int32 i = Drops.Num() - 1; i >= 0; i--)
	{
		FBSDrop& Dr = Drops[i];
		Dr.T += Dt;
		if (Dr.State == FBSDrop::Incoming)
		{
			const FVector Goal(Dr.LandX, Dr.LandY, 1.6f);
			const FVector D = Goal - Dr.Med;
			if (D.Size() > 0.25f && Dr.T < 8) Dr.Med += D.GetSafeNormal() * FMath::Min(D.Size(), 11.f * Dt);
			else { Dr.State = FBSDrop::Unloading; Dr.T = 0; if (Dr.Medivac) Dr.Medivac->Play(TEXT("stand work start"), false); PlaySound(BSSounds::MedivacUnload, 0.5f); }
			if (Dr.Medivac) Dr.Medivac->SetSim(Dr.Med.X, Dr.Med.Y, Dr.Med.Z, PI / 2);
		}
		else if (Dr.State == FBSDrop::Unloading)
		{
			if (Dr.T > 0.45f && Dr.Cargo.Num())
			{
				Dr.T = 0.15f;
				const FBSInfantryDef* D = Dr.Cargo[0];
				Dr.Cargo.RemoveAt(0);
				FBSWalker W; W.Def = D; W.X = Dr.LandX + Rnd(-0.3f, 0.3f); W.Y = Dr.LandY + Rnd(-0.2f, 0.2f); W.Yaw = PI / 2;
				W.Actor = AcquireModel(D->Key);
				if (W.Actor) { W.Actor->SetTeamColor(TerranColor); W.Actor->SetSim(W.X, W.Y, 0, W.Yaw); }
				if (W.Actor) Walkers.Add(W);
				else { Bunker.Units.Add({ D, TrooperHp(D), TrooperHp(D), Rnd(0, 0.3f) }); }
			}
			else if (!Dr.Cargo.Num() && Dr.T > 0.5f)
			{
				Dr.State = FBSDrop::Leaving; Dr.T = 0;
				if (Dr.Medivac) { Dr.Medivac->Play(TEXT("stand work end"), false); }
			}
		}
		else
		{
			Dr.Med += FVector(0, -11 * Dt, 2.5f * Dt);
			if (Dr.Medivac) { if (Dr.T > 0.4f) Dr.Medivac->Play(WalkNames, true); Dr.Medivac->SetSim(Dr.Med.X, Dr.Med.Y, Dr.Med.Z, -PI / 2); }
			if (Dr.T > 2.8f) { ReleaseModel(Dr.Medivac); Drops.RemoveAt(i); }
		}
	}
	// Troops on foot head for the nearest side hatch of the bunker and climb in.
	for (int32 i = Walkers.Num() - 1; i >= 0; i--)
	{
		FBSWalker& W = Walkers[i];
		if (W.Delay > 0) { W.Delay -= Dt; continue; }
		// Bunker full: fall in behind it instead.
		int32 Heading = 0;
		for (const FBSWalker& O : Walkers) if (&O != &W && O.Delay <= 0) Heading++;
		if (!Bunker.bDead && Bunker.Units.Num() + Heading >= Bunker.Slots && Squad.Num() < BSDefs::SquadMax)
		{
			JoinSquad(W.Def, TrooperHp(W.Def), W.X, W.Y, W.Actor);
			Walkers.RemoveAt(i);
			continue;
		}
		const FVector2D Door(W.X < 0 ? -BSDefs::BunkerHalfSize - 0.1f : BSDefs::BunkerHalfSize + 0.1f, 0.4f);
		const float Dx = Door.X - W.X, Dy = Door.Y - W.Y, Dd = FMath::Sqrt(Dx * Dx + Dy * Dy);
		if (Dd < 0.3f || Bunker.bDead)
		{
			if (!Bunker.bDead && Bunker.Units.Num() < Bunker.Slots)
			{
				Bunker.Units.Add({ W.Def, TrooperHp(W.Def), TrooperHp(W.Def), Rnd(0, 0.3f) });
				if (Bunker.Actor) Bunker.Actor->Play(TEXT("stand work"), true);
				Floater(0, 0, 3, TEXT("+") + W.Def->Name, FLinearColor(0.5f, 0.85f, 1));
				PlaySound(BSSounds::MedivacUnload, 0.25f, 0.3f);
			}
			ReleaseModel(W.Actor);
			Walkers.RemoveAt(i);
			continue;
		}
		const float Sp = FMath::Min(Dd, 3.15f * Dt);
		W.X += Dx / Dd * Sp; W.Y += Dy / Dd * Sp; W.Yaw = FMath::Atan2(Dy, Dx);
		if (W.Actor)
		{
			if (!W.bWalking) { W.bWalking = true; W.Actor->Play(WalkNames, true, 1.2f); }
			W.Actor->SetSim(W.X, W.Y, 0, W.Yaw);
		}
	}
}

// ---- Squad behind the bunker ------------------------------------------------------------------------
TSharedPtr<FBSTrooper> ABSGame::JoinSquad(const FBSInfantryDef* Def, float Hp, float X, float Y, ABSModelActor* Actor)
{
	const FVector2D* Spot = SquadSpots.FindByPredicate([this](const FVector2D& S) { return !Squad.ContainsByPredicate([&S](const TSharedPtr<FBSTrooper>& M) { return M->Spot == S; }); });
	if (!Spot) { ReleaseModel(Actor); return nullptr; }
	TSharedPtr<FBSTrooper> M = MakeShared<FBSTrooper>();
	M->Def = Def; M->Hp = Hp; M->MaxHp = TrooperHp(Def); M->Cd = Rnd(0, 0.3f); M->X = X; M->Y = Y; M->Spot = *Spot;
	M->Team = NextTeam; NextTeam ^= 1;        // alternate fire teams
	M->Actor = Actor ? Actor : AcquireModel(Def->Key);
	if (M->Actor) { M->Actor->SetTeamColor(TerranColor); M->Actor->SetSim(X, Y, 0, M->Yaw); }
	Squad.Add(M);
	return M;
}

// Send a trooper out of the bunker's side hatch to stand behind it.
bool ABSGame::Eject(int32 Index)
{
	if (Bunker.bDead || !Bunker.Units.IsValidIndex(Index)) return false;
	if (Squad.Num() >= BSDefs::SquadMax) { Toast(TEXT("No room behind the bunker")); return false; }
	const FBSInfantryUnit U = Bunker.Units[Index];
	Bunker.Units.RemoveAt(Index);
	int32 Left = 0;
	for (const TSharedPtr<FBSTrooper>& M : Squad) Left += M->X < 0;
	const float Side = Left <= Squad.Num() - Left ? -1.f : 1.f;
	if (TSharedPtr<FBSTrooper> M = JoinSquad(U.Def, U.Hp, Side * (BSDefs::BunkerHalfSize + 0.3f), 0.4f, nullptr)) M->Cd = U.Cd;
	if (!Bunker.Units.Num() && Bunker.Actor) Bunker.Actor->Play(StandNames, true);
	Floater(0, 0, 3, U.Def->Name + TEXT(" out"), FLinearColor(0.5f, 0.85f, 1));
	return true;
}

bool ABSGame::Board(int32 Index)
{
	if (!Squad.IsValidIndex(Index) || Squad[Index]->bDead) return false;
	if (Squad[Index]->Fwd.bOn) { Toast(TEXT("Holding the high ground")); return false; }
	if (Bunker.Units.Num() >= Bunker.Slots) { Toast(TEXT("Bunker is full")); return false; }
	Squad[Index]->bBoarding = !Squad[Index]->bBoarding;
	return true;
}

// Troopers path round the bunker: straight to the goal if the way is clear, else via the corner that makes the
// shortest clear route.
FVector2D ABSGame::RoundBunker(float X, float Y, float Gx, float Gy) const
{
	const float B = BSDefs::BunkerHalfSize + 0.25f, C = B + 0.3f;
	auto Hits = [B](float Ax, float Ay, float Bx, float By)   // segment against the box (slab test)
	{
		float T0 = 0, T1 = 1;
		const float Dx = Bx - Ax, Dy = By - Ay;
		const float Ps[4] = { -Dx, Dx, -Dy, Dy }, Qs[4] = { Ax + B, B - Ax, Ay + B, B - Ay };
		for (int32 k = 0; k < 4; k++)
		{
			if (FMath::Abs(Ps[k]) < 1e-9f) { if (Qs[k] < 0) return false; continue; }
			const float R = Qs[k] / Ps[k];
			if (Ps[k] < 0) { if (R > T1) return false; if (R > T0) T0 = R; }
			else { if (R < T0) return false; if (R < T1) T1 = R; }
		}
		return T0 <= T1;
	};
	if ((FMath::Abs(X) < B && FMath::Abs(Y) < B) || !Hits(X, Y, Gx, Gy)) return FVector2D(Gx, Gy);
	FVector2D Nearest(Gx, Gy); float BestLen = 1e9f;
	for (const FVector2D Cn : { FVector2D(C, C), FVector2D(-C, C), FVector2D(C, -C), FVector2D(-C, -C) })
	{
		if (Hits(X, Y, Cn.X, Cn.Y)) continue;
		const float L = FMath::Sqrt(Dist2(X, Y, Cn.X, Cn.Y)) + FMath::Sqrt(Dist2(Cn.X, Cn.Y, Gx, Gy)) + (Hits(Cn.X, Cn.Y, Gx, Gy) ? 2 * C : 0);
		if (L < BestLen) { BestLen = L; Nearest = Cn; }
	}
	return Nearest;
}

void ABSGame::UpdateSquad(float Dt)
{
	const float Hs = BSDefs::BunkerHalfSize;
	const bool bStim = Ab.StimUntil > Time;
	// Keep a little elbow room so troopers converging on one spot do not stack. Anyone standing on their own spot is
	// already spaced out and lets others through the ranks (on the high ground, its spot is its post there).
	auto Home = [](const FBSTrooper& M) { const FVector2D& S = M.Fwd.bOn && !M.Fwd.bBack ? M.Fwd.Post : M.Spot; return FMath::Abs(M.X - S.X) + FMath::Abs(M.Y - S.Y) < 0.3f; };
	for (int32 i = 0; i < Squad.Num(); i++)
		for (int32 j = i + 1; j < Squad.Num(); j++)
		{
			FBSTrooper& A = *Squad[i]; FBSTrooper& B = *Squad[j];
			if (Home(A) || Home(B)) continue;
			const float Dx = B.X - A.X, Dy = B.Y - A.Y, D2 = Dx * Dx + Dy * Dy;
			if (D2 < 0.49f && D2 > 1e-6f) { const float D = FMath::Sqrt(D2), Push = (0.7f - D) / D * 0.25f; A.X -= Dx * Push; A.Y -= Dy * Push; B.X += Dx * Push; B.Y += Dy * Push; }
		}
	// Fire and movement: when the squad advances, its two fire teams take turns - one bounds forward while the other
	// holds and gives covering fire.
	BoundT += Dt;
	if (BoundT > 1.4f) { BoundT = 0; BoundTeam ^= 1; }
	const bool bBothTeams = Squad.ContainsByPredicate([](const TSharedPtr<FBSTrooper>& M) { return M->Team == 0; })
		&& Squad.ContainsByPredicate([](const TSharedPtr<FBSTrooper>& M) { return M->Team == 1; });
	SquadDeaths.RemoveAll([this](const FVector& K) { return Time - K.Z > 6; });
	const bool bLane = Mode == EBSMode::Lane;
	auto Forward = [bLane](const FBSTrooper& M) { return bLane ? M.Y > 0.6f : FMath::Sqrt(M.X * M.X + M.Y * M.Y) > 3.2f; };   // ahead of the bunker's line
	TArray<TSharedPtr<FBSTrooper>> Snapshot = Squad;
	for (const TSharedPtr<FBSTrooper>& MP : Snapshot)
	{
		FBSTrooper& M = *MP;
		if (M.bDead) continue;
		const FBSInfantryDef* D = M.Def;
		auto Pred = [D](const FBSEnemy& E) { return !(D->bGroundOnly && E.Def->bFlying); };
		const float Range = D->Range + (M.Fwd.bOn && BSTerrain::OnHigh(M.X, M.Y) ? 1.f : 0.f);   // the forward post on the high ground outranges the lane
		// Morale: when a third of the group around it has just been killed, the group falls back level with the bunker.
		if (!M.bBoarding && !M.Fwd.bOn && !bStim && M.RetreatUntil <= Time && Forward(M))
		{
			int32 Lost = 0, Near = 0;
			for (const FVector& K : SquadDeaths) Lost += Dist2(K.X, K.Y, M.X, M.Y) < 3.5f * 3.5f;
			for (const TSharedPtr<FBSTrooper>& O : Squad) Near += !O->bDead && Dist2(O->X, O->Y, M.X, M.Y) < 3.5f * 3.5f;
			if (Lost >= 2 && Lost * 3 >= Lost + Near)
			{
				for (const TSharedPtr<FBSTrooper>& O : Squad) if (!O->bDead && Dist2(O->X, O->Y, M.X, M.Y) < 3.5f * 3.5f) O->RetreatUntil = Time + 8;
				Toast(TEXT("Squad falling back"));
			}
		}
		float Gx = M.Spot.X, Gy = M.Spot.Y;
		FVector2D Fg = FVector2D::ZeroVector; bool bFinal = false;
		if (M.bBoarding) { Gx = (M.X < 0 ? -1 : 1) * (Hs + 0.3f); Gy = 0.4f; }
		else if (M.Fwd.bOn && !FwdGoal(M.Fwd, M.X, M.Y, Fg, bFinal)) { M.Fwd.bOn = false; M.Fwd.bBack = false; }   // back down in the stem: home to its spot
		else if (M.Fwd.bOn)
		{
			// Expedition to the high ground (UpdateExpansion): the fire teams bound along the route, one covering while the
			// other moves; at the post they hold and fire.
			if (bBothTeams && M.Team != BoundTeam && EnemiesInRange(M.X, M.Y, Range, Pred).Num()) { Gx = M.X; Gy = M.Y; }
			else { Gx = Fg.X; Gy = Fg.Y; }
		}
		else if (M.RetreatUntil > Time && !bStim)
		{
			if (!Forward(M)) { Gx = M.X; Gy = M.Y; }                                   // back on the line: hold and fire
			else if (bLane) { Gx = FMath::Clamp(M.X, -6.f, 6.f); Gx = (Gx < 0 ? -1 : 1) * FMath::Max(FMath::Abs(Gx), Hs + 0.8f); Gy = 0.4f; }
			else { const float A = FMath::Atan2(M.Y, M.X); Gx = FMath::Cos(A) * 2.8f; Gy = FMath::Sin(A) * 2.8f; }
		}
		else if (EnemiesInRange(M.X, M.Y, Range, Pred).Num()) { Gx = M.X; Gy = M.Y; }   // hold and fire
		else if (bStim)
		{
			// Stimmed: the whole squad charges the nearest zerg until it is in range, no bounding, no falling back.
			const FBSEnemy* Th = nullptr; float Nearest = 1e9f;
			for (const TSharedPtr<FBSEnemy>& E : Enemies)
			{
				if (!E->Alive() || !Pred(*E) || Dist2(0, 0, E->X, E->Y) > 24 * 24) continue;
				const float Dd = Dist2(M.X, M.Y, E->X, E->Y);
				if (Dd < Nearest) { Nearest = Dd; Th = E.Get(); }
			}
			if (Th)
			{
				const float A = FMath::Atan2(M.Y - Th->Y, M.X - Th->X), R = D->Range * 0.85f + Th->Radius;
				Gx = Th->X + FMath::Cos(A) * R; Gy = Th->Y + FMath::Sin(A) * R;
			}
		}
		else
		{
			// Advance on the nearest zerg that is attacking our position from beyond our reach.
			const FBSEnemy* Th = nullptr; float Nearest = 1e9f;
			for (const TSharedPtr<FBSEnemy>& E : Enemies)
			{
				if (!E->Alive() || !E->bAttacking || !Pred(*E) || Dist2(0, 0, E->X, E->Y) > 14 * 14) continue;
				const float Dd = Dist2(M.X, M.Y, E->X, E->Y);
				if (Dd < Nearest) { Nearest = Dd; Th = E.Get(); }
			}
			if (Th && bBothTeams && M.Team != BoundTeam) { Gx = M.X; Gy = M.Y; }        // covering while the other team bounds
			else if (Th)
			{
				const float A = FMath::Atan2(M.Y - Th->Y, M.X - Th->X), R = D->Range * 0.8f + Th->Radius;
				Gx = Th->X + FMath::Cos(A) * R; Gy = Th->Y + FMath::Sin(A) * R;
			}
		}
		const float Far = FMath::Sqrt(Dist2(M.X, M.Y, Gx, Gy));
		const FVector2D Wp = RoundBunker(M.X, M.Y, Gx, Gy);
		const float Dx = Wp.X - M.X, Dy = Wp.Y - M.Y, Dd = FMath::Max(0.001f, FMath::Sqrt(Dx * Dx + Dy * Dy));
		if (Far > 0.12f)
		{
			const float Sp = FMath::Min(Dd, 3.15f * (bStim ? 1.5f : 1.f) * Dt), Ox = M.X, Oy = M.Y;
			StepGround(M.X, M.Y, Dx / Dd * Sp, Dy / Dd * Sp);
			if (M.X != Ox || M.Y != Oy) M.Yaw = FMath::Atan2(M.Y - Oy, M.X - Ox);
			if (M.Actor && M.Anim != TEXT("walk")) { M.Anim = TEXT("walk"); M.Actor->Play(WalkNames, true, bStim ? 1.8f : 1.2f); }
			if (M.Actor) M.Actor->SetSim(M.X, M.Y, BSTerrain::Height(M.X, M.Y), M.Yaw);
			continue;
		}
		if (M.bBoarding)
		{
			M.bBoarding = false;
			if (!Bunker.bDead && Bunker.Units.Num() < Bunker.Slots)
			{
				Bunker.Units.Add({ D, M.Hp, M.MaxHp, M.Cd });
				if (Bunker.Actor) Bunker.Actor->Play(TEXT("stand work"), true);
				ReleaseModel(M.Actor); M.Actor = nullptr; M.bDead = true;
				Squad.Remove(MP);
				Floater(0, 0, 3, TEXT("+") + D->Name, FLinearColor(0.5f, 0.85f, 1));
			}
			else Toast(TEXT("Bunker is full"));
			continue;
		}
		// Holding position: fire at whatever comes into (unboosted) range.
		M.Cd -= Dt * (bStim ? 1.5f : 1.f);
		if (M.Cd <= 0)
		{
			TSharedPtr<FBSEnemy> Target;
			if (TSharedPtr<FBSEnemy> F = Focus.Pin())
				if (F->Alive() && Pred(*F) && FMath::Sqrt(Dist2(M.X, M.Y, F->X, F->Y)) - F->Radius <= Range) Target = F;
			if (!Target) { TArray<TSharedPtr<FBSEnemy>> L = EnemiesInRange(M.X, M.Y, Range, Pred); if (L.Num()) Target = L[0]; }
			if (Target)
			{
				M.Cd += D->Cd;
				M.Yaw = FMath::Atan2(Target->Y - M.Y, Target->X - M.X);
				FireInfantry(D, FVector(M.X + FMath::Cos(M.Yaw) * 0.45f, M.Y + FMath::Sin(M.Yaw) * 0.45f, BSTerrain::Height(M.X, M.Y) + 0.85f), Target, M.Fwd.bOn);
				const float Len = M.Actor ? M.Actor->Play(TEXT("attack"), false, bStim ? 2.1f : 1.4f) : 0.f;
				M.Anim = TEXT("attack"); M.StandAt = Time + FMath::Min(D->Cd / (bStim ? 1.5f : 1.f), Len > 0 ? Len : 0.5f);
			}
			else M.Cd = 0.1f;
		}
		if (M.Actor && (M.Anim == TEXT("walk") || (M.Anim == TEXT("attack") && Time > M.StandAt))) { M.Anim = TEXT("stand"); M.Actor->Play(StandNames, true); }
		if (M.Actor) M.Actor->SetSim(M.X, M.Y, BSTerrain::Height(M.X, M.Y), M.Yaw);
	}
}

// A trooper dies: outside, it falls with SC2's death animation; inside, it is carried out. The caller removes it.
void ABSGame::KillTrooper(const FBSInfantryDef* Def, float X, float Y, float Yaw, ABSModelActor* Actor, bool bStim)
{
	if (Actor) SquadDeaths.Add(FVector(X, Y, Time));
	if (Actor) { ReleaseModel(Actor); Corpse(Def->Death, X, Y, BSTerrain::Height(X, Y), Yaw, 3.5f, 1, true); }
	Toast(Def->Name + (bStim ? TEXT(" overdosed on stim") : TEXT(" down")));
	Floater(X, Y, 2.5f, bStim ? TEXT("OVERDOSE") : TEXT("KIA"), FLinearColor(1, 0.42f, 0.35f));
}

// ---- Structures: missile turrets and the sensor tower ---------------------------------------------------------
bool ABSGame::BuildStructure(bool bSam)
{
	FVector2D Slot;
	if (bSam)
	{
		if (SamCount() >= BSDefs::SamMax) { Toast(TEXT("Turret limit reached")); return false; }
		const FVector2D* Free = SamSlots.FindByPredicate([this](const FVector2D& Sl) { return !Structures.ContainsByPredicate([&Sl](const TSharedPtr<FBSStructure>& S) { return S->X == Sl.X && S->Y == Sl.Y; }); });
		if (!Free) { Toast(TEXT("No room")); return false; }
		Slot = *Free;
	}
	else
	{
		if (HasSensor()) { Toast(TEXT("Sensor tower already built")); return false; }
		Slot = SensorSlot;
	}
	const int32 Cost = bSam ? BSDefs::SamCost : BSDefs::SensorCost;
	if (Minerals < Cost) { Toast(TEXT("Not enough minerals")); return false; }
	Minerals -= Cost;
	TSharedPtr<FBSStructure> S = MakeShared<FBSStructure>();
	S->bSam = bSam; S->X = Slot.X; S->Y = Slot.Y;
	S->MaxHp = bSam ? BSDefs::SamHp : BSDefs::SensorHp; S->Hp = S->MaxHp * 0.1f;
	S->Radius = bSam ? BSDefs::SamRadius : BSDefs::SensorRadius;
	S->Actor = AcquireModel(bSam ? TEXT("missileturret") : TEXT("sensortower"));
	if (S->Actor) { S->Actor->SetTeamColor(TerranColor); S->Actor->SetSim(S->X, S->Y, 0, S->Yaw); S->Actor->Play(TEXT("birth"), false); }
	Structures.Add(S);
	Toast(bSam ? TEXT("Building missile turret") : TEXT("Building sensor tower"));
	return true;
}

void ABSGame::UpdateStructures(float Dt)
{
	TArray<TSharedPtr<FBSStructure>> Snapshot = Structures;
	for (const TSharedPtr<FBSStructure>& SP : Snapshot)
	{
		FBSStructure& S = *SP;
		if (S.bDead) continue;
		if (!S.bReady)
		{
			// Rises from 10% to full health while it builds, as in SC2.
			const float Build = S.bCc ? BSDefs::CcBuild : BSDefs::BuildTime;
			S.T += Dt; S.Hp = FMath::Min(S.MaxHp, S.Hp + S.MaxHp * 0.9f / Build * Dt);
			if (S.T >= Build)
			{
				S.bReady = true;
				if (S.Actor) S.Actor->Play(StandNames, true);
				if (!S.bCc) Toast(S.bSam ? TEXT("Missile turret online") : TEXT("Sensor tower online: radar active"));
			}
			continue;
		}
		if (S.bCc) { Minerals += BSDefs::CcIncome * Dt; continue; }      // the forward base mines
		if (!S.bSam) continue;
		S.Cd -= Dt;
		if (S.Cd > 0) continue;
		TArray<TSharedPtr<FBSEnemy>> L = EnemiesInRange(S.X, S.Y, BSDefs::SamRange, [](const FBSEnemy& E) { return E.Def->bFlying; });
		if (!L.Num()) { S.Cd = 0.1f; continue; }
		const TSharedPtr<FBSEnemy>& Target = L[0];
		S.Cd = BSDefs::SamCd;
		S.Yaw = FMath::Atan2(Target->Y - S.Y, Target->X - S.X);
		if (S.Actor) { S.Actor->SetSim(S.X, S.Y, 0, S.Yaw); S.Actor->Play(TEXT("attack"), false); }
		for (int32 k = 0; k < BSDefs::SamShots; k++)
		{
			const float Off = k ? 0.35f : -0.35f;
			FBSProjectile P; P.Kind = EBSProjKind::Sam; P.bFriendly = true;
			P.From = FVector(S.X - FMath::Sin(S.Yaw) * Off, S.Y + FMath::Cos(S.Yaw) * Off, 1.6f);
			P.To = FVector(Target->X, Target->Y, Target->Z); P.T = -k * 0.12f; P.Dur = 0.4f; P.Dmg = BSDefs::SamDmg; P.TargetEnemy = Target; P.ImpactFx = TEXT("samImpact");
			AddProjectileModel(P, TEXT("projSam"), TEXT("samMissile"));
			if (!P.Model) AddProjectileVisual(P, FLinearColor(1, 0.8f, 0.5f), 0.15f);
			Projectiles.Add(P);
		}
		PlaySound(BSSounds::SamFire, 0.45f, 0.2f);
	}
}

void ABSGame::DestroyStructure(const TSharedPtr<FBSStructure>& S)
{
	S->bDead = true;
	ReleaseModel(S->Actor); S->Actor = nullptr;
	Structures.Remove(S);
	if (S->bCc)
	{
		Corpse(TEXT("commandcenterDeath"), S->X, S->Y, BSTerrain::Height(S->X, S->Y), S->Yaw, 4, BSDefs::CcScale, true);
		PlaySound(BSSounds::Explosion, 0.9f);
		Shake = FMath::Min(1.f, Shake + 0.5f);
		Toast(TEXT("Forward base destroyed"));
		return;
	}
	Corpse(S->bSam ? TEXT("missileturretDeath") : TEXT("sensortowerDeath"), S->X, S->Y, 0, S->Yaw, 4, 1, true);
	PlaySound(BSSounds::Explosion, 0.6f);
	Toast(S->bSam ? TEXT("Missile turret destroyed") : TEXT("Sensor tower destroyed: radar offline"));
}

// ---- Forward base on the high ground ------------------------------------------------------------------------
// Lane only. Once the army is maxed out - the battlecruiser on station, the Odin in position and a full squad - it
// expands onto the plateau east of the stem: the Odin, the goliaths and a detachment of troopers go up the
// stem, into the arm and up its ramp (BSTerrain::LedgeRoute), clear the plateau, then an SCV builds a command center there.
// The base mines minerals, and troopers up there outrange the lane (+1) and never miss for firing uphill. If the
// battlecruiser or the Odin is lost, or the squad is cut down below ExpandMinSquad, they fall back (port of Game.expandReady
// and friends).
bool ABSGame::ExpandReady() const
{
	const TSharedPtr<FBSMech>* Odin = Mechs.FindByPredicate([](const TSharedPtr<FBSMech>& M) { return M->Def->Key == TEXT("odin"); });
	return FwdRoute.Num() > 0 && Bc.IsValid() && Bc->bOnStation && Odin && (*Odin)->bReady && TroopsFull();
}

bool ABSGame::ExpandHolds() const
{
	return Bc.IsValid() && Mechs.ContainsByPredicate([](const TSharedPtr<FBSMech>& M) { return M->Def->Key == TEXT("odin"); }) && Squad.Num() >= BSDefs::ExpandMinSquad;
}

int32 ABSGame::FwdCount() const
{
	int32 N = 0;
	for (const TSharedPtr<FBSTrooper>& M : Squad) N += M->Fwd.bOn;
	for (const TSharedPtr<FBSMech>& M : Mechs) N += M->Fwd.bOn;
	for (const TSharedPtr<FBSScv>& S : Scvs) N += S->Fwd.bOn;
	return N;
}

// Next point for a unit on the expedition: along the route to its post on the high ground (bFinal: the post itself), or
// back down it. False once it is back down in the stem.
bool ABSGame::FwdGoal(FBSFwd& F, float X, float Y, FVector2D& Out, bool& bFinal) const
{
	bFinal = false;
	for (;;)
	{
		if (F.bBack)
		{
			if (F.Leg <= 0) return false;
			const FVector2D& P = FwdRoute[F.Leg - 1];
			if (Dist2(X, Y, P.X, P.Y) < 0.9f * 0.9f) { F.Leg--; continue; }
			Out = P; return true;
		}
		if (F.Leg >= FwdRoute.Num()) { Out = F.Post; bFinal = true; return true; }
		const FVector2D& P = FwdRoute[F.Leg];
		if (Dist2(X, Y, P.X, P.Y) < 0.9f * 0.9f) { F.Leg++; continue; }
		Out = P; return true;
	}
}

void ABSGame::BeginExpansion()
{
	// Anyone still on the way back down turns round where they are.
	auto Send = [](FBSFwd& F, const FVector2D& Post) { if (!F.bOn) F.Leg = 0; F.bOn = true; F.bBack = false; F.Post = Post; };
	int32 G = 0;
	for (const TSharedPtr<FBSMech>& M : Mechs)
	{
		if (M->Def->Key == TEXT("odin")) Send(M->Fwd, FwdOdin);
		else if (M->Def->Key == TEXT("goliath")) Send(M->Fwd, FwdGoliath[G++ % FwdGoliath.Num()]);
	}
	// A detachment of troopers (those already out there first).
	TArray<TSharedPtr<FBSTrooper>> Pick;
	for (const TSharedPtr<FBSTrooper>& M : Squad) if (!M->bDead && !M->bBoarding && M->Fwd.bOn) Pick.Add(M);
	for (const TSharedPtr<FBSTrooper>& M : Squad) if (!M->bDead && !M->bBoarding && !M->Fwd.bOn) Pick.Add(M);
	for (int32 i = 0; i < FMath::Min(Pick.Num(), BSDefs::ExpandTroopers); i++) Send(Pick[i]->Fwd, FwdTroopers[i]);
	for (const TSharedPtr<FBSScv>& S : Scvs) if (S->Fwd.bOn) Send(S->Fwd, FwdScv);
	Expand = EExpand::Advance; ExpandT = 0;
	Toast(TEXT("Expanding to the high ground"));
}

void ABSGame::FallBack()
{
	for (const TSharedPtr<FBSTrooper>& M : Squad) if (M->Fwd.bOn) M->Fwd.bBack = true;
	for (const TSharedPtr<FBSMech>& M : Mechs) if (M->Fwd.bOn) M->Fwd.bBack = true;
	for (const TSharedPtr<FBSScv>& S : Scvs) if (S->Fwd.bOn) S->Fwd.bBack = true;
	Expand = EExpand::Fallback; ExpandT = 0;
	Toast(TEXT("Falling back from the high ground"));
}

TSharedPtr<FBSStructure> ABSGame::ForwardBase() const
{
	for (const TSharedPtr<FBSStructure>& S : Structures) if (S->bCc) return S;
	return nullptr;
}

void ABSGame::UpdateExpansion(float Dt)
{
	ExpandT += Dt;
	if (Expand == EExpand::Off) { if (ExpandReady()) BeginExpansion(); return; }
	if (Expand == EExpand::Fallback) { if (!FwdCount()) { Expand = EExpand::Off; ExpandT = 0; } return; }
	if (!ExpandHolds()) { FallBack(); return; }
	const TSharedPtr<FBSStructure> Cc = ForwardBase();
	if (Expand == EExpand::Advance)
	{
		// Taken once the Odin stands at its post and (almost) nothing on the ground contests the plateau round it.
		const TSharedPtr<FBSMech>* OdinP = Mechs.FindByPredicate([](const TSharedPtr<FBSMech>& M) { return M->Def->Key == TEXT("odin"); });
		const FBSMech& Odin = **OdinP;
		const bool bThere = Odin.Fwd.bOn && !Odin.Fwd.bBack && Odin.Fwd.Leg >= FwdRoute.Num() && Dist2(Odin.X, Odin.Y, Odin.Fwd.Post.X, Odin.Fwd.Post.Y) < 0.5f * 0.5f;
		int32 Foes = 0;
		for (const TSharedPtr<FBSEnemy>& E : Enemies)
			Foes += E->Alive() && !E->Def->bFlying && BSTerrain::OnHigh(E->X, E->Y) && Dist2(E->X, E->Y, FwdPost.X, FwdPost.Y) < BSDefs::ExpandClear * BSDefs::ExpandClear;
		if (bThere && Foes <= 2)
		{
			ExpandT = 0;
			if (Cc && Cc->bReady) { Expand = EExpand::Held; Toast(TEXT("Forward base established")); }
			else { Expand = EExpand::Build; Toast(TEXT("High ground taken")); }
		}
	}
	else if (Expand == EExpand::Build)
	{
		if (Cc && Cc->bReady) { Expand = EExpand::Held; ExpandT = 0; Toast(TEXT("Forward base established")); }
		else if (!Cc && !Scvs.ContainsByPredicate([](const TSharedPtr<FBSScv>& S) { return S->Fwd.bOn && !S->Fwd.bBack; }))
		{
			// An SCV goes up to build it (the one nearest the way up).
			const FVector2D& W0 = FwdRoute[0];
			TSharedPtr<FBSScv> BestScv;
			for (const TSharedPtr<FBSScv>& S : Scvs)
				if (!S->Fwd.bOn && (!BestScv || Dist2(S->X, S->Y, W0.X, W0.Y) < Dist2(BestScv->X, BestScv->Y, W0.X, W0.Y))) BestScv = S;
			if (BestScv) { BestScv->Fwd.bOn = true; BestScv->Fwd.bBack = false; BestScv->Fwd.Leg = 0; BestScv->Fwd.Post = FwdScv; }
		}
	}
	else if (Expand == EExpand::Held && !Cc) { Expand = EExpand::Advance; ExpandT = 0; }
}

// The builder reached the site: the command center goes up (it rises from 10% health like the turrets).
TSharedPtr<FBSStructure> ABSGame::PlaceForwardBase()
{
	TSharedPtr<FBSStructure> S = MakeShared<FBSStructure>();
	S->bSam = false; S->bCc = true; S->X = FwdCc.X; S->Y = FwdCc.Y;
	S->MaxHp = BSDefs::CcHp; S->Hp = S->MaxHp * 0.1f; S->Armor = BSDefs::CcArmor; S->Radius = BSDefs::CcRadius;
	S->Actor = AcquireModel(TEXT("commandcenter"));
	if (S->Actor)
	{
		S->Actor->SetTeamColor(TerranColor); S->Actor->SetScale(BSDefs::CcScale);
		S->Actor->SetSim(S->X, S->Y, BSTerrain::Height(S->X, S->Y), S->Yaw); S->Actor->Play(TEXT("birth"), false);
	}
	Structures.Add(S);
	Toast(TEXT("Building a forward command center"));
	return S;
}

// The SCV sent up to the high ground: walks the route, builds the command center, then stays to repair it.
void ABSGame::UpdateFwdScv(FBSScv& S, float Dt)
{
	FVector2D G = FVector2D::ZeroVector; bool bFinal = false;
	if (!FwdGoal(S.Fwd, S.X, S.Y, G, bFinal)) { S.Fwd.bOn = false; S.Fwd.bBack = false; return; }   // back down: it rejoins the repair crew
	const float Far = FMath::Sqrt(Dist2(S.X, S.Y, G.X, G.Y));
	if (Far > 0.25f)
	{
		const float Sp = FMath::Min(Far, BSDefs::ScvSpeed * Dt), Ox = S.X, Oy = S.Y;
		StepGround(S.X, S.Y, (G.X - S.X) / Far * Sp, (G.Y - S.Y) / Far * Sp);
		if (S.X != Ox || S.Y != Oy) S.Yaw = FMath::Atan2(S.Y - Oy, S.X - Ox);
		if (S.Anim != TEXT("walk") && S.Actor) { S.Anim = TEXT("walk"); S.Actor->Play(TEXT("walk"), true, 1.4f); }
	}
	else
	{
		TSharedPtr<FBSStructure> Cc = ForwardBase();
		if (!Cc && Expand == EExpand::Build) Cc = PlaceForwardBase();
		if (Cc && (!Cc->bReady || Cc->Hp < Cc->MaxHp - 1))
		{
			S.Yaw = FMath::Atan2(Cc->Y - S.Y, Cc->X - S.X);
			if (S.Anim != TEXT("work") && S.Actor) { S.Anim = TEXT("work"); S.Actor->Play(TEXT("work"), true); PlaySound(BSSounds::ScvRepair, 0.25f, 1.5f); }
			if (Cc->bReady) { const float Amount = FMath::Min3(BSDefs::ScvRepairRate * Dt, Cc->MaxHp - Cc->Hp, Minerals / 0.3f); if (Amount > 0) { Cc->Hp += Amount; Minerals -= Amount * 0.3f; } }
		}
		else if (S.Anim != TEXT("stand") && S.Actor) { S.Anim = TEXT("stand"); S.Actor->Play(StandNames, true); }
	}
	if (S.Actor) S.Actor->SetSim(S.X, S.Y, BSTerrain::Height(S.X, S.Y), S.Yaw);
}

bool ABSGame::AddScv(bool bFree)
{
	if (Scvs.Num() >= BSDefs::ScvMax) { Toast(TEXT("SCV limit reached")); return false; }
	if (!bFree) { if (Minerals < BSDefs::ScvCost) { Toast(TEXT("Not enough minerals")); return false; } Minerals -= BSDefs::ScvCost; }
	TSharedPtr<FBSScv> S = MakeShared<FBSScv>();
	S->Idle = ScvIdle[Scvs.Num() % ScvIdle.Num()];
	S->X = S->Idle.X; S->Y = S->Idle.Y - 6;
	S->Actor = AcquireModel(TEXT("scv"));
	if (S->Actor) { S->Actor->SetTeamColor(TerranColor); S->Actor->SetSim(S->X, S->Y, 0, PI / 2); }
	Scvs.Add(S);
	return true;
}

bool ABSGame::DropTank()
{
	if (Tanks.Num() >= TankMax) { Toast(TEXT("No room for more tanks")); return false; }
	const int32 Cost = TankCost();
	if (Minerals < Cost) { Toast(TEXT("Not enough minerals")); return false; }
	Minerals -= Cost;
	TSharedPtr<FBSTank> T = MakeShared<FBSTank>();
	// First slot no living tank is using (a replacement for a destroyed tank goes where the old one stood).
	const FVector2D* Free = TankSlots.FindByPredicate([this](const FVector2D& S) { return !Tanks.ContainsByPredicate([&S](const TSharedPtr<FBSTank>& K) { return !K->bDead && FVector2D::Distance(FVector2D(K->X, K->Y), S) < 0.5f; }); });
	if (!Free) { Minerals += Cost; Toast(TEXT("No room for more tanks")); return false; }
	const FVector2D Slot = *Free;
	T->X = Slot.X; T->Y = Slot.Y;
	T->Med = FVector(Slot.X, Mode == EBSMode::Lane ? -16.f : -28.f, 5.5f);
	T->Medivac = AcquireModel(TEXT("medivac"));
	if (T->Medivac) { T->Medivac->SetTeamColor(TerranColor); T->Medivac->SetSim(T->Med.X, T->Med.Y, T->Med.Z, PI / 2); T->Medivac->Play(WalkNames, true); }
	Tanks.Add(T);
	Toast(TEXT("Medivac inbound with a siege tank"));
	return true;
}

bool ABSGame::CallAirStrike()
{
	if (Ab.StrikeCd > 0) { Toast(TEXT("Air support is refuelling")); return false; }
	if (Minerals < BSDefs::BansheeCost) { Toast(TEXT("Not enough minerals")); return false; }
	Minerals -= BSDefs::BansheeCost;
	Ab.StrikeCd = StrikeCooldown();
	const bool bLane = Mode == EBSMode::Lane;
	for (int32 i = 0; i < BSDefs::BansheeCount + CardLevel(TEXT("rotors")); i++)
	{
		FBSBanshee B;
		B.Post = StrikePosts[i % StrikePosts.Num()];
		B.X = B.Post.X * 0.5f; B.Y = (bLane ? -16.f : -34.f) - i * 1.2f; B.Z = BSDefs::BansheeFly + 1.5f; B.Yaw = PI / 2; B.Cd = 0.3f + i * 0.2f;
		B.Actor = AcquireModel(TEXT("banshee"));
		if (B.Actor) { B.Actor->SetTeamColor(TerranColor); B.Actor->Play(WalkNames, true); }
		B.Hp = B.MaxHp = BSDefs::BansheeHp;
		Banshees.Add(MakeShared<FBSBanshee>(B));
	}
	Toast(TEXT("Banshees inbound"));
	return true;
}

bool ABSGame::UseYamato()
{
	if (Ab.YamatoCd > 0) { Toast(TEXT("Yamato cannon recharging")); return false; }
	if (Minerals < BSDefs::YamatoCost) { Toast(TEXT("Not enough minerals")); return false; }
	// A Battlecruiser in orbit fires on the densest cluster of zerg.
	auto Any = [](const FBSEnemy& X) { return X.Alive(); };
	TSharedPtr<FBSEnemy> Pick; int32 PickN = 0;
	for (const TSharedPtr<FBSEnemy>& En : Enemies)
	{
		if (!En->Alive()) continue;
		const int32 Cnt = EnemiesInRange(En->X, En->Y, BSDefs::YamatoRadius, Any).Num();
		if (Cnt > PickN) { PickN = Cnt; Pick = En; }
	}
	if (!Pick) { Toast(TEXT("No targets")); return false; }
	Minerals -= BSDefs::YamatoCost; Ab.YamatoCd = BSDefs::YamatoCooldown;
	FBSProjectile P; P.Kind = EBSProjKind::Yamato; P.bFriendly = true;
	P.From = FVector(Pick->X * 0.5f, Pick->Y - 16.f, 14.f); P.To = FVector(Pick->X, Pick->Y, 0.6f); P.Dur = 0.55f;
	P.TargetEnemy = Pick; P.Dmg = BSDefs::YamatoDmg;
	AddProjectileModel(P, TEXT("projYamato"), TEXT("yamatoMissile"));
	Projectiles.Add(P);
	PlaySound(BSSounds::TankFire, 0.8f, 0.1f);
	Toast(TEXT("Yamato cannon fired"));
	return true;
}

void ABSGame::YamatoImpact(const FVector& At, float Dmg)
{
	auto Any = [](const FBSEnemy& X) { return X.Alive(); };
	TArray<TSharedPtr<FBSEnemy>> Hit = EnemiesInRange(At.X, At.Y, BSDefs::YamatoRadius, Any);
	if (Hit.Num()) HurtEnemy(Hit[0], Dmg - BSDefs::YamatoSplash, true);
	for (const TSharedPtr<FBSEnemy>& V : Hit) if (V->Alive()) HurtEnemy(V, BSDefs::YamatoSplash, true);
	if (!PlayFx(TEXT("yamatoImpact"), FVector(At.X, At.Y, 0.3f), 0, 1.2f))
	{
		EmitExplosion(FVector(At.X, At.Y, 0.4f), BSDefs::YamatoRadius, FLinearColor(1, 0.72f, 0.35f), FLinearColor(0.85f, 0.2f, 0.04f), 34);
		AddFx(EBSFxKind::Blast, FVector(At.X, At.Y, 0.05f), FVector::ZeroVector, 0.9f, BSDefs::YamatoRadius * 1.3f, FLinearColor(1, 0.6f, 0.25f), 8);
	}
	PlaySound(BSSounds::TankHit, 0.9f, 0.1f); PlaySound(BSSounds::Explosion, 0.7f);
	Shake = 1;
}

void ABSGame::AddProjectileModel(FBSProjectile& P, const TCHAR* Model, const TCHAR* Trail, float TrailScale)
{
	const float Yaw = FMath::Atan2(P.To.Y - P.From.Y, P.To.X - P.From.X);
	if (Model && Assets->Model(Model))
	{
		P.Model = AcquireModel(Model);
		if (P.Model) { P.Model->SetSim(P.From.X, P.From.Y, P.From.Z, Yaw); P.Model->Play(StandNames, true); }
	}
	if (Trail) P.Trail = PlayFx(Trail, P.From, Yaw, TrailScale);
}

FVector ABSGame::BunkerSlit(float Ang) const
{
	// The SC2 bunker has four firing slits (Ref_Weapon, 02, 04, 06). Use the one facing the target.
	// The importer renames the attachment bones: "Attach_Weapon 02" arrives as "Attach_Weapon_02".
	static const FName Slits[] = { TEXT("Attach_Weapon"), TEXT("Attach_Weapon_02"), TEXT("Attach_Weapon_04"), TEXT("Attach_Weapon_06") };
	FVector Slit = FVector(FMath::Cos(Ang) * 1.5f, FMath::Sin(Ang) * 1.5f, 1.3f);
	if (!Bunker.Actor || !Bunker.Actor->Mesh) return Slit;
	float SlitDot = -2;
	for (const FName& Bone : Slits)
	{
		if (Bunker.Actor->Mesh->GetBoneIndex(Bone) == INDEX_NONE) continue;
		const FVector Wp = Bunker.Actor->Mesh->GetBoneLocation(Bone);
		const FVector Sim(Wp.Y / BSSpace::UnitsPerMetre, Wp.X / BSSpace::UnitsPerMetre, Wp.Z / BSSpace::UnitsPerMetre);
		const float Dot = FVector2D(Sim.X, Sim.Y).GetSafeNormal() | FVector2D(FMath::Cos(Ang), FMath::Sin(Ang));
		if (Dot > SlitDot) { SlitDot = Dot; Slit = Sim; }
	}
	return Slit;
}

// SC2's marine muzzle flash is a small animated mesh (the particles only add sparks).
void ABSGame::MuzzleMesh(const FVector& At, float Yaw, float Delay, float Scale)
{
	if (!Assets->Model(TEXT("fxMuzzle"))) return;
	FBSFlash F; F.A = AcquireModel(TEXT("fxMuzzle")); F.P = At; F.Yaw = Yaw; F.Delay = Delay; F.Life = 0.09f; F.Scale = Scale;
	if (!F.A) return;
	F.A->SetSim(0, 0, -1000, 0);
	Flashes.Add(F);
}

void ABSGame::LaunchGlaive(const FVector& From, const FBSTarget& To, float Dmg, int32 Bounces, const TArray<FBSTarget>& Hit, const FBSZergDef* D)
{
	FBSProjectile P;
	P.Kind = EBSProjKind::Glaive; P.bFriendly = false; P.ZDef = D; P.TargetDef = To;
	P.From = From; P.To = FVector(To.X, To.Y, 1.0f);
	P.Dur = FMath::Max(0.12f, FVector::Dist(P.From, P.To) / 14.f);
	P.Dmg = Dmg; P.Bounces = Bounces; P.AlreadyHit = Hit; P.ImpactFx = TEXT("mutaliskImpact");
	AddProjectileModel(P, TEXT("projMutalisk"), TEXT("mutaliskMissile"));
	if (!P.Model) AddProjectileVisual(P, FLinearColor(0.6f, 1, 0.3f), 0.25f);
	Projectiles.Add(P);
}

bool ABSGame::UseStim()
{
	if (!Up.bStim) { Toast(TEXT("Research Stimpack first")); return false; }
	if (Ab.StimCd > 0) return false;
	if (!Bunker.Units.Num() && !Squad.Num()) { Toast(TEXT("No troops to stim")); return false; }
	// Everyone stims; anyone too hurt to take it dies.
	for (int32 i = Bunker.Units.Num() - 1; i >= 0; i--)
	{
		FBSInfantryUnit& U = Bunker.Units[i];
		if (U.Hp <= U.Def->StimHp) { const FBSInfantryDef* D = U.Def; Bunker.Units.RemoveAt(i); KillTrooper(D, 0, 0, 0, nullptr, true); }
		else U.Hp -= U.Def->StimHp;
	}
	for (const TSharedPtr<FBSTrooper>& M : TArray<TSharedPtr<FBSTrooper>>(Squad))
	{
		if (M->Hp <= M->Def->StimHp) { M->bDead = true; KillTrooper(M->Def, M->X, M->Y, M->Yaw, M->Actor, true); M->Actor = nullptr; Squad.Remove(M); }
		else M->Hp -= M->Def->StimHp;
	}
	Ab.StimUntil = Time + BSDefs::StimDuration; Ab.StimCd = BSDefs::StimCooldown;
	for (const TSharedPtr<FBSTrooper>& M : Squad) { M->RetreatUntil = 0; if (M->Anim == TEXT("walk")) M->Anim.Reset(); }   // galvanised: the squad goes forward
	PlaySound(BSSounds::Stim, 0.9f);
	Floater(0, 0, 3, TEXT("STIM!"), FLinearColor(1, 0.83f, 0.3f));
	if (Squad.Num())
	{
		FVector2D Mid = FVector2D::ZeroVector;
		for (const TSharedPtr<FBSTrooper>& M : Squad) Mid += FVector2D(M->X, M->Y);
		Mid /= Squad.Num();
		Floater(Mid.X, Mid.Y, 2.4f, TEXT("STIM!"), FLinearColor(1, 0.83f, 0.3f));
	}
	return true;
}

bool ABSGame::EmergencyRepair()
{
	if (Ab.RepairCd > 0) return false;
	if (Bunker.Hp >= Bunker.MaxHp) { Toast(TEXT("Bunker is intact")); return false; }
	if (Minerals < BSDefs::RepairCost) { Toast(TEXT("Not enough minerals")); return false; }
	Minerals -= BSDefs::RepairCost; Ab.RepairCd = BSDefs::RepairCooldown;
	Bunker.Hp = FMath::Min(Bunker.MaxHp, Bunker.Hp + BSDefs::RepairAmount);
	Floater(0, 0, 3, TEXT("+150"), FLinearColor(0.5f, 0.9f, 1));
	PlaySound(BSSounds::ScvRepair);
	return true;
}

int32 ABSGame::UpgradeLevel(const FString& Id) const
{
	if (Id == TEXT("weapons")) return Up.Weapons;
	if (Id == TEXT("vehicle")) return Up.Vehicle;
	if (Id == TEXT("stim")) return Up.bStim; if (Id == TEXT("neosteel")) return Up.bNeosteel;
	if (Id == TEXT("plating")) return Up.Plating; if (Id == TEXT("medivac")) return Up.bMedivac;
	if (Id == TEXT("hisec")) return Up.bHiSec;
	if (Id == TEXT("rails")) return Up.bRails;
	return 0;
}

int32 ABSGame::CostOfUpgrade(const FString& Id) const
{
	const FBSUpgradeDef* D = BSDefs::FindUpgrade(Id);
	if (!D) return 0;
	const int32 L = UpgradeLevel(Id);
	return D->Costs.IsValidIndex(L) ? D->Costs[L] : D->Costs.Last();
}

bool ABSGame::UpgradeAvailable(const FString& Id) const
{
	const FBSUpgradeDef* D = BSDefs::FindUpgrade(Id);
	if (!D) return false;
	if (Id == TEXT("medivac") && !Up.bStim) return false;
	if (Id == TEXT("vehicle") && Tanks.Num() == 0) return false;
	if (Id == TEXT("rails") && !Up.bHiSec) return false;
	return UpgradeLevel(Id) < D->Costs.Num();
}

// One level of research (bought as a shop card).
bool ABSGame::Research(const FString& Id)
{
	if (!UpgradeAvailable(Id)) return false;
	if (Id == TEXT("stim")) { Up.bStim = true; Toast(TEXT("Stimpack researched - hit STIM")); }
	else if (Id == TEXT("weapons")) { Up.Weapons++; Toast(FString::Printf(TEXT("Infantry Weapons level %d"), Up.Weapons)); }
	else if (Id == TEXT("vehicle")) { Up.Vehicle++; Toast(FString::Printf(TEXT("Vehicle Weapons level %d"), Up.Vehicle)); }
	else if (Id == TEXT("neosteel")) { Up.bNeosteel = true; Bunker.Slots = BSDefs::BunkerSlotsUpgraded; Toast(TEXT("Bunker expanded to 6 slots")); }
	else if (Id == TEXT("plating")) { Up.Plating++; Bunker.MaxHp += 150; Bunker.Hp += 150; if (Up.Plating <= 3) Bunker.Armor += 1; Toast(FString::Printf(TEXT("Building Armor level %d"), Up.Plating)); }
	else if (Id == TEXT("hisec")) { Up.bHiSec = true; Toast(TEXT("Hi-Sec Auto Tracking online")); }
	else if (Id == TEXT("medivac")) { Up.bMedivac = true; Toast(TEXT("Medivac on station")); }
	else if (Id == TEXT("rails")) { Up.bRails = true; Toast(TEXT("Magnetic rails fitted: +2 bunker range")); }
	return true;
}

bool ABSGame::BuyUpgrade(const FString& Id)
{
	if (!Shop.bOpen) return false;
	for (int32 i = 0; i < Shop.Offers.Num(); i++)
		if (!Shop.Bought[i] && Shop.Offers[i] == Id && BSDefs::FindCard(Id)->Up == Id) return BuyCard(i);
	return false;
}

// ---- The shop, between waves ----------------------------------------------------------------------------------
// A wave ends once all of it has spawned and its zerg are dead (or ShopLinger seconds after the last spawn, the
// stragglers left frozen on the field): clear bonus, interest, then the shop pauses the fight until NEXT WAVE.
void ABSGame::EndWave()
{
	const bool bCleared = !WaveAlive();
	const int32 Bonus = bCleared ? BSDefs::ClearBonusA + BSDefs::ClearBonusB * Wave : 0;
	if (bCleared) { Minerals += Bonus; Score += 100 * Wave; }
	const int32 Spoils = CardLevel(TEXT("spoils"));
	const int32 Interest = FMath::Min(50 * Spoils, FMath::FloorToInt(Minerals * 0.1f * Spoils));
	Minerals += Interest;
	WavesCleared = Wave;
	Shop = FShop();
	Shop.bOpen = true; Shop.Wave = Wave; Shop.Bonus = Bonus; Shop.Interest = Interest; Shop.Free = BSMeta::Perk(TEXT("reroll"));
	Shop.Offers = RollOffers();
	Shop.Bought.Init(false, Shop.Offers.Num());
	WaveBanner = FString::Printf(TEXT("WAVE %d CLEARED"), Wave); WaveBannerT = Time; WaveSerial++;
}

void ABSGame::CloseShop()
{
	if (!Shop.bOpen) return;
	Shop.bOpen = false;
	StartWave();
}

int32 ABSGame::CardLevel(const FBSCardDef& C) const
{
	if (!C.Up.IsEmpty()) return UpgradeLevel(C.Up);
	if (!C.Tech.IsEmpty()) return TechUnlocked.Contains(C.Tech) ? 1 : 0;
	const int32* L = Cards.Find(C.Id);
	return L ? *L : 0;
}

int32 ABSGame::CardMax(const FBSCardDef& C) const
{
	if (C.Up.IsEmpty()) return C.Max;
	const FBSUpgradeDef* U = BSDefs::FindUpgrade(C.Up);
	return U ? U->Costs.Num() : 0;
}

int32 ABSGame::CardCost(const FBSCardDef& C) const
{
	if (!C.Up.IsEmpty()) return CostOfUpgrade(C.Up);
	return C.Costs[FMath::Min(CardLevel(C), C.Costs.Num() - 1)];
}

FString ABSGame::CardIcon(const FBSCardDef& C) const
{
	if (C.Up.IsEmpty()) return C.Icon;
	const FBSUpgradeDef* U = BSDefs::FindUpgrade(C.Up);
	return U->Icons[FMath::Min(UpgradeLevel(C.Up), U->Icons.Num() - 1)];
}

FString ABSGame::CardDesc(const FBSCardDef& C) const
{
	if (C.Up.IsEmpty()) return C.Desc;
	return BSDefs::FindUpgrade(C.Up)->Desc;
}

// What this run needs first (FBSCardDef Req).
bool ABSGame::CardReq(const FBSCardDef& C) const
{
	const FString& R = C.Req;
	if (R.IsEmpty()) return true;
	if (R == TEXT("tank")) return Tanks.Num() > 0;
	if (R == TEXT("stim")) return Up.bStim;
	if (R == TEXT("hisec")) return Up.bHiSec;
	if (R == TEXT("medivac")) return Up.bMedivac;
	if (R == TEXT("bc")) return TechUnlocked.Contains(TEXT("bc"));
	int32 i = R.Len();
	while (i > 0 && FChar::IsDigit(R[i - 1])) i--;
	const FString Key = R.Left(i);
	const int32 N = FCString::Atoi(*R.Mid(i));
	return Key == TEXT("wave") ? Wave >= N : UpgradeLevel(Key) >= N;
}

bool ABSGame::CardOpen(const FBSCardDef& C) const
{
	return BSMeta::Unlocked(C) && CardLevel(C) < CardMax(C) && CardReq(C) && Wave >= BSDefs::FindRarity(C.Rarity).From;
}

// Weighted by rarity (Field Intelligence shifts weight from common cards to the rarer ones, research weighs double); no
// repeats in one offer, and the first card is always research while there is any left.
TArray<FString> ABSGame::RollOffers()
{
	const int32 Luck = BSMeta::Perk(TEXT("luck")), N = BSDefs::ShopOffers + BSMeta::Perk(TEXT("offer"));
	auto Wt = [Luck](const FBSCardDef* C)
	{
		return BSDefs::FindRarity(C->Rarity).Weight * (C->Rarity == TEXT("common") ? 1 - 0.15f * Luck : 1 + 0.3f * Luck) * (C->Up.IsEmpty() ? 1.f : BSDefs::ShopResearchWeight);
	};
	TArray<const FBSCardDef*> Pool;
	for (const FBSCardDef& C : BSDefs::Cards()) if (CardOpen(C)) Pool.Add(&C);
	TArray<FString> Out;
	while (Out.Num() < N && Pool.Num())
	{
		TArray<const FBSCardDef*> From = Pool;
		if (Out.Num() == 0 && Pool.ContainsByPredicate([](const FBSCardDef* C) { return !C->Up.IsEmpty(); }))
			From = Pool.FilterByPredicate([](const FBSCardDef* C) { return !C->Up.IsEmpty(); });
		float Total = 0;
		for (const FBSCardDef* C : From) Total += Wt(C);
		float Roll = FMath::FRand() * Total;
		int32 i = 0;
		for (; i < From.Num() - 1; i++) { Roll -= Wt(From[i]); if (Roll <= 0) break; }
		Out.Add(From[i]->Id);
		Pool.Remove(From[i]);
	}
	return Out;
}

bool ABSGame::Reroll()
{
	if (!Shop.bOpen) return false;
	const int32 Cost = RerollCost();
	if (Minerals < Cost) { Toast(TEXT("Not enough minerals")); return false; }
	Minerals -= Cost;
	if (Shop.Free > 0) Shop.Free--; else Shop.Rerolls++;
	Shop.Offers = RollOffers();
	Shop.Bought.Init(false, Shop.Offers.Num());
	return true;
}

bool ABSGame::BuyCard(int32 Index)
{
	if (!Shop.bOpen || !Shop.Offers.IsValidIndex(Index) || Shop.Bought[Index]) return false;
	const FBSCardDef* C = BSDefs::FindCard(Shop.Offers[Index]);
	if (!C || CardLevel(*C) >= CardMax(*C)) return false;
	const int32 Cost = CardCost(*C);
	if (Minerals < Cost) { Toast(TEXT("Not enough minerals")); return false; }
	Minerals -= Cost;
	Shop.Bought[Index] = true;
	ApplyCard(*C);
	if (!bAutoPlay) for (const FBSCardDef* U : BSMeta::RecordBuy(C->Id)) Toast(TEXT("New card unlocked: ") + BSMeta::CardName(*U));
	return true;
}

void ABSGame::ApplyCard(const FBSCardDef& C)
{
	if (!C.Up.IsEmpty()) { Research(C.Up); return; }
	if (!C.Tech.IsEmpty())
	{
		TechUnlocked.Add(C.Tech);
		const FBSTechCard* T = BSDefs::FindTech(C.Tech);
		WaveBanner = TEXT("NEW TECH  //  ") + T->Banner; WaveBannerT = Time; WaveSerial++;
		Toast(T->Name + TEXT(" in the armoury"));
		return;
	}
	const int32 L = ++Cards.FindOrAdd(C.Id);
	if (C.Id == TEXT("refinery")) Income += 1;
	Toast(C.Max > 1 ? FString::Printf(TEXT("%s level %d"), *C.Name, L) : C.Name);
}

// ---- High-tech reinforcements --------------------------------------------------------------------------------

bool ABSGame::TechReady(const FString& Id, int32& OutCost, FString& OutTag) const
{
	if (Id == TEXT("nuke")) { OutCost = BSDefs::NukeCost; OutTag = FString::Printf(TEXT("%ds"), FMath::CeilToInt(NukeCd)); return NukeCd <= 0; }
	if (Id == TEXT("vikings")) { OutCost = BSDefs::VikingCost; OutTag = FString::Printf(TEXT("%ds"), FMath::CeilToInt(VikingCd)); return VikingCd <= 0; }
	OutTag = TEXT("MAX");
	if (Id == TEXT("bc")) { OutCost = BSDefs::BcCost; return !Bc.IsValid(); }
	const FBSMechDef* D = BSDefs::FindMech(Id);
	OutCost = D ? D->Cost : 0;
	return D && TechCount(Id) < D->Max;
}

bool ABSGame::BuyTech(const FString& Id)
{
	if (!TechUnlocked.Contains(Id)) return false;
	if (Id == TEXT("nuke")) return LaunchNuke();
	if (Id == TEXT("vikings")) return CallVikings();
	if (Id == TEXT("bc")) return CallBattlecruiser();
	return AddMech(Id);
}

void ABSGame::GrantAllTech()
{
	Minerals += 1e6f;
	if (!Tanks.Num()) DropTank();
	for (const FBSCardDef& C : BSDefs::Cards())
		for (int32 Guard = 0; Guard < 20 && CardLevel(C) < CardMax(C); Guard++) ApplyCard(C);
	Minerals += 20000;
}

// Goliaths, thors and the odin walk in from the south to their posts, then fight with separate anti-ground and
// anti-air weapons.
bool ABSGame::AddMech(const FString& Kind)
{
	const FBSMechDef* D = BSDefs::FindMech(Kind);
	const TArray<FVector2D>* Slots = MechSlots.Find(Kind);
	if (!D || !Slots) return false;
	if (TechCount(Kind) >= D->Max) { Toast(TEXT("No room for more")); return false; }
	const FVector2D* Slot = Slots->FindByPredicate([this](const FVector2D& Sl) { return !Mechs.ContainsByPredicate([&Sl](const TSharedPtr<FBSMech>& M) { return M->Slot == Sl; }); });
	if (!Slot) { Toast(TEXT("No room for more")); return false; }
	if (Minerals < D->Cost) { Toast(TEXT("Not enough minerals")); return false; }
	Minerals -= D->Cost;
	TSharedPtr<FBSMech> M = MakeShared<FBSMech>();
	M->Def = D; M->Slot = *Slot; M->X = Slot->X; M->Y = Mode == EBSMode::Lane ? -9.f : -14.f; M->Hp = M->MaxHp = D->Hp;
	M->Actor = AcquireModel(Kind);
	if (M->Actor) { M->Actor->SetTeamColor(TerranColor); M->Actor->SetSim(M->X, M->Y, BSTerrain::Height(M->X, M->Y), M->Yaw); }
	Mechs.Add(M);
	Toast(D->Name + TEXT(" inbound"));
	return true;
}

void ABSGame::UpdateMechs(float Dt)
{
	TArray<TSharedPtr<FBSMech>> Snapshot = Mechs;
	for (const TSharedPtr<FBSMech>& MP : Snapshot)
	{
		FBSMech& M = *MP;
		if (M.bDead) continue;
		const FBSMechDef* D = M.Def;
		bool bMoving = false;
		if (M.Fwd.bOn)
		{
			// On the expedition to the high ground (UpdateExpansion): walk the route, guns firing on the move.
			FVector2D G = FVector2D::ZeroVector; bool bFinal = false;
			if (!FwdGoal(M.Fwd, M.X, M.Y, G, bFinal)) { M.Fwd.bOn = false; M.Fwd.bBack = false; M.bReady = false; }   // back down in the stem: on to its post
			else
			{
				M.bReady = true;                                                  // (even if a viper yanked it about)
				const float Far = FMath::Sqrt(Dist2(M.X, M.Y, G.X, G.Y));
				if (Far > 0.15f)
				{
					const FVector2D Wp = RoundBunker(M.X, M.Y, G.X, G.Y);
					const float Dx = Wp.X - M.X, Dy = Wp.Y - M.Y, Dd = FMath::Max(0.001f, FMath::Sqrt(Dx * Dx + Dy * Dy)), Sp = FMath::Min(Dd, D->Speed * Dt);
					StepGround(M.X, M.Y, Dx / Dd * Sp, Dy / Dd * Sp);
					M.Yaw += FMath::Clamp(FMath::FindDeltaAngleRadians(M.Yaw, FMath::Atan2(Dy, Dx)), -3 * Dt, 3 * Dt);
					if (M.Actor && M.Anim != TEXT("walk")) { M.Anim = TEXT("walk"); M.Actor->Play(WalkNames, true); }
					bMoving = true;
				}
				else if (M.Anim == TEXT("walk")) { M.Anim = TEXT("stand"); if (M.Actor) M.Actor->Play(StandNames, true); }
			}
		}
		if (!M.bReady)
		{
			const FVector2D Wp = RoundBunker(M.X, M.Y, M.Slot.X, M.Slot.Y);
			const float Far = FVector2D::Distance(M.Slot, FVector2D(M.X, M.Y)), Dx = Wp.X - M.X, Dy = Wp.Y - M.Y, Dd = FMath::Max(0.001f, FMath::Sqrt(Dx * Dx + Dy * Dy));
			if (Far > 0.15f)
			{
				const float Sp = FMath::Min(Dd, D->Speed * Dt);
				StepGround(M.X, M.Y, Dx / Dd * Sp, Dy / Dd * Sp);
				M.Yaw += FMath::Clamp(FMath::FindDeltaAngleRadians(M.Yaw, FMath::Atan2(Dy, Dx)), -3 * Dt, 3 * Dt);
				if (M.Actor && M.Anim != TEXT("walk")) { M.Anim = TEXT("walk"); M.Actor->Play(WalkNames, true); }
			}
			else { M.bReady = true; M.Anim = TEXT("stand"); if (M.Actor) M.Actor->Play(StandNames, true); Toast(D->Name + TEXT(" in position")); }
			if (M.Actor) M.Actor->SetSim(M.X, M.Y, BSTerrain::Height(M.X, M.Y), M.Yaw);
			continue;
		}
		const float Bonus = Up.Vehicle * 2.f;
		M.CdG -= Dt; M.CdA -= Dt;
		for (const bool bAir : { false, true })
		{
			if ((bAir ? M.CdA : M.CdG) > 0) continue;
			const float Range = bAir ? D->ARange : D->GRange;
			auto Pred = [bAir](const FBSEnemy& E) { return E.Def->bFlying == bAir; };
			TSharedPtr<FBSEnemy> Target;
			if (TSharedPtr<FBSEnemy> F = Focus.Pin())
				if (F->Alive() && Pred(*F) && FMath::Sqrt(Dist2(M.X, M.Y, F->X, F->Y)) - F->Radius <= Range) Target = F;
			if (!Target) { TArray<TSharedPtr<FBSEnemy>> L = EnemiesInRange(M.X, M.Y, Range, Pred); if (L.Num()) Target = L[0]; }
			if (!Target) { (bAir ? M.CdA : M.CdG) = 0.15f; continue; }
			(bAir ? M.CdA : M.CdG) = bAir ? D->ACd : D->GCd;
			M.Aim = FMath::Atan2(Target->Y - M.Y, Target->X - M.X);
			MechFire(M, Target, bAir, (bAir ? D->ADmg : D->GDmg) + Bonus);
		}
		if (!bMoving) M.Yaw += FMath::Clamp(FMath::FindDeltaAngleRadians(M.Yaw, M.Aim), -4 * Dt, 4 * Dt);
		if (M.Actor) M.Actor->SetSim(M.X, M.Y, BSTerrain::Height(M.X, M.Y), M.Yaw);
	}
}

// Muzzles of a mech's ground or air weapons, in sim space: its weapon bones (goliath autocannons and missile racks,
// thor/odin arm cannons and shoulder pods), or their rest-pose spots (model space: x right, y up, z forward).
TArray<FVector> ABSGame::MechMuzzles(const FBSMech& M, bool bAir) const
{
	struct FMz { const TCHAR* Bone; float Rx, Ry, Rz; };
	const FString& Kind = M.Def->Key;
	TArray<FMz> L;
	if (Kind == TEXT("goliath"))
		L = bAir ? TArray<FMz>{ { TEXT("Ref_Weapon_02"), -0.82f, 2.15f, 0.32f }, { TEXT("Ref_Weapon_05"), 0.85f, 2.15f, 0.32f }, { TEXT("Ref_Weapon_04"), -0.83f, 2.31f, 0.29f }, { TEXT("Ref_Weapon_07"), 0.86f, 2.31f, 0.29f } }
			: TArray<FMz>{ { TEXT("Ref_Weapon"), -0.83f, 1.56f, 0.87f }, { TEXT("Ref_Weapon_01"), 0.89f, 1.56f, 0.87f } };
	else if (Kind == TEXT("thor"))
		L = bAir ? TArray<FMz>{ { TEXT("Ref_Weapon_01"), -1.49f, 4.2f, -0.91f }, { TEXT("Ref_Weapon_04"), 1.5f, 4.31f, -0.76f }, { TEXT("Ref_Weapon_02"), -0.93f, 4.52f, -0.87f }, { TEXT("Ref_Weapon_03"), 0.86f, 4.63f, -0.81f } }
			: TArray<FMz>{ { TEXT("Ref_Weapon_07"), -2.14f, 1.69f, 1.83f }, { TEXT("Ref_Weapon_05"), 2.15f, 1.68f, 1.84f }, { TEXT("Ref_Weapon_08"), -2.18f, 1.17f, 1.83f }, { TEXT("Ref_Weapon_06"), 2.21f, 1.17f, 1.83f } };
	else
		L = bAir ? TArray<FMz>{ { TEXT("Ref_Weapon_01"), -1.14f, 5.02f, -0.97f }, { TEXT("Ref_Weapon_04"), 1.42f, 4.88f, -0.75f }, { TEXT("Ref_Weapon_02"), -0.73f, 5.27f, -1.4f }, { TEXT("Ref_Weapon_03"), 1.13f, 5.13f, -1.28f } }
			: TArray<FMz>{ { TEXT("Ref_Weapon_07"), -2.22f, 2.27f, 1.4f }, { TEXT("Ref_Weapon_05"), 2.24f, 2.47f, 1.83f }, { TEXT("Ref_Weapon_08"), -2.25f, 1.76f, 1.51f }, { TEXT("Ref_Weapon_06"), 2.27f, 1.98f, 1.99f } };
	const float H = BSTerrain::Height(M.X, M.Y), Cs = FMath::Cos(M.Yaw), Sn = FMath::Sin(M.Yaw);
	TArray<FVector> Out;
	for (const FMz& Z : L)
	{
		const FName Bone(Z.Bone);
		if (M.Actor && M.Actor->Mesh && M.Actor->Mesh->GetBoneIndex(Bone) != INDEX_NONE)
		{
			const FVector Wp = M.Actor->Mesh->GetBoneLocation(Bone);
			Out.Add(FVector(Wp.Y / BSSpace::UnitsPerMetre, Wp.X / BSSpace::UnitsPerMetre, Wp.Z / BSSpace::UnitsPerMetre));
		}
		else Out.Add(FVector(M.X + Cs * Z.Rz + Sn * Z.Rx, M.Y + Sn * Z.Rz - Cs * Z.Rx, H + Z.Ry));
	}
	return Out;
}

// One volley: goliath autocannons and hellfire missiles, thor arm cannons and javelin barrage, odin heavy cannons
// and missile salvos. Every shot flashes at its own muzzle.
void ABSGame::MechFire(FBSMech& M, const TSharedPtr<FBSEnemy>& Target, bool bAir, float Dmg)
{
	const FBSMechDef* D = M.Def;
	const FString& Kind = D->Key;
	const float Ang = M.Aim;
	const int32 Shots = bAir ? D->AShots : D->GShots;
	const FVector Hit(Target->X, Target->Y, Target->Z + Target->Def->Height * 0.5f);
	if (M.Actor) M.Actor->Play(TEXT("attack"), false);
	const TArray<FVector> Mz = MechMuzzles(M, bAir);
	if (Kind == TEXT("goliath") && !bAir)
	{
		for (int32 k = 0; k < Shots; k++)
		{
			const FVector& Muzzle = Mz[k % Mz.Num()];
			PlayFx(TEXT("marineLaunch"), Muzzle, Ang, 2.2f, k * 0.12f);
			MuzzleMesh(Muzzle, Ang, k * 0.12f, 1.6f);
			PlayFx(TEXT("marineImpact"), Hit + FVector(Rnd(-0.2f, 0.2f), Rnd(-0.2f, 0.2f), 0), Ang + PI, 1.6f, k * 0.12f + 0.04f);
			HurtEnemy(Target, Dmg);
		}
		PlaySound(TEXT("goliathFire"), 0.45f, 0.15f);
		return;
	}
	const TCHAR* Trail = nullptr; const TCHAR* Impact = nullptr; const TCHAR* Launch = nullptr;
	float Splash = 0, Scale = 1, Arc = bAir ? 0.8f : 0.4f, LaunchScale = 1, FlashScale = 0;
	if (Kind == TEXT("goliath")) { Trail = TEXT("goliathAirMissile"); Impact = TEXT("goliathAirImpact"); Launch = TEXT("vikingLaunch"); }
	else if (Kind == TEXT("thor"))
	{
		if (bAir) { Trail = TEXT("thorShoulderMissile"); Impact = TEXT("thorShoulderImpact"); Launch = TEXT("thorShoulderLaunch"); Splash = D->ASplash; }
		else { Trail = TEXT("thorArmMissile"); Impact = TEXT("thorArmImpact"); Launch = TEXT("thorArmLaunch"); FlashScale = 2.2f; }
	}
	else
	{
		if (bAir) { Trail = TEXT("thorShoulderMissile"); Impact = TEXT("odinShoulderImpact"); Launch = TEXT("thorShoulderLaunch"); Splash = D->ASplash; LaunchScale = 1.4f; }
		else { Impact = TEXT("tankImpact"); Launch = TEXT("tankLaunch"); Splash = D->GSplash; Scale = 1.6f; Arc = 3; LaunchScale = 1.6f; FlashScale = 3; }
	}
	for (int32 k = 0; k < Shots; k++)
	{
		const FVector& Muzzle = Mz[k % Mz.Num()];
		if (Launch) PlayFx(Launch, Muzzle, Ang, LaunchScale, k * 0.08f);
		if (FlashScale > 0) MuzzleMesh(Muzzle, Ang, k * 0.08f, FlashScale);
		FBSProjectile P; P.Kind = EBSProjKind::Missile; P.bFriendly = true; P.From = Muzzle; P.To = Hit; P.T = -k * 0.08f;
		P.Dur = 0.45f + (Arc > 2 ? 0.15f : 0.f); P.Arc = Arc; P.Dmg = Dmg; P.TargetEnemy = Target; P.ImpactFx = Impact; P.ImpactScale = Scale;
		P.Splash = Splash; P.SplashAir = bAir ? 1 : 0;
		if (Trail) AddProjectileModel(P, nullptr, Trail);
		if (!P.Trail) AddProjectileVisual(P, FLinearColor(1, 0.75f, 0.4f), 0.2f);
		Projectiles.Add(P);
	}
	PlaySound(Kind == TEXT("goliath") ? TEXT("goliathFire") : TEXT("thorFire"), Kind == TEXT("odin") ? 0.8f : 0.5f, 0.15f);
	if (Kind == TEXT("odin") && !bAir) Shake = FMath::Min(1.f, Shake + 0.2f);
}

void ABSGame::KillMech(const TSharedPtr<FBSMech>& M)
{
	M->bDead = true;
	ReleaseModel(M->Actor); M->Actor = nullptr;
	Mechs.Remove(M);
	Corpse(M->Def->Key + TEXT("Death"), M->X, M->Y, BSTerrain::Height(M->X, M->Y), M->Yaw, 6, 1, true);
	PlaySound(BSSounds::Explosion, 0.8f);
	Toast(M->Def->Name + TEXT(" destroyed"));
}

// Battlecruiser on station: flies in over the lane and stays, out of reach.
bool ABSGame::CallBattlecruiser()
{
	if (Bc.IsValid()) { Toast(TEXT("The battlecruiser is already on station")); return false; }
	if (Minerals < BSDefs::BcCost) { Toast(TEXT("Not enough minerals")); return false; }
	Minerals -= BSDefs::BcCost;
	const bool bLane = Mode == EBSMode::Lane;
	Bc = MakeShared<FBSBattlecruiser>();
	// On station over the tip of the mesa where the lane forks (the bottom of the V), nose to the south, a little scaled
	// down: it overlooks both arms and the stem without hiding the lane.
	Bc->X = Bc->Sx = 0; Bc->Y = bLane ? -24.f : -34.f; Bc->Z = 10; Bc->Sy = bLane ? 13.f : 0.f; Bc->Sz = bLane ? 6.5f : 8.f;
	Bc->Actor = AcquireModel(TEXT("battlecruiser"));
	// The hull's hardpoints (Ref_Weapon bones; the importer turns "Ref_Weapon 10" into "Ref_Weapon_10").
	for (const TCHAR* Bone : { TEXT("Ref_Weapon_10"), TEXT("Ref_Weapon_13"), TEXT("Ref_Weapon_12"), TEXT("Ref_Weapon_15"), TEXT("Ref_Weapon_24"), TEXT("Ref_Weapon_25") })
	{
		FBSBcTurret Tu; Tu.Bone = Bone;
		Bc->Turrets.Add(Tu);
	}
	if (Bc->Actor) { Bc->Actor->SetScale(0.75f); Bc->Actor->SetTeamColor(TerranColor); Bc->Actor->SetSim(Bc->X, Bc->Y, Bc->Z, Bc->Yaw); Bc->Actor->Play(WalkNames, true); }
	Toast(TEXT("Battlecruiser inbound"));
	return true;
}

void ABSGame::UpdateBc(float Dt)
{
	if (!Bc.IsValid()) return;
	FBSBattlecruiser& B = *Bc;
	B.T += Dt;
	if (!B.bOnStation)
	{
		const FVector D = FVector(B.Sx, B.Sy, B.Sz) - FVector(B.X, B.Y, B.Z);
		if (D.Size() > 0.2f) { const FVector Mv = D.GetSafeNormal() * FMath::Min(D.Size(), 7.f * Dt); B.X += Mv.X; B.Y += Mv.Y; B.Z += Mv.Z; }
		else { B.bOnStation = true; if (B.Actor) B.Actor->Play(StandNames, true); Toast(TEXT("Battlecruiser on station")); }
		if (B.Actor) B.Actor->SetSim(B.X, B.Y, B.Z, B.Yaw);
		return;
	}
	B.Yaw += FMath::Clamp(FMath::FindDeltaAngleRadians(B.Yaw, -PI / 2), -0.6f * Dt, 0.6f * Dt);   // swing round to face south
	if (B.Actor) B.Actor->SetSim(B.X, B.Y, B.Sz + 0.25f * FMath::Sin(B.T * 0.7f), B.Yaw);
	auto Any = [](const FBSEnemy& E) { return E.Alive(); };
	// Laser batteries, as in StarCraft II: ATS against ground, ATA against air, single amber bolts in quick succession (one
	// every 0.16 s, slowed by devourer acid) that walk along the hull's six hardpoints. Each battery keeps its own target,
	// and scourge and devourers closing on the ship are taken first (point defence, see BcThreat). The bolt is drawn as a
	// tracer (the wraith's laser is an M3 ribbon, which is not imported) and lands with the wraith's laser hit.
	B.CdL -= Dt * (B.AcidUntil > Time ? 0.7f : 1.f);
	BcAlerts();
	while (B.CdL <= 0)
	{
		B.CdL += BSDefs::BcLaserCd;
		bool bFired = false;
		for (int32 n = 0; n < B.Turrets.Num() && !bFired; n++)
		{
			const int32 i = B.Next = (B.Next + 1) % B.Turrets.Num();
			FBSBcTurret& Tu = B.Turrets[i];
			FVector At(B.X + (i % 2 ? 1.6f : -1.6f), B.Y + (1 - i / 2) * 1.8f, B.Sz - 0.3f);
			if (B.Actor && B.Actor->Mesh && B.Actor->Mesh->GetBoneIndex(Tu.Bone) != INDEX_NONE)
			{
				const FVector Wp = B.Actor->Mesh->GetBoneLocation(Tu.Bone);
				At = FVector(Wp.Y / BSSpace::UnitsPerMetre, Wp.X / BSSpace::UnitsPerMetre, Wp.Z / BSSpace::UnitsPerMetre);
			}
			TArray<TSharedPtr<FBSEnemy>> L = EnemiesInRange(At.X, At.Y, BSDefs::BcRange, Any);
			const TSharedPtr<FBSEnemy> Threat = BcThreat();
			TSharedPtr<FBSEnemy> E = Tu.Target.Pin();
			if (Threat) E = Threat;
			else if (!E || !E->Alive() || FMath::Sqrt(Dist2(At.X, At.Y, E->X, E->Y)) - E->Radius > BSDefs::BcRange)
				E = L.Num() ? L[FMath::RandRange(0, FMath::Min(4, L.Num()) - 1)] : nullptr;
			Tu.Target = E;
			if (!E) continue;
			HurtEnemy(E, E->Def->bFlying ? BSDefs::BcLaserA : BSDefs::BcLaserG);
			const FVector Hit(E->X + Rnd(-0.15f, 0.15f), E->Y + Rnd(-0.15f, 0.15f), E->Z + E->Def->Height * 0.5f);
			AddFx(EBSFxKind::Tracer, At, Hit, 0.1f, 0.05f, FLinearColor(1.f, 0.62f, 0.18f), 22);
			PlayFx(TEXT("laserImpact"), Hit, FMath::Atan2(At.Y - Hit.Y, At.X - Hit.X), 1.2f);
			PlaySound(TEXT("laserHit"), 0.25f, 0.12f);
			PlaySound(TEXT("bcFire"), 0.3f, 0.3f);
			bFired = true;
		}
		if (!bFired) { B.CdL = 0.2f; break; }
	}
	// Missile pods (the Wings of Liberty armoury upgrade): an anti-air volley every few seconds, splashing other flyers.
	// Scourge and devourers closing on the ship get the first missiles.
	B.CdM -= Dt;
	if (B.CdM <= 0)
	{
		TArray<TSharedPtr<FBSEnemy>> L = EnemiesInRange(B.X, B.Y, BSDefs::BcRange + 2, [](const FBSEnemy& E) { return E.Def->bFlying; });
		L.StableSort([](const TSharedPtr<FBSEnemy>& P, const TSharedPtr<FBSEnemy>& Q) { return P->Def->bAntiAir && !Q->Def->bAntiAir; });
		B.CdM = L.Num() ? BSDefs::BcMissileCd : 0.5f;
		for (int32 k = 0; k < FMath::Min(BSDefs::BcMissiles, L.Num()); k++)
		{
			FBSProjectile P; P.Kind = EBSProjKind::Missile; P.bFriendly = true;
			P.From = FVector(B.X + Rnd(-2, 2), B.Y + Rnd(-1, 1), B.Sz - 0.4f); P.To = FVector(L[k]->X, L[k]->Y, L[k]->Z + 0.4f);
			P.T = -k * 0.1f; P.Dur = 0.6f; P.Arc = 1.2f; P.Dmg = BSDefs::BcMissileDmg; P.TargetEnemy = L[k]; P.ImpactFx = TEXT("bcMissileImpact");
			P.Splash = BSDefs::BcMissileSplash; P.SplashAir = 1;
			AddProjectileModel(P, nullptr, TEXT("bcMissile"));
			if (!P.Trail) AddProjectileVisual(P, FLinearColor(1, 0.7f, 0.35f), 0.2f);
			Projectiles.Add(P);
		}
		if (L.Num()) PlaySound(TEXT("bcMissiles"), 0.5f);
	}
	// Yamato cannon on its own, at the toughest zerg in reach: it charges at the bow for a couple of seconds first.
	const FVector Bow(B.X + FMath::Cos(B.Yaw) * 2.5f, B.Y + FMath::Sin(B.Yaw) * 2.5f, B.Sz);
	if (B.YamatoT >= 0)
	{
		B.YamatoT += Dt;
		if (B.YamatoFx) MoveFx(B.YamatoFx, Bow, B.Yaw);
		if (B.YamatoT >= BSDefs::BcYamatoCharge)
		{
			B.YamatoT = -1;
			if (B.YamatoFx) { StopFx(B.YamatoFx); B.YamatoFx = 0; }
			TSharedPtr<FBSEnemy> Pick = B.YamatoTarget.Pin();
			if (!Pick || !Pick->Alive())
			{
				Pick.Reset();
				for (const TSharedPtr<FBSEnemy>& E : EnemiesInRange(B.X, B.Y, BSDefs::BcRange + 4, Any)) if (!Pick || E->Hp > Pick->Hp) Pick = E;
			}
			if (Pick)
			{
				FBSProjectile P; P.Kind = EBSProjKind::Yamato; P.bFriendly = true;
				P.From = Bow; P.To = FVector(Pick->X, Pick->Y, 0.6f); P.Dur = 0.5f; P.TargetEnemy = Pick; P.Dmg = BSDefs::BcYamato;
				AddProjectileModel(P, TEXT("projYamato"), TEXT("yamatoMissile"));
				Projectiles.Add(P);
				PlaySound(TEXT("bcYamato"), 0.8f);
				Floater(B.X, B.Y, B.Sz + 1, TEXT("YAMATO"), FLinearColor(0.74f, 0.42f, 1));
			}
		}
	}
	else if ((B.CdY -= Dt) <= 0)
	{
		TSharedPtr<FBSEnemy> Pick;
		for (const TSharedPtr<FBSEnemy>& E : EnemiesInRange(B.X, B.Y, BSDefs::BcRange + 4, Any)) if (!Pick || E->Hp > Pick->Hp) Pick = E;
		B.CdY = Pick ? BSDefs::BcYamatoCd : 1.f;
		if (Pick)
		{
			B.YamatoT = 0; B.YamatoTarget = Pick;
			B.YamatoFx = PlayFx(TEXT("yamatoCharge"), Bow, B.Yaw, 1.4f);
			PlaySound(TEXT("bcYamatoCharge"), 0.7f);
			Floater(B.X, B.Y, B.Sz + 1, TEXT("YAMATO CHARGING"), FLinearColor(0.74f, 0.42f, 1));
		}
	}
}

// Point defence: the closest scourge within reach of the hull (a little past the batteries' normal range), else the
// closest devourer. Scourge only have to touch the ship, so they always come first.
TSharedPtr<FBSEnemy> ABSGame::BcThreat() const
{
	TSharedPtr<FBSEnemy> Pick;
	if (!Bc.IsValid()) return Pick;
	float Bs = 1e9f;
	for (const TSharedPtr<FBSEnemy>& E : Enemies)
	{
		if (!E->Alive() || !E->Def->bAntiAir) continue;
		const float D = FMath::Sqrt(Dist2(Bc->X, Bc->Y, E->X, E->Y)) - E->Radius;
		if (D > BSDefs::BcRange + BSDefs::BcPdRange) continue;
		const float S = D - (E->Def->bSuicide ? 100.f : 0.f);
		if (S < Bs) { Bs = S; Pick = E; }
	}
	return Pick;
}

// Warnings so the battlecruiser never dies unannounced: scourge or devourers going for it (at most every few seconds)
// and the hull dropping to half and to a quarter.
void ABSGame::BcAlerts()
{
	FBSBattlecruiser& B = *Bc;
	if (Time >= B.AlertAt)
	{
		int32 NScourge = 0, NDevourers = 0;
		for (const TSharedPtr<FBSEnemy>& E : Enemies)
			if (E->Alive() && E->Def->bAntiAir && E->Target.Kind == FBSTarget::Bc) (E->Def->bSuicide ? NScourge : NDevourers)++;
		if (NScourge || NDevourers)
		{
			B.AlertAt = Time + BSDefs::BcAlertGap;
			Toast(NScourge ? TEXT("Scourge inbound on the Battlecruiser!") : TEXT("Devourers attacking the Battlecruiser!"));
		}
	}
	const float Frac = B.Hp / FMath::Max(1.f, B.MaxHp);
	const int32 Level = Frac <= 0.25f ? 2 : Frac <= 0.5f ? 1 : 0;
	if (Level > B.HullWarn)
	{
		B.HullWarn = Level;
		Toast(Level == 2 ? TEXT("Battlecruiser hull critical!") : TEXT("Battlecruiser hull at 50%"));
		Floater(B.X, B.Y, B.Sz + 1.5f, Level == 2 ? TEXT("HULL CRITICAL") : TEXT("HULL 50%"), FLinearColor(1.f, 0.42f, 0.35f));
	}
}

// Viking wing: four fighters take up posts high over the lane and shoot down air for a while.
bool ABSGame::CallVikings()
{
	if (VikingCd > 0) { Toast(TEXT("Vikings are rearming")); return false; }
	if (Minerals < BSDefs::VikingCost) { Toast(TEXT("Not enough minerals")); return false; }
	Minerals -= BSDefs::VikingCost; VikingCd = BSDefs::VikingCooldown;
	for (int32 i = 0; i < BSDefs::VikingCount; i++)
	{
		const FVector2D Post = StrikePosts[i % StrikePosts.Num()];
		FBSBanshee V; V.Post = FVector2D(Post.X * 1.2f, Post.Y + 2); V.X = Post.X * 0.6f; V.Y = (Mode == EBSMode::Lane ? -18.f : -34.f) - i * 1.2f;
		V.Z = BSDefs::VikingFly + 1.5f; V.Yaw = PI / 2; V.Cd = 0.4f + i * 0.2f;
		V.Actor = AcquireModel(TEXT("vikingfighter"));
		if (V.Actor) { V.Actor->SetTeamColor(TerranColor); V.Actor->Play(WalkNames, true); }
		V.Hp = V.MaxHp = BSDefs::VikingHp;
		VikingWing.Add(MakeShared<FBSBanshee>(V));
	}
	Toast(TEXT("Viking wing inbound"));
	return true;
}

void ABSGame::UpdateVikings(float Dt)
{
	const float HomeY = Mode == EBSMode::Lane ? -20.f : -36.f;
	for (int32 i = VikingWing.Num() - 1; i >= 0; i--)
	{
		FBSBanshee& V = *VikingWing[i];
		V.T += Dt;
		if (V.State == FBSBanshee::In)
		{
			const FVector D = FVector(V.Post.X, V.Post.Y, BSDefs::VikingFly) - FVector(V.X, V.Y, V.Z);
			if (D.Size() > 0.2f) { const FVector Mv = D.GetSafeNormal() * FMath::Min(D.Size(), BSDefs::VikingSpeed * Dt); V.X += Mv.X; V.Y += Mv.Y; V.Z += Mv.Z; V.Yaw = FMath::Atan2(D.Y, D.X); }
			else { V.State = FBSBanshee::Hover; V.T = 0; }
		}
		else if (V.State == FBSBanshee::Hover)
		{
			// A lazy figure-of-eight round the post, turning to engage.
			V.X = V.Post.X + FMath::Sin(V.T * 0.8f + i) * 1.2f; V.Y = V.Post.Y + FMath::Sin(V.T * 1.6f + i) * 0.6f; V.Z = BSDefs::VikingFly + 0.2f * FMath::Sin(V.T * 2 + i);
			TArray<TSharedPtr<FBSEnemy>> L = EnemiesInRange(V.X, V.Y, BSDefs::VikingRange, [](const FBSEnemy& E) { return E.Def->bFlying; });
			if (L.Num()) V.Yaw += FMath::Clamp(FMath::FindDeltaAngleRadians(V.Yaw, FMath::Atan2(L[0]->Y - V.Y, L[0]->X - V.X)), -4 * Dt, 4 * Dt);
			V.Cd -= Dt * (V.AcidUntil > Time ? 0.7f : 1.f);
			if (V.Cd <= 0 && L.Num())
			{
				V.Cd = BSDefs::VikingCd;
				for (int32 k = 0; k < BSDefs::VikingShots; k++)
				{
					const float Off = k ? 0.4f : -0.4f;
					FBSProjectile P; P.Kind = EBSProjKind::Missile; P.bFriendly = true;
					P.From = FVector(V.X - FMath::Sin(V.Yaw) * Off, V.Y + FMath::Cos(V.Yaw) * Off, V.Z - 0.2f); P.To = FVector(L[0]->X, L[0]->Y, L[0]->Z);
					P.T = -k * 0.1f; P.Dur = 0.4f; P.Dmg = BSDefs::VikingDmg; P.TargetEnemy = L[0]; P.ImpactFx = TEXT("vikingImpact");
					AddProjectileModel(P, nullptr, TEXT("vikingMissile"));
					if (!P.Trail) AddProjectileVisual(P, FLinearColor(1, 0.8f, 0.5f), 0.15f);
					Projectiles.Add(P);
				}
				PlayFx(TEXT("vikingLaunch"), FVector(V.X, V.Y, V.Z), V.Yaw);
				PlaySound(TEXT("vikingFire"), 0.45f, 0.2f);
			}
			else if (V.Cd <= 0) V.Cd = 0.1f;
			if (V.T > BSDefs::VikingHover) { V.State = FBSBanshee::Out; V.T = 0; }
		}
		else
		{
			V.Y -= BSDefs::VikingSpeed * Dt; V.Z += 2 * Dt;
			V.Yaw += FMath::Clamp(FMath::FindDeltaAngleRadians(V.Yaw, -PI / 2), -3 * Dt, 3 * Dt);
			if (V.Y <= HomeY) { ReleaseModel(V.Actor); VikingWing.RemoveAt(i); continue; }
		}
		if (V.Actor) V.Actor->SetSim(V.X, V.Y, V.Z, V.Yaw);
	}
}

// Tactical nuke: a ghost paints the densest pack well away from the bunker; the warhead falls, then everything in
// the blast takes 300 (half at the edge).
bool ABSGame::LaunchNuke()
{
	if (NukeCd > 0) { Toast(TEXT("Nuke silo reloading")); return false; }
	if (Minerals < BSDefs::NukeCost) { Toast(TEXT("Not enough minerals")); return false; }
	auto Any = [](const FBSEnemy& E) { return E.Alive(); };
	TSharedPtr<FBSEnemy> Pick; int32 PickN = 0;
	for (const TSharedPtr<FBSEnemy>& E : Enemies)
	{
		if (!E->Alive() || FMath::Sqrt(E->X * E->X + E->Y * E->Y) < BSDefs::NukeMinDist) continue;
		const int32 N = EnemiesInRange(E->X, E->Y, BSDefs::NukeRadius * 0.6f, Any).Num();
		if (N > PickN) { PickN = N; Pick = E; }
	}
	if (!Pick) { Toast(TEXT("No target clear of the bunker")); return false; }
	Minerals -= BSDefs::NukeCost; NukeCd = BSDefs::NukeCooldown;
	FBSNuke N; N.X = Pick->X; N.Y = Pick->Y;
	N.Fall = PlayFx(TEXT("nukeFalling"), FVector(N.X, N.Y, 14), 0, 1.4f);
	Nukes.Add(N);
	PlaySound(TEXT("nukeLaunch"), 0.9f);
	Toast(TEXT("Nuclear launch detected"));
	Floater(N.X, N.Y, 1, TEXT("NUKE"), FLinearColor(1, 0.42f, 0.35f));
	return true;
}

void ABSGame::UpdateNukes(float Dt)
{
	auto Any = [](const FBSEnemy& E) { return E.Alive(); };
	for (int32 i = Nukes.Num() - 1; i >= 0; i--)
	{
		FBSNuke& N = Nukes[i];
		N.T += Dt;
		if (N.T < BSDefs::NukeDelay) { MoveFx(N.Fall, FVector(N.X, N.Y, 14 * (1 - N.T / BSDefs::NukeDelay)), 0); continue; }
		const FBSNuke Done = N;
		Nukes.RemoveAt(i);
		StopFx(Done.Fall);
		if (!PlayFx(TEXT("nuke"), FVector(Done.X, Done.Y, BSTerrain::Height(Done.X, Done.Y)), 0, 1.6f))
			EmitExplosion(FVector(Done.X, Done.Y, 0.5f), BSDefs::NukeRadius, FLinearColor(1, 0.85f, 0.5f), FLinearColor(0.9f, 0.3f, 0.05f), 60);
		for (const TSharedPtr<FBSEnemy>& E : EnemiesInRange(Done.X, Done.Y, BSDefs::NukeRadius, Any))
			HurtEnemy(E, FMath::Sqrt(Dist2(Done.X, Done.Y, E->X, E->Y)) < BSDefs::NukeRadius * 0.5f ? BSDefs::NukeDmg : BSDefs::NukeDmg * 0.5f, true);
		PlaySound(BSSounds::Explosion, 1.f); PlaySound(BSSounds::TankHit, 0.9f);
		Shake = 1;
	}
}

bool ABSGame::FocusAt(float SimX, float SimY)
{
	TSharedPtr<FBSEnemy> BestE; float BestD = 4;
	for (const TSharedPtr<FBSEnemy>& E : Enemies)
	{
		const float D = FMath::Sqrt(Dist2(SimX, SimY, E->X, E->Y)) - E->Radius;
		if (D < BestD) { BestD = D; BestE = E; }
	}
	if (!BestE) return false;
	Focus = BestE; FocusUntil = Time + 4;
	return true;
}

// ---- Waves ---------------------------------------------------------------------------------------------
void ABSGame::StartWave()
{
	if (Wave > 0) WriteTelemetry(TEXT("wave"));
	Tele = FTele();
	Tele.KillsAtStart = Kills; Tele.WaveStart = Time;
	Wave++;
	LingerT = 0;
	const int32 Mule = 30 * CardLevel(TEXT("mule"));
	if (Mule) { Minerals += Mule; Floater(0, -3, 2, FString::Printf(TEXT("+%d MULE"), Mule), FLinearColor(0.5f, 0.85f, 1)); }
	const int32 Wv = Wave;
	const bool bFinale = Wv == BSDefs::FinaleWave && !Finale.bOn;
	auto Quad = [](float X) { return 20 + 8 * X + 0.3f * X * X; };
	WaveLen = FMath::RoundToFloat(BSDefs::WaveLenStart + (BSDefs::WaveLenEnd - BSDefs::WaveLenStart) * FMath::Clamp((Wv - 1) / float(BSDefs::WaveLenBy - 1), 0.f, 1.f));
	float Budget = (Wv <= BSDefs::WaveKnee ? Quad(Wv) : Quad(BSDefs::WaveKnee) + BSDefs::WavePerWaveA * (Wv - BSDefs::WaveKnee) + BSDefs::WavePerWaveB * FMath::Square(float(Wv - BSDefs::WaveKnee))) * (bHard ? 1.15f : 1.f) * (bFinale ? BSDefs::FinaleBudget : 1.f);
	// Late game: every high-tech card unlocked brings stranger zerg, and more of them. Guardians come for the mechs;
	// scourge and devourers only when there is Terran air to hunt. The finale throws in the whole late roster.
	// Rare units (Max: brood queens) never number more than Max on the field and on their way, counting this wave.
	const int32 Tier = bFinale ? FMath::Max(LateTier(), BSDefs::Tech().Num()) : LateTier();
	const bool bAirUp = AirPresence();
	auto Wt = [&](const FBSZergDef* D) { return D->Weight * (D->Late ? 0.6f + 0.4f * Tier : 1.f) * (D->bPreferMech && Mechs.Num() ? 2.f : 1.f) * (D->bAntiAir && !bAirUp ? 0.f : 1.f); };
	TArray<const FBSZergDef*> Types;
	for (const FBSZergDef& D : BSDefs::Zerg())
		if (D.Budget > 0 && Wv >= FMath::CeilToInt(D.Unlock * (bHard ? 0.85f : 1.f)) && (!D.Late || Tier >= D.Late) && (!D.bHops || Mode == EBSMode::Lane)) Types.Add(&D);   // winged zerglings only in lane mode, where they hop the mesa
	TArray<FString> List;
	TMap<FString, int32> Have;
	for (const TSharedPtr<FBSEnemy>& E : Enemies) Have.FindOrAdd(E->Def->Key)++;
	for (const FBSSpawn& S : SpawnQueue) Have.FindOrAdd(S.Type)++;
	auto Can = [&](const FBSZergDef* D) { return D->Budget <= Budget && !(D->Max && Have.FindRef(D->Key) >= D->Max); };
	for (int32 Guard = 0; Budget > 0 && Guard < 400; Guard++)
	{
		float Total = 0;
		for (const FBSZergDef* D : Types) if (Can(D)) Total += Wt(D);
		if (Total <= 0) break;
		float Roll = FMath::FRand() * Total;
		for (const FBSZergDef* D : Types)
		{
			if (!Can(D)) continue;
			Roll -= Wt(D);
			if (Roll <= 0) { List.Add(D->Key); Budget -= D->Budget; Have.FindOrAdd(D->Key)++; break; }
		}
	}
	if (Wv % 5 == 0) { List.Add(Wv >= 15 ? TEXT("ultralisk") : TEXT("roach")); List.Add(TEXT("roach")); }
	// On top of the budget: a zergling swarm that snowballs (4 at wave 1, ~30 by wave 8, ~75 by wave 15, ~100 by wave 20).
	auto Sq = [](float X) { return 2 + 1.2f * X + 0.3f * X * X; };
	const int32 Swarm = FMath::RoundToInt(Wv <= BSDefs::SwarmKnee ? Sq(Wv) : FMath::Min<float>(BSDefs::SwarmMax, Sq(BSDefs::SwarmKnee) + BSDefs::SwarmPerWave * (Wv - BSDefs::SwarmKnee)));
	if (bHard && Wv % 10 == 0 && Wv >= 20) { List.Add(TEXT("ultralisk")); List.Add(TEXT("ultralisk")); }
	for (int32 i = List.Num() - 1; i > 0; i--) List.Swap(i, FMath::RandRange(0, i));

	// Split into packs of 2-5 and spread the packs evenly over the first ~22 s of the wave, so every wave
	// fully arrives before the next one starts (the old schedule let late-wave packs spill over for minutes).
	TArray<TArray<FString>> Packs;
	while (List.Num())
	{
		const int32 N = FMath::Min(List.Num(), 2 + FMath::RandRange(0, 3));
		Packs.Add(TArray<FString>(List.GetData(), N));
		List.RemoveAt(0, N);
	}
	const float Spread = WaveLen - 8;
	int32 Added = 0;
	for (FBSSpawn& Left : SpawnQueue) Left.T = 0.f;      // anything held back by the alive cap comes first
	// Lane packs pick an approach: mostly true north, the rest sweeping in from the north-east and north-west.
	auto PackAng = [this](float FlankChance) { return Mode == EBSMode::Lane ? (FMath::FRand() < FlankChance ? (FMath::RandBool() ? 1.f : 2.f) : 0.f) : FMath::FRand() * 2 * PI; };
	for (int32 g = 0; g < Packs.Num(); g++)
	{
		const float T = 0.5f + Spread * (g + Rnd(0.f, 0.8f)) / Packs.Num();
		const float Ang = PackAng(0.2f);
		for (const FString& Type : Packs[g]) { SpawnQueue.Add({ T, Type, Ang }); Added++; }
	}
	for (int32 Left = Swarm; Left > 0; )
	{
		const int32 N = FMath::Min(Left, 6 + FMath::RandRange(0, 10));
		const float T = 0.5f + Rnd(0.f, Spread);
		const float Ang = PackAng(0.55f);
		// Late game (lane mode): some of the swarm are winged zerglings that hop straight over the mesa, a few more with
		// every tech card past the one that brings them.
		const int32 WlLate = BSDefs::FindZerg(TEXT("wingling"))->Late;
		const float Wl = Mode == EBSMode::Lane && Tier >= WlLate ? FMath::Min(0.4f, 0.1f * (Tier - WlLate + 1)) : 0.f;
		for (int32 k = 0; k < N; k++) { SpawnQueue.Add({ T + k * 0.04f, FMath::FRand() < Wl ? TEXT("wingling") : TEXT("zergling"), Ang }); Added++; }
		Left -= N;
	}
	// Late game: overlords drop zerglings behind our lines, and nydus worms breach there.
	if (Tier)
	{
		const int32 DropsN = FMath::FRand() < 0.4f + 0.1f * Tier ? 1 + FMath::FloorToInt(FMath::FRand() * (1 + Tier / 2.f)) : 0;
		for (int32 i = 0; i < DropsN; i++) { SpawnQueue.Add({ 4 + Rnd(0.f, Spread), TEXT("overlord"), 0.f }); Added++; }
		if (Tier >= 2 && FMath::FRand() < 0.25f + 0.08f * Tier) { SpawnQueue.Add({ 6 + Rnd(0.f, Spread * 0.6f), TEXT("nydus"), 0.f }); Added++; }
	}
	// Mid game on: queens walk with the wave planting creep tumors, and (two waves later) overlords drift over the field
	// generating creep.
	const int32 Cw = CreepWave();
	if (Wv >= Cw)
	{
		const int32 Queens = FMath::Min(4, 1 + (Wv - Cw) / 6);
		for (int32 i = 0; i < Queens; i++) { SpawnQueue.Add({ 1 + Rnd(0.f, Spread * 0.7f), TEXT("queen"), PackAng(0.3f) }); Added++; }
		if (Wv == Cw) Toast(TEXT("Zerg queens are spreading creep"));
	}
	if (Wv >= Cw + 2)
	{
		const int32 Lords = FMath::Min(3, 1 + (Wv - Cw - 2) / 8);
		for (int32 i = 0; i < Lords; i++) { SpawnQueue.Add({ 2 + Rnd(0.f, Spread), TEXT("overlordCreep"), 0.f }); Added++; }
	}
	SpawnQueue.Sort([](const FBSSpawn& A, const FBSSpawn& B) { return A.T < B.T; });
	Tele.Spawned = Added;
	SpawnTimer = 0;
	WaveTimer = WaveLen;
	Minerals += 20 + 8 * Wv;
	WaveBanner = FString::Printf(TEXT("WAVE %d"), Wv);
	if (Wv % 5 == 0) WaveBanner += TEXT("  //  HEAVY ASSAULT");
	WaveBannerT = Time; WaveSerial++;
	for (const BSDefs::FMilestone& Ms : BSDefs::Milestones)
	{
		if (Ms.Wave != Wv || bFinale) continue;
		TArray<FString> Items; FString(Ms.Zerg).ParseIntoArray(Items, TEXT(","));
		for (const FString& It : Items)
		{
			FString K, Nstr; It.Split(TEXT(":"), &K, &Nstr);
			for (int32 i = 0; i < FCString::Atoi(*Nstr); i++) SpawnQueue.Add({ 3 + Rnd(0.f, Spread * 0.6f), K, PackAng(0.4f) });
		}
		for (int32 i = 0; i < Ms.Nydus; i++) SpawnQueue.Add({ 5.f + i * 4, TEXT("nydus"), 0.f });
		SpawnQueue.Sort([](const FBSSpawn& A, const FBSSpawn& B) { return A.T < B.T; });
		if (Ms.Pods) DropPods(Ms.Pods, 6, 2.5f);
		WaveBanner = FString::Printf(TEXT("WAVE %d  //  %s"), Wv, Ms.Name);
		Toast(Ms.Toast);
		Shake = 0.6f;
	}
	if (bFinale) StartFinale();
}

// ---- Wave 100 finale ------------------------------------------------------------------------------------
// Kerrigan leads a leviathan and a maxed-out wave in person, while drop pods slam down all round the base.
void ABSGame::StartFinale()
{
	Finale.bOn = true; Finale.T = Time;
	SpawnQueue.Add({ 2.f, TEXT("leviathan"), 0.f });
	SpawnQueue.Add({ 5.f, TEXT("kerrigan"), 0.f });
	SpawnQueue.Sort([](const FBSSpawn& A, const FBSSpawn& B) { return A.T < B.T; });
	DropPods(BSDefs::FinalePods, BSDefs::PodStart, BSDefs::PodEvery);
	WaveBanner = FString::Printf(TEXT("WAVE %d  //  THE QUEEN OF BLADES"), Wave); WaveBannerT = Time; WaveSerial++;
	Toast(TEXT("Kerrigan leads the swarm in person!"));
	PlaySound(TEXT("finaleRoar"), 1.f);
	Shake = 1;
}

// Drop pods: N of them, one every few seconds, each from a different side (behind our lines too), on open low ground.
void ABSGame::DropPods(int32 N, float T0, float Every)
{
	static const TCHAR* PodZerg[] = { TEXT("hydralisk"), TEXT("hydralisk"), TEXT("roach"), TEXT("roach"), TEXT("lurker") };
	const bool bLane = Mode == EBSMode::Lane;
	const float A0 = FMath::FRand() * 2 * PI;
	for (int32 i = 0; i < N; i++)
	{
		const int32 Slot = (i * 3) % N;               // stepping round the compass, so consecutive pods come from far apart
		float X = 0, Y = -Rnd(5, 9);
		bool bOk = false;
		for (int32 Guard = 0; Guard < 40 && !bOk; Guard++)
		{
			const float A = A0 + Slot * 2 * PI / N + Rnd(-0.3f, 0.3f) + Guard * 0.05f, R = Rnd(BSDefs::PodMinR, BSDefs::PodMaxR);
			const float Px = FMath::Cos(A) * R, Py = FMath::Sin(A) * R;
			if (bLane && (FMath::Abs(Px) > 20 || BSTerrain::LowSdf(Px, Py) > -0.4f || !BSTerrain::Walkable(Px, Py))) continue;
			X = Px; Y = Py; bOk = true;
		}
		FBSPod P;
		P.X = X; P.Y = Y; P.Yaw = Rnd(0, 2 * PI); P.T = -(T0 + i * Every + Rnd(0, 0.5f));
		if (i % 2 == 0) P.Cargo.Add(TEXT("ultralisk"));
		while (P.Cargo.Num() < BSDefs::PodCargo) P.Cargo.Add(PodZerg[FMath::RandRange(0, 4)]);
		Pods.Add(P);
	}
}

void ABSGame::UpdatePods(float Dt)
{
	for (int32 i = Pods.Num() - 1; i >= 0; i--)
	{
		FBSPod& P = Pods[i];
		P.T += Dt;
		if (P.T < 0) continue;
		const float Gz = BSTerrain::Height(P.X, P.Y);
		if (!P.bDropped)
		{
			P.bDropped = true;
			// The pod model streaks down and bursts open by itself (its Birth sequence); its trail rides along.
			P.Actor = AcquireModel(TEXT("droppod"));
			if (P.Actor) { P.Actor->SetTeamColor(ZergColor); P.Actor->SetSim(P.X, P.Y, Gz, P.Yaw); P.Actor->Play(TEXT("birth"), false); }
			PlayFx(TEXT("podFall"), FVector(P.X, P.Y, Gz), P.Yaw);
			PlaySound(TEXT("podDrop"), 0.6f, 0.2f);
		}
		if (!P.bLanded && P.T >= BSDefs::PodLand)
		{
			P.bLanded = true;
			if (!PlayFx(TEXT("podImpact"), FVector(P.X, P.Y, Gz), P.Yaw))
				EmitExplosion(FVector(P.X, P.Y, Gz + 0.3f), 1.6f, FLinearColor(0.8f, 1.f, 0.4f), FLinearColor(0.3f, 0.5f, 0.05f), 16);
			PlaySound(TEXT("podImpact"), 0.7f, 0.15f);
			Shake = FMath::Min(1.f, Shake + 0.3f);
			const TArray<FBSTarget> Under = Targets;
			for (const FBSTarget& T : Under) if (FMath::Sqrt(Dist2(P.X, P.Y, T.X, T.Y)) - T.Radius <= BSDefs::PodRadius) DamageDefender(T, BSDefs::PodDmg);
			if (!bPodsLanded) { bPodsLanded = true; Toast(TEXT("Drop pods landing all round the base!")); }
		}
		if (P.bLanded && P.T >= BSDefs::PodLand + 0.5f && P.Cargo.Num() && (P.SpawnT -= Dt) <= 0)
		{
			P.SpawnT = 0.3f;
			const FString K = P.Cargo[0]; P.Cargo.RemoveAt(0);
			const FVector2D At(P.X + Rnd(-1.2f, 1.2f), P.Y + Rnd(-1.2f, 1.2f));
			SpawnEnemy(K, 0, &At);
		}
		if (!P.Cargo.Num() && P.T > BSDefs::PodLand + 6) { ReleaseModel(P.Actor); Pods.RemoveAt(i); }
	}
}

// Kerrigan: Leaping Strike onto a defender out of reach, Apocalypse on the thickest knot of defenders. Returns true while
// she is in the air or channelling (her usual walking and fighting wait).
bool ABSGame::UpdateKerrigan(const TSharedPtr<FBSEnemy>& EP, const FBSTarget& T, float Dt)
{
	FBSEnemy& E = *EP;
	E.LeapCd -= Dt; E.ApocCd -= Dt;
	if (E.BlastAt >= 0 && Time >= E.BlastAt)
	{
		E.BlastAt = -1;
		StopFx(E.BlastFx); E.BlastFx = 0;
		const FVector At(E.Blast.X, E.Blast.Y, BSTerrain::Height(E.Blast.X, E.Blast.Y));
		if (!PlayFx(TEXT("apocalypse"), At, 0, 1.2f)) EmitExplosion(At + FVector(0, 0, 0.3f), BSDefs::ApocRadius, FLinearColor(0.85f, 0.5f, 1.f), FLinearColor(0.35f, 0.1f, 0.6f), 24);
		const TArray<FBSTarget> Under = Targets;
		for (const FBSTarget& O : Under) if (FMath::Sqrt(Dist2(E.Blast.X, E.Blast.Y, O.X, O.Y)) - O.Radius <= BSDefs::ApocRadius) DamageDefender(O, BSDefs::ApocDmg);
		PlaySound(BSSounds::Explosion, 0.9f);
		Shake = FMath::Min(1.f, Shake + 0.5f);
	}
	if (E.LeapT >= 0)
	{
		E.LeapT += Dt;
		const float K = FMath::Clamp(E.LeapT / BSDefs::LeapDur, 0.f, 1.f);
		E.X = E.LeapFrom.X + (E.LeapTo.X - E.LeapFrom.X) * K; E.Y = E.LeapFrom.Y + (E.LeapTo.Y - E.LeapFrom.Y) * K;
		E.Z = BSTerrain::Height(E.X, E.Y) + FMath::Sin(K * PI) * 3;
		if (K >= 1)
		{
			E.LeapT = -1; E.Z = BSTerrain::Height(E.X, E.Y); E.Anim.Reset();
			PlayFx(TEXT("leapImpact"), FVector(E.X, E.Y, E.Z), E.Yaw, 1.4f);
			const TArray<FBSTarget> Under = Targets;
			for (const FBSTarget& O : Under) if (FMath::Sqrt(Dist2(E.X, E.Y, O.X, O.Y)) - O.Radius <= BSDefs::LeapRadius) DamageDefender(O, BSDefs::LeapDmg);
			PlaySound(TEXT("ultraliskAttack"), 0.6f);
			Shake = FMath::Min(1.f, Shake + 0.4f);
		}
		return true;
	}
	if (E.CastUntil > Time) return true;
	const float Reach = FMath::Sqrt(Dist2(E.X, E.Y, T.X, T.Y)) - T.Radius - E.Radius;
	if (E.ApocCd <= 0 && Reach <= BSDefs::ApocRange)
	{
		// The defender with the most others round it.
		const FBSTarget* Knot = nullptr; int32 BestN = -1;
		for (const FBSTarget& O : Targets)
		{
			if (FMath::Sqrt(Dist2(E.X, E.Y, O.X, O.Y)) - O.Radius > BSDefs::ApocRange + E.Radius) continue;
			int32 N = 0;
			for (const FBSTarget& Q : Targets) N += Dist2(Q.X, Q.Y, O.X, O.Y) < BSDefs::ApocRadius * BSDefs::ApocRadius;
			if (N > BestN) { BestN = N; Knot = &O; }
		}
		if (Knot)
		{
			E.ApocCd = BSDefs::ApocCd; E.CastUntil = Time + BSDefs::ApocDelay; E.Anim = TEXT("spell");
			if (E.Actor) E.Actor->Play(TEXT("spell"), false);
			E.Yaw = FMath::Atan2(Knot->Y - E.Y, Knot->X - E.X);
			E.Blast = FVector2D(Knot->X, Knot->Y); E.BlastAt = Time + BSDefs::ApocDelay;
			E.BlastFx = PlayFx(TEXT("apocalypseChannel"), FVector(Knot->X, Knot->Y, BSTerrain::Height(Knot->X, Knot->Y)), 0, 1.2f);
			PlaySound(TEXT("kerriganPsi"), 0.8f);
			Floater(E.X, E.Y, E.Z + 3, TEXT("APOCALYPSE"), FLinearColor(0.75f, 0.55f, 1.f));
			return true;
		}
	}
	if (E.LeapCd <= 0 && Reach > BSDefs::LeapMin && Reach < BSDefs::LeapMax)
	{
		const float A = FMath::Atan2(E.Y - T.Y, E.X - T.X), Off = T.Radius + E.Radius + 0.2f;
		const float Tx = T.X + FMath::Cos(A) * Off, Ty = T.Y + FMath::Sin(A) * Off;
		if (BSTerrain::Walkable(Tx, Ty))
		{
			E.LeapCd = BSDefs::LeapCd; E.Route.Reset(); E.bPerched = false;
			E.LeapT = 0; E.LeapFrom = FVector2D(E.X, E.Y); E.LeapTo = FVector2D(Tx, Ty);
			E.Yaw = FMath::Atan2(Ty - E.Y, Tx - E.X); E.Anim = TEXT("leap");
			if (E.Actor) E.Actor->Play(TEXT("jump"), true);
			PlayFx(TEXT("psionicShift"), FVector(E.X, E.Y, E.Z + 1), E.Yaw);
			PlaySound(TEXT("kerriganLeap"), 0.8f);
			Floater(E.X, E.Y, E.Z + 3, TEXT("LEAPING STRIKE"), FLinearColor(0.75f, 0.55f, 1.f));
			return true;
		}
		E.LeapCd = 1;
	}
	return false;
}

// The leviathan bombards like any ranged zerg (bile, see UpdateProjectiles) and every so often spawns a brood of mutalisks.
void ABSGame::UpdateLeviathan(const TSharedPtr<FBSEnemy>& EP, float Dt)
{
	FBSEnemy& E = *EP;
	if ((E.BroodCd -= Dt) > 0) return;
	E.BroodCd = BSDefs::BroodCd;
	for (int32 k = 0; k < BSDefs::BroodCount; k++) { const FVector2D At(E.X + Rnd(-2.5f, 2.5f), E.Y + Rnd(-2.5f, 2.5f)); SpawnEnemy(TEXT("mutalisk"), 0, &At); }
	E.CastUntil = Time + 1.2f; E.Anim = TEXT("spell");
	if (E.Actor) E.Actor->Play(TEXT("spell"), false);
	PlaySound(TEXT("finaleRoar"), 0.7f, 2.f);
	Floater(E.X, E.Y, E.Z + 2, TEXT("MUTALISKS"), FLinearColor(0.75f, 0.55f, 1.f));
}

// A boss is down; both down (with none still to come) wins the finale. The game carries on.
void ABSGame::BossDown(FBSEnemy& E)
{
	if (E.BlastAt >= 0) { StopFx(E.BlastFx); E.BlastFx = 0; E.BlastAt = -1; }
	Toast(E.Def->Name + TEXT(" is down!"));
	Floater(E.X, E.Y, E.Z + 3, E.Def->Name.ToUpper() + TEXT(" DOWN"), FLinearColor(1.f, 0.83f, 0.3f));
	Shake = 1;
	if (!Finale.bOn || Finale.bWon || Bosses().Num()) return;
	for (const FBSSpawn& S : SpawnQueue) { const FBSZergDef* D = BSDefs::FindZerg(S.Type); if (D && !D->Boss.IsEmpty()) return; }
	Finale.bWon = true; Finale.WonAt = Time;
	Score += BSDefs::FinaleVictory;
	WaveBanner = TEXT("VICTORY  //  THE QUEEN OF BLADES HAS FALLEN"); WaveBannerT = Time; WaveSerial++;
	Toast(TEXT("Victory! The swarm fights on without her"));
}

TSharedPtr<FBSEnemy> ABSGame::SpawnEnemy(const FString& Type, float Ang, const FVector2D* At)
{
	const FBSZergDef* D = BSDefs::FindZerg(Type);
	if (!D) return nullptr;
	float X, Y;
	const bool bLane = Mode == EBSMode::Lane;
	const float SideS = FMath::RandBool() ? 1.f : -1.f, Ar = FMath::FRand() * 2 * PI;
	if (At) { X = At->X + Rnd(-0.6f, 0.6f); Y = At->Y + Rnd(-0.6f, 0.6f); }
	else if (D->bTransport) { X = bLane ? SideS * 23 : FMath::Cos(Ar) * 27; Y = bLane ? Rnd(-3, 6) : FMath::Sin(Ar) * 27; }          // overlords come in from a flank
	else if (D->bNydus) { const float R = Rnd(5.5f, 8); X = bLane ? SideS * Rnd(2.5f, 4.5f) : FMath::Cos(Ar) * R; Y = bLane ? Rnd(-8.5f, -7) : FMath::Sin(Ar) * R; }   // behind our lines
	else if (D->bCreeper) { X = bLane ? SideS * Rnd(10, 18) : FMath::Cos(Ar) * 29; Y = bLane ? Rnd(30, 34) : FMath::Sin(Ar) * 29; }   // creep overlords drift in from afar
	else if (Mode == EBSMode::Lane)
	{
		const int32 Side = FMath::RoundToInt(Ang);         // 0 north, 1 north-east, 2 north-west
		if ((Side == 1 || Side == 2) && !D->bHops)        // winged zerglings always come from the north, over the mesa
		{
			// Somewhere down the arm (see BSTerrain), not on the cliffs beside it.
			const FVector2D P = BSTerrain::Active() ? BSTerrain::ArmPoint(Side == 1 ? 1 : -1, Rnd(0.55f, 0.85f), Rnd(-2.2f, 2.2f)) : FVector2D((Side == 1 ? 1 : -1) * Rnd(10.f, 13.f), Rnd(17, 23));
			X = P.X; Y = P.Y;
		}
		else { X = Rnd(-4.6f, 4.6f); Y = Rnd(27, 31); }
	}
	else { const float R = Rnd(25, 28); X = FMath::Cos(Ang + Rnd(-0.35f, 0.35f)) * R; Y = FMath::Sin(Ang + Rnd(-0.35f, 0.35f)) * R; }
	TSharedPtr<FBSEnemy> E = MakeShared<FBSEnemy>();
	E->Id = NextId++; E->Def = D; E->X = X; E->Y = Y; E->Z = D->bFlying ? D->Fly : BSTerrain::Height(X, Y); E->Yaw = FMath::Atan2(-Y, -X);
	E->Hp = E->MaxHp = D->Boss.IsEmpty() ? D->Hp * (1 + BSDefs::WaveHp * (bHard ? 1.25f : 1.f) * FMath::Max(0, Wave - BSDefs::WaveHpFrom)) : D->Hp;   // late waves get tougher
	if (!D->Boss.IsEmpty()) { E->LeapCd = 3; E->ApocCd = 6; E->BroodCd = 8; }
	E->Cd = Rnd(0, D->Cd); E->Radius = D->Radius; E->Life = D->Life;
	// Lane terrain: ground zerg follow the Y; some ranged ones climb a ramp to a ledge over the bunker.
	if (!D->bFlying && !At && !D->bHops && !D->bNydus)
	{
		const bool bRanged = D->Projectile == EBSProjectile::Acid || D->Projectile == EBSProjectile::Spine || D->Projectile == EBSProjectile::Bullet;
		E->bHighRoute = Mode == EBSMode::Lane && bRanged && FMath::FRand() < 0.4f;
		E->Route = BSTerrain::Route(X, Y, E->bHighRoute);
	}
	// A strain or skin variant now and then, for variety (only models with the animations the game needs). With
	// VariantFrom the variety dials up slowly (see FBSZergDef).
	E->ModelKey = D->Model; E->DeathKey = D->Death;
	int32 Pool = D->Variants.Num();
	float Share = 0.45f;
	if (Pool && D->VariantFrom)
	{
		const int32 Vw = Wave - D->VariantFrom;
		Pool = Vw >= 0 ? FMath::Min(Pool, 1 + Vw / FMath::Max(1, D->VariantStep)) : 0;
		Share = FMath::Min(0.45f, 0.04f * (Vw + 1));
	}
	if (Pool && FMath::FRand() < Share)
	{
		const FString& V = D->Variants[FMath::RandRange(0, Pool - 1)];
		const FBSModel* VM = Assets->Model(V);
		if (VM && VM->Find(WalkNames) && (D->AttackAnims.Num() == 0 || VM->Find(D->AttackAnims)))
		{
			E->ModelKey = V;
			if (Assets->Model(V + TEXT("Death"))) E->DeathKey = V + TEXT("Death");
		}
	}
	E->Actor = AcquireModel(E->ModelKey);
	if (E->Actor)
	{
		E->Actor->SetScale(D->Scale);
		E->Actor->Mesh->SetCastShadow(!(D->bLight && Enemies.Num() > BSDefs::ShadowCrowd));
		E->Actor->SetTeamColor(ZergColor);
		E->Actor->SetSim(X, Y, E->Z, E->Yaw);
	}
	PlayEnemyAnim(*E, TEXT("walk"));
	E->Seed = Rnd(0, 10);
	if (D->bTransport)
	{
		const float A2 = FMath::FRand() * 2 * PI;
		E->Exit = FVector2D(X, Y); E->Cargo = 5 + LateTier();
		E->Drop = bLane ? FVector2D(Rnd(-4, 4), Rnd(-6.5f, -3)) : FVector2D(FMath::Cos(A2) * Rnd(4, 6.5f), FMath::Sin(A2) * Rnd(4, 6.5f));
	}
	if (D->bNydus)
	{
		E->Z = BSTerrain::Height(X, Y) - 3;
		static const TCHAR* Kinds[] = { TEXT("zergling"), TEXT("zergling"), TEXT("zergling"), TEXT("roach"), TEXT("hydralisk") };   // no winglings: they only come over the mesa
		for (int32 i = 0; i < 6 + 2 * LateTier(); i++) E->CargoList.Add(Kinds[FMath::RandRange(0, 4)]);
		PlayFx(TEXT("nydusBirth"), FVector(X, Y, BSTerrain::Height(X, Y)), 0, 1.4f);
		Toast(TEXT("Nydus worm breaching behind our lines!"));
	}
	SetupCreeper(*E);
	Enemies.Add(E);
	return E;
}

// Gone without a kill (an overlord flying off, a nydus worm burrowing away).
void ABSGame::Despawn(const TSharedPtr<FBSEnemy>& E)
{
	E->Hp = 0;
	Enemies.Remove(E);
	ReleaseModel(E->Actor); E->Actor = nullptr;
	if (Focus.Pin() == E) Focus.Reset();
}

// Overlord drop: fly to a point behind our lines, unload zerglings one by one, fly back out. Badly hurt, it drops the
// cargo wherever it is.
void ABSGame::UpdateOverlord(const TSharedPtr<FBSEnemy>& EP, float Dt)
{
	FBSEnemy& E = *EP;
	enum { In, Unload, Out };
	if (E.State == Unload)
	{
		E.UnloadT -= Dt;
		if (E.UnloadT <= 0 && E.Cargo > 0) { E.Cargo--; E.UnloadT = 0.2f; const FVector2D At(E.X, E.Y); SpawnEnemy(TEXT("zergling"), 0, &At); }
		if (E.Cargo <= 0) { E.State = Out; PlayEnemyAnim(E, TEXT("walk")); }
		return;
	}
	const FVector2D G = E.State == Out ? E.Exit : E.Drop;
	const float Dx = G.X - E.X, Dy = G.Y - E.Y, Dd = FMath::Sqrt(Dx * Dx + Dy * Dy);
	if (Dd < 0.3f || (E.State == In && E.Hp < E.MaxHp * 0.5f))
	{
		if (E.State == In) { E.State = Unload; E.UnloadT = 0.4f; PlayEnemyAnim(E, TEXT("stand")); PlaySound(TEXT("overlordUnload"), 0.6f); Toast(TEXT("Overlord dropping zerglings behind our lines")); }
		else Despawn(EP);
		return;
	}
	const float S = FMath::Min(Dd, E.Def->Speed * Dt);
	E.X += Dx / Dd * S; E.Y += Dy / Dd * S;
	E.Yaw += FMath::Clamp(FMath::FindDeltaAngleRadians(E.Yaw, FMath::Atan2(Dy, Dx)), -2 * Dt, 2 * Dt);
	PlayEnemyAnim(E, TEXT("walk"));
}

// Nydus worm: rises out of the ground, disgorges its cargo, then sinks back unless it is killed first.
void ABSGame::UpdateNydus(const TSharedPtr<FBSEnemy>& EP, float Dt)
{
	FBSEnemy& E = *EP;
	enum { Birth, Open, Close };
	E.T += Dt;
	const float Gz = BSTerrain::Height(E.X, E.Y);
	if (E.State == Birth)
	{
		E.Z = Gz - 3 * (1 - FMath::Clamp(E.T / 1.6f, 0.f, 1.f));
		if (E.T >= 1.6f) { E.State = Open; E.T = 0; PlayEnemyAnim(E, TEXT("stand")); PlaySound(TEXT("nydusUnload"), 0.8f); }
		return;
	}
	if (E.State == Open)
	{
		E.SpawnT -= Dt;
		if (E.SpawnT <= 0 && E.CargoList.Num()) { E.SpawnT = 0.8f; const FVector2D At(E.X + Rnd(-1, 1), E.Y + Rnd(-1, 1)); const FString K = E.CargoList[0]; E.CargoList.RemoveAt(0); SpawnEnemy(K, 0, &At); }
		if (!E.CargoList.Num() && E.T > 14) { E.State = Close; E.T = 0; }
		return;
	}
	E.Z = Gz - 3 * FMath::Clamp(E.T / 1.6f, 0.f, 1.f);
	if (E.T >= 1.6f) Despawn(EP);
}

// Brood queen (Brood War): Spawn Broodling on the most valuable ground unit in reach: a sieged tank, then a goliath,
// thor or odin, then a trooper outside the bunker, then an SCV (the nearest within each).
const FBSTarget* ABSGame::BroodTarget(const FBSEnemy& E) const
{
	const FBSTarget* Pick = nullptr; float Bs = -1e9f;
	for (const FBSTarget& T : Targets)
	{
		const int32 Pr = BroodPriority(T.Kind);
		if (!Pr || !T.IsValid(Bunker)) continue;
		const float Dd = FMath::Sqrt(Dist2(E.X, E.Y, T.X, T.Y)) - T.Radius - E.Radius;
		if (Dd > E.Def->Range + 1) continue;
		if (Pr * 100 - Dd > Bs) { Bs = Pr * 100 - Dd; Pick = &T; }
	}
	return Pick;
}

// A parasite dart flies at the victim (see BroodlingHit); then the queen backs off while the next one grows.
void ABSGame::CastBroodling(FBSEnemy& E)
{
	const FBSTarget* Tg = BroodTarget(E);
	if (!Tg) { E.Cd = 1; return; }                 // nothing worth it in reach: look again in a second
	E.Anim = TEXT("spell"); E.CastUntil = Time + 1;
	if (E.Actor) E.Actor->Play(TEXT("spell"), false);
	E.bBackOff = true;
	FBSProjectile P;
	P.bFriendly = false; P.Kind = EBSProjKind::BroodSpell; P.ZDef = E.Def; P.TargetDef = *Tg;
	P.From = FVector(E.X, E.Y, E.Z + E.Def->Height * 0.6f);
	P.To = FVector(Tg->X, Tg->Y, BSTerrain::Height(Tg->X, Tg->Y) + 0.8f);
	P.Arc = 0.6f; P.Dur = FMath::Clamp(FMath::Sqrt(Dist2(E.X, E.Y, Tg->X, Tg->Y)) / 16.f, 0.25f, 0.8f);
	AddProjectileModel(P, nullptr, TEXT("broodSpellMissile"));
	if (!P.Trail) AddProjectileVisual(P, FLinearColor(0.7f, 1, 0.35f), 0.2f);
	Projectiles.Add(P);
}

// Spawn Broodling lands. Troopers and SCVs die outright, as in Brood War; tanks and mechs are too big to hatch in one go
// here and take BroodSpellDmg (armour ignored) instead. Two broodlings burst out of the victim and fight for 8 s.
void ABSGame::BroodlingHit(const FBSTarget& T)
{
	float Hp = 0, Armor = 0;
	switch (T.Kind)
	{
	case FBSTarget::Tank: if (auto P = T.TankRef.Pin()) { Hp = P->Hp; Armor = P->Armor; } break;
	case FBSTarget::Mech: if (auto P = T.MechRef.Pin()) { Hp = P->Hp; Armor = P->Def->Armor; } break;
	case FBSTarget::Trooper: if (auto P = T.TrooperRef.Pin()) Hp = P->Hp; break;
	case FBSTarget::Scv: if (auto P = T.ScvRef.Pin()) Hp = P->Hp; break;
	default: return;
	}
	const bool bKill = T.Kind == FBSTarget::Trooper || T.Kind == FBSTarget::Scv;
	const float X = T.X, Y = T.Y, Gz = BSTerrain::Height(X, Y);
	if (!PlayFx(TEXT("broodSpellImpact"), FVector(X, Y, Gz + 0.3f), 0))
		AddFx(EBSFxKind::Blast, FVector(X, Y, Gz + 0.05f), FVector::ZeroVector, 0.4f, 0.9f, FLinearColor(0.6f, 1, 0.25f), 6);
	PlaySound(TEXT("broodSpell"), 0.8f); PlaySound(TEXT("broodlingBirth"), 0.5f);
	DamageDefender(T, bKill ? Hp + 100 : BroodSpellDmg + Armor);
	Floater(X, Y, Gz + 3, bKill ? FString(TEXT("BROODLINGS")) : FString::Printf(TEXT("BROODLINGS -%d"), FMath::RoundToInt(BroodSpellDmg)), FLinearColor(0.71f, 1.f, 0.35f));
	for (int32 k = 0; k < 2; k++) if (Enemies.Num() < MaxAlive()) { const FVector2D At(X, Y); SpawnEnemy(TEXT("broodling"), 0, &At); }
}

// ---- Creep ------------------------------------------------------------------------------------------------
namespace { enum ECreeperState : int32 { CreeperIn = 0, CreeperWork = 1, CreeperOut = 2 }; }

// A coarse coverage grid over the field (see BSCreep); the creep overlay mesh is shaded from it.
void ABSGame::InitCreep()
{
	const bool bLane = Mode == EBSMode::Lane;
	Creep = FCreepGrid();
	Creep.X0 = bLane ? -24.f : -30.f; Creep.Y0 = bLane ? -18.f : -30.f; Creep.Cell = BSCreep::Cell;
	Creep.Nx = FMath::RoundToInt((bLane ? 48.f : 60.f) / BSCreep::Cell); Creep.Ny = FMath::RoundToInt((bLane ? 52.f : 60.f) / BSCreep::Cell);
	Creep.C.Init(0.f, Creep.Nx * Creep.Ny); Creep.Burn.Init(0.f, Creep.Nx * Creep.Ny); Creep.Src.Init(0, Creep.Nx * Creep.Ny);
	BuildCreepMesh();
}

float ABSGame::CreepAt(float X, float Y) const
{
	if (!Creep.Nx) return 0;
	const int32 I = FMath::FloorToInt((X - Creep.X0) / Creep.Cell), J = FMath::FloorToInt((Y - Creep.Y0) / Creep.Cell);
	return I < 0 || J < 0 || I >= Creep.Nx || J >= Creep.Ny ? 0.f : Creep.C[J * Creep.Nx + I];
}

// Visit the cells whose centres lie within R of (X, Y).
void ABSGame::CreepCells(float X, float Y, float R, TFunctionRef<void(int32)> Fn) const
{
	const FCreepGrid& G = Creep;
	const int32 I0 = FMath::Max(0, FMath::FloorToInt((X - R - G.X0) / G.Cell)), I1 = FMath::Min(G.Nx - 1, FMath::FloorToInt((X + R - G.X0) / G.Cell));
	const int32 J0 = FMath::Max(0, FMath::FloorToInt((Y - R - G.Y0) / G.Cell)), J1 = FMath::Min(G.Ny - 1, FMath::FloorToInt((Y + R - G.Y0) / G.Cell));
	for (int32 j = J0; j <= J1; j++)
		for (int32 i = I0; i <= I1; i++)
		{
			const float Cx = G.X0 + (i + 0.5f) * G.Cell, Cy = G.Y0 + (j + 0.5f) * G.Cell;
			if ((Cx - X) * (Cx - X) + (Cy - Y) * (Cy - Y) <= R * R) Fn(j * G.Nx + i);
		}
}

// Four times a second: creep grows where a tumor or a generating overlord reaches, fades slowly (lingers) elsewhere, and
// stays away for a while where firebats scorched the ground.
void ABSGame::UpdateCreep(float Dt)
{
	FCreepGrid& G = Creep;
	if (!G.Nx) return;
	G.T += Dt;
	if (G.T < BSCreep::Update) return;
	const float St = G.T; G.T = 0;
	FMemory::Memzero(G.Src.GetData(), G.Src.Num());
	auto Mark = [&G](int32 K) { G.Src[K] = 1; };
	for (const TSharedPtr<FBSEnemy>& E : Enemies)
	{
		if (!E->Alive()) continue;
		if (E->Def->bTumor) CreepCells(E->X, E->Y, E->CreepR, Mark);
		else if (E->Def->bCreeper && E->State == CreeperWork) CreepCells(E->X, E->Y, BSCreep::OverlordRadius, Mark);
	}
	int32 Cover = 0; bool bChanged = false;
	for (int32 K = 0; K < G.C.Num(); K++)
	{
		if (G.Burn[K] > 0) G.Burn[K] = FMath::Max(0.f, G.Burn[K] - St);
		const float V = G.C[K], Nv = G.Src[K] && G.Burn[K] <= 0 ? FMath::Min(1.f, V + BSCreep::Grow * St) : FMath::Max(0.f, V - BSCreep::Decay * St);
		if (Nv != V) { G.C[K] = Nv; bChanged = true; }
		if (Nv > BSCreep::On) Cover++;
	}
	G.Cover = Cover * G.Cell * G.Cell;           // square metres under creep
	if (bChanged) G.Version++;
}

// Firebat flames: the creep in the burst burns away and the ground stays scorched for a while; tumors there are singed.
void ABSGame::BurnCreep(float X, float Y, float R, bool bSinge)
{
	FCreepGrid& G = Creep;
	if (!G.Nx) return;
	bool bHit = false;
	CreepCells(X, Y, R, [&G, &bHit](int32 K) { if (G.C[K] > 0) bHit = true; G.C[K] = FMath::Max(0.f, G.C[K] - 0.85f); G.Burn[K] = BSCreep::BurnRegrow; });
	if (bHit) G.Version++;
	if (!bSinge) return;
	TArray<TSharedPtr<FBSEnemy>> Snapshot = Enemies;
	for (const TSharedPtr<FBSEnemy>& E : Snapshot)
		if (E->Def->bTumor && E->Alive() && FMath::Sqrt(Dist2(X, Y, E->X, E->Y)) - E->Radius <= R) HurtEnemy(E, BSCreep::BurnTumorDmg, true);
}

int32 ABSGame::TumorCount() const
{
	int32 N = 0;
	for (const TSharedPtr<FBSEnemy>& E : Enemies) if (E->Def->bTumor && E->Alive()) N++;
	return N;
}

bool ABSGame::WaveAlive() const
{
	for (const TSharedPtr<FBSEnemy>& E : Enemies) if (!E->Def->bTumor && !E->Def->bCreeper) return true;
	return false;
}

// A creep tumor at (X, Y), if the spot is open ground clear of the bunker and of other tumors, and under the cap.
TSharedPtr<FBSEnemy> ABSGame::PlantTumor(float X, float Y)
{
	if (!BSTerrain::Walkable(X, Y) || FMath::Sqrt(X * X + Y * Y) < BSCreep::TumorMinBunker || TumorCount() >= TumorCap()) return nullptr;
	for (const TSharedPtr<FBSEnemy>& E : Enemies)
		if (E->Def->bTumor && E->Alive() && Dist2(E->X, E->Y, X, Y) < BSCreep::TumorGap * BSCreep::TumorGap) return nullptr;
	const FVector2D At(X, Y);
	TSharedPtr<FBSEnemy> E = SpawnEnemy(TEXT("creeptumor"), 0, &At);
	PlaySound(TEXT("tumorBirth"), 0.5f, 0.3f);
	return E;
}

void ABSGame::SetupCreeper(FBSEnemy& E)
{
	const FBSZergDef* D = E.Def;
	if (D->bCreeper)
	{
		const float A = FMath::FRand() * 2 * PI, R = Rnd(9, 14);
		E.State = CreeperIn; E.T = 0; E.Exit = FVector2D(E.X, E.Y);
		if (Mode == EBSMode::Lane) { const float Hx = Rnd(-7, 7), Hy = Rnd(10, 18); E.Hover = FVector2D(Hx, Hy); }
		else E.Hover = FVector2D(FMath::Cos(A) * R, FMath::Sin(A) * R);
	}
	if (D->bTumor)
	{
		E.bBurrowed = true; E.T = 0; E.AnimT = 0; E.CreepR = 0.6f; E.bSpread = false; E.Anim = TEXT("birth");
		if (E.Actor) E.Actor->Play(TEXT("stand morph start"), false);
	}
	if (D->bTumors) E.TumorCd = Rnd(BSCreep::QueenFirst0, BSCreep::QueenFirst1);
}

// Creep tumor: buds, burrows, spreads creep out to its radius, and after a while buds one more tumor nearby on its creep,
// toward the bunker (trying again a little later if there is no room).
void ABSGame::UpdateTumor(const TSharedPtr<FBSEnemy>& EP, float Dt)
{
	FBSEnemy& E = *EP;
	E.T += Dt; E.AnimT += Dt;
	E.CreepR = FMath::Min(BSCreep::TumorRadius, E.CreepR + BSCreep::TumorGrowth * Dt);
	if (E.Anim == TEXT("birth") && E.AnimT > 1.3f) { E.Anim = TEXT("cover"); E.AnimT = 0; if (E.Actor) E.Actor->Play(TEXT("cover start"), false); }
	else if (E.Anim != TEXT("stand") && E.Anim != TEXT("birth") && E.AnimT > 1.3f) { E.Anim = TEXT("stand"); if (E.Actor) E.Actor->Play(TEXT("cover"), true); }
	if (E.bSpread || E.T < BSCreep::TumorSpread) return;
	E.bSpread = true;
	const float Toward = FMath::Atan2(-E.Y, -E.X);
	for (int32 k = 0; k < 6; k++)
	{
		const float A = Toward + Rnd(-1.1f, 1.1f), R = Rnd(BSCreep::TumorHop0, BSCreep::TumorHop1);
		const float X = E.X + FMath::Cos(A) * R, Y = E.Y + FMath::Sin(A) * R;
		if (OnCreep(X, Y) && PlantTumor(X, Y)) { E.Anim = TEXT("spell"); E.AnimT = 0; if (E.Actor) E.Actor->Play(TEXT("cover spell start"), false); return; }
	}
	E.bSpread = false; E.T = BSCreep::TumorSpread - 4;
}

// Queens plant a creep tumor every few seconds as they walk with the wave (standing still while they do).
bool ABSGame::QueenCreep(FBSEnemy& E, float Dt)
{
	if (E.CastUntil > Time) { E.bAttacking = false; return true; }
	E.TumorCd -= Dt;
	if (E.TumorCd > 0) return false;
	E.TumorCd = BSCreep::QueenEvery;
	if (!PlantTumor(E.X + FMath::Cos(E.Yaw) * 0.9f, E.Y + FMath::Sin(E.Yaw) * 0.9f)) { E.TumorCd = 2; return false; }
	E.CastUntil = Time + 0.9f; E.Anim = TEXT("spell"); E.bAttacking = false;
	if (E.Actor) E.Actor->Play(TEXT("spell"), false);
	PlaySound(TEXT("tumorPlant"), 0.6f, 0.3f);
	return true;
}

// Creep overlord: drifts in over the field, then hangs there generating creep beneath it while edging toward the bunker
// (into anti-air reach), and finally drifts back out.
void ABSGame::UpdateCreeper(const TSharedPtr<FBSEnemy>& EP, float Dt)
{
	FBSEnemy& E = *EP;
	E.T += Dt;
	if (E.State == CreeperWork)
	{
		const float R = (float)E.Hover.Size();
		if (R > BSCreep::OverlordMinDist) E.Hover *= FMath::Max(BSCreep::OverlordMinDist, R - BSCreep::OverlordDrift * Dt) / R;
		if (E.T > BSCreep::OverlordLife) { E.State = CreeperOut; PlayEnemyAnim(E, TEXT("walk")); }
	}
	const FVector2D G = E.State == CreeperOut ? E.Exit : E.Hover;
	const float Dx = G.X - E.X, Dy = G.Y - E.Y, Dd = FMath::Sqrt(Dx * Dx + Dy * Dy);
	if (E.State == CreeperIn && Dd < 0.5f)
	{
		E.State = CreeperWork; E.T = 0; E.Anim = TEXT("work");
		if (E.Actor) E.Actor->Play(TEXT("stand work"), true);
		PlaySound(TEXT("generateCreep"), 0.6f);
		Toast(TEXT("Overlord generating creep"));
	}
	if (E.State == CreeperOut && Dd < 0.5f) { Despawn(EP); return; }
	if (Dd > 0.05f)
	{
		const float S = FMath::Min(Dd, E.Def->Speed * Dt);
		E.X += Dx / Dd * S; E.Y += Dy / Dd * S;
		if (E.State != CreeperWork) E.Yaw += FMath::Clamp(FMath::FindDeltaAngleRadians(E.Yaw, FMath::Atan2(Dy, Dx)), -2 * Dt, 2 * Dt);
	}
}

// The creep overlay: a translucent grid laid 5 cm over the ground (half-metre vertices over the creep grid). Vertex alpha
// is the coverage (bilinear between cell centres) and vertex colour the ground's lighting (as the browser's ground shader
// lights it), so the unlit M_Creep shades like the terrain under it.
void ABSGame::BuildCreepMesh()
{
	if (!Assets->CreepMaterial || !Creep.Nx) return;
	CreepMesh = NewObject<UProceduralMeshComponent>(this, TEXT("CreepMesh"));
	CreepMesh->SetupAttachment(GetRootComponent());
	CreepMesh->RegisterComponent();
	CreepMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	CreepMesh->SetCastShadow(false);
	CreepMesh->SetWorldLocation(FVector::ZeroVector);
	const float Step = Creep.Cell * 0.5f, E = 0.15f;
	const FVector Sun = FVector(-0.45f, -0.35f, 0.82f).GetSafeNormal();      // sim space
	CreepVx = Creep.Nx * 2 + 1; CreepVy = Creep.Ny * 2 + 1;
	CreepV.Reset(); CreepN.Reset(); CreepUv.Reset(); CreepCol.Reset(); CreepShade.Reset();
	for (int32 j = 0; j < CreepVy; j++)
		for (int32 i = 0; i < CreepVx; i++)
		{
			const float X = Creep.X0 + i * Step, Y = Creep.Y0 + j * Step;
			const float Hx = (BSTerrain::Height(X + E, Y) - BSTerrain::Height(X - E, Y)) / (2 * E), Hy = (BSTerrain::Height(X, Y + E) - BSTerrain::Height(X, Y - E)) / (2 * E);
			const FVector Ns = FVector(-Hx, -Hy, 1).GetSafeNormal();                  // sim space
			CreepV.Add(BSSpace::ToWorld(X, Y, BSTerrain::Height(X, Y) + 0.05f));
			CreepN.Add(FVector(Ns.Y, Ns.X, Ns.Z));                                     // world axes (sim x/y swap)
			CreepUv.Add(FVector2D(X / 5, Y / 5));
			const float Shade = 0.62f + 0.5f * FMath::Max(0.f, (float)FVector::DotProduct(Ns, Sun));
			CreepShade.Add(Shade);
			CreepCol.Add(FLinearColor(Shade, Shade, Shade, 0));
		}
	TArray<int32> Tri;
	Tri.Reserve((CreepVx - 1) * (CreepVy - 1) * 6);
	for (int32 j = 0; j < CreepVy - 1; j++)
		for (int32 i = 0; i < CreepVx - 1; i++)
		{
			const int32 A = j * CreepVx + i, B = A + 1, C = A + CreepVx, D = C + 1;
			Tri.Append({ A, B, D, A, D, C });                    // front faces up, as the terrain mesh
		}
	TArray<FVector2D> NoUv; TArray<FProcMeshTangent> Tan;
	CreepMesh->CreateMeshSection_LinearColor(0, CreepV, Tri, CreepN, CreepUv, NoUv, NoUv, NoUv, CreepCol, Tan, false, false);
	CreepMesh->SetMaterial(0, Assets->CreepMaterial);
	CreepMesh->SetVisibility(false);
	CreepMeshVer = -1;
}

// At most five times a second, and only when the grid changed: refresh the overlay's vertex alpha.
void ABSGame::UpdateCreepMesh()
{
	if (!CreepMesh || Creep.Version == CreepMeshVer || Time - CreepMeshT < 0.2f) return;
	CreepMeshVer = Creep.Version; CreepMeshT = Time;
	const float Step = Creep.Cell * 0.5f;
	const float* C = Creep.C.GetData();
	bool bAny = false;
	for (int32 j = 0; j < CreepVy; j++)
		for (int32 i = 0; i < CreepVx; i++)
		{
			const float Cx = FMath::Clamp(i * Step / Creep.Cell - 0.5f, 0.f, Creep.Nx - 1.f), Fy = FMath::Clamp(j * Step / Creep.Cell - 0.5f, 0.f, Creep.Ny - 1.f);
			const int32 I0 = FMath::FloorToInt(Cx), J0 = FMath::FloorToInt(Fy), I1 = FMath::Min(I0 + 1, Creep.Nx - 1), J1 = FMath::Min(J0 + 1, Creep.Ny - 1);
			const float Tx = Cx - I0, Ty = Fy - J0;
			const float V = FMath::Lerp(FMath::Lerp(C[J0 * Creep.Nx + I0], C[J0 * Creep.Nx + I1], Tx), FMath::Lerp(C[J1 * Creep.Nx + I0], C[J1 * Creep.Nx + I1], Tx), Ty);
			CreepCol[j * CreepVx + i].A = V;
			bAny |= V > 0.01f;
		}
	CreepMesh->SetVisibility(bAny);
	if (bAny) CreepMesh->UpdateMeshSection_LinearColor(0, CreepV, CreepN, CreepUv, CreepCol, TArray<FProcMeshTangent>());
}

// Defiler (alternating dark swarm and plague) and viper (abduct) spells.
void ABSGame::CastSpell(FBSEnemy& E, const FBSTarget& T)
{
	const FBSZergDef* D = E.Def;
	if (D->Spell == TEXT("broodling")) { CastBroodling(E); return; }
	E.Anim = TEXT("spell"); E.CastUntil = Time + 1;
	if (E.Actor) E.Actor->Play(TEXT("spell"), false);
	if (D->Spell == TEXT("abduct"))
	{
		// Yank the nearest trooper outside, goliath or thor out of position toward the viper.
		TSharedPtr<FBSTrooper> BestT; TSharedPtr<FBSMech> BestM; float Bd = (D->Range + 1) * (D->Range + 1);
		for (const TSharedPtr<FBSTrooper>& M : Squad) if (!M->bDead && Dist2(E.X, E.Y, M->X, M->Y) < Bd) { Bd = Dist2(E.X, E.Y, M->X, M->Y); BestT = M; }
		for (const TSharedPtr<FBSMech>& M : Mechs) if (!M->bDead && M->Def->Key != TEXT("odin") && Dist2(E.X, E.Y, M->X, M->Y) < Bd) { Bd = Dist2(E.X, E.Y, M->X, M->Y); BestM = M; BestT.Reset(); }
		if (!BestT && !BestM) { E.Cd = 1; return; }
		const float Ox = BestT ? BestT->X : BestM->X, Oy = BestT ? BestT->Y : BestM->Y, Oz = BSTerrain::Height(Ox, Oy) + 1;
		const float A = FMath::Atan2(Oy - E.Y, Ox - E.X);
		AddFx(EBSFxKind::Tracer, FVector(E.X, E.Y, E.Z), FVector(Ox, Oy, Oz), 0.6f, 0.06f, FLinearColor(0.55f, 0.25f, 1.f), 14);
		PlayFx(TEXT("viperTongueImpact"), FVector(Ox, Oy, Oz), A);
		// Pulled toward the viper, but only as far as open ground at the victim's own level (never into a cliff).
		float Tx = Ox, Ty = Oy;
		const float Want = FMath::Max(0.f, FMath::Sqrt(Dist2(Ox, Oy, E.X, E.Y)) - 1.5f), Z0 = BSTerrain::Height(Ox, Oy);
		for (float S = 0.2f; S <= Want; S += 0.2f)
		{
			const float X = Ox - FMath::Cos(A) * S, Y = Oy - FMath::Sin(A) * S;
			if (!BSTerrain::Walkable(X, Y) || FMath::Abs(BSTerrain::Height(X, Y) - Z0) > 0.5f || (Mode == EBSMode::Lane && FMath::Abs(X) > 21)) break;
			Tx = X; Ty = Y;
		}
		FBSAbduct Ab2; Ab2.Trooper = BestT; Ab2.Mech = BestM; Ab2.Fx = Ox; Ab2.Fy = Oy; Ab2.Tx = Tx; Ab2.Ty = Ty;
		Abductions.Add(Ab2);
		PlaySound(TEXT("viperAbduct"), 0.7f);
		Floater(Ox, Oy, Oz + 1, TEXT("ABDUCTED"), FLinearColor(0.75f, 0.55f, 1.f));
		return;
	}
	E.NextSpell = E.NextSpell == 1 ? 2 : E.NextSpell == 2 ? 1 : FMath::RandRange(1, 2);
	if (E.NextSpell == 1)
	{
		// Dark swarm over the zerg pressing the defiler's target: ranged fire into it does a quarter damage.
		const float Dd = FMath::Max(0.001f, FMath::Sqrt(Dist2(E.X, E.Y, T.X, T.Y))), K = FMath::Min(3.5f, Dd) / Dd;
		FBSSwarm Sw; Sw.X = T.X + (E.X - T.X) * K; Sw.Y = T.Y + (E.Y - T.Y) * K; Sw.Until = Time + 8;
		Sw.Fx = PlayFx(TEXT("darkSwarm"), FVector(Sw.X, Sw.Y, BSTerrain::Height(Sw.X, Sw.Y)), 0, 1.3f);
		Swarms.Add(Sw);
		Toast(TEXT("Dark swarm"));
	}
	else
	{
		// Plague: everything of ours near the target bleeds 75 over 5 s, but it cannot kill.
		for (const FBSTarget& O : Targets)
		{
			if (FMath::Sqrt(Dist2(T.X, T.Y, O.X, O.Y)) - O.Radius > 2.5f) continue;
			const float Until = Time + 5;
			switch (O.Kind)
			{
			case FBSTarget::Bunker: Bunker.PlagueUntil = Until; break;
			case FBSTarget::Tank: if (auto P = O.TankRef.Pin()) P->PlagueUntil = Until; break;
			case FBSTarget::Scv: if (auto P = O.ScvRef.Pin()) P->PlagueUntil = Until; break;
			case FBSTarget::Trooper: if (auto P = O.TrooperRef.Pin()) P->PlagueUntil = Until; break;
			case FBSTarget::Structure: if (auto P = O.StructureRef.Pin()) P->PlagueUntil = Until; break;
			case FBSTarget::Mech: if (auto P = O.MechRef.Pin()) P->PlagueUntil = Until; break;
			default: break;
			}
			PlayFx(TEXT("plague"), FVector(O.X, O.Y, BSTerrain::Height(O.X, O.Y) + 0.5f), 0);
		}
		PlaySound(TEXT("plague"), 0.7f);
		Toast(TEXT("Plague!"));
	}
}

void ABSGame::UpdateSpells(float Dt)
{
	for (int32 i = Swarms.Num() - 1; i >= 0; i--) if (Time > Swarms[i].Until) { StopFx(Swarms[i].Fx); Swarms.RemoveAt(i); }
	auto Bleed = [this, Dt](float& Hp, float Until, float& Flash) { if (Until > Time && Hp > 1) { Hp = FMath::Max(1.f, Hp - 15 * Dt); Flash = FMath::Max(Flash, 0.4f); } };
	Bleed(Bunker.Hp, Bunker.PlagueUntil, Bunker.Flash);
	for (auto& P : Tanks) Bleed(P->Hp, P->PlagueUntil, P->Flash);
	for (auto& P : Scvs) Bleed(P->Hp, P->PlagueUntil, P->Flash);
	for (auto& P : Squad) Bleed(P->Hp, P->PlagueUntil, P->Flash);
	for (auto& P : Structures) Bleed(P->Hp, P->PlagueUntil, P->Flash);
	for (auto& P : Mechs) Bleed(P->Hp, P->PlagueUntil, P->Flash);
	for (int32 i = Abductions.Num() - 1; i >= 0; i--)
	{
		FBSAbduct& A = Abductions[i];
		A.T += Dt;
		const float K = FMath::Clamp(A.T / 0.6f, 0.f, 1.f);
		TSharedPtr<FBSTrooper> Tr = A.Trooper.Pin(); TSharedPtr<FBSMech> Me = A.Mech.Pin();
		if ((Tr && Tr->bDead) || (Me && Me->bDead) || (!Tr && !Me)) { Abductions.RemoveAt(i); continue; }
		float& X = Tr ? Tr->X : Me->X; float& Y = Tr ? Tr->Y : Me->Y;
		X = A.Fx + (A.Tx - A.Fx) * K; Y = A.Fy + (A.Ty - A.Fy) * K;
		if (K >= 1) { if (Me) Me->bReady = false; Abductions.RemoveAt(i); }        // a mech walks back to its post
	}
}

// Terran air the scourge and devourers go for.
void ABSGame::BuildAirTargets()
{
	AirTargets.Reset();
	if (Bc.IsValid() && Bc->bOnStation && !Bc->bDead)
	{
		FBSTarget T; T.Kind = FBSTarget::Bc; T.BcRef = Bc; T.X = Bc->X; T.Y = Bc->Y; T.Z = Bc->Sz; T.Radius = 2.4f; T.bArmored = true;
		AirTargets.Add(T);
	}
	for (const TSharedPtr<FBSBanshee>& V : VikingWing)
		if (!V->bDead && V->State != FBSBanshee::In)
		{
			FBSTarget T; T.Kind = FBSTarget::Air; T.AirRef = V; T.bViking = true; T.X = V->X; T.Y = V->Y; T.Z = V->Z; T.Radius = 0.6f; T.bArmored = true;
			AirTargets.Add(T);
		}
	for (const TSharedPtr<FBSBanshee>& B : Banshees)
		if (!B->bDead)
		{
			FBSTarget T; T.Kind = FBSTarget::Air; T.AirRef = B; T.X = B->X; T.Y = B->Y; T.Z = B->Z; T.Radius = 0.6f; T.bLight = true;
			AirTargets.Add(T);
		}
}

void ABSGame::KillAir(const FBSTarget& T)
{
	if (T.Kind == FBSTarget::Bc)
	{
		TSharedPtr<FBSBattlecruiser> B = T.BcRef.Pin();
		B->bDead = true;
		ReleaseModel(B->Actor); B->Actor = nullptr;
		Corpse(TEXT("battlecruiserDeath"), B->X, B->Y, B->Sz, B->Yaw, 5, 0.75f, true);
		PlaySound(BSSounds::Explosion, 1.f);
		Shake = 1;
		Bc.Reset();
		Toast(TEXT("Battlecruiser lost!"));
		return;
	}
	TSharedPtr<FBSBanshee> A = T.AirRef.Pin();
	A->bDead = true;
	ReleaseModel(A->Actor); A->Actor = nullptr;
	Corpse(T.bViking ? TEXT("vikingDeath") : TEXT("bansheeDeath"), A->X, A->Y, A->Z, A->Yaw, 5, 1, true);
	PlaySound(BSSounds::Explosion, 0.6f);
	if (T.bViking) { VikingWing.Remove(A); Toast(TEXT("Viking down")); }
	else { Banshees.Remove(A); Toast(TEXT("Banshee down")); }
}

void ABSGame::PlayEnemyAnim(FBSEnemy& E, const FString& Kind)
{
	if (E.Anim == Kind || !E.Actor) return;
	E.Anim = Kind;
	const FBSZergDef* D = E.Def;
	if (Kind == TEXT("walk"))
	{
		const float Len = E.Actor->Play(WalkNames, true);
		// Match the stride to ground speed: SC2 walk cycles are authored for roughly 1.4 m per second of animation.
		const float Sp = E.SlowUntil > Time ? D->Speed * 0.5f : D->Speed;
		E.Actor->SetRate(Len > 0 ? FMath::Clamp(Sp / 2.8f, 0.6f, 2.2f) : 1);
	}
	else if (Kind == TEXT("attack"))
	{
		const float Len = E.Actor->Play(D->AttackAnims, true);
		E.Actor->SetRate(Len > 0 ? Len / EnemyCd(E) : 1);
	}
	else if (Kind == TEXT("stand")) E.Actor->Play(StandNames, true);
	else if (Kind == TEXT("burrow")) E.Actor->Play(TEXT("burrow"), false);
	else if (Kind == TEXT("spell")) E.Actor->Play(TEXT("spell"), false);
}

// ---- Combat helpers ---------------------------------------------------------------------------------------
void ABSGame::BuildTargets()
{
	Targets.Reset();
	if (!Bunker.bDead)
	{
		FBSTarget T; T.Kind = FBSTarget::Bunker; T.X = 0; T.Y = 0; T.Radius = BSDefs::BunkerHalfSize; T.bStructure = true; T.bArmored = true;
		Targets.Add(T);
	}
	for (const TSharedPtr<FBSTank>& K : Tanks)
		if (K->State == FBSTank::Ready && !K->bDead)
		{
			FBSTarget T; T.Kind = FBSTarget::Tank; T.TankRef = K; T.X = K->X; T.Y = K->Y; T.Radius = BSDefs::TankRadius; T.bArmored = true;
			Targets.Add(T);
		}
	for (const TSharedPtr<FBSScv>& S : Scvs)
	{
		FBSTarget T; T.Kind = FBSTarget::Scv; T.ScvRef = S; T.X = S->X; T.Y = S->Y; T.Radius = BSDefs::ScvRadius; T.bLight = true;
		Targets.Add(T);
	}
	for (const TSharedPtr<FBSTrooper>& M : Squad)
	{
		if (M->bDead) continue;
		const bool bMarauder = M->Def->Key == TEXT("marauder");
		FBSTarget T; T.Kind = FBSTarget::Trooper; T.TrooperRef = M; T.X = M->X; T.Y = M->Y; T.Radius = 0.375f; T.bLight = !bMarauder; T.bArmored = bMarauder;
		Targets.Add(T);
	}
	for (const TSharedPtr<FBSMech>& M : Mechs)
	{
		if (M->bDead) continue;
		FBSTarget T; T.Kind = FBSTarget::Mech; T.MechRef = M; T.X = M->X; T.Y = M->Y; T.Radius = M->Def->Radius; T.bArmored = true;
		Targets.Add(T);
	}
	for (const TSharedPtr<FBSStructure>& S : Structures)
	{
		if (S->bDead) continue;
		FBSTarget T; T.Kind = FBSTarget::Structure; T.StructureRef = S; T.X = S->X; T.Y = S->Y; T.Radius = S->Radius; T.bStructure = true; T.bArmored = true;
		Targets.Add(T);
	}
}

void ABSGame::DamageDefender(const FBSTarget& T, float Dmg)
{
	if (!T.IsValid(Bunker)) return;
	if (T.Kind == FBSTarget::Bunker)
	{
		const float Real = FMath::Max(0.5f, Dmg - Bunker.Armor);
		Bunker.Hp -= Real; Bunker.Flash = 1;
		Tele.Damage += Real; TotalDamage += Real;
		if (FirstDamageAt < 0) FirstDamageAt = Time;
		Shake = FMath::Min(1.f, Shake + Real / 60.f);
		if (Bunker.Hp <= 0) GameOver();
	}
	else if (T.Kind == FBSTarget::Tank)
	{
		TSharedPtr<FBSTank> K = T.TankRef.Pin();
		K->Hp -= FMath::Max(0.5f, Dmg - K->Armor); K->Flash = 1;
		if (K->Hp <= 0 && !K->bDead)
		{
			K->bDead = true;
			Corpse(TEXT("siegetankDeath"), K->X, K->Y, 0, PI / 2, 6, 1, true);
			PlaySound(BSSounds::TankDeath, 0.8f);
			ReleaseModel(K->Actor); K->Actor = nullptr;
			if (K->Medivac) { ReleaseModel(K->Medivac); K->Medivac = nullptr; }
			Tanks.Remove(K);
			Toast(TEXT("Siege tank destroyed"));
		}
	}
	else if (T.Kind == FBSTarget::Scv)
	{
		TSharedPtr<FBSScv> S = T.ScvRef.Pin();
		S->Hp -= FMath::Max(0.5f, Dmg); S->Flash = 1;
		if (S->Hp <= 0 && !S->bDead)
		{
			S->bDead = true;
			Corpse(TEXT("scvDeath"), S->X, S->Y, 0, S->Yaw, 3.5f, 1, true);
			ReleaseModel(S->Actor); S->Actor = nullptr;
			Scvs.Remove(S);
			Toast(TEXT("SCV lost"));
		}
	}
	else if (T.Kind == FBSTarget::Trooper)
	{
		TSharedPtr<FBSTrooper> M = T.TrooperRef.Pin();
		M->Hp -= FMath::Max(0.5f, Dmg - (M->Def->Key == TEXT("marauder") ? 1.f : 0.f)); M->Flash = 1;
		if (M->Hp <= 0 && !M->bDead)
		{
			M->bDead = true;
			KillTrooper(M->Def, M->X, M->Y, M->Yaw, M->Actor, false); M->Actor = nullptr;
			Squad.Remove(M);
		}
	}
	else if (T.Kind == FBSTarget::Mech)
	{
		TSharedPtr<FBSMech> M = T.MechRef.Pin();
		M->Hp -= FMath::Max(0.5f, Dmg - M->Def->Armor); M->Flash = 1;
		if (M->Hp <= 0 && !M->bDead) KillMech(M);
	}
	else if (T.Kind == FBSTarget::Bc)
	{
		TSharedPtr<FBSBattlecruiser> B = T.BcRef.Pin();
		B->Hp -= FMath::Max(0.5f, Dmg - B->Armor); B->Flash = 1;
		if (B->Hp <= 0 && !B->bDead) KillAir(T);
	}
	else if (T.Kind == FBSTarget::Air)
	{
		TSharedPtr<FBSBanshee> A = T.AirRef.Pin();
		A->Hp -= FMath::Max(0.5f, Dmg); A->Flash = 1;
		if (A->Hp <= 0 && !A->bDead) KillAir(T);
	}
	else if (T.Kind == FBSTarget::Structure)
	{
		TSharedPtr<FBSStructure> S = T.StructureRef.Pin();
		S->Hp -= FMath::Max(0.5f, Dmg - S->Armor); S->Flash = 1;
		if (S->Hp <= 0 && !S->bDead) DestroyStructure(S);
	}
}

void ABSGame::HurtEnemy(const TSharedPtr<FBSEnemy>& E, float Dmg, bool bSplash)
{
	if (!E || !E->Alive()) return;
	// Dark swarm: ground zerg under a cloud take a quarter from shots (blasts and flame still get through).
	if (!bSplash && !E->Def->bFlying)
		for (const FBSSwarm& Sw : Swarms) if (Dist2(Sw.X, Sw.Y, E->X, E->Y) < Sw.R * Sw.R) { Dmg *= 0.25f; break; }
	E->Hp -= FMath::Max(0.5f, Dmg - E->Def->Armor);
	E->Flash = 1;
	if (E->Hp <= 0) KillEnemy(E, false);
}

void ABSGame::KillEnemy(const TSharedPtr<FBSEnemy>& E, bool bSuicide)
{
	const FBSZergDef* D = E->Def;
	E->Hp = 0;
	Kills++; Minerals += D->Bounty * (1 + 0.15f * CardLevel(TEXT("bounty"))); Score += D->Bounty * 2;
	if (!D->SoundDeath.IsEmpty()) PlaySound(D->SoundDeath, 0.5f);
	if (D->bSuicide && !bSuicide && !D->bAntiAir) BanelingBlast(*E, true);      // a shot-down baneling still bursts (scourge do not)
	Enemies.Remove(E);
	ReleaseModel(E->Actor); E->Actor = nullptr;
	if (D->Death.IsEmpty()) PlayFx(TEXT("nydusBirth"), FVector(E->X, E->Y, BSTerrain::Height(E->X, E->Y)), 0, 1.6f);      // the nydus worm has no death model
	else Corpse(E->DeathKey.IsEmpty() ? D->Death : E->DeathKey, E->X, E->Y, E->Z, E->Yaw, D->DeathHold, D->Scale);
	if (!D->Boss.IsEmpty()) BossDown(*E);
}

void ABSGame::BanelingBlast(const FBSEnemy& E, bool bFromDeath)
{
	const FBSZergDef* D = E.Def;
	AddFx(EBSFxKind::Blast, FVector(E.X, E.Y, 0.05f), FVector::ZeroVector, 0.45f, D->Splash, FLinearColor(0.45f, 1.0f, 0.15f), 3);
	if (!Assets->Fx(TEXT("banelingDeath"))) EmitExplosion(FVector(E.X, E.Y, 0.3f), D->Splash * 0.8f, FLinearColor(0.75f, 1.f, 0.4f), FLinearColor(0.2f, 0.55f, 0.05f), 18);
	BuildTargets();
	for (const FBSTarget& T : Targets)
	{
		const float Dd = FMath::Sqrt(Dist2(E.X, E.Y, T.X, T.Y)) - T.Radius;
		if (Dd > D->Splash) continue;
		const float Dmg = T.bStructure ? D->DmgStructure : (T.bLight ? D->Dmg + D->DmgLight : D->Dmg);
		DamageDefender(T, bFromDeath ? Dmg * 0.5f : Dmg);
	}
	PlaySound(BSSounds::BanelingDeath, 0.7f);
}

TArray<TSharedPtr<FBSEnemy>> ABSGame::EnemiesInRange(float X, float Y, float Range, TFunctionRef<bool(const FBSEnemy&)> Pred) const
{
	TArray<TPair<float, TSharedPtr<FBSEnemy>>> Hits;
	for (const TSharedPtr<FBSEnemy>& E : Enemies)
	{
		if (!E->Alive() || !Pred(*E)) continue;
		const float D = FMath::Sqrt(Dist2(X, Y, E->X, E->Y)) - E->Radius;
		if (D <= Range) Hits.Add({ D, E });
	}
	Hits.Sort([](const auto& A, const auto& B) { return A.Key < B.Key; });
	TArray<TSharedPtr<FBSEnemy>> Out;
	for (auto& H : Hits) Out.Add(H.Value);
	return Out;
}

void ABSGame::Corpse(const FString& Key, float X, float Y, float Z, float Yaw, float Hold, float Scale, bool bTerran)
{
	if (NearEye(FVector(X, Y, Z)) && !Key.StartsWith(TEXT("bunker"))) return;   // bunker view: no corpse (or gore burst) against the eye
	const float Gz = BSTerrain::Height(X, Y);
	PlayFx(Key, FVector(X, Y, FMath::Max(Z, Gz)), Yaw, Scale);
	ABSModelActor* A = AcquireModel(Key);
	if (!A) return;
	// Corpses are darkened and kept low so living units always read on top of the carnage.
	A->SetTeamColor((bTerran ? TerranColor : ZergColor) * 0.35f);
	A->SetScalarParam(TEXT("EmissiveStrength"), 0.25f);
	A->SetScalarParam(TEXT("DepthPush"), 180.f);       // 1.8 m behind: live zerg always draw on top
	A->SetScale(Scale);
	A->SetSim(X, Y, FMath::Min(Z, Gz), Yaw);
	A->Mesh->CastShadow = false;
	A->Mesh->MarkRenderStateDirty();
	// The plain "Death" sequence: the numbered ones are special variants SC2 picks by damage type (fire, acid, blasts)
	// that blow up or stretch body parts; only fall back to them if there is no plain one.
	float Len = A->Play(TEXT("death"), false);
	if (Len <= 0) Len = A->PlayRandomWithPrefix(TEXT("death"), false);
	FBSCorpse Cp;
	Cp.Actor = A; Cp.X = X; Cp.Y = Y; Cp.Z = FMath::Min(Z, Gz); Cp.Yaw = Yaw;
	Cp.bSink = !Key.StartsWith(TEXT("bunker"));
	Cp.Hold = Cp.bSink ? FMath::Clamp(FMath::Min(Len, 4.f), 2.5f, 4.f) : FMath::Max(Hold, FMath::Min(Len, 8.f));
	// Baneling chunks fade out in SC2 through visibility tracks we do not import; drop them right after the burst.
	if (Key.StartsWith(TEXT("baneling"))) { Cp.Hold = 0.45f; Cp.bVanish = true; Cp.bSink = false; }
	if (Z > 0.5f) Cp.Hold = FMath::Max(Cp.Hold, 2.f);      // flyers fall from the sky
	Corpses.Add(Cp);
}

// ---- Tick ------------------------------------------------------------------------------------------------
void ABSGame::Tick(float Dt)
{
	Super::Tick(Dt);
	if (!Assets) return;
	if (CrowdTest > 0 && bStarted) CrowdTick(Dt);
	// Bots shop at once and carry on.
	if (Shop.bOpen && bAutoPlay && !bOver) { AutoShop(); CloseShop(); }
	if (bStarted && !bPaused && !bOver && !Shop.bOpen)
	{
		Acc += FMath::Min(Dt, 0.1f) * TimeScale;
		int32 N = 0;
		const float StepDt = 1.f / 60.f;
		const int32 MaxSteps = FMath::Max(24, FMath::CeilToInt(TimeScale * 3));
		while (Acc >= StepDt && N++ < MaxSteps && !bOver && !Shop.bOpen) { Step(StepDt); Acc -= StepDt; }
		if (Shop.bOpen) Acc = 0;
	}
	// Effects, corpses and particles follow game speed (so slow motion, pause and the shop apply to them too).
	UpdateVisuals(bPaused || Shop.bOpen ? 0 : Dt * TimeScale);
}

void ABSGame::Step(float Dt)
{
	Time += Dt;
	Minerals += Income * Dt;
	Score += Dt * 0.5f;
	Shake = FMath::Max(0.f, Shake - Dt * 2);
	Ab.StimCd = FMath::Max(0.f, Ab.StimCd - Dt); Ab.StrikeCd = FMath::Max(0.f, Ab.StrikeCd - Dt); Ab.RepairCd = FMath::Max(0.f, Ab.RepairCd - Dt); Ab.YamatoCd = FMath::Max(0.f, Ab.YamatoCd - Dt);
	NukeCd = FMath::Max(0.f, NukeCd - Dt); VikingCd = FMath::Max(0.f, VikingCd - Dt);

	WaveTimer -= Dt;
	if (Wave == 0) { if (WaveTimer <= 0) StartWave(); }
	else if (SpawnQueue.Num() == 0)
	{
		LingerT += Dt;
		if (!WaveAlive() || LingerT > BSDefs::ShopLinger) { EndWave(); return; }
	}
	SpawnTimer += Dt;
	// Due spawns come out in order while there is room on the field; at the alive cap they wait their turn (bosses
	// always come through).
	while (SpawnQueue.Num() && SpawnQueue[0].T <= SpawnTimer)
	{
		if (Enemies.Num() >= MaxAlive())
		{
			const int32 Bi = SpawnQueue.IndexOfByPredicate([this](const FBSSpawn& Q) { const FBSZergDef* QD = BSDefs::FindZerg(Q.Type); return Q.T <= SpawnTimer && QD && !QD->Boss.IsEmpty(); });
			if (Bi != INDEX_NONE) { const FBSSpawn B = SpawnQueue[Bi]; SpawnQueue.RemoveAt(Bi); SpawnEnemy(B.Type, B.Ang); }
			break;
		}
		const FBSSpawn S = SpawnQueue[0];
		SpawnQueue.RemoveAt(0);
		SpawnEnemy(S.Type, S.Ang);
	}
	BuildTargets();
	BuildAirTargets();
	UpdateEnemies(Dt);
	UpdateCreep(Dt);
	UpdateSpells(Dt);
	UpdateBunker(Dt);
	UpdateTanks(Dt);
	UpdateScvs(Dt);
	UpdateSquad(Dt);
	UpdateStructures(Dt);
	UpdateExpansion(Dt);
	UpdateBanshees(Dt);
	UpdateMechs(Dt);
	UpdateBc(Dt);
	UpdateVikings(Dt);
	UpdateNukes(Dt);
	UpdatePods(Dt);
	UpdateProjectiles(Dt);
	UpdateReinforcements(Dt);
	if (bAutoPlay) { AutoT -= Dt; if (AutoT <= 0) { AutoT = 0.8f; AutoPlay(); } }
	Tele.PeakAlive = FMath::Max(Tele.PeakAlive, Enemies.Num());
	GamePeakAlive = FMath::Max(GamePeakAlive, Enemies.Num());
	Tele.MinHpPct = FMath::Min(Tele.MinHpPct, Bunker.Hp / FMath::Max(1.f, Bunker.MaxHp));
	if (Enemies.Num() == 0) Tele.IdleTime += Dt;
	Tele.MineralSum += Minerals * Dt; Tele.MineralSamples += Dt;
	if (TSharedPtr<FBSEnemy> F = Focus.Pin()) { if (!F->Alive() || Time > FocusUntil) Focus.Reset(); }
}

void ABSGame::UpdateEnemies(float Dt)
{
	// Separation: zerg push apart where they overlap. A spatial hash keeps it near-linear in the crowd size (each unit
	// checks only the cells within its reach of the biggest unit about).
	const int32 N = Enemies.Num();
	constexpr float SepCell = 2.f;
	float MaxR = 0;
	TMap<int32, TArray<int32, TInlineAllocator<8>>> Grid;
	auto CellKey = [](int32 Cx, int32 Cy) { return Cx * 4096 + Cy; };
	for (int32 i = 0; i < N; i++)
	{
		FBSEnemy& A = *Enemies[i]; A.Sx = 0; A.Sy = 0; MaxR = FMath::Max(MaxR, A.Radius);
		Grid.FindOrAdd(CellKey(FMath::FloorToInt(A.X / SepCell), FMath::FloorToInt(A.Y / SepCell))).Add(i);
	}
	for (int32 i = 0; i < N; i++)
	{
		FBSEnemy& A = *Enemies[i];
		if (A.bBurrowed) continue;                   // burrowed lurkers are underground: walk over them
		const int32 Cx = FMath::FloorToInt(A.X / SepCell), Cy = FMath::FloorToInt(A.Y / SepCell), Span = FMath::CeilToInt((A.Radius + MaxR + 0.1f) / SepCell);
		for (int32 Gx = Cx - Span; Gx <= Cx + Span; Gx++) for (int32 Gy = Cy - Span; Gy <= Cy + Span; Gy++)
		{
			const auto* Bucket = Grid.Find(CellKey(Gx, Gy));
			if (!Bucket) continue;
			for (const int32 j : *Bucket)
			{
				if (j <= i) continue;
				FBSEnemy& B = *Enemies[j];
				if (B.bBurrowed || A.Def->bFlying != B.Def->bFlying) continue;
				const float Dx = B.X - A.X, Dy = B.Y - A.Y, Min = A.Radius + B.Radius + 0.1f, D2 = Dx * Dx + Dy * Dy;
				if (D2 < Min * Min && D2 > 1e-6f)
				{
					const float D = FMath::Sqrt(D2), Push = (Min - D) / D * 0.5f;
					A.Sx -= Dx * Push; A.Sy -= Dy * Push; B.Sx += Dx * Push; B.Sy += Dy * Push;
				}
			}
		}
	}
	TArray<TSharedPtr<FBSEnemy>> Snapshot = Enemies;
	for (const TSharedPtr<FBSEnemy>& EP : Snapshot)
	{
		FBSEnemy& E = *EP;
		if (!E.Alive()) continue;
		const FBSZergDef* D = E.Def;
		if (E.Life > 0) { E.Life -= Dt; if (E.Life <= 0) { KillEnemy(EP, true); Kills--; continue; } }
		if (D->bTransport) { UpdateOverlord(EP, Dt); continue; }
		if (D->bNydus) { UpdateNydus(EP, Dt); continue; }
		if (D->bTumor) { UpdateTumor(EP, Dt); continue; }
		if (D->bCreeper) { UpdateCreeper(EP, Dt); continue; }
		if (D->bTumors && QueenCreep(E, Dt)) continue;
		E.Retarget -= Dt;
		const TArray<FBSTarget>& Pool = D->bAntiAir ? AirTargets : Targets;       // scourge and devourers hunt Terran air
		if (!E.Target.IsValid(Bunker) || E.Retarget <= 0)
		{
			const FBSTarget* BestT = nullptr; float Bd = 1e9f;
			for (const FBSTarget& T : Pool)
			{
				if (T.Kind == FBSTarget::Scv && FMath::FRand() < 0.5f) continue;
				// Down in the lane, walkers cannot reach anything up on the high ground (the forward base) unless it is in
				// reach already; they would only pile up under the cliff.
				bool bUp = false;
				if (T.Kind == FBSTarget::Trooper) { if (auto P = T.TrooperRef.Pin()) bUp = P->Fwd.bOn; }
				else if (T.Kind == FBSTarget::Mech) { if (auto P = T.MechRef.Pin()) bUp = P->Fwd.bOn; }
				else if (T.Kind == FBSTarget::Scv) { if (auto P = T.ScvRef.Pin()) bUp = P->Fwd.bOn; }
				else if (T.Kind == FBSTarget::Structure) { if (auto P = T.StructureRef.Pin()) bUp = P->bCc; }
				if (bUp && !D->bFlying && !D->bHops && !D->bAntiAir && BSTerrain::OnHigh(T.X, T.Y) && !BSTerrain::OnHigh(E.X, E.Y)
					&& FMath::Sqrt(Dist2(E.X, E.Y, T.X, T.Y)) - T.Radius - E.Radius > D->Range) continue;
				// Guardians single out mechs; brood queens make for tanks, mechs, troopers.
				const float Dd = FMath::Sqrt(Dist2(E.X, E.Y, T.X, T.Y)) - T.Radius - (D->bPreferMech && T.Kind == FBSTarget::Mech ? 12.f : 0.f)
					- (D->Spell == TEXT("broodling") ? 4.f * BroodPriority(T.Kind) : 0.f);
				if (Dd < Bd) { Bd = Dd; BestT = &T; }
			}
			if (BestT) { E.Target = *BestT; E.Idle = 0; } else E.Target = FBSTarget();
			E.Retarget = Rnd(0.5f, 0.8f);
		}
		if (E.Target.Kind == FBSTarget::None)
		{
			// Nothing in the air to hunt: hang back up the lane for a while, then give up and fly off (so idle hunters never
			// fill the alive cap and hold up the waves behind them).
			if (D->bAntiAir)
			{
				E.Idle += Dt;
				const bool bLeave = E.Idle > 8;
				const float A = FMath::Atan2(E.Y, E.X), R = bLeave ? 34.f : 13.f;
				const float Gx = Mode == EBSMode::Lane ? FMath::Clamp(E.X, -6.f, 6.f) : FMath::Cos(A) * R, Gy = Mode == EBSMode::Lane ? (bLeave ? 34.f : 12.f) : FMath::Sin(A) * R;
				const float Gd = FMath::Sqrt(Dist2(Gx, Gy, E.X, E.Y));
				if (bLeave && Gd < 1) { Despawn(EP); continue; }
				if (Gd > 0.5f)
				{
					const float S = FMath::Min(Gd, D->Speed * Dt);
					E.X += (Gx - E.X) / Gd * S; E.Y += (Gy - E.Y) / Gd * S;
					E.Yaw += FMath::Clamp(FMath::FindDeltaAngleRadians(E.Yaw, FMath::Atan2(Gy - E.Y, Gx - E.X)), -3 * Dt, 3 * Dt);
				}
				PlayEnemyAnim(E, TEXT("walk"));
			}
			continue;
		}
		// Refresh the target position (tanks, SCVs and air support move).
		for (const FBSTarget& T : Pool) if (T.Same(E.Target)) { E.Target.X = T.X; E.Target.Y = T.Y; E.Target.Z = T.Z; break; }
		const FBSTarget& T = E.Target;
		if (D->Boss == TEXT("kerrigan") && UpdateKerrigan(EP, T, Dt)) continue;
		if (D->Boss == TEXT("leviathan")) UpdateLeviathan(EP, Dt);
		const float Dx = T.X - E.X, Dy = T.Y - E.Y, Dd = FMath::Sqrt(Dx * Dx + Dy * Dy);
		const float Reach = Dd - T.Radius - E.Radius;
		// A brood queen with no prey about (only the bunker or buildings) presses in under the guns to find some.
		const bool bPrey = D->Spell != TEXT("broodling") || BroodPriority(T.Kind) > 0;
		// Flyers still about well after the wave's last spawn close in under the guns rather than siege from afar.
		const bool bPressIn = D->bFlying && SpawnQueue.Num() == 0 && LingerT > BSDefs::ShopPressIn;
		const float Range = FMath::Min(bPrey ? D->Range : 4.f, bPressIn ? 4.f : TNumericLimits<float>::Max()) + (T.Kind == FBSTarget::Bunker ? 0.2f : 0);
		const bool bSlowed = E.SlowUntil > Time;
		if (D->bSuicide && Reach <= Range)
		{
			if (D->bAntiAir) { DamageDefender(T, D->Dmg); PlayFx(TEXT("mutaliskImpact"), FVector(E.X, E.Y, E.Z), E.Yaw, 1.6f); PlaySound(TEXT("scourgeHit"), 0.6f, 0.1f); }
			else BanelingBlast(E, false);
			KillEnemy(EP, true); continue;
		}
		if (E.bBackOff)
		{
			// Brood queen after a cast: draw back out of reach while the next broodling grows, then sweep in again.
			E.Cd -= Dt * (bSlowed ? 0.8f : 1.f);
			if (E.Cd <= 1.5f) E.bBackOff = false;
			else
			{
				E.bAttacking = false;
				if (Reach < Range + 3 && Dd > 0.01f)
				{
					const float S = D->Speed * 0.8f * Dt;
					E.X -= Dx / Dd * S; E.Y -= Dy / Dd * S;
					if (Mode == EBSMode::Lane) { E.X = FMath::Clamp(E.X, -21.f, 21.f); E.Y = FMath::Min(E.Y, 34.f); }
				}
				E.Yaw += FMath::Clamp(FMath::FindDeltaAngleRadians(E.Yaw, FMath::Atan2(Dy, Dx)), -3 * Dt, 3 * Dt);     // keeps an eye on her prey
				if (E.CastUntil <= Time) PlayEnemyAnim(E, Reach < Range + 3 ? TEXT("walk") : TEXT("stand"));
				continue;
			}
		}
		const bool bHigh = BSTerrain::OnHigh(E.X, E.Y);          // the high ground adds a metre of reach
		if (D->bBurrows)
		{
			if (!E.bBurrowed && Reach <= Range - 1) { E.bBurrowed = true; E.BurrowT = 1.0f; E.UnburrowT = 0; PlayEnemyAnim(E, TEXT("burrow")); }
			else if (E.bBurrowed)
			{
				E.BurrowT -= Dt;
				// Its prey dead or gone and nothing else in reach: come up after a moment and move on.
				if (Reach > Range + (bHigh ? 1.f : 0.f))
				{
					const FBSTarget* Near = Targets.FindByPredicate([&](const FBSTarget& O) { return FMath::Sqrt(Dist2(E.X, E.Y, O.X, O.Y)) - O.Radius - E.Radius <= D->Range + (bHigh ? 1.f : 0.f); });
					if (Near) { E.Target = *Near; E.UnburrowT = 0; }
					else { E.UnburrowT += Dt; if (E.UnburrowT > 1.5f) E.bBurrowed = false; }
				}
				else E.UnburrowT = 0;
			}
		}
		const bool bCanAttack = Reach <= Range + (bHigh ? 1.f : 0.f) && !(D->bBurrows && (!E.bBurrowed || E.BurrowT > 0)) && !(E.Route.Num() && E.bHighRoute);
		if (bCanAttack)
		{
			E.bAttacking = true;
			E.Yaw = FMath::Atan2(Dy, Dx);
			if (D->AttackAnims.Num()) PlayEnemyAnim(E, TEXT("attack"));
			else if (!D->bBurrows && E.CastUntil <= Time) PlayEnemyAnim(E, TEXT("stand"));
			E.Cd -= Dt * (bSlowed ? 0.8f : 1.f);
			if (E.Cd <= 0)
			{
				const float Cd = EnemyCd(E);
				E.Cd += Cd;
				E.HitAt = Time + Cd * D->HitFrac;
				E.HitTarget = T;
				if (!D->SoundAttack.IsEmpty()) PlaySound(D->SoundAttack, 0.35f, 0.15f);
				if (!D->Spell.IsEmpty()) CastSpell(E, T);
				else if (D->Projectile != EBSProjectile::None)
				{
					FBSProjectile P;
					P.bFriendly = false; P.ZDef = D; P.TargetDef = T; P.Source = EP;
					P.From = FVector(E.X, E.Y, E.Z + D->Height * 0.6f);
					P.To = FVector(T.X, T.Y, T.Z >= 0 ? T.Z : 1.0f);
					P.Dur = FMath::Max(0.15f, Cd * D->HitFrac);
					P.Dmg = D->Dmg;
					switch (D->Projectile)
					{
					case EBSProjectile::Acid: P.Kind = EBSProjKind::Acid; P.Arc = 1.5f; P.ImpactFx = TEXT("roachImpact"); AddProjectileModel(P, nullptr, TEXT("roachMissile"), 0.7f); if (!P.Trail) AddProjectileVisual(P, FLinearColor(0.5f, 1, 0.15f), 0.35f); break;
					case EBSProjectile::Spine: P.Kind = EBSProjKind::Spine; AddProjectileModel(P, TEXT("projHydralisk"), nullptr); if (!P.Model) AddProjectileVisual(P, FLinearColor(0.8f, 1, 0.5f), 0.15f); break;
					case EBSProjectile::Bullet:
					{
						// Infested marines: a rifle burst from the muzzle with the same SC2 muzzle mesh and launch sparks as our marines.
						const float Aim = FMath::Atan2(T.Y - E.Y, T.X - E.X);
						P.From = FVector(E.X + FMath::Cos(Aim) * 0.45f, E.Y + FMath::Sin(Aim) * 0.45f, E.Z + D->Height * 0.7f);
						P.Kind = EBSProjKind::Bullet; P.ImpactFx = TEXT("marineImpact");
						bool bFx = true;
						for (int32 r = 0; r < 3; r++) { bFx = PlayFx(TEXT("marineLaunch"), P.From, Aim, 1.4f, r * 0.08f) != 0 && bFx; MuzzleMesh(P.From, Aim, r * 0.08f); }
						if (!bFx) AddProjectileVisual(P, FLinearColor(1, 0.85f, 0.5f), 0.08f);
						break;
					}
					case EBSProjectile::Glaive: P.Kind = EBSProjKind::Glaive; P.Bounces = 3; P.ImpactFx = TEXT("mutaliskImpact"); AddProjectileModel(P, TEXT("projMutalisk"), TEXT("mutaliskMissile")); if (!P.Model) AddProjectileVisual(P, FLinearColor(0.6f, 1, 0.3f), 0.25f); break;
					case EBSProjectile::Bile: P.Kind = EBSProjKind::Bile; P.Arc = 2.5f; P.ImpactFx = TEXT("roachImpact"); P.ImpactScale = 2.2f; AddProjectileModel(P, TEXT("projLeviathan"), TEXT("guardianMissile"), 1.6f); if (!P.Model && !P.Trail) AddProjectileVisual(P, FLinearColor(0.7f, 1, 0.3f), 0.6f); break;
					case EBSProjectile::Brood: P.Kind = EBSProjKind::Brood; AddProjectileVisual(P, FLinearColor(0.9f, 0.6f, 0.3f), 0.4f); P.Arc = 3; break;
					case EBSProjectile::Spines: P.Kind = EBSProjKind::Spines; break;
					case EBSProjectile::Guardian: P.Kind = EBSProjKind::Guardian; P.Arc = 2; P.ImpactFx = TEXT("guardianImpact"); AddProjectileModel(P, TEXT("projGuardian"), TEXT("guardianMissile"), 1.6f); if (P.Model) P.Model->SetScale(2.4f); if (!P.Model && !P.Trail) AddProjectileVisual(P, FLinearColor(0.7f, 1, 0.4f), 0.35f); break;
					case EBSProjectile::Spores: P.Kind = EBSProjKind::Spores; P.ImpactFx = TEXT("acidSpores"); AddProjectileModel(P, nullptr, TEXT("devourerMissile")); if (!P.Trail) AddProjectileVisual(P, FLinearColor(0.6f, 1, 0.2f), 0.3f); break;
					default: break;
					}
					Projectiles.Add(P);
				}
				else E.bPendingHit = true;
			}
			if (E.bPendingHit && Time >= E.HitAt)
			{
				E.bPendingHit = false;
				const FBSTarget& Tg = E.HitTarget;
				if (Tg.IsValid(Bunker))
				{
					float Dmg = D->Dmg;
					if (Tg.bLight) Dmg += D->DmgLight;
					if (Tg.bArmored) Dmg += D->DmgArmored;
					DamageDefender(Tg, Dmg);
					if (!D->ImpactFx.IsEmpty())
					{
						const float A = FMath::Atan2(E.Y - Tg.Y, E.X - Tg.X);
						PlayFx(D->ImpactFx, FVector(Tg.X + FMath::Cos(A) * Tg.Radius, Tg.Y + FMath::Sin(A) * Tg.Radius, 0.5f), A);
					}
					if (D->Cleave > 0)
						for (const FBSTarget& O : Targets)
							if (!O.Same(Tg) && FMath::Sqrt(Dist2(Tg.X, Tg.Y, O.X, O.Y)) - O.Radius < D->Cleave) DamageDefender(O, Dmg * 0.5f);
				}
			}
		}
		else if (E.bPerched)
		{
			E.bAttacking = false;
			PlayEnemyAnim(E, TEXT("stand"));                         // holding the ledge, waiting for something in range
		}
		else if (!(D->bBurrows && E.bBurrowed))
		{
			E.bAttacking = false;
			PlayEnemyAnim(E, TEXT("walk"));
			// Head for the next waypoint of the route through the Y, then straight for the target.
			float Gx = T.X, Gy = T.Y;
			if (E.Route.Num())
			{
				Gx = E.Route[0].X; Gy = E.Route[0].Y;
				if (Dist2(Gx, Gy, E.X, E.Y) < 0.81f) { E.Route.RemoveAt(0); if (!E.Route.Num() && E.bHighRoute) E.bPerched = true; }
			}
			const float Gd = FMath::Max(0.001f, FMath::Sqrt(Dist2(Gx, Gy, E.X, E.Y)));
			// Zerg on creep move faster (ground units only).
			E.bOnCreep = !D->bFlying && OnCreep(E.X, E.Y);
			const float Sp = D->Speed * (bSlowed ? 0.5f : 1.f) * (E.bOnCreep ? BSCreep::Speed : 1.f);
			const float Nx = (Gx - E.X) / Gd, Ny = (Gy - E.Y) / Gd;
			const float Mx = Nx * Sp * Dt + E.Sx * 0.8f, My = Ny * Sp * Dt + E.Sy * 0.8f;
			// Cliffs: ground units steer around them; flyers ignore them, and winged zerglings hop straight over.
			if (D->bFlying || D->bHops) { E.X += Mx; E.Y += My; } else StepGround(E.X, E.Y, Mx, My);
			if (Mode == EBSMode::Lane) E.X = FMath::Clamp(E.X, -21.f, 21.f);
			if (D->bHops)
			{
				const float Gz = BSTerrain::Height(E.X, E.Y);
				E.Z = FMath::Max(Gz, E.Z + (Gz - E.Z) * FMath::Min(1.f, Dt * 5)) + FMath::Abs(FMath::Sin(Time * 7 + E.Seed)) * 0.06f;
			}
			else if (!D->bFlying) E.Z = BSTerrain::Height(E.X, E.Y);
			if (E.bPerched) PlayEnemyAnim(E, TEXT("stand"));
			// Face the way it is heading, not the way the crowd shoves it (big units were turning round and backing up),
			// and turn at a creature's pace instead of snapping.
			const float WantYaw = FMath::Atan2(Ny, Nx);
			const float TurnRate = (D->Radius >= 0.9f ? 3.f : 8.f) * Dt;
			E.Yaw += FMath::Clamp(FMath::FindDeltaAngleRadians(E.Yaw, WantYaw), -TurnRate, TurnRate);
		}
	}
}

void ABSGame::UpdateBunker(float Dt)
{
	if (Bunker.bDead) return;
	const bool bStim = Ab.StimUntil > Time;
	const float Bonus = BSDefs::BunkerRangeBonus + BSDefs::BunkerHalfSize + (Up.bHiSec ? BSDefs::HiSecRange : 0.f) + (Up.bRails ? BSDefs::RailsRange : 0.f) + (Mode == EBSMode::Arena ? 1.f : 0.f);
	if (const int32 Regen = 3 * CardLevel(TEXT("firesup"))) Bunker.Hp = FMath::Min(Bunker.MaxHp, Bunker.Hp + Regen * Dt);
	const float Heal = 1 + 0.6f * CardLevel(TEXT("medpacks"));
	if (Up.bMedivac)
	{
		for (FBSInfantryUnit& U : Bunker.Units) U.Hp = FMath::Min(U.MaxHp, U.Hp + 9 * Heal * Dt);
		for (const TSharedPtr<FBSTrooper>& M : Squad) if (!M->bDead) M->Hp = FMath::Min(M->MaxHp, M->Hp + 9 * Heal * Dt);
		// The medivac hovers over the bunker and also mends the most hurt SCV nearby (12.6 HP/s, as in SC2).
		if (!StationMedivac && Assets->Model(TEXT("medivac")))
		{
			StationMedivac = AcquireModel(TEXT("medivac"));
			if (StationMedivac) StationMedivac->SetTeamColor(TerranColor);
		}
		FBSScv* Patient = nullptr;
		for (const TSharedPtr<FBSScv>& S : Scvs)
			if (!S->bDead && S->Hp < S->MaxHp - 0.5f && FMath::Sqrt(Dist2(0, 0.5f, S->X, S->Y)) < 6.f && (!Patient || S->Hp / S->MaxHp < Patient->Hp / Patient->MaxHp)) Patient = S.Get();
		if (Patient) Patient->Hp = FMath::Min(Patient->MaxHp, Patient->Hp + 12.6f * Heal * Dt);
		if (StationMedivac)
		{
			// Parked at the bunker's shoulder; it only turns (slowly) toward whoever it is healing.
			const FVector2D Park(1.9f, -0.9f);
			const float WantYaw = Patient ? FMath::Atan2(Patient->Y - Park.Y, Patient->X - Park.X) : PI / 2;
			StationYaw += FMath::Clamp(FMath::FindDeltaAngleRadians(StationYaw, WantYaw), -1.5f * Dt, 1.5f * Dt);
			StationMedivac->SetSim(Park.X, Park.Y, 2.7f + 0.12f * FMath::Sin(Time * 1.7f), StationYaw);
			StationMedivac->SetScale(0.75f);
			// Hold each animation at least a second so a topped-up SCV does not make it flicker.
			StationAnimT += Dt;
			const FString Want = Patient ? TEXT("work") : TEXT("idle");
			if (StationAnim != Want && StationAnimT > 1.f)
			{
				StationAnim = Want; StationAnimT = 0;
				StationMedivac->Play(Patient ? TArray<FString>{ TEXT("stand work"), TEXT("attack") } : WalkNames, true);
			}
		}
	}
	for (FBSInfantryUnit& U : Bunker.Units)
	{
		U.Cd -= Dt * (bStim ? 1.5f : 1.f);
		if (U.Cd > 0) continue;
		const FBSInfantryDef* D = U.Def;
		const float Range = D->Range + Bonus;
		auto Pred = [D](const FBSEnemy& E) { return !(D->bGroundOnly && E.Def->bFlying); };
		TSharedPtr<FBSEnemy> Target;
		if (TSharedPtr<FBSEnemy> F = Focus.Pin())
			if (F->Alive() && Pred(*F) && FMath::Sqrt(Dist2(0, 0, F->X, F->Y)) - F->Radius <= Range) Target = F;
		if (!Target)
		{
			TArray<TSharedPtr<FBSEnemy>> L = EnemiesInRange(0, 0, Range, Pred);
			if (!L.Num()) { U.Cd = 0.05f; continue; }
			Target = L[0];
			if (D->Key == TEXT("marauder")) for (auto& E : L) if (E->Def->bArmored) { Target = E; break; }
			if (D->Key == TEXT("firebat")) for (auto& E : L) if (E->Def->bLight) { Target = E; break; }
		}
		U.Cd += D->Cd;
		// The slit facing the target, with a little spread along it so a fireteam's rounds don't all leave from one pixel.
		const float Ang = FMath::Atan2(Target->Y, Target->X);
		FireInfantry(D, BunkerSlit(Ang) + FVector(-FMath::Sin(Ang), FMath::Cos(Ang), 0) * Rnd(-0.18f, 0.18f), Target);
	}
}

// One shot from a trooper at a muzzle position (a bunker slit, or the rifle of a soldier standing outside).
void ABSGame::FireInfantry(const FBSInfantryDef* D, const FVector& Muzzle, const TSharedPtr<FBSEnemy>& Target, bool bFwd)
{
	auto Pred = [D](const FBSEnemy& E) { return !(D->bGroundOnly && E.Def->bFlying); };
	const bool bMiss = BSTerrain::OnHigh(Target->X, Target->Y) && !(bFwd && BSTerrain::OnHigh(Muzzle.X, Muzzle.Y)) && FMath::FRand() < 0.3f;   // firing up at the high ground
	const float DmgBase = bMiss ? 0.f : D->Dmg + Up.Weapons;
	const FVector Hit(Target->X, Target->Y, Target->Z + Target->Def->Height * 0.5f);
	const float Ang = FMath::Atan2(Hit.Y - Muzzle.Y, Hit.X - Muzzle.X);
	if (D->Key == TEXT("marine"))
	{
		if (DmgBase > 0) HurtEnemy(Target, DmgBase);
		// Three-round bursts with SC2's muzzle mesh and impact sparks.
		bool bFx = true;
		for (int32 r = 0; r < 3 && bFx; r++)
		{
			bFx = PlayFx(TEXT("marineLaunch"), Muzzle, Ang, 1.4f, r * 0.08f) != 0;
			MuzzleMesh(Muzzle, Ang, r * 0.08f);
			PlayFx(TEXT("marineImpact"), Hit + FVector(Rnd(-0.2f, 0.2f), Rnd(-0.2f, 0.2f), Rnd(-0.15f, 0.25f)), Ang + PI, 1.f, r * 0.08f + 0.03f);
		}
		if (!bFx) EmitGunfire(Muzzle, Hit);
		MuzzleFlash = 1;
		PlaySound(BSSounds::MarineFire, 0.3f, 0.09f);
	}
	else if (D->Key == TEXT("firebat"))
	{
		const float Hx = Target->X, Hy = Target->Y;
		const int32 Inc = CardLevel(TEXT("incinerator"));
		if (DmgBase > 0) for (auto& V : EnemiesInRange(Target->X, Target->Y, D->Splash * (1 + 0.4f * Inc), Pred)) HurtEnemy(V, DmgBase + 2 * Inc + (V->Def->bLight ? D->DmgLight : 0), true);
		// The flames burn the creep away where they land and along the way.
		BurnCreep(Hx, Hy, BSCreep::BurnRadius, true);
		BurnCreep((Muzzle.X + Hx) / 2, (Muzzle.Y + Hy) / 2, BSCreep::BurnRadius * 0.6f, false);
		EmitFlame(Muzzle, FVector(Hx, Hy, 0.4f));
		MuzzleFlash = 1;
		PlaySound(BSSounds::FirebatFire, 0.45f, 0.3f);
	}
	else
	{
		FBSProjectile P; P.Kind = EBSProjKind::Grenade; P.bFriendly = true; P.From = Muzzle; P.To = Hit; P.Dur = 0.3f; P.Arc = 0.6f;
		P.TargetEnemy = Target; P.Dmg = DmgBase; P.IDef = D; P.ImpactFx = TEXT("marauderImpact");
		PlayFx(TEXT("marauderLaunch"), Muzzle, Ang);
		AddProjectileModel(P, TEXT("projMarauder"), TEXT("marauderMissile"));
		if (!P.Model) AddProjectileVisual(P, FLinearColor(0.5f, 0.8f, 1), 0.18f);
		Projectiles.Add(P);
		PlaySound(BSSounds::MarauderFire, 0.4f, 0.2f);
	}
}

void ABSGame::UpdateTanks(float Dt)
{
	TArray<TSharedPtr<FBSTank>> Snapshot = Tanks;
	for (const TSharedPtr<FBSTank>& KP : Snapshot)
	{
		FBSTank& K = *KP;
		if (K.bDead) continue;
		if (K.State == FBSTank::Incoming)
		{
			K.T += Dt;
			const FVector Goal(K.X, K.Y, 5.5f);
			const FVector D = Goal - K.Med;
			if (D.Size() > 0.3f && K.T < 10) { K.Med += D.GetSafeNormal() * 9 * Dt; }
			else
			{
				K.State = FBSTank::Unloading; K.T = 0;
				if (K.Medivac) K.Medivac->Play(TEXT("stand work start"), false);
				PlaySound(BSSounds::MedivacUnload, 0.6f);
			}
			if (K.Medivac) K.Medivac->SetSim(K.Med.X, K.Med.Y, K.Med.Z, PI / 2);
		}
		else if (K.State == FBSTank::Unloading)
		{
			K.T += Dt;
			if (K.T > 1.6f && !K.Actor)
			{
				K.Actor = AcquireModel(TEXT("siegetankMorph"));
				if (K.Actor) { K.Actor->SetTeamColor(TerranColor); K.Actor->SetSim(K.X, K.Y, 0, PI / 2); K.Actor->Play(TEXT("morph start"), false); }
				AddFx(EBSFxKind::Blast, FVector(K.X, K.Y, 0.05f), FVector::ZeroVector, 0.6f, 1.6f, FLinearColor(0.6f, 0.75f, 1), 3);
				Shake = FMath::Min(1.f, Shake + 0.2f);
			}
			if (K.T > 2.2f && K.Medivac) K.Medivac->Play(TEXT("stand work end"), false);
			if (K.T > 3.6f) { K.State = FBSTank::Leaving; K.T = 0; if (K.Medivac) K.Medivac->Play(WalkNames, true); }
		}
		else if (K.State == FBSTank::Leaving)
		{
			K.T += Dt;
			K.Med += FVector(0, -10 * Dt, 2 * Dt);
			if (K.Medivac) K.Medivac->SetSim(K.Med.X, K.Med.Y, K.Med.Z, -PI / 2);
			if (K.T > 2.0f)
			{
				if (K.Actor) ReleaseModel(K.Actor);
				K.Actor = AcquireModel(TEXT("siegetank"));
				if (K.Actor) { K.Actor->SetTeamColor(TerranColor); K.Actor->SetSim(K.X, K.Y, 0, PI / 2); K.Actor->Play(TEXT("stand"), true); K.Actor->EnableTurret(TEXT("Ref_TurretZ"), TankTurretOffsetDeg); }
				K.State = FBSTank::Ready; K.MedT = 0;
				Toast(TEXT("Siege tank in position"));
			}
		}
		else
		{
			if (K.Medivac)
			{
				K.MedT += Dt; K.Med += FVector(0, -10 * Dt, 2 * Dt);
				K.Medivac->SetSim(K.Med.X, K.Med.Y, K.Med.Z, -PI / 2);
				if (K.MedT > 3) { ReleaseModel(K.Medivac); K.Medivac = nullptr; }
			}
			K.Cd -= Dt;
			if (K.Cd > 0) continue;
			const float Reach = BSDefs::TankRange + (Up.bHiSec ? BSDefs::HiSecRange : 0.f) + 2 * CardLevel(TEXT("siegerange"));
			auto Pred = [&K](const FBSEnemy& E) { return !E.Def->bFlying && FMath::Sqrt(Dist2(K.X, K.Y, E.X, E.Y)) > BSDefs::TankMinRange; };
			TSharedPtr<FBSEnemy> Target;
			if (TSharedPtr<FBSEnemy> F = Focus.Pin())
				if (F->Alive() && Pred(*F) && FMath::Sqrt(Dist2(K.X, K.Y, F->X, F->Y)) <= Reach) Target = F;
			if (!Target)
			{
				TArray<TSharedPtr<FBSEnemy>> L = EnemiesInRange(K.X, K.Y, Reach, Pred);
				if (!L.Num()) { K.Cd = 0.1f; continue; }
				int32 BestN = -1;
				for (int32 i = 0; i < FMath::Min(8, L.Num()); i++)
				{
					const int32 Cnt = EnemiesInRange(L[i]->X, L[i]->Y, 1.1f, Pred).Num();
					if (Cnt > BestN) { BestN = Cnt; Target = L[i]; }
				}
			}
			K.Cd = BSDefs::TankCd;
			const float Ang = FMath::Atan2(Target->Y - K.Y, Target->X - K.X);
			// The hull stays put; only the turret traverses to the target.
			if (K.Actor) { K.Actor->SetTurretAim(Ang); K.Actor->Play(TEXT("attack"), false); }
			FBSProjectile P; P.Kind = EBSProjKind::Shell; P.bFriendly = true;
			P.From = FVector(K.X + FMath::Cos(Ang) * 1.4f, K.Y + FMath::Sin(Ang) * 1.4f, 1.3f);
			P.To = FVector(Target->X, Target->Y, 0.1f); P.Dur = 0.55f; P.Arc = 4;
			P.Dmg = BSDefs::TankDmg + 5 * Up.Vehicle; P.DmgArmored = BSDefs::TankDmgArmored + 3 * Up.Vehicle;
			P.ImpactFx = TEXT("tankImpact");
			if (!PlayFx(TEXT("tankLaunch"), P.From, Ang)) { AddProjectileVisual(P, FLinearColor(1, 0.75f, 0.35f), 0.3f); AddFx(EBSFxKind::Flash, P.From, P.From, 0.14f, 0.7f, FLinearColor(1, 0.8f, 0.45f), 14); }
			else P.Dur = 0.35f;
			Projectiles.Add(P);
			PlaySound(BSSounds::TankFire, 0.7f, 0.2f);
			Shake = FMath::Min(1.f, Shake + 0.15f);
		}
	}
}

void ABSGame::UpdateScvs(float Dt)
{
	// Repair jobs, most urgent first: the bunker gets a head start, and when it is in real danger everyone works on it.
	struct FJob { float X, Y, R, Cost; float* Hp; float Max, Need; };
	TArray<FJob> Jobs;
	const float BunkerFrac = Bunker.MaxHp > 0 ? Bunker.Hp / Bunker.MaxHp : 1;
	if (!Bunker.bDead && Bunker.Hp < Bunker.MaxHp - 1) Jobs.Add({ 0, 0, BSDefs::BunkerHalfSize, 0.25f, &Bunker.Hp, Bunker.MaxHp, BunkerFrac - 0.2f });
	if (BunkerFrac > 0.4f)
		for (const TSharedPtr<FBSTank>& K : Tanks)
			if (K->State == FBSTank::Ready && !K->bDead && K->Hp < K->MaxHp - 1) Jobs.Add({ K->X, K->Y, BSDefs::TankRadius, 0.4f, &K->Hp, K->MaxHp, K->Hp / K->MaxHp });
	// (Nothing up on the high ground: the SCV posted at the forward base looks after that.)
	if (BunkerFrac > 0.4f)
		for (const TSharedPtr<FBSStructure>& St : Structures)
			if (St->bReady && !St->bDead && !St->bCc && St->Hp < St->MaxHp - 1) Jobs.Add({ St->X, St->Y, St->Radius, 0.3f, &St->Hp, St->MaxHp, St->Hp / St->MaxHp });
	if (BunkerFrac > 0.4f)
		for (const TSharedPtr<FBSMech>& M : Mechs)
			if (M->bReady && !M->bDead && !M->Fwd.bOn && M->Hp < M->MaxHp - 1) Jobs.Add({ M->X, M->Y, M->Def->Radius, 0.4f, &M->Hp, M->MaxHp, M->Hp / M->MaxHp });
	Jobs.Sort([](const FJob& A, const FJob& B) { return A.Need < B.Need; });
	TArray<TSharedPtr<FBSScv>> Snapshot = Scvs;
	for (const TSharedPtr<FBSScv>& SP : Snapshot) if (SP->Fwd.bOn) UpdateFwdScv(*SP, Dt);
	int32 ScvIndex = 0;
	for (const TSharedPtr<FBSScv>& SP : Scvs)
	{
		FBSScv& S = *SP;
		if (S.Fwd.bOn) continue;
		float JobX = 0, JobY = 0, JobR = 0, Cost = 0;
		float* JobHp = nullptr; float JobMax = 0;
		if (Jobs.Num())
		{
			const FJob& J = Jobs[ScvIndex++ % Jobs.Num()];
			JobX = J.X; JobY = J.Y; JobR = J.R; Cost = J.Cost; JobHp = J.Hp; JobMax = J.Max;
		}
		float Gx, Gy, Stop;
		if (JobHp)
		{
			const float A = FMath::Atan2(S.Idle.Y - JobY, S.Idle.X - JobX);
			Gx = JobX + FMath::Cos(A) * (JobR + 0.6f); Gy = JobY + FMath::Sin(A) * (JobR + 0.6f); Stop = 0.25f;
		}
		else { Gx = S.Idle.X; Gy = S.Idle.Y; Stop = 0.3f; }
		const float Dx = Gx - S.X, Dy = Gy - S.Y, Dd = FMath::Sqrt(Dx * Dx + Dy * Dy);
		if (Dd > Stop)
		{
			const float Sp = FMath::Min(Dd, BSDefs::ScvSpeed * Dt);
			S.X += Dx / Dd * Sp; S.Y += Dy / Dd * Sp; S.Yaw = FMath::Atan2(Dy, Dx);
			if (S.Anim != TEXT("walk") && S.Actor) { S.Anim = TEXT("walk"); S.Actor->Play(TEXT("walk"), true, 1.4f); }
		}
		else if (JobHp)
		{
			S.Yaw = FMath::Atan2(JobY - S.Y, JobX - S.X);
			if (S.Anim != TEXT("work") && S.Actor) { S.Anim = TEXT("work"); S.Actor->Play(TEXT("work"), true); PlaySound(BSSounds::ScvRepair, 0.25f, 1.5f); }
			const float Amount = FMath::Min3(BSDefs::ScvRepairRate * (1 + 0.5f * CardLevel(TEXT("scvrepair"))) * Dt, JobMax - *JobHp, Minerals / Cost);
			if (Amount > 0) { *JobHp += Amount; Minerals -= Amount * Cost; }
			if (FMath::FRand() < Dt * 6) AddFx(EBSFxKind::Flash, FVector(S.X + FMath::Cos(S.Yaw) * 0.5f, S.Y + FMath::Sin(S.Yaw) * 0.5f, 0.6f), FVector::ZeroVector, 0.08f, 0.15f, FLinearColor(0.6f, 0.85f, 1), 16);
		}
		else if (S.Anim != TEXT("stand") && S.Actor) { S.Anim = TEXT("stand"); S.Actor->Play(StandNames, true); }
		if (S.Actor) S.Actor->SetSim(S.X, S.Y, 0, S.Yaw);
	}
}

// Air strike: fly in, hover over a post firing missiles at ground zerg passing below, then leave.
void ABSGame::UpdateBanshees(float Dt)
{
	const float HomeY = Mode == EBSMode::Lane ? -18.f : -36.f;
	for (int32 i = Banshees.Num() - 1; i >= 0; i--)
	{
		FBSBanshee& B = *Banshees[i];
		B.T += Dt;
		if (B.State == FBSBanshee::In)
		{
			const FVector D = FVector(B.Post.X, B.Post.Y, BSDefs::BansheeFly) - FVector(B.X, B.Y, B.Z);
			if (D.Size() > 0.2f)
			{
				const FVector Mv = D.GetSafeNormal() * FMath::Min(D.Size(), BSDefs::BansheeSpeed * Dt);
				B.X += Mv.X; B.Y += Mv.Y; B.Z += Mv.Z; B.Yaw = FMath::Atan2(D.Y, D.X);
			}
			else { B.State = FBSBanshee::Hover; B.T = 0; if (B.Actor) B.Actor->Play(StandNames, true); }
		}
		else if (B.State == FBSBanshee::Hover)
		{
			TArray<TSharedPtr<FBSEnemy>> L = EnemiesInRange(B.X, B.Y, BSDefs::BansheeRange, [](const FBSEnemy& E) { return !E.Def->bFlying; });
			if (L.Num()) B.Yaw += FMath::Clamp(FMath::FindDeltaAngleRadians(B.Yaw, FMath::Atan2(L[0]->Y - B.Y, L[0]->X - B.X)), -4 * Dt, 4 * Dt);
			B.Cd -= Dt * (B.AcidUntil > Time ? 0.7f : 1.f);
			if (B.Cd <= 0 && L.Num())
			{
				B.Cd = BSDefs::BansheeCd;
				for (int32 k = 0; k < BSDefs::BansheeShots; k++)
				{
					const float Off = k ? 0.4f : -0.4f;
					FBSProjectile P; P.Kind = EBSProjKind::Rocket; P.bFriendly = true;
					P.From = FVector(B.X - FMath::Sin(B.Yaw) * Off, B.Y + FMath::Cos(B.Yaw) * Off, B.Z - 0.3f);
					P.To = FVector(L[0]->X, L[0]->Y, L[0]->Z + 0.3f); P.T = -k * 0.1f; P.Dur = 0.35f; P.Dmg = BSDefs::BansheeDmg; P.TargetEnemy = L[0]; P.ImpactFx = TEXT("bansheeImpact");
					AddProjectileModel(P, TEXT("projBanshee"), TEXT("bansheeMissile"));
					if (!P.Model) AddProjectileVisual(P, FLinearColor(1, 0.7f, 0.3f), 0.15f);
					Projectiles.Add(P);
				}
				const float Len = B.Actor ? B.Actor->Play(TEXT("attack"), false) : 0.f;
				B.StandAt = Time + (Len > 0 ? Len : 0.5f);
				PlaySound(BSSounds::BansheeFire, 0.5f, 0.2f);
			}
			else if (B.Cd <= 0) B.Cd = 0.1f;
			if (B.StandAt > 0 && Time > B.StandAt) { B.StandAt = 0; if (B.Actor) B.Actor->Play(StandNames, true); }
			B.Z = BSDefs::BansheeFly + 0.15f * FMath::Sin(Time * 2.1f + i);
			if (B.T > BSDefs::BansheeHover) { B.State = FBSBanshee::Out; B.T = 0; if (B.Actor) B.Actor->Play(WalkNames, true); }
		}
		else
		{
			B.Y -= BSDefs::BansheeSpeed * Dt; B.Z += 1.5f * Dt;
			B.Yaw += FMath::Clamp(FMath::FindDeltaAngleRadians(B.Yaw, -PI / 2), -3 * Dt, 3 * Dt);
			if (B.Y <= HomeY) { ReleaseModel(B.Actor); Banshees.RemoveAt(i); continue; }
		}
		if (B.Actor) B.Actor->SetSim(B.X, B.Y, B.Z, B.Yaw);
	}
}

void ABSGame::UpdateProjectiles(float Dt)
{
	for (int32 i = Projectiles.Num() - 1; i >= 0; i--)
	{
		FBSProjectile& P = Projectiles[i];
		P.T += Dt;
		if (P.T < 0) continue;                     // staggered launch (second missile of a pair)
		if (TSharedPtr<FBSEnemy> TE = P.TargetEnemy.Pin()) P.To = FVector(TE->X, TE->Y, TE->Z + TE->Def->Height * 0.5f);
		if (P.Visual)
		{
			const float K = FMath::Clamp(P.T / P.Dur, 0.f, 1.f);
			FVector Pos = FMath::Lerp(P.From, P.To, K);
			Pos.Z += FMath::Sin(K * PI) * P.Arc;
			P.Visual->SetWorldLocation(W(Pos));
		}
		if (P.Model || P.Trail)
		{
			const float K = FMath::Clamp(P.T / P.Dur, 0.f, 1.f);
			FVector Pos = FMath::Lerp(P.From, P.To, K);
			Pos.Z += FMath::Sin(K * PI) * P.Arc;
			const float Yaw = FMath::Atan2(P.To.Y - P.From.Y, P.To.X - P.From.X);
			if (P.Model) P.Model->SetSim(Pos.X, Pos.Y, Pos.Z, Yaw);
			if (P.Trail) MoveFx(P.Trail, Pos, Yaw);
		}
		if (P.T < P.Dur) continue;
		FBSProjectile Done = P;
		Projectiles.RemoveAt(i);
		ReleaseMesh(Done.Visual);
		if (Done.Model) ReleaseModel(Done.Model);
		if (Done.Trail) StopFx(Done.Trail);
		const float ImpactYaw = FMath::Atan2(Done.From.Y - Done.To.Y, Done.From.X - Done.To.X);
		const bool bImpactFx = !Done.ImpactFx.IsEmpty() && PlayFx(Done.ImpactFx, Done.To, ImpactYaw, Done.ImpactScale) != 0;
		if (Done.bFriendly)
		{
			if (Done.Kind == EBSProjKind::Shell)
			{
				for (auto& E : EnemiesInRange(Done.To.X, Done.To.Y, BSDefs::TankSplash3 + 0.5f, [](const FBSEnemy& E) { return !E.Def->bFlying; }))
				{
					const float Dd = FMath::Sqrt(Dist2(Done.To.X, Done.To.Y, E->X, E->Y)) - E->Radius;
					const float Mult = Dd <= BSDefs::TankSplash1 ? 1 : Dd <= BSDefs::TankSplash2 ? 0.5f : Dd <= BSDefs::TankSplash3 ? 0.25f : 0;
					if (Mult > 0) HurtEnemy(E, (Done.Dmg + (E->Def->bArmored ? Done.DmgArmored : 0)) * Mult, true);
				}
				if (!bImpactFx)
				{
					AddFx(EBSFxKind::Blast, FVector(Done.To.X, Done.To.Y, 0.05f), FVector::ZeroVector, 0.6f, 1.8f, FLinearColor(1, 0.55f, 0.2f), 6);
					EmitExplosion(FVector(Done.To.X, Done.To.Y, 0.3f), 1.4f, FLinearColor(1, 0.62f, 0.25f), FLinearColor(0.75f, 0.18f, 0.03f), 14);
				}
				PlaySound(BSSounds::TankHit, 0.6f, 0.1f);
				Shake = FMath::Min(1.f, Shake + 0.1f);
			}
			else if (Done.Kind == EBSProjKind::Grenade)
			{
				TSharedPtr<FBSEnemy> TE = Done.TargetEnemy.Pin();
				if (TE && Done.Dmg > 0)
				{
					const int32 Con = CardLevel(TEXT("concussive"));
					TE->SlowUntil = Time + Done.IDef->Slow + Con; TE->Anim.Reset();
					HurtEnemy(TE, Done.Dmg + (TE->Def->bArmored ? Done.IDef->DmgArmored + 4 * Con : 0));
				}
				if (!bImpactFx) AddFx(EBSFxKind::Blast, FVector(Done.To.X, Done.To.Y, 0.05f), FVector::ZeroVector, 0.3f, 0.6f, FLinearColor(0.45f, 0.75f, 1), 6);
			}
			else if (Done.Kind == EBSProjKind::Rocket)
			{
				if (TSharedPtr<FBSEnemy> TE = Done.TargetEnemy.Pin()) HurtEnemy(TE, Done.Dmg);
				if (!bImpactFx)
				{
					AddFx(EBSFxKind::Blast, FVector(Done.To.X, Done.To.Y, 0.05f), FVector::ZeroVector, 0.35f, 0.8f, FLinearColor(1, 0.7f, 0.35f), 6);
					AddFx(EBSFxKind::Flash, Done.To, Done.To, 0.15f, 0.5f, FLinearColor(1, 0.8f, 0.5f), 10);
				}
				PlaySound(BSSounds::BansheeHit, 0.35f, 0.1f);
			}
			else if (Done.Kind == EBSProjKind::Missile)
			{
				if (Done.Splash > 0)
				{
					const int8 Air = Done.SplashAir;
					for (const TSharedPtr<FBSEnemy>& V : EnemiesInRange(Done.To.X, Done.To.Y, Done.Splash, [Air](const FBSEnemy& E) { return Air < 0 || E.Def->bFlying == (Air == 1); }))
						HurtEnemy(V, Done.Dmg, true);
				}
				else if (TSharedPtr<FBSEnemy> TE = Done.TargetEnemy.Pin()) HurtEnemy(TE, Done.Dmg);
				if (!bImpactFx) AddFx(EBSFxKind::Flash, Done.To, Done.To, 0.15f, 0.5f * Done.ImpactScale, FLinearColor(1, 0.75f, 0.4f), 10);
			}
			else if (Done.Kind == EBSProjKind::Sam)
			{
				if (TSharedPtr<FBSEnemy> TE = Done.TargetEnemy.Pin()) HurtEnemy(TE, Done.Dmg);
				if (!bImpactFx) AddFx(EBSFxKind::Flash, Done.To, Done.To, 0.15f, 0.5f, FLinearColor(1, 0.8f, 0.5f), 10);
			}
			else if (Done.Kind == EBSProjKind::Yamato) YamatoImpact(Done.To, Done.Dmg);
		}
		else
		{
			const FBSTarget& Tg = Done.TargetDef;
			const FBSZergDef* D = Done.ZDef;
			if (D->Bombard > 0)
			{
				// Leviathan bile: everything of ours under the splash, whether or not the target is still there.
				const TArray<FBSTarget> Under = Targets;
				for (const FBSTarget& O : Under) if (FMath::Sqrt(Dist2(Done.To.X, Done.To.Y, O.X, O.Y)) - O.Radius <= D->Bombard) DamageDefender(O, D->Dmg);
				if (!bImpactFx) AddFx(EBSFxKind::Blast, FVector(Done.To.X, Done.To.Y, 0.05f), FVector::ZeroVector, 0.45f, D->Bombard, FLinearColor(0.55f, 1, 0.2f), 5);
				Shake = FMath::Min(1.f, Shake + 0.15f);
				continue;
			}
			if (!Tg.IsValid(Bunker)) continue;
			if (Done.Kind == EBSProjKind::BroodSpell) { BroodlingHit(Tg); continue; }
			float Dmg = D->Dmg;
			if (Tg.bLight) Dmg += D->DmgLight;
			if (Tg.bArmored) Dmg += D->DmgArmored;
			if (Done.Kind == EBSProjKind::Spines)
			{
				TSharedPtr<FBSEnemy> Src = Done.Source.Pin();
				const float Fx0 = Src ? Src->X : Done.From.X, Fy0 = Src ? Src->Y : Done.From.Y, Ex = Tg.X, Ey = Tg.Y;
				const float L = FMath::Max(0.01f, FMath::Sqrt(Dist2(Fx0, Fy0, Ex, Ey)));
				for (const FBSTarget& O : Targets)
				{
					const float Px = O.X - Fx0, Py = O.Y - Fy0, Proj = FMath::Clamp((Px * (Ex - Fx0) + Py * (Ey - Fy0)) / (L * L), 0.f, 1.f);
					const float Cx = Fx0 + (Ex - Fx0) * Proj, Cy = Fy0 + (Ey - Fy0) * Proj;
					if (FMath::Sqrt(Dist2(O.X, O.Y, Cx, Cy)) - O.Radius < 0.6f) DamageDefender(O, Dmg);
				}
				AddFx(EBSFxKind::Spines, FVector(Fx0, Fy0, 0.15f), FVector(Ex, Ey, 0.15f), 0.5f, 0.12f, FLinearColor(0.7f, 1, 0.3f), 5);
			}
			else
			{
				if (Done.Kind == EBSProjKind::Glaive) Dmg = Done.Dmg;
				DamageDefender(Tg, Dmg);
				if (D->Acid > 0)                    // devourer acid: the target fires slower for a while
				{
					if (auto B = Tg.BcRef.Pin()) B->AcidUntil = Time + D->Acid;
					if (auto A = Tg.AirRef.Pin()) A->AcidUntil = Time + D->Acid;
				}
				if (Done.Kind == EBSProjKind::Glaive && Done.Bounces > 0)
				{
					// The glaive ricochets to the nearest defender it has not hit yet, losing a third of its punch each time.
					TArray<FBSTarget> Hit = Done.AlreadyHit; Hit.Add(Tg);
					const FBSTarget* Next = nullptr; float Nearest = 4.5f;
					for (const FBSTarget& O : Targets)
					{
						if (!O.IsValid(Bunker) || Hit.ContainsByPredicate([&O](const FBSTarget& H) { return H.Same(O); })) continue;
						const float Dd = FMath::Sqrt(Dist2(Tg.X, Tg.Y, O.X, O.Y)) - O.Radius;
						if (Dd < Nearest) { Nearest = Dd; Next = &O; }
					}
					if (Next) LaunchGlaive(FVector(Tg.X, Tg.Y, 1.0f), *Next, Dmg * 0.67f, Done.Bounces - 1, Hit, D);
				}
				if (D->bBroodlings && Enemies.Num() < 80)
					for (int32 k = 0; k < 2; k++) { const FVector2D At(Tg.X + Rnd(-1, 1), Tg.Y + Rnd(-1, 1) + 1.5f); SpawnEnemy(TEXT("broodling"), 0, &At); }
				if (!bImpactFx) AddFx(EBSFxKind::Blast, FVector(Tg.X, Tg.Y, 0.05f), FVector::ZeroVector, 0.25f, 0.5f,
					Done.Kind == EBSProjKind::Acid ? FLinearColor(0.55f, 1, 0.2f) : FLinearColor(1, 0.7f, 0.5f), 5);
			}
		}
	}
}

void ABSGame::UpdateVisuals(float Dt)
{
	// -BSShowSlits: mark the bunker's four gun slits (red Weapon, green 02, blue 04, yellow 06).
	static const bool bShowSlits = FParse::Param(FCommandLine::Get(), TEXT("BSShowSlits"));
	if (bShowSlits && Bunker.Actor)
	{
		static const FName Slits[] = { TEXT("Attach_Weapon"), TEXT("Attach_Weapon_02"), TEXT("Attach_Weapon_04"), TEXT("Attach_Weapon_06") };
		static const FLinearColor Cols[] = { FLinearColor::Red, FLinearColor::Green, FLinearColor::Blue, FLinearColor::Yellow };
		for (int32 i = 0; i < 4; i++)
		{
			if (Bunker.Actor->Mesh->GetBoneIndex(Slits[i]) == INDEX_NONE) continue;
			const FVector Wp = Bunker.Actor->Mesh->GetBoneLocation(Slits[i]);
			FBSParticle& P = Emit();
			P.P = FVector(Wp.Y / BSSpace::UnitsPerMetre, Wp.X / BSSpace::UnitsPerMetre, Wp.Z / BSSpace::UnitsPerMetre);
			P.Life = P.MaxLife = 0.05f; P.Size0 = P.Size1 = 0.35f; P.Inten = 20; P.C0 = P.C1 = P.C2 = Cols[i];
		}
	}
	for (int32 i = Flashes.Num() - 1; i >= 0; i--)
	{
		FBSFlash& F = Flashes[i];
		if (F.Delay > 0) { F.Delay -= Dt; continue; }
		if (!F.bShown) { F.bShown = true; F.A->SetScale(F.Scale); F.A->SetSim(F.P.X, F.P.Y, F.P.Z, F.Yaw); F.A->PlayRandomWithPrefix(TEXT("stand"), false); }
		F.Life -= Dt;
		if (F.Life <= 0) { ReleaseModel(F.A); Flashes.RemoveAtSwap(i); }
	}
	UpdateCreepMesh();
	// Enemies: position, facing, hit flash.
	for (const TSharedPtr<FBSEnemy>& E : Enemies)
	{
		E->Flash = FMath::Max(0.f, E->Flash - Dt * 9);
		if (E->Actor)
		{
			E->Actor->SetSim(E->X, E->Y, E->Z, E->Yaw); E->Actor->SetHitFlash(E->Flash * 0.22f);
			if (E->Def->Cleave > 0) E->Actor->SetSwipe(E->bAttacking && FMath::Abs(Time - E->HitAt) < 0.22f);
		}
	}
	Bunker.Flash = FMath::Max(0.f, Bunker.Flash - Dt * 4);
	if (Bunker.Actor) Bunker.Actor->SetHitFlash(Bunker.Flash * 0.22f);   // a hint of white: constant zergling hits used to white it out
	for (auto& K : Tanks) { K->Flash = FMath::Max(0.f, K->Flash - Dt * 4); if (K->Actor) K->Actor->SetHitFlash(K->Flash * 0.3f); }
	for (auto& S : Scvs) { S->Flash = FMath::Max(0.f, S->Flash - Dt * 4); if (S->Actor) S->Actor->SetHitFlash(S->Flash * 0.3f); }
	// Stimmed troopers outside glow red-orange and pulse, as in StarCraft II.
	const bool bStimmed = Ab.StimUntil > Time;
	const FLinearColor StimColor = FMath::Lerp(TerranColor, FLinearColor(1.f, 0.22f, 0.06f), 0.55f + 0.3f * FMath::Sin(Time * 9.f));
	for (auto& M : Squad)
	{
		M->Flash = FMath::Max(0.f, M->Flash - Dt * 4);
		if (!M->Actor) continue;
		M->Actor->SetHitFlash(M->Flash * 0.3f + (bStimmed ? 0.06f : 0.f));
		if (bStimmed || M->bStimLook) { M->Actor->SetTeamColor(bStimmed ? StimColor : TerranColor); M->bStimLook = bStimmed; }
	}
	for (auto& Mc : Mechs) { Mc->Flash = FMath::Max(0.f, Mc->Flash - Dt * 4); if (Mc->Actor) Mc->Actor->SetHitFlash(Mc->Flash * 0.3f); }
	for (auto& St : Structures) { St->Flash = FMath::Max(0.f, St->Flash - Dt * 4); if (St->Actor) St->Actor->SetHitFlash(St->Flash * 0.3f); }

	UpdateParticles(Dt);
	UpdateFx(Dt);
	MuzzleFlash = FMath::Max(0.f, MuzzleFlash - Dt * 14);
	if (MuzzleLight)
	{
		MuzzleLight->SetWorldLocation(BSSpace::ToWorld(0, 1.2f, 1.6f));
		MuzzleLight->SetIntensity(MuzzleFlash * Rnd(2500, 5000) + (Ab.StimUntil > Time ? 300 : 0));
	}

	for (int32 i = Corpses.Num() - 1; i >= 0; i--)
	{
		FBSCorpse& Cp = Corpses[i];
		Cp.T += Dt;
		if (Cp.T > Cp.Hold) { if (Cp.Actor) { Cp.Actor->Mesh->CastShadow = true; Cp.Actor->Mesh->MarkRenderStateDirty(); } ReleaseModel(Cp.Actor); Corpses.RemoveAt(i); continue; }
		// Settle into the ground over the second half of the hold, as SC2 corpses decay.
		if (Cp.bSink && Cp.Actor)
		{
			const float K = FMath::Clamp((Cp.T / Cp.Hold - 0.45f) / 0.55f, 0.f, 1.f);
			Cp.Actor->SetSim(Cp.X, Cp.Y, Cp.Z - 0.9f * K * K, Cp.Yaw);
		}
	}
	for (int32 i = Floaters.Num() - 1; i >= 0; i--) { Floaters[i].T += Dt; if (Floaters[i].T > 1.3f) Floaters.RemoveAt(i); }

	for (int32 i = Fx.Num() - 1; i >= 0; i--)
	{
		FBSFx& F = Fx[i];
		F.T += Dt;
		const float K = FMath::Clamp(F.T / F.Dur, 0.f, 1.f);
		if (K >= 1) { ReleaseMesh(F.Comp); Fx.RemoveAt(i); continue; }
		const FVector A = W(F.A), B = W(F.B);
		switch (F.Kind)
		{
		case EBSFxKind::Tracer:
		case EBSFxKind::Spines:
		{
			const FVector D = B - A; const float Len = D.Size();
			F.Comp->SetWorldLocationAndRotation((A + B) * 0.5f, FRotationMatrix::MakeFromZ(D.GetSafeNormal()).Rotator());
			const float Th = F.Radius * BSSpace::UnitsPerMetre / 100.f * (F.Kind == EBSFxKind::Spines ? (1 - K * 0.5f) : 1);
			F.Comp->SetWorldScale3D(FVector(Th, Th, Len / 100.f));
			F.Mid->SetScalarParameterValue(TEXT("Fade"), 1 - K);
			break;
		}
		case EBSFxKind::Flash:
		{
			F.Comp->SetWorldLocation(A);
			F.Comp->SetWorldScale3D(FVector(F.Radius * (1 - K * 0.4f) * 2.f));
			F.Mid->SetScalarParameterValue(TEXT("Fade"), 1 - K);
			break;
		}
		case EBSFxKind::Blast:
		{
			F.Comp->SetWorldLocation(A);
			F.Comp->SetWorldScale3D(FVector(F.Radius * 2.f, F.Radius * 2.f, 1));
			F.Mid->SetScalarParameterValue(TEXT("Radius"), 0.12f + 0.33f * FMath::Sqrt(K));
			F.Mid->SetScalarParameterValue(TEXT("Thickness"), 0.04f + 0.06f * (1 - K));
			F.Mid->SetScalarParameterValue(TEXT("Fade"), 1 - K * K);
			break;
		}
		case EBSFxKind::Flame:
		{
			const FVector D = A - B; const float Len = D.Size();
			F.Comp->SetWorldLocationAndRotation((A + B) * 0.5f, FRotationMatrix::MakeFromZ(D.GetSafeNormal()).Rotator());
			const float Wd = F.Radius * (0.5f + K) * Rnd(0.85f, 1.15f);
			F.Comp->SetWorldScale3D(FVector(Wd, Wd, Len / 100.f));
			F.Mid->SetScalarParameterValue(TEXT("Fade"), (1 - K) * Rnd(0.7f, 1.f));
			break;
		}
		}
	}
}

void ABSGame::CrowdTick(float Dt)
{
	WaveTimer = 1e9f;
	static const TCHAR* Mix[] = { TEXT("zergling"), TEXT("zergling"), TEXT("zergling"), TEXT("roach"), TEXT("hydralisk"), TEXT("baneling") };
	for (int32 i = Enemies.Num(); i < CrowdTest; i++)
	{
		const FVector2D At(Rnd(-12, 12), Rnd(12, 26));
		if (TSharedPtr<FBSEnemy> E = SpawnEnemy(Mix[i % 6], 0, &At)) { E->Hp = E->MaxHp = 1e6f; }
	}
	CrowdT += Dt; CrowdSum += Dt; CrowdFrames++;
	if (CrowdT >= 5)
	{
		UE_LOG(LogTemp, Display, TEXT("BSCrowd alive=%d frame=%.2fms fps=%.1f"), Enemies.Num(), 1000 * CrowdSum / CrowdFrames, CrowdFrames / CrowdSum);
		CrowdT = 0; CrowdSum = 0; CrowdFrames = 0;
	}
}

void ABSGame::AutoPlay()
{
	const float Before = Minerals;
	const float StimCd = Ab.StimCd, StrikeCd = Ab.StrikeCd, RepairCd = Ab.RepairCd, YamatoCd = Ab.YamatoCd;
	AutoPlayImpl();
	if (Minerals < Before - 0.01f) { Tele.Purchases++; TotalPurchases++; Tele.Spent += Before - Minerals; TotalSpent += Before - Minerals; }
	if (Ab.StimCd > StimCd || Ab.StrikeCd > StrikeCd || Ab.RepairCd > RepairCd || Ab.YamatoCd > YamatoCd) Tele.AbilityUses++;
}

// Research only comes from the shop now: during a wave BuyUpgrade fails, so a commander that wants research banks its
// minerals for the next shop (see AutoShop).
void ABSGame::AutoPlayImpl()
{
	if (Bot == TEXT("passive")) return;                       // baseline: the starting kit only
	if (Bot == TEXT("random"))
	{
		// A distracted player: random affordable choices half the time, abilities at random.
		if (FMath::FRand() < 0.15f) UseStim();
		if (FMath::FRand() < 0.05f) CallAirStrike();
		if (FMath::FRand() < 0.04f) UseYamato();
		if (Bunker.Hp < Bunker.MaxHp * 0.5f && FMath::FRand() < 0.3f) EmergencyRepair();
		if (FMath::FRand() < 0.5f) return;
		const int32 Pick = FMath::RandRange(0, 10);
		switch (Pick)
		{
		case 0: AddInfantry(TEXT("marine")); break; case 1: AddInfantry(TEXT("firebat")); break; case 2: AddInfantry(TEXT("marauder")); break;
		case 3: AddScv(); break; case 4: DropTank(); break; case 5: BuyUpgrade(TEXT("stim")); break; case 6: BuyUpgrade(TEXT("weapons")); break;
		case 7: BuyUpgrade(TEXT("neosteel")); break; case 8: BuyUpgrade(TEXT("plating")); break; case 9: BuyUpgrade(FMath::RandBool() ? TEXT("vehicle") : TEXT("hisec")); break;
		default: BuyUpgrade(TEXT("medivac")); break;
		}
		return;
	}
	if (Bunker.Hp < Bunker.MaxHp * 0.45f) EmergencyRepair();
	if (Up.bStim && Ab.StimCd <= 0 && Enemies.Num() > 6) UseStim();
	if (Enemies.Num() > 14 && Ab.StrikeCd <= 0 && Minerals > 260) CallAirStrike();
	if (Enemies.Num() > 18 && Ab.YamatoCd <= 0 && Minerals > BSDefs::YamatoCost + 100) UseYamato();
	const int32 N = Bunker.Units.Num() + PendingInfantry();   // troops on their way count too
	if (Bot == TEXT("marines"))
	{
		// Bio only: marines, stim, weapons, a bigger bunker, medivac and plating. No tanks, no SCVs.
		if (N < Bunker.Slots && Minerals >= 50) { AddInfantry(TEXT("marine")); return; }
		if (!Up.bStim && Minerals >= 100) { BuyUpgrade(TEXT("stim")); return; }
		if (UpgradeAvailable(TEXT("weapons")) && Minerals >= CostOfUpgrade(TEXT("weapons"))) { BuyUpgrade(TEXT("weapons")); return; }
		if (!Up.bNeosteel && Minerals >= 150) { BuyUpgrade(TEXT("neosteel")); return; }
		if (UpgradeAvailable(TEXT("medivac")) && Minerals >= 200) { BuyUpgrade(TEXT("medivac")); return; }
		if (UpgradeAvailable(TEXT("hisec")) && Minerals >= 200) { BuyUpgrade(TEXT("hisec")); return; }
		if (UpgradeAvailable(TEXT("plating")) && Minerals >= CostOfUpgrade(TEXT("plating"))) { BuyUpgrade(TEXT("plating")); return; }
		return;
	}
	if (Bot == TEXT("mech"))
	{
		// Tanks first, SCVs to keep them alive, vehicle upgrades; the bunker keeps its starting marines.
		if (Tanks.Num() < TankMax) { if (Minerals >= TankCost()) DropTank(); return; }
		if (UpgradeAvailable(TEXT("hisec")) && Minerals >= 200) { BuyUpgrade(TEXT("hisec")); return; }
		if (Scvs.Num() < 3 && Minerals >= 50) { AddScv(); return; }
		if (UpgradeAvailable(TEXT("vehicle")) && Minerals >= CostOfUpgrade(TEXT("vehicle"))) { BuyUpgrade(TEXT("vehicle")); return; }
		if (UpgradeAvailable(TEXT("plating")) && Minerals >= CostOfUpgrade(TEXT("plating"))) { BuyUpgrade(TEXT("plating")); return; }
		if (N < Bunker.Slots && Minerals >= 100) { AddInfantry(TEXT("marauder")); return; }
		return;
	}
	// Balanced commander (the default, also used for demos).
	const int32 InfantryN = N;
	int32 Firebats = 0; for (auto& U : Bunker.Units) if (U.Def->Key == TEXT("firebat")) Firebats++;
	if (InfantryN < 3 && Minerals >= 50) { AddInfantry(TEXT("marine")); return; }
	if (!Up.bStim && Minerals >= 100) { BuyUpgrade(TEXT("stim")); return; }
	if (InfantryN >= 4 && Tanks.Num() < 1) { if (Minerals >= TankCost()) DropTank(); return; }   // save up for the first tank
	if (InfantryN < Bunker.Slots && Minerals >= 100) { AddInfantry(Firebats < 1 ? TEXT("firebat") : (InfantryN % 2 ? TEXT("marauder") : TEXT("marine"))); return; }
	if (Up.Weapons < 1 && Minerals >= 100) { BuyUpgrade(TEXT("weapons")); return; }
	if (Scvs.Num() < 2 && Minerals >= 50) { AddScv(); return; }
	if (!Up.bNeosteel && Minerals >= 150) { BuyUpgrade(TEXT("neosteel")); return; }
	if (Tanks.Num() < FMath::Min(TankMax, 2) && Minerals >= TankCost()) { DropTank(); return; }
	if (UpgradeAvailable(TEXT("weapons")) && Minerals >= CostOfUpgrade(TEXT("weapons"))) { BuyUpgrade(TEXT("weapons")); return; }
	if (UpgradeAvailable(TEXT("medivac")) && Minerals >= 200) { BuyUpgrade(TEXT("medivac")); return; }
	if (UpgradeAvailable(TEXT("plating")) && Up.Plating < 1 && Minerals >= 250) { BuyUpgrade(TEXT("plating")); return; }
	if (UpgradeAvailable(TEXT("vehicle")) && Minerals >= CostOfUpgrade(TEXT("vehicle"))) { BuyUpgrade(TEXT("vehicle")); return; }
	if (UpgradeAvailable(TEXT("hisec")) && Minerals >= 200) { BuyUpgrade(TEXT("hisec")); return; }
	if (Tanks.Num() < TankMax && Minerals >= TankCost()) { DropTank(); return; }
	if (UpgradeAvailable(TEXT("plating")) && Minerals >= CostOfUpgrade(TEXT("plating"))) { BuyUpgrade(TEXT("plating")); return; }
}

// Bots in the shop: research in the order the balanced commander wants it, then the strongest passives, rerolling
// when nothing on offer is wanted (port of the headless test bot's shop policy).
void ABSGame::AutoShop()
{
	static const TCHAR* Order[] = { TEXT("weapons"), TEXT("neosteel"), TEXT("stim"), TEXT("medivac"), TEXT("plating"), TEXT("hisec"), TEXT("vehicle"), TEXT("rails"),
		TEXT("refinery"), TEXT("shield"), TEXT("firesup"), TEXT("spoils"), TEXT("incinerator"), TEXT("bounty"), TEXT("mule"), TEXT("scvrepair"), TEXT("medpacks"),
		TEXT("concussive"), TEXT("rotors"), TEXT("recruit"), TEXT("siegerange"), TEXT("goliath"), TEXT("thor"), TEXT("nuke"), TEXT("bc"), TEXT("odin"), TEXT("vikings") };
	auto Rank = [](const FString& Id) { for (int32 i = 0; i < UE_ARRAY_COUNT(Order); i++) if (Id == Order[i]) return i; return 99; };
	if (Bot == TEXT("passive")) return;
	for (int32 Pass = 0; Pass < 4; Pass++)
	{
		TArray<int32> Idx;
		for (int32 i = 0; i < Shop.Offers.Num(); i++) if (!Shop.Bought[i]) Idx.Add(i);
		Idx.Sort([&](int32 A, int32 B) { return Rank(Shop.Offers[A]) < Rank(Shop.Offers[B]); });
		bool bAny = false;
		for (const int32 i : Idx)
		{
			const FBSCardDef* C = BSDefs::FindCard(Shop.Offers[i]);
			if ((Rank(C->Id) < 11 || Minerals > 500) && Minerals >= CardCost(*C) && BuyCard(i)) bAny = true;
		}
		if (!bAny && Minerals > RerollCost() + 150) { Reroll(); continue; }
		break;
	}
}

void ABSGame::GameOver()
{
	if (bOver) return;
	bOver = true; Bunker.bDead = true; Bunker.Hp = 0;
	ReleaseModel(Bunker.Actor); Bunker.Actor = nullptr;
	Corpse(TEXT("bunkerDeath"), 0, 0, 0, PI / 2, 60, 1, true);
	AddFx(EBSFxKind::Blast, FVector(0, 0, 0.05f), FVector::ZeroVector, 1.2f, 6, FLinearColor(1, 0.5f, 0.2f), 10);
	PlaySound(BSSounds::Explosion, 0.9f);
	Shake = 1;
	FinalScore = FMath::RoundToInt(Score + Kills * 10 + Wave * 250);
	if (EndReason.IsEmpty()) EndReason = TEXT("overrun");
	WriteTelemetry(TEXT("wave"));
	WriteTelemetry(TEXT("final"));
	if (bAutoPlay) return;
	MetaResult = BSMeta::EndRun(Mode == EBSMode::Arena, WavesCleared);
	bNewBest = FinalScore > Best;
	Best = FMath::Max(Best, FinalScore);
	GConfig->SetInt(TEXT("BunkerSurvivors"), *(FString(TEXT("Best.")) + (Mode == EBSMode::Lane ? TEXT("Lane") : TEXT("Arena"))), Best, GGameUserSettingsIni);
	GConfig->Flush(false, GGameUserSettingsIni);
}

FString ABSGame::DebugState() const
{
	FString B = TEXT("none");
	if (Bunker.Actor)
	{
		const FBoxSphereBounds Bd = Bunker.Actor->Mesh->Bounds;
		B = FString::Printf(TEXT("loc=%s hidden=%d visible=%d mesh=%s bounds=%s r=%.0f anim=%s"), *Bunker.Actor->GetActorLocation().ToCompactString(),
			Bunker.Actor->IsHidden() ? 1 : 0, Bunker.Actor->Mesh->IsVisible() ? 1 : 0, Bunker.Actor->Mesh->GetSkeletalMeshAsset() ? *Bunker.Actor->Mesh->GetSkeletalMeshAsset()->GetName() : TEXT("null"),
			*Bd.Origin.ToCompactString(), Bd.SphereRadius, *Bunker.Actor->CurrentSequence());
	}
	FString Out;
	for (const TSharedPtr<FBSTrooper>& M : Squad) Out += FString::Printf(TEXT("%.1f,%.1f "), M->X, M->Y);
	FString Str;
	for (const TSharedPtr<FBSStructure>& S : Structures) Str += FString::Printf(TEXT("%s:%d:%.0f "), S->bCc ? TEXT("cc") : S->bSam ? TEXT("sam") : TEXT("sensor"), S->bReady ? 1 : 0, S->Hp);
	FString Ban;
	for (const TSharedPtr<FBSBanshee>& Bn : Banshees) Ban += FString::Printf(TEXT("%d@%.1f,%.1f "), (int32)Bn->State, Bn->X, Bn->Y);
	return FString::Printf(TEXT("t=%.1f wave=%d enemies=%d kills=%d minerals=%.0f bunker=%.0f/%.0f units=%d tanks=%d scvs=%d over=%d squad=[%s] structures=[%s] banshees=[%s] radar=%d rails=%d bunkerActor[%s]"),
		Time, Wave, Enemies.Num(), Kills, Minerals, Bunker.Hp, Bunker.MaxHp, Bunker.Units.Num(), Tanks.Num(), Scvs.Num(), bOver ? 1 : 0, *Out, *Str, *Ban, SensorUp() ? 1 : 0, Up.bRails ? 1 : 0, *B);
}

void ABSGame::WriteTelemetry(const FString& Kind)
{
	if (TelemetryPath.IsEmpty()) return;
	FString Line;
	const TCHAR* ModeS = Mode == EBSMode::Lane ? TEXT("lane") : TEXT("arena");
	if (Kind == TEXT("wave"))
	{
		Line = FString::Printf(TEXT("{\"kind\":\"wave\",\"mode\":\"%s\",\"bot\":\"%s\",\"wave\":%d,\"t\":%.1f,\"duration\":%.1f,\"spawned\":%d,\"peakAlive\":%d,\"kills\":%d,\"minHpPct\":%.3f,\"damage\":%.1f,\"idle\":%.1f,\"avgMinerals\":%.1f,\"spent\":%.1f,\"purchases\":%d,\"abilities\":%d,\"units\":%d,\"tanks\":%d,\"scvs\":%d,\"weapons\":%d,\"stim\":%d}"),
			ModeS, *Bot, Wave, Time, Time - Tele.WaveStart, Tele.Spawned, Tele.PeakAlive, Kills - Tele.KillsAtStart, Tele.MinHpPct, Tele.Damage, Tele.IdleTime,
			Tele.MineralSamples > 0 ? Tele.MineralSum / Tele.MineralSamples : Minerals, Tele.Spent, Tele.Purchases, Tele.AbilityUses,
			Bunker.Units.Num(), Tanks.Num(), Scvs.Num(), Up.Weapons, Up.bStim ? 1 : 0);
	}
	else
	{
		Line = FString::Printf(TEXT("{\"kind\":\"final\",\"mode\":\"%s\",\"bot\":\"%s\",\"wave\":%d,\"t\":%.1f,\"end\":\"%s\",\"kills\":%d,\"score\":%d,\"peakAlive\":%d,\"firstDamageAt\":%.1f,\"totalDamage\":%.1f,\"totalSpent\":%.1f,\"purchases\":%d}"),
			ModeS, *Bot, Wave, Time, *EndReason, Kills, FMath::RoundToInt(Score + Kills * 10 + Wave * 250), GamePeakAlive, FirstDamageAt, TotalDamage, TotalSpent, TotalPurchases);
	}
	FFileHelper::SaveStringToFile(Line + TEXT("\n"), *TelemetryPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM, &IFileManager::Get(), FILEWRITE_Append);
}

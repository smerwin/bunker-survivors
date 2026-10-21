#include "BSCameraPawn.h"
#include "BSGame.h"
#include "BSModelActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Engine.h"

namespace
{
	float EaseInOut(float T) { return T * T * (3 - 2 * T); }
	// Lane: 9 m to 42 m; arena: 11 m to 52 m. Exponential so every pinch step feels equal.
	float ArmLength(float Z, bool bArena) { const float A = bArena ? 1100.f : 900.f, B = bArena ? 5200.f : 4200.f; return A * FMath::Pow(B / A, Z); }
	float LogRatio(bool bArena) { return FMath::Loge(bArena ? 5200.f / 1100.f : 4200.f / 900.f); }
}

ABSCameraPawn::ABSCameraPawn()
{
	PrimaryActorTick.bCanEverTick = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(GetRootComponent());
	Camera->bConstrainAspectRatio = false;
	// The FOV we compute is horizontal; pin that interpretation so portrait screens are not zoomed in.
	Camera->bOverrideAspectRatioAxisConstraint = true;
	Camera->AspectRatioAxisConstraint = EAspectRatioAxisConstraint::AspectRatio_MaintainXFOV;
	Camera->PostProcessBlendWeight = 1.f;
}

void ABSCameraPawn::SetGame(ABSGame* InGame) { Game = InGame; }
void ABSCameraPawn::SetArena(bool bInArena) { bArena = bInArena; ResetView(); Zoom = TargetZoom; Pan = TargetPan; Yaw = TargetYaw; }

void ABSCameraPawn::ZoomBy(float Factor)
{
	TargetZoom = FMath::Clamp(TargetZoom + FMath::Loge(FMath::Max(Factor, 0.01f)) / LogRatio(bArena), 0.f, 1.f);
}

void ABSCameraPawn::PanPixels(float Dx, float Dy, float ViewHeightPx)
{
	const float MetresPerPx = ArmLength(Zoom, bArena) / 100.f / FMath::Max(ViewHeightPx, 1.f) * 1.4f;
	TargetPan.X -= Dx * MetresPerPx;
	TargetPan.Y += Dy * MetresPerPx;
	const float Lim = bArena ? 18.f : 6.f;
	TargetPan.X = FMath::Clamp(TargetPan.X, -Lim, Lim);
	TargetPan.Y = FMath::Clamp(TargetPan.Y, bArena ? -Lim : -6.f, bArena ? Lim : 14.f);
}

void ABSCameraPawn::Orbit(float DeltaYawDeg) { if (bArena) TargetYaw += DeltaYawDeg; }

void ABSCameraPawn::ResetView() { TargetZoom = 0.38f; TargetPan = FVector2D::ZeroVector; if (!bArena) TargetYaw = 0; }

void ABSCameraPawn::Tick(float Dt)
{
	Super::Tick(Dt);
	if (bAttract) { TargetYaw += Dt * 4.f; Yaw = TargetYaw; }   // slow orbit behind the start screen
	const float K = 1 - FMath::Exp(-Dt * 9.f);           // critically damped follow
	Zoom += (TargetZoom - Zoom) * K;
	Pan += (TargetPan - Pan) * K;
	Yaw += (TargetYaw - Yaw) * K;

	const float Z = Zoom, E = EaseInOut(Z);
	const float Arm = ArmLength(Z, bArena);
	const float Pitch = FMath::Lerp(28.f, 68.f, E);                // degrees below horizontal
	const float VFov = FMath::Lerp(52.f, 40.f, Z);

	// Focus: lane looks ahead of the bunker toward the threat; the arena centres on the bunker.
	// The bottom deck covers roughly the lower quarter of the screen, so the lane focus stays close enough
	// that the bunker sits just above it.
	const float Ahead = bArena ? FMath::Lerp(1.5f, -2.f, Z) : FMath::Lerp(1.2f, 3.2f, Z);
	const float YawRad = FMath::DegreesToRadians(Yaw);
	const FVector2D Fwd(FMath::Cos(YawRad), FMath::Sin(YawRad)), Right(-FMath::Sin(YawRad), FMath::Cos(YawRad));
	const FVector2D FocusXY = Fwd * (Ahead + Pan.Y) + Right * Pan.X;               // world X/Y in metres
	const FVector Focus(FocusXY.X * 100.f, FocusXY.Y * 100.f, bArena ? 60.f : 80.f);

	const FRotator Rot(-Pitch, Yaw, 0);
	FVector Loc = Focus - Rot.Vector() * Arm;
	if (ABSGame* G = Game.Get())
	{
		ShakeT += Dt * 40.f;
		const float S = G->Shake * (1 - Z * 0.8f) * 18.f;
		Loc += FVector(FMath::PerlinNoise1D(ShakeT) * S, FMath::PerlinNoise1D(ShakeT + 31.7f) * S, FMath::PerlinNoise1D(ShakeT + 77.3f) * S * 0.6f);
	}
	SetActorLocation(Focus);
	Camera->SetWorldLocationAndRotation(Loc, Rot);

	// Unreal's FOV is horizontal: convert the desired vertical FOV for the current aspect ratio.
	FVector2D Size(16, 9);
	if (GEngine && GEngine->GameViewport) GEngine->GameViewport->GetViewportSize(Size);
	const float Aspect = Size.Y > 0 ? Size.X / Size.Y : 16.f / 9.f;
	const float HFov = 2.f * FMath::RadiansToDegrees(FMath::Atan(FMath::Tan(FMath::DegreesToRadians(VFov) * 0.5f) * Aspect));
	Camera->SetFieldOfView(FMath::Clamp(HFov, 20.f, 120.f));

	// Depth of field only when close: focus on the bunker, fading out past zoom 0.3.
	FPostProcessSettings& P = Camera->PostProcessSettings;
	const float Dof = FMath::Clamp(1.f - Z / 0.3f, 0.f, 1.f);
	P.bOverride_DepthOfFieldFocalDistance = P.bOverride_DepthOfFieldFstop = P.bOverride_DepthOfFieldSensorWidth = true;
	P.DepthOfFieldFocalDistance = Arm;
	P.DepthOfFieldSensorWidth = 24.576f;
	P.DepthOfFieldFstop = FMath::Lerp(32.f, 2.8f, Dof);
}

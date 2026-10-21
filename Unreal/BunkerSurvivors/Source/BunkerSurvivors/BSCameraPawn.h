// Camera whose angle follows the zoom: close in it drops to a low view up the lane, far out it rises to a
// near top-down tactical view. Field of view, focus point and depth of field follow the same curve.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "BSCameraPawn.generated.h"

class UCameraComponent;
class ABSGame;

UCLASS()
class ABSCameraPawn : public APawn
{
	GENERATED_BODY()
public:
	ABSCameraPawn();
	virtual void Tick(float Dt) override;

	void SetGame(ABSGame* InGame);
	void SetArena(bool bInArena);
	void SetAttract(bool bOn) { bAttract = bOn; if (bOn) { bArena = false; Zoom = TargetZoom = 0.16f; Yaw = TargetYaw = -35.f; Pan = TargetPan = FVector2D::ZeroVector; } }
	void ZoomBy(float Factor);          // > 1 zooms out
	void SetZoom(float Z, bool bSnap = false) { TargetZoom = FMath::Clamp(Z, 0.f, 1.f); if (bSnap) Zoom = TargetZoom; }
	void PanPixels(float Dx, float Dy, float ViewHeightPx);
	void Orbit(float DeltaYawDeg);
	void ResetView();
	float GetZoom() const { return Zoom; }
	float GetYaw() const { return Yaw; }

	UPROPERTY(VisibleAnywhere) TObjectPtr<UCameraComponent> Camera;

private:
	TWeakObjectPtr<ABSGame> Game;
	bool bArena = false, bAttract = false;
	float Zoom = 0.38f, TargetZoom = 0.38f;
	float Yaw = 0, TargetYaw = 0;
	FVector2D Pan = FVector2D::ZeroVector, TargetPan = FVector2D::ZeroVector;    // metres, in view-aligned axes
	float ShakeT = 0;
};

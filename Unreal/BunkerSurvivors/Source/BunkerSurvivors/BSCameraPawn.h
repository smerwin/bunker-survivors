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
	// bInFlip (swarm mode, lane): the lane seen from the zerg side, from behind their base (bunker at the top of the
	// screen, the base at the bottom).
	void SetArena(bool bInArena, bool bInFlip = false);
	void SetAttract(bool bOn) { bAttract = bOn; if (bOn) { bArena = false; Zoom = TargetZoom = 0.16f; Yaw = TargetYaw = -35.f; Pan = TargetPan = FVector2D::ZeroVector; } }
	void ZoomBy(float Factor);          // > 1 zooms out
	void SetZoom(float Z, bool bSnap = false) { TargetZoom = FMath::Clamp(Z, 0.f, 1.f); if (bSnap) Zoom = TargetZoom; }
	void PanPixels(float Dx, float Dy, float ViewHeightPx);
	void Orbit(float DeltaYawDeg);
	void ResetView();
	float GetZoom() const { return Zoom; }
	float GetYaw() const { return bBunkerView ? FpYaw : Yaw; }
	// Bunker view: a first-person eye inside the bunker, dragged to look all the way round (sky view otherwise).
	void SetBunkerView(bool bOn);
	bool IsBunkerView() const { return bBunkerView; }

	UPROPERTY(VisibleAnywhere) TObjectPtr<UCameraComponent> Camera;

private:
	TWeakObjectPtr<ABSGame> Game;
	bool bArena = false, bAttract = false, bFlip = false;
	static constexpr float FlipZoom = 0.88f;      // the flipped view's default zoom (about 42 m out)
	float FlipFocus() const;                      // metres up the lane the flipped view looks at (portrait 24, landscape 20)
	float Zoom = 0.38f, TargetZoom = 0.38f;
	float Yaw = 0, TargetYaw = 0;
	FVector2D Pan = FVector2D::ZeroVector, TargetPan = FVector2D::ZeroVector;    // metres, in view-aligned axes
	float ShakeT = 0;
	float SquadBack = 0;                          // eases the lane view back to show troopers standing behind the bunker
	bool bBunkerView = false;
	float FpYaw = 0, FpPitch = 10, FpFov = 70;   // degrees: heading (0 = up the lane), look-down angle, vertical FOV
};

// A pooled actor that shows one StarCraft II model and plays its sequences by name.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BSModelActor.generated.h"

class USkeletalMeshComponent;
class UMaterialInstanceDynamic;
struct FBSModel;

UCLASS()
class ABSModelActor : public AActor
{
	GENERATED_BODY()
public:
	ABSModelActor();

	void SetModel(const FBSModel* InModel);
	const FBSModel* GetModel() const { return Model; }

	// Plays the first matching sequence. Returns its length in seconds (0 if the model has none).
	float Play(const TArray<FString>& Names, bool bLoop, float Rate = 1.f);
	float Play(const FString& Name, bool bLoop, float Rate = 1.f) { return Play(TArray<FString>{ Name }, bLoop, Rate); }
	float PlayRandomWithPrefix(const FString& Prefix, bool bLoop);
	float SequenceLength(const TArray<FString>& Names) const;
	void SetRate(float Rate);
	FString CurrentSequence() const { return CurrentName; }

	// Simulation space: metres on the ground plane, sim x -> world Y, sim y -> world X (threats come from +X).
	void SetSim(float X, float Y, float Z, float YawRad);
	void SetScale(float S);
	void SetTeamColor(const FLinearColor& C);
	void SetHitFlash(float Amount);
	void SetScalarParam(FName Name, float Value);

	// Turret support: the animated pose is copied to a poseable mesh each frame and one bone is turned to aim.
	void EnableTurret(FName Bone, float OffsetDeg);
	void SetTurretAim(float SimYawRad) { TurretAimSim = SimYawRad; bTurretAiming = true; }
	bool HasTurret() const { return Poseable != nullptr; }
	virtual void Tick(float Dt) override;
	UPROPERTY() TObjectPtr<class UPoseableMeshComponent> Poseable;
	void Activate();
	void Deactivate();

	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> Root;
	UPROPERTY(VisibleAnywhere) TObjectPtr<USkeletalMeshComponent> Mesh;

	float HitFlash = 0.f;

private:
	const FBSModel* Model = nullptr;
	UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> Mids;
	FString CurrentName;
	FName TurretBone;
	float TurretOffsetDeg = 0, TurretAimSim = 0, TurretYawShown = 0;
	bool bTurretAiming = false;
};

// Model meshes face -Y (SC2 convention); the mesh component is rotated so actors face +X.
namespace BSSpace
{
	constexpr float UnitsPerMetre = 100.f;
	inline FVector ToWorld(float SimX, float SimY, float Z) { return FVector(SimY * UnitsPerMetre, SimX * UnitsPerMetre, Z * UnitsPerMetre); }
	// Sim yaw is atan2(dy, dx) in sim space; world yaw in degrees for an actor facing +X.
	inline float ToWorldYawDeg(float SimYawRad) { return 90.f - FMath::RadiansToDegrees(SimYawRad); }
}

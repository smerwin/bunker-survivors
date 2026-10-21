#include "BSModelActor.h"
#include "BSAssets.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/AnimSequence.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Components/PoseableMeshComponent.h"

// Mesh-space yaw that turns an SC2 model (facing -Y) to face the actor's +X.
// SC2 models face -Y in the source files; after import that ends up pointing the other way, hence -90.
static constexpr float MeshYawOffset = -90.f;

ABSModelActor::ABSModelActor()
{
	// Only turret actors tick (after animation) to re-aim the turret bone.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	Mesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Root);
	Mesh->SetRelativeRotation(FRotator(0, MeshYawOffset, 0));
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetGenerateOverlapEvents(false);
	Mesh->bEnableUpdateRateOptimizations = true;
	Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
	Mesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	Mesh->bCastDynamicShadow = true;
	Mesh->CastShadow = true;
}

void ABSModelActor::SetModel(const FBSModel* InModel)
{
	Model = InModel;
	Mids.Reset();
	if (!Model || !Model->Mesh) return;
	Mesh->SetSkeletalMesh(Model->Mesh);
	for (int32 i = 0; i < Mesh->GetNumMaterials(); i++)
		Mids.Add(Mesh->CreateAndSetMaterialInstanceDynamic(i));
}

float ABSModelActor::Play(const TArray<FString>& Names, bool bLoop, float Rate)
{
	if (!Model) return 0.f;
	UAnimSequence* Anim = Model->Find(Names);
	if (!Anim) return 0.f;
	Mesh->PlayAnimation(Anim, bLoop);
	Mesh->SetPlayRate(Rate);
	CurrentName = Names.Num() ? Names[0] : FString();
	return Anim->GetPlayLength();
}

float ABSModelActor::PlayRandomWithPrefix(const FString& Prefix, bool bLoop)
{
	if (!Model) return 0.f;
	TArray<UAnimSequence*> All = Model->FindAllWithPrefix(Prefix);
	if (!All.Num()) return 0.f;
	UAnimSequence* Anim = All[FMath::RandRange(0, All.Num() - 1)];
	Mesh->PlayAnimation(Anim, bLoop);
	Mesh->SetPlayRate(1.f);
	CurrentName = Prefix;
	return Anim->GetPlayLength();
}

float ABSModelActor::SequenceLength(const TArray<FString>& Names) const
{
	UAnimSequence* Anim = Model ? Model->Find(Names) : nullptr;
	return Anim ? Anim->GetPlayLength() : 0.f;
}

void ABSModelActor::SetRate(float Rate) { Mesh->SetPlayRate(Rate); }

void ABSModelActor::SetSim(float X, float Y, float Z, float YawRad)
{
	SetActorLocationAndRotation(BSSpace::ToWorld(X, Y, Z), FRotator(0, BSSpace::ToWorldYawDeg(YawRad), 0));
}

void ABSModelActor::SetScale(float S) { SetActorScale3D(FVector(S)); }

void ABSModelActor::SetTeamColor(const FLinearColor& C)
{
	for (UMaterialInstanceDynamic* M : Mids) if (M) M->SetVectorParameterValue(TEXT("TeamColor"), C);
}

void ABSModelActor::SetHitFlash(float Amount)
{
	if (FMath::IsNearlyEqual(Amount, HitFlash, 0.01f)) return;
	HitFlash = Amount;
	for (UMaterialInstanceDynamic* M : Mids) if (M) M->SetScalarParameterValue(TEXT("HitFlash"), Amount);
}

void ABSModelActor::SetScalarParam(FName Name, float Value)
{
	for (UMaterialInstanceDynamic* M : Mids) if (M) M->SetScalarParameterValue(Name, Value);
}

void ABSModelActor::EnableTurret(FName Bone, float OffsetDeg)
{
	TurretBone = Bone; TurretOffsetDeg = OffsetDeg;
	if (Poseable || !Model || !Model->Mesh) return;
	Poseable = NewObject<UPoseableMeshComponent>(this, TEXT("TurretPose"));
	Poseable->SetupAttachment(Root);
	Poseable->SetRelativeRotation(Mesh->GetRelativeRotation());
	Poseable->SetSkinnedAssetAndUpdate(Model->Mesh);
	Poseable->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Poseable->RegisterComponent();
	for (int32 i = 0; i < Mids.Num(); i++) Poseable->SetMaterial(i, Mids[i]);
	// The original mesh keeps animating (hidden); the poseable copy is what renders.
	Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	Mesh->SetVisibility(false);
	SetActorTickEnabled(true);
}

void ABSModelActor::Tick(float Dt)
{
	Super::Tick(Dt);
	if (!Poseable || IsHidden()) return;
	Poseable->CopyPoseFromSkeletalComponent(Mesh);
	if (TurretBone.IsNone()) return;
	// Turn the turret about world up by the aim angle relative to the hull, on top of the animated pose.
	const float HullYaw = GetActorRotation().Yaw;
	const float Target = bTurretAiming ? BSSpace::ToWorldYawDeg(TurretAimSim) - HullYaw : 0.f;
	const float Diff = FMath::FindDeltaAngleDegrees(TurretYawShown, Target);
	TurretYawShown += FMath::Clamp(Diff, -360.f * Dt, 360.f * Dt);      // 360 deg/s traverse
	const FQuat Cur = Poseable->GetBoneRotationByName(TurretBone, EBoneSpaces::WorldSpace).Quaternion();
	const FQuat Turn(FVector::UpVector, FMath::DegreesToRadians(TurretYawShown + TurretOffsetDeg));
	Poseable->SetBoneRotationByName(TurretBone, (Turn * Cur).Rotator(), EBoneSpaces::WorldSpace);
}

void ABSModelActor::Activate()
{
	SetActorHiddenInGame(false);
	Mesh->SetComponentTickEnabled(true);
	SetActorScale3D(FVector(1));
	SetHitFlash(0.f);
	SetScalarParam(TEXT("EmissiveStrength"), 2.f);
	bTurretAiming = false; TurretYawShown = 0;
}

void ABSModelActor::Deactivate()
{
	Mesh->Stop();
	SetActorHiddenInGame(true);
	Mesh->SetComponentTickEnabled(false);
	SetActorLocation(FVector(0, 0, -100000));
}

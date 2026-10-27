// Runtime access to the StarCraft II assets imported under /Game/SC2 (see Scripts/import_sc2.py).
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "BSAssets.generated.h"

class USkeletalMesh;
class UAnimSequence;
class USoundBase;
class UTexture2D;
class UMaterialInterface;
class UStaticMesh;
struct FSlateBrush;
struct FBSFxDef;

struct FBSModel
{
	FString Key;
	USkeletalMesh* Mesh = nullptr;
	TMap<FString, UAnimSequence*> Anims;     // normalised sequence name ("stand 01") -> anim

	// First exact match from the candidates, then first prefix match. Null if none.
	UAnimSequence* Find(const TArray<FString>& Candidates) const;
	UAnimSequence* Find(const FString& Name) const { return Find(TArray<FString>{ Name }); }
	TArray<UAnimSequence*> FindAllWithPrefix(const FString& Prefix) const;
};

UCLASS()
class UBSAssets : public UObject
{
	GENERATED_BODY()
public:
	void LoadAll();
	const FBSModel* Model(const FString& Key) const { return Models.Find(Key); }
	USoundBase* Sound(const FString& Key) const;                 // random variant
	UTexture2D* Icon(const FString& Name) const;
	const FSlateBrush* IconBrush(const FString& Name);
	const FBSFxDef* Fx(const FString& Key);                     // SC2 particle effect, loaded on first use
	UTexture2D* FxTexture(const FString& Sc2Path);

	UPROPERTY() TObjectPtr<UMaterialInterface> FxMaterial;
	UPROPERTY() TObjectPtr<UMaterialInterface> FxRingMaterial;
	UPROPERTY() TObjectPtr<UMaterialInterface> GroundMaterial;
	UPROPERTY() TObjectPtr<UMaterialInterface> TerrainMaterial;   // lane chokepoint ground and cliffs
	UPROPERTY() TObjectPtr<UMaterialInterface> CreepMaterial;     // zerg creep overlay (vertex alpha = coverage)
	UPROPERTY() TObjectPtr<UMaterialInterface> ParticleMaterial;
	UPROPERTY() TObjectPtr<UMaterialInterface> SpriteAddMaterial;
	UPROPERTY() TObjectPtr<UMaterialInterface> SpriteBlendMaterial;
	UPROPERTY() TObjectPtr<UStaticMesh> Sphere;
	UPROPERTY() TObjectPtr<UStaticMesh> Cylinder;
	UPROPERTY() TObjectPtr<UStaticMesh> Plane;
	UPROPERTY() TObjectPtr<UStaticMesh> Cone;

	FString Summary;

private:
	TMap<FString, FBSModel> Models;
	TMap<FString, TArray<USoundBase*>> Sounds;
	TMap<FString, UTexture2D*> Icons;
	TMap<FString, TSharedPtr<FSlateBrush>> Brushes;
	TMap<FString, TSharedPtr<FBSFxDef>> FxDefs;
	UPROPERTY() TMap<FString, TObjectPtr<UTexture2D>> FxTextures;
	UPROPERTY() TArray<TObjectPtr<UObject>> KeepAlive;
};

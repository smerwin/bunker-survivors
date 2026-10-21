#include "BSAssets.h"
#include "BSFx.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/AnimSequence.h"
#include "Sound/SoundWave.h"
#include "Engine/Texture2D.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Styling/SlateBrush.h"

static FString NormaliseAnimName(const FString& AssetName, const FString& MeshName)
{
	FString N = AssetName;
	if (N.StartsWith(MeshName, ESearchCase::IgnoreCase)) N.RightChopInline(MeshName.Len());
	N.ReplaceInline(TEXT("_"), TEXT(" "));
	N.TrimStartAndEndInline();
	return N.ToLower();
}

UAnimSequence* FBSModel::Find(const TArray<FString>& Candidates) const
{
	for (const FString& C : Candidates)
		if (UAnimSequence* const* A = Anims.Find(C.ToLower())) return *A;
	for (const FString& C : Candidates)
	{
		const FString L = C.ToLower();
		for (const auto& Pair : Anims)
			if (Pair.Key.StartsWith(L)) return Pair.Value;
	}
	return nullptr;
}

TArray<UAnimSequence*> FBSModel::FindAllWithPrefix(const FString& Prefix) const
{
	TArray<UAnimSequence*> Out;
	const FString L = Prefix.ToLower();
	for (const auto& Pair : Anims)
		if (Pair.Key.StartsWith(L)) Out.Add(Pair.Value);
	return Out;
}

void UBSAssets::LoadAll()
{
	IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
	if (!FPlatformProperties::RequiresCookedData())
		Registry.ScanPathsSynchronous({ TEXT("/Game/SC2"), TEXT("/Game/Materials") }, true);

	TArray<FAssetData> Assets;
	Registry.GetAssetsByPath(FName(TEXT("/Game/SC2")), Assets, true);

	// Pass 1: skeletal meshes define the models (folder /Game/SC2/Models/<key>/...).
	TMap<FString, FString> FolderToKey;
	for (const FAssetData& A : Assets)
	{
		if (A.AssetClassPath != USkeletalMesh::StaticClass()->GetClassPathName()) continue;
		const FString Pkg = A.PackagePath.ToString();                      // /Game/SC2/Models/<key>/<name>/SkeletalMeshes
		TArray<FString> Parts; Pkg.ParseIntoArray(Parts, TEXT("/"));
		const int32 Mi = Parts.IndexOfByKey(TEXT("Models"));
		if (Mi == INDEX_NONE || Mi + 1 >= Parts.Num()) continue;
		const FString Key = Parts[Mi + 1];
		if (USkeletalMesh* Mesh = Cast<USkeletalMesh>(A.GetAsset()))
		{
			FBSModel& M = Models.FindOrAdd(Key);
			M.Key = Key; M.Mesh = Mesh;
			KeepAlive.Add(Mesh);
			FolderToKey.Add(Pkg, Key);
		}
	}
	// Pass 2: animations live next to their mesh.
	int32 AnimCount = 0;
	for (const FAssetData& A : Assets)
	{
		if (A.AssetClassPath != UAnimSequence::StaticClass()->GetClassPathName()) continue;
		const FString* Key = FolderToKey.Find(A.PackagePath.ToString());
		if (!Key) continue;
		FBSModel& M = Models[*Key];
		if (UAnimSequence* Anim = Cast<UAnimSequence>(A.GetAsset()))
		{
			M.Anims.Add(NormaliseAnimName(A.AssetName.ToString(), M.Mesh->GetName()), Anim);
			KeepAlive.Add(Anim);
			AnimCount++;
		}
	}
	// Sounds: S_<key>_<n>; icons: T_<name with underscores>.
	for (const FAssetData& A : Assets)
	{
		const FString Name = A.AssetName.ToString();
		if (A.PackagePath == FName(TEXT("/Game/SC2/Sounds")) && Name.StartsWith(TEXT("S_")))
		{
			FString Key = Name.Mid(2);
			int32 Us; if (Key.FindLastChar('_', Us)) Key.LeftInline(Us);
			if (USoundBase* S = Cast<USoundBase>(A.GetAsset())) { Sounds.FindOrAdd(Key).Add(S); KeepAlive.Add(S); }
		}
		else if (A.PackagePath == FName(TEXT("/Game/SC2/Icons")) && Name.StartsWith(TEXT("T_")))
		{
			if (UTexture2D* T = Cast<UTexture2D>(A.GetAsset())) { Icons.Add(Name.Mid(2).Replace(TEXT("_"), TEXT("-")), T); KeepAlive.Add(T); }
		}
	}

	FxMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_FX.M_FX"));
	FxRingMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_FXRing.M_FXRing"));
	GroundMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_Ground.M_Ground"));
	ParticleMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_FXParticle.M_FXParticle"));
	SpriteAddMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_FXSpriteAdd.M_FXSpriteAdd"));
	SpriteBlendMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_FXSpriteBlend.M_FXSpriteBlend"));
	Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	Cylinder = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	Plane = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
	Cone = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cone.Cone"));

	Summary = FString::Printf(TEXT("%d models, %d animations, %d sound groups, %d icons"), Models.Num(), AnimCount, Sounds.Num(), Icons.Num());
	UE_LOG(LogTemp, Display, TEXT("BunkerSurvivors assets: %s"), *Summary);
}

USoundBase* UBSAssets::Sound(const FString& Key) const
{
	const TArray<USoundBase*>* L = Sounds.Find(Key);
	return (L && L->Num()) ? (*L)[FMath::RandRange(0, L->Num() - 1)] : nullptr;
}

UTexture2D* UBSAssets::Icon(const FString& Name) const
{
	UTexture2D* const* T = Icons.Find(Name);
	return T ? *T : nullptr;
}

const FSlateBrush* UBSAssets::IconBrush(const FString& Name)
{
	if (TSharedPtr<FSlateBrush>* B = Brushes.Find(Name)) return B->Get();
	UTexture2D* T = Icon(Name);
	if (!T) return nullptr;
	TSharedPtr<FSlateBrush> B = MakeShared<FSlateBrush>();
	B->SetResourceObject(T);
	B->ImageSize = FVector2D(76, 76);
	B->DrawAs = ESlateBrushDrawType::Image;
	Brushes.Add(Name, B);
	return B.Get();
}

const FBSFxDef* UBSAssets::Fx(const FString& Key)
{
	if (const TSharedPtr<FBSFxDef>* Found = FxDefs.Find(Key)) return Found->Get();
	// Effect descriptions are staged as plain files (DirectoriesToAlwaysStageAsUFS in DefaultGame.ini).
	FString Text;
	TSharedPtr<FBSFxDef> Def;
	if (FFileHelper::LoadFileToString(Text, *FPaths::Combine(FPaths::ProjectContentDir(), TEXT("SC2/FXData"), Key + TEXT(".json"))))
		Def = FBSFxDef::Load(Key, Text);
	if (!Def) UE_LOG(LogTemp, Warning, TEXT("BS: no particle effect '%s'"), *Key);
	FxDefs.Add(Key, Def);
	return Def.Get();
}

UTexture2D* UBSAssets::FxTexture(const FString& Sc2Path)
{
	if (TObjectPtr<UTexture2D>* Found = FxTextures.Find(Sc2Path)) return Found->Get();
	const FString Name = BSFx::TextureAssetName(Sc2Path);
	UTexture2D* T = LoadObject<UTexture2D>(nullptr, *FString::Printf(TEXT("/Game/SC2/FX/%s.%s"), *Name, *Name));
	if (!T) UE_LOG(LogTemp, Warning, TEXT("BS: missing particle texture %s (%s)"), *Name, *Sc2Path);
	FxTextures.Add(Sc2Path, T);
	return T;
}

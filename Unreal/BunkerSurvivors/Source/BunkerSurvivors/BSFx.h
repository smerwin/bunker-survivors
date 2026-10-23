// StarCraft II particle effects, exported from the game's own M3 emitters by scripts/export-assets.js (server/m3fx.js)
// and replayed here with instanced sprites. Positions and velocities are in sim metres (SC2 game units).
#pragma once

#include "CoreMinimal.h"

class FJsonValue;
class UTexture2D;

// A value sampled over the effect's sequence (constant when Fps == 0). Dim floats per sample.
struct FBSFxCurve
{
	int32 Dim = 1;
	float Fps = 0;
	TArray<float> V;
	void Parse(const TSharedPtr<FJsonValue>& J, int32 InDim);
	float Get(float T, int32 K = 0) const
	{
		if (Fps <= 0 || V.Num() <= Dim) return V.IsValidIndex(K) ? V[K] : 0.f;
		const int32 N = V.Num() / Dim;
		const float F = FMath::Clamp(T * Fps, 0.f, float(N - 1));
		const int32 I = FMath::FloorToInt(F), J = FMath::Min(I + 1, N - 1);
		return FMath::Lerp(V[I * Dim + K], V[J * Dim + K], F - I);
	}
	FVector Vec(float T) const { return FVector(Get(T, 0), Get(T, 1), Get(T, 2)); }
	FLinearColor Color(float T) const { return FLinearColor(Get(T, 0), Get(T, 1), Get(T, 2), Get(T, 3)); }
};

struct FBSFxEmitter
{
	FString Texture;                 // SC2 texture path, e.g. assets/textures/glow_yellow1.dds
	int32 Blend = 2;                 // M3 blend mode: 1 alpha blend, 2 add, 3 alpha add, 4/5 mod
	int32 Cols = 1, Rows = 1, Frames[4] = { 0, 0, 0, 0 };
	float Phase1 = 1;
	bool bRandFrame = false;
	int32 MaxParticles = 100, Type = 0, EmitType = 0, Area = 0;
	float LengthWidth = 1, Gravity = 0, Drag = 0, Mid[4] = { 0.5f, 0.5f, 0.5f, 0.5f };
	FBSFxCurve Pos, Axis, Rate, Speed1, Speed2, AngX, AngY, SprX, SprY, Life1, Life2, AreaSize, AreaRadius, Size, Size2, Rot, Rot2;
	FBSFxCurve Col[3], Col2[3];
	bool bRandSpeed = false, bRandLife = false, bSize2 = false, bRot2 = false, bCol2 = false;
	TArray<FVector2f> Bursts;        // (seconds, count)
	mutable int32 Batch = -1;        // render batch, resolved the first time the emitter plays
};

struct FBSFxVariant { float Duration = 1; TArray<FBSFxEmitter> Emitters; };

struct FBSFxDef
{
	FString Key;
	bool bLoop = false;
	TArray<FBSFxVariant> Variants;
	static TSharedPtr<FBSFxDef> Load(const FString& Key, const FString& JsonText);
};

// One playing effect.
struct FBSFxInstance
{
	const FBSFxDef* Def = nullptr;
	int32 Var = 0;
	FVector Pos = FVector::ZeroVector;
	float Yaw = 0, Scale = 1, T = 0, Delay = 0;
	bool bLoop = false, bStopped = false;
	TArray<float> Acc;
	TArray<int32> NextBurst, Alive;
};

struct FBSSprite
{
	const FBSFxEmitter* E = nullptr;
	int32 Batch = 0, Inst = 0, Em = 0;
	FVector P = FVector::ZeroVector, V = FVector::ZeroVector, Spawn = FVector::ZeroVector;
	float Age = 0, Life = 1, Speed0 = 1, Frame0 = 0, Scale = 1;
	float S[3] = { 1, 1, 1 }, R[3] = { 0, 0, 0 };
	FLinearColor C[3];
};

namespace BSFx
{
	// Asset name used for an exported SC2 particle texture (must match Scripts/import_sc2.py).
	FString TextureAssetName(const FString& Sc2Path);
}

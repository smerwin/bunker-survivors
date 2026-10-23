#include "BSFx.h"
#include "BSGame.h"
#include "BSAssets.h"
#include "BSModelActor.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Camera/PlayerCameraManager.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/Texture2D.h"

// ---- Definitions ----------------------------------------------------------------------------------------
FString BSFx::TextureAssetName(const FString& Sc2Path)
{
	FString Base = FPaths::GetBaseFilename(Sc2Path);
	FString Out = TEXT("FX_");
	for (TCHAR Ch : Base) Out.AppendChar(FChar::IsAlnum(Ch) || Ch == '_' ? Ch : '_');
	return Out;
}

static void AppendNumbers(const TSharedPtr<FJsonValue>& J, TArray<float>& Out)
{
	if (!J.IsValid()) return;
	if (J->Type == EJson::Number) Out.Add((float)J->AsNumber());
	else if (J->Type == EJson::Boolean) Out.Add(J->AsBool() ? 1.f : 0.f);
	else if (J->Type == EJson::Array) for (const TSharedPtr<FJsonValue>& X : J->AsArray()) AppendNumbers(X, Out);
}

void FBSFxCurve::Parse(const TSharedPtr<FJsonValue>& J, int32 InDim)
{
	Dim = InDim; Fps = 0; V.Reset();
	if (!J.IsValid() || J->Type == EJson::Null) { V.Init(0.f, Dim); return; }
	if (J->Type == EJson::Object)
	{
		const TSharedPtr<FJsonObject> O = J->AsObject();
		Fps = (float)O->GetNumberField(TEXT("fps"));
		for (const TSharedPtr<FJsonValue>& X : O->GetArrayField(TEXT("v"))) AppendNumbers(X, V);
	}
	else AppendNumbers(J, V);
	while (V.Num() < Dim) V.Add(0.f);
}

TSharedPtr<FBSFxDef> FBSFxDef::Load(const FString& Key, const FString& JsonText)
{
	TSharedPtr<FJsonObject> Root;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(JsonText), Root) || !Root.IsValid()) return nullptr;
	TSharedPtr<FBSFxDef> D = MakeShared<FBSFxDef>();
	D->Key = Key;
	D->bLoop = Root->HasField(TEXT("loop")) && Root->GetBoolField(TEXT("loop"));
	for (const TSharedPtr<FJsonValue>& VJ : Root->GetArrayField(TEXT("variants")))
	{
		const TSharedPtr<FJsonObject> VO = VJ->AsObject();
		FBSFxVariant& Var = D->Variants.AddDefaulted_GetRef();
		Var.Duration = FMath::Max(0.05f, (float)VO->GetNumberField(TEXT("duration")));
		for (const TSharedPtr<FJsonValue>& EJ : VO->GetArrayField(TEXT("emitters")))
		{
			const TSharedPtr<FJsonObject> O = EJ->AsObject();
			FBSFxEmitter& E = Var.Emitters.AddDefaulted_GetRef();
			auto Arr = [&O](const TCHAR* F) { return O->GetArrayField(F); };
			E.Texture = O->GetStringField(TEXT("texture"));
			E.Blend = (int32)O->GetNumberField(TEXT("blend"));
			E.Cols = FMath::Max(1, (int32)O->GetNumberField(TEXT("cols")));
			E.Rows = FMath::Max(1, (int32)O->GetNumberField(TEXT("rows")));
			const TArray<TSharedPtr<FJsonValue>> Fr = Arr(TEXT("frames"));
			for (int32 i = 0; i < 4 && i < Fr.Num(); i++) E.Frames[i] = (int32)Fr[i]->AsNumber();
			E.Phase1 = Fr.Num() > 4 ? FMath::Clamp((float)Fr[4]->AsNumber(), 0.01f, 1.f) : 1.f;
			E.bRandFrame = O->GetBoolField(TEXT("randFrame"));
			// SC2 leaves a grid on many single-image textures; it only applies when frames animate or are randomised.
			if (!E.bRandFrame && E.Frames[1] <= E.Frames[0] && E.Frames[3] <= E.Frames[0] && E.Frames[0] == 0) { E.Cols = 1; E.Rows = 1; }
			E.MaxParticles = FMath::Max(1, (int32)O->GetNumberField(TEXT("maxParticles")));
			E.Type = (int32)O->GetNumberField(TEXT("type"));
			E.LengthWidth = (float)O->GetNumberField(TEXT("lengthWidth"));
			E.EmitType = (int32)O->GetNumberField(TEXT("emitType"));
			E.Area = (int32)O->GetNumberField(TEXT("area"));
			E.Gravity = (float)O->GetNumberField(TEXT("gravity"));
			E.Drag = (float)O->GetNumberField(TEXT("drag"));
			const TArray<TSharedPtr<FJsonValue>> Mid = Arr(TEXT("mid"));
			for (int32 i = 0; i < 4 && i < Mid.Num(); i++) E.Mid[i] = FMath::Clamp((float)Mid[i]->AsNumber(), 0.01f, 0.99f);
			E.Pos.Parse(O->TryGetField(TEXT("pos")), 3);
			E.Axis.Parse(O->TryGetField(TEXT("axis")), 6);
			E.Rate.Parse(O->TryGetField(TEXT("rate")), 1);
			const TArray<TSharedPtr<FJsonValue>> Sp = Arr(TEXT("speed")), An = Arr(TEXT("angle")), Spr = Arr(TEXT("spread")), Li = Arr(TEXT("life"));
			E.Speed1.Parse(Sp[0], 1); E.Speed2.Parse(Sp[1], 1); E.bRandSpeed = Sp[2]->AsBool();
			E.AngX.Parse(An[0], 1); E.AngY.Parse(An[1], 1); E.SprX.Parse(Spr[0], 1); E.SprY.Parse(Spr[1], 1);
			E.Life1.Parse(Li[0], 1); E.Life2.Parse(Li[1], 1); E.bRandLife = Li[2]->AsBool();
			E.AreaSize.Parse(O->TryGetField(TEXT("areaSize")), 3);
			E.AreaRadius.Parse(O->TryGetField(TEXT("areaRadius")), 1);
			E.Size.Parse(O->TryGetField(TEXT("size")), 3);
			E.Rot.Parse(O->TryGetField(TEXT("rot")), 3);
			const TSharedPtr<FJsonValue> S2 = O->TryGetField(TEXT("size2")), R2 = O->TryGetField(TEXT("rot2")), C2 = O->TryGetField(TEXT("color2"));
			E.bSize2 = S2.IsValid() && S2->Type != EJson::Null; if (E.bSize2) E.Size2.Parse(S2, 3);
			E.bRot2 = R2.IsValid() && R2->Type != EJson::Null; if (E.bRot2) E.Rot2.Parse(R2, 3);
			const TArray<TSharedPtr<FJsonValue>> Co = Arr(TEXT("color"));
			for (int32 i = 0; i < 3; i++) E.Col[i].Parse(Co[i], 4);
			E.bCol2 = C2.IsValid() && C2->Type == EJson::Array;
			if (E.bCol2) { const TArray<TSharedPtr<FJsonValue>> C2a = C2->AsArray(); for (int32 i = 0; i < 3; i++) E.Col2[i].Parse(C2a[i], 4); }
			for (const TSharedPtr<FJsonValue>& B : Arr(TEXT("bursts")))
			{
				const TArray<TSharedPtr<FJsonValue>> P = B->AsArray();
				E.Bursts.Add(FVector2f((float)P[0]->AsNumber(), (float)P[1]->AsNumber()));
			}
		}
	}
	return D->Variants.Num() ? D : nullptr;
}

// ---- Playback -------------------------------------------------------------------------------------------
static float Rnd01() { return FMath::FRand(); }

// Three-key curve over a particle's life with an adjustable middle key.
static float Curve3(const float K[3], float T, float Mid)
{
	return T < Mid ? FMath::Lerp(K[0], K[1], T / Mid) : FMath::Lerp(K[1], K[2], (T - Mid) / (1 - Mid));
}

int32 ABSGame::FxBatch(const FString& Texture, int32 Blend)
{
	const bool bAdd = Blend == 2 || Blend == 3;
	const FString Key = Texture + (bAdd ? TEXT("|add") : TEXT("|blend"));
	if (const int32* Found = FxBatchIndex.Find(Key)) return *Found;
	UInstancedStaticMeshComponent* Ism = NewObject<UInstancedStaticMeshComponent>(this);
	Ism->SetupAttachment(GetRootComponent());
	Ism->RegisterComponent();
	Ism->SetStaticMesh(Assets->Plane);
	Ism->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Ism->SetCastShadow(false);
	Ism->SetNumCustomDataFloats(8);
	Ism->SetTranslucentSortPriority(bAdd ? 2 : 1);
	if (UMaterialInterface* Parent = bAdd ? Assets->SpriteAddMaterial : Assets->SpriteBlendMaterial)
	{
		UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(Parent, this);
		if (UTexture2D* Tex = Assets->FxTexture(Texture)) Mid->SetTextureParameterValue(TEXT("Tex"), Tex);
		Ism->SetMaterial(0, Mid);
	}
	const int32 Index = FxBatches.Add(Ism);
	FxBatchIndex.Add(Key, Index);
	return Index;
}

int32 ABSGame::PlayFx(const FString& Key, const FVector& Pos, float Yaw, float Scale, float Delay)
{
	if (!Assets) return 0;
	const FBSFxDef* Def = Assets->Fx(Key);
	if (!Def) return 0;
	FBSFxInstance I;
	I.Def = Def; I.Var = FMath::RandRange(0, Def->Variants.Num() - 1);
	I.Pos = Pos; I.Yaw = Yaw; I.Scale = Scale; I.Delay = Delay; I.bLoop = Def->bLoop;
	const int32 N = Def->Variants[I.Var].Emitters.Num();
	I.Acc.Init(0.f, N); I.NextBurst.Init(0, N); I.Alive.Init(0, N);
	const int32 H = NextFx++;
	FxInstances.Add(H, MoveTemp(I));
	return H;
}

void ABSGame::MoveFx(int32 H, const FVector& Pos, float Yaw)
{
	if (FBSFxInstance* I = FxInstances.Find(H)) { I->Pos = Pos; I->Yaw = Yaw; }
}

void ABSGame::StopFx(int32 H)
{
	if (FBSFxInstance* I = FxInstances.Find(H)) I->bStopped = true;
}

void ABSGame::SpawnSprite(int32 H, FBSFxInstance& I, int32 Ei, const FBSFxEmitter& E, float T)
{
	if (FxSprites.Num() >= 8000 || I.Alive[Ei] >= E.MaxParticles) return;
	if (E.Batch < 0) E.Batch = FxBatch(E.Texture, E.Blend);
	// Emitter frame in model space: local Z is the emission axis.
	const FVector Ax(E.Axis.Get(T, 3), E.Axis.Get(T, 4), E.Axis.Get(T, 5));
	const FVector Az(E.Axis.Get(T, 0), E.Axis.Get(T, 1), E.Axis.Get(T, 2));
	const FVector Ay = FVector::CrossProduct(Az, Ax);
	FVector Local = E.Pos.Vec(T);
	const FVector Sz = E.AreaSize.Vec(T);
	const float Rad = E.AreaRadius.Get(T);
	FVector Off = FVector::ZeroVector;
	switch (E.Area)
	{
	case 1: Off = FVector(Rnd01() - 0.5f, Rnd01() - 0.5f, 0) * Sz; break;                                       // plane
	case 2: Off = FMath::VRand() * Rad * FMath::Pow(Rnd01(), 1.f / 3.f); break;                                   // sphere
	case 3: Off = FVector(Rnd01() - 0.5f, Rnd01() - 0.5f, Rnd01() - 0.5f) * Sz; break;                           // cuboid
	case 4: case 5: { const float A = Rnd01() * 2 * PI, R = Rad * FMath::Sqrt(Rnd01());                            // cylinder / disc
		Off = FVector(FMath::Cos(A) * R, FMath::Sin(A) * R, E.Area == 4 ? (Rnd01() - 0.5f) * Sz.Z : 0); break; }
	default: break;
	}
	Local += Ax * Off.X + Ay * Off.Y + Az * Off.Z;
	// Direction.
	FVector Dir;
	switch (E.EmitType)
	{
	case 1: case 3: Dir = FMath::VRand(); break;
	case 2: Dir = Az * (FMath::RandBool() ? 1 : -1); break;
	default:
	{
		const float Rx = E.AngX.Get(T) + (Rnd01() * 2 - 1) * E.SprX.Get(T);
		const float Ry = E.AngY.Get(T) + (Rnd01() * 2 - 1) * E.SprY.Get(T);
		Dir = Az.RotateAngleAxisRad(Rx, Ax);
		Dir = Dir.RotateAngleAxisRad(Ry, Ay);
	}
	}
	const float Speed = E.bRandSpeed ? FMath::Lerp(E.Speed1.Get(T), E.Speed2.Get(T), Rnd01()) : E.Speed1.Get(T);
	// Model space (SC2 models face -Y) to sim space: rotate by yaw + 90 degrees.
	const float Phi = I.Yaw + PI / 2;
	const float Cs = FMath::Cos(Phi), Sn = FMath::Sin(Phi);
	auto ToSim = [Cs, Sn](const FVector& V) { return FVector(V.X * Cs - V.Y * Sn, V.X * Sn + V.Y * Cs, V.Z); };

	FBSSprite& S = FxSprites.AddDefaulted_GetRef();
	S.E = &E; S.Batch = E.Batch; S.Inst = H; S.Em = Ei; S.Scale = I.Scale;
	S.P = S.Spawn = I.Pos + ToSim(Local) * I.Scale;
	S.V = ToSim(Dir * Speed) * I.Scale;
	S.Speed0 = FMath::Max(0.01f, S.V.Size());
	S.Life = FMath::Max(0.02f, E.bRandLife ? FMath::Lerp(E.Life1.Get(T), E.Life2.Get(T), Rnd01()) : E.Life1.Get(T));
	const FVector Sz1 = E.Size.Vec(T);
	const FVector Sz2 = E.bSize2 ? E.Size2.Vec(T) : Sz1;
	const float Ks = Rnd01();
	S.S[0] = FMath::Lerp(Sz1.X, Sz2.X, Ks); S.S[1] = FMath::Lerp(Sz1.Y, Sz2.Y, Ks); S.S[2] = FMath::Lerp(Sz1.Z, Sz2.Z, Ks);
	const FVector R1 = E.Rot.Vec(T);
	const FVector R2 = E.bRot2 ? E.Rot2.Vec(T) : R1;
	const float Kr = Rnd01();
	S.R[0] = FMath::Lerp(R1.X, R2.X, Kr); S.R[1] = FMath::Lerp(R1.Y, R2.Y, Kr); S.R[2] = FMath::Lerp(R1.Z, R2.Z, Kr);
	const float Kc = Rnd01();
	for (int32 k = 0; k < 3; k++) S.C[k] = E.bCol2 ? FMath::Lerp(E.Col[k].Color(T), E.Col2[k].Color(T), Kc) : E.Col[k].Color(T);
	const int32 NFrames = E.Cols * E.Rows;
	if (NFrames > 1 && E.bRandFrame) S.Frame0 = (float)FMath::RandRange(E.Frames[0], FMath::Max(E.Frames[0], FMath::Min(E.Frames[1] > 0 ? E.Frames[1] : NFrames - 1, NFrames - 1)));
	else S.Frame0 = -1;
	I.Alive[Ei]++;
}

void ABSGame::UpdateFx(float Dt)
{
	// Emit.
	TArray<int32> Done;
	for (TPair<int32, FBSFxInstance>& Pair : FxInstances)
	{
		FBSFxInstance& I = Pair.Value;
		if (I.Delay > 0) { I.Delay -= Dt; continue; }
		const FBSFxVariant& Var = I.Def->Variants[I.Var];
		const float T0 = I.T, T1 = I.T + Dt;
		if (!I.bStopped)
			for (int32 e = 0; e < Var.Emitters.Num(); e++)
			{
				const FBSFxEmitter& E = Var.Emitters[e];
				const float Tc = FMath::Min(T1, Var.Duration);
				I.Acc[e] += FMath::Max(0.f, E.Rate.Get(Tc)) * Dt;
				int32 N = FMath::FloorToInt(I.Acc[e]);
				I.Acc[e] -= N;
				while (I.NextBurst[e] < E.Bursts.Num() && E.Bursts[I.NextBurst[e]].X <= T1) N += FMath::RoundToInt(E.Bursts[I.NextBurst[e]++].Y);
				for (int32 k = 0; k < N; k++) SpawnSprite(Pair.Key, I, e, E, Tc);
			}
		I.T = T1;
		if (I.T >= Var.Duration)
		{
			if (I.bLoop && !I.bStopped) { I.T = FMath::Fmod(I.T, Var.Duration); for (int32& B : I.NextBurst) B = 0; }
			else if (T0 < Var.Duration || I.bStopped) Done.Add(Pair.Key);
		}
		else if (I.bStopped) Done.Add(Pair.Key);
	}
	for (int32 H : Done) FxInstances.Remove(H);

	// Simulate.
	for (int32 i = FxSprites.Num() - 1; i >= 0; i--)
	{
		FBSSprite& S = FxSprites[i];
		S.Age += Dt;
		if (S.Age >= S.Life)
		{
			if (FBSFxInstance* I = FxInstances.Find(S.Inst)) if (I->Alive.IsValidIndex(S.Em)) I->Alive[S.Em]--;
			FxSprites.RemoveAtSwap(i, EAllowShrinking::No);
			continue;
		}
		if (S.E->Drag > 0) S.V *= FMath::Exp(-S.E->Drag * Dt);
		S.V.Z += S.E->Gravity * S.Scale * Dt;
		S.P += S.V * Dt;
	}

	// Render: one instanced batch per texture and blend mode.
	FVector CamLoc = FVector::ZeroVector;
	if (APlayerCameraManager* Cam = UGameplayStatics::GetPlayerCameraManager(this, 0)) CamLoc = Cam->GetCameraLocation();
	const int32 NB = FxBatches.Num();
	TArray<TArray<FTransform>> Xf; Xf.SetNum(NB);
	TArray<TArray<float>> Data; Data.SetNum(NB);
	for (const FBSSprite& S : FxSprites)
	{
		const FBSFxEmitter& E = *S.E;
		const float T = FMath::Clamp(S.Age / S.Life, 0.f, 1.f);
		const float Size = FMath::Max(0.f, Curve3(S.S, T, E.Mid[0])) * S.Scale;
		const float Ang = Curve3(S.R, T, E.Mid[3]);
		FLinearColor C;
		C.R = T < E.Mid[1] ? FMath::Lerp(S.C[0].R, S.C[1].R, T / E.Mid[1]) : FMath::Lerp(S.C[1].R, S.C[2].R, (T - E.Mid[1]) / (1 - E.Mid[1]));
		C.G = T < E.Mid[1] ? FMath::Lerp(S.C[0].G, S.C[1].G, T / E.Mid[1]) : FMath::Lerp(S.C[1].G, S.C[2].G, (T - E.Mid[1]) / (1 - E.Mid[1]));
		C.B = T < E.Mid[1] ? FMath::Lerp(S.C[0].B, S.C[1].B, T / E.Mid[1]) : FMath::Lerp(S.C[1].B, S.C[2].B, (T - E.Mid[1]) / (1 - E.Mid[1]));
		C.A = T < E.Mid[2] ? FMath::Lerp(S.C[0].A, S.C[1].A, T / E.Mid[2]) : FMath::Lerp(S.C[1].A, S.C[2].A, (T - E.Mid[2]) / (1 - E.Mid[2]));
		// Flipbook frame: phase 1 then phase 2 over the particle's life.
		float Frame = 0;
		const int32 NFrames = E.Cols * E.Rows;
		if (NFrames > 1)
		{
			if (S.Frame0 >= 0) Frame = S.Frame0;
			else
			{
				const int32 F0 = E.Frames[0], F1 = E.Frames[1] > F0 ? E.Frames[1] : NFrames - 1, F2 = E.Frames[3] > F1 ? E.Frames[3] : F1;
				Frame = T < E.Phase1 ? FMath::Lerp((float)F0, (float)F1, T / E.Phase1) : FMath::Lerp((float)F1, (float)F2, (T - E.Phase1) / FMath::Max(0.01f, 1 - E.Phase1));
				Frame = FMath::Min(FMath::FloorToFloat(Frame), (float)(NFrames - 1));
			}
		}
		const FVector W = BSSpace::ToWorld(S.P.X, S.P.Y, S.P.Z);
		const FVector ToCam = (CamLoc - W).GetSafeNormal();
		float Length = Size, Width = Size;
		FVector Along;
		const bool bVelocity = E.Type == 1 || E.Type == 7 || E.Type == 9;
		if (bVelocity)
		{
			FVector Vw(S.V.Y, S.V.X, S.V.Z);
			if (E.Type == 9) Vw = W - BSSpace::ToWorld(S.Spawn.X, S.Spawn.Y, S.Spawn.Z);
			Along = Vw - ToCam * FVector::DotProduct(Vw, ToCam);
			if (Along.SizeSquared() < 1e-6f) Along = FVector::CrossProduct(ToCam, FVector::UpVector);
			if (E.Type == 9) Length = FMath::Max(Size, Vw.Size() / BSSpace::UnitsPerMetre);
			else Length = Size * FMath::Max(1.f, E.LengthWidth * FMath::Clamp(S.V.Size() / S.Speed0, 0.25f, 1.f));
		}
		else
		{
			FVector Right = FVector::CrossProduct(ToCam, FVector::UpVector);
			if (Right.SizeSquared() < 1e-4f) Right = FVector::CrossProduct(ToCam, FVector::ForwardVector);
			Along = Right.GetSafeNormal().RotateAngleAxisRad(Ang, ToCam);
			if (E.Type == 6) Length = Size * E.LengthWidth;
		}
		const bool bFlat = E.Type == 5;
		if (bFlat) { Along = FVector(FMath::Cos(Ang), FMath::Sin(Ang), 0); Length = Width = Size; }
		const FRotator Rot = FRotationMatrix::MakeFromZX(bFlat ? FVector::UpVector : ToCam, Along.GetSafeNormal()).Rotator();
		const FVector Pos = bVelocity && E.Type == 9 ? (W + BSSpace::ToWorld(S.Spawn.X, S.Spawn.Y, S.Spawn.Z)) * 0.5f : W;
		Xf[S.Batch].Add(FTransform(Rot, bFlat ? FVector(Pos.X, Pos.Y, FMath::Max(Pos.Z, 2.f)) : Pos, FVector(Length, Width, 1)));
		Data[S.Batch].Append({ C.R, C.G, C.B, C.A, Frame, (float)E.Cols, (float)E.Rows, 0.f });
	}
	for (int32 b = 0; b < NB; b++)
	{
		UInstancedStaticMeshComponent* Ism = FxBatches[b];
		if (!Ism) continue;
		TArray<FTransform>& X = Xf[b];
		TArray<float>& D = Data[b];
		const int32 N = X.Num();
		if (N == 0 && Ism->GetInstanceCount() == 0) continue;
		while (Ism->GetInstanceCount() < N) Ism->AddInstance(FTransform(FRotator::ZeroRotator, FVector(0, 0, -100000), FVector(0.0001f)), true);
		for (int32 i = N; i < Ism->GetInstanceCount(); i++) { X.Add(FTransform(FRotator::ZeroRotator, FVector(0, 0, -100000), FVector(0.0001f))); D.Append({ 0, 0, 0, 0, 0, 1, 1, 0 }); }
		Ism->BatchUpdateInstancesTransforms(0, X, true, false, true);
		for (int32 i = 0; i < X.Num(); i++) Ism->SetCustomData(i, TArrayView<const float>(D.GetData() + i * 8, 8), false);
		Ism->MarkRenderStateDirty();
	}
}

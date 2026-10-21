#include "BSGameMode.h"
#include "BSAssets.h"
#include "BSCameraPawn.h"
#include "SBSHud.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HighResScreenshot.h"
#include "UnrealClient.h"
#include "Widgets/SWeakWidget.h"
#include "Camera/PlayerCameraManager.h"
#include "BSModelActor.h"
#include "Components/SkeletalMeshComponent.h"

ABSGameMode::ABSGameMode()
{
	PrimaryActorTick.bCanEverTick = true;
	DefaultPawnClass = ABSCameraPawn::StaticClass();
	PlayerControllerClass = ABSPlayerController::StaticClass();
}

void ABSGameMode::StartPlay()
{
	int32 Seed = 0;
	if (FParse::Value(FCommandLine::Get(), TEXT("BSSeed="), Seed)) { FMath::RandInit(Seed); FMath::SRandInit(Seed); }
	Assets = NewObject<UBSAssets>(this);
	Assets->LoadAll();
	Super::StartPlay();

	const TCHAR* Cmd = FCommandLine::Get();
	FParse::Value(Cmd, TEXT("BSShotDir="), ShotDir);
	FParse::Value(Cmd, TEXT("BSShotEvery="), ShotEvery);
	FParse::Value(Cmd, TEXT("BSQuitAfter="), QuitAfter);
	NextShot = ShotEvery > 0 ? ShotEvery : 0;

	FString Auto = UGameplayStatics::ParseOption(OptionsString, TEXT("mode"));
	if (Auto.IsEmpty()) FParse::Value(Cmd, TEXT("BSAutoStart="), Auto);
	if (Auto == TEXT("lane")) StartGame(EBSMode::Lane);
	else if (Auto == TEXT("arena")) StartGame(EBSMode::Arena);
	else
	{
		// Start screen: a staged, slowly orbiting scene behind the menu.
		Game = GetWorld()->SpawnActor<ABSGame>();
		Game->Init(Assets, EBSMode::Lane);
		Game->SetupAttract();
		if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
			if (ABSCameraPawn* Cam = Cast<ABSCameraPawn>(PC->GetPawn())) { Cam->SetGame(Game); Cam->SetAttract(true); }
	}
}

void ABSGameMode::StartGame(EBSMode Mode)
{
	if (Game && Game->bStarted) return;
	if (Game) { Game->Destroy(); Game = nullptr; }
	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
		if (ABSCameraPawn* Cam = Cast<ABSCameraPawn>(PC->GetPawn())) Cam->SetAttract(false);
	Game = GetWorld()->SpawnActor<ABSGame>();
	Game->Init(Assets, Mode);
	const TCHAR* Cmd = FCommandLine::Get();
	FParse::Value(Cmd, TEXT("BSSpeed="), Game->TimeScale);
	float Minerals = 0;
	if (FParse::Value(Cmd, TEXT("BSMinerals="), Minerals)) Game->Minerals = Minerals;
	Game->bAutoPlay = FParse::Param(Cmd, TEXT("BSAutoPlay"));
	FParse::Value(Cmd, TEXT("BSBot="), Game->Bot);
	FParse::Value(Cmd, TEXT("BSTelemetry="), Game->TelemetryPath);
	Game->bStarted = true;
	// Weapon-effects test: a pack of zerg right outside the bunker, fireteam with a firebat.
	if (FParse::Param(Cmd, TEXT("BSFxTest")))
	{
		Game->AddInfantry(TEXT("firebat"), true);
		for (int32 i = 0; i < 8; i++) Game->DebugSpawn(TEXT("zergling"), -2.5f + (i % 4) * 1.7f, 6.5f + (i / 4) * 1.4f);
		Game->DebugSpawn(TEXT("roach"), 0.5f, 9.f);
	}
	// Debug lineup: -BSShowcase=zergling,bunker:stand,scv:walk spawns models in a row in front of the bunker.
	FString Showcase;
	if (FParse::Value(Cmd, TEXT("BSShowcase="), Showcase, false))
	{
		TArray<FString> Items; Showcase.ParseIntoArray(Items, TEXT(","));
		for (int32 i = 0; i < Items.Num(); i++)
		{
			FString Key = Items[i], Anim;
			Items[i].Split(TEXT(":"), &Key, &Anim);
			if (Key.IsEmpty()) Key = Items[i];
			const FBSModel* M = Assets->Model(Key);
			if (!M) { UE_LOG(LogTemp, Warning, TEXT("Showcase: no model %s"), *Key); continue; }
			ABSModelActor* A = GetWorld()->SpawnActor<ABSModelActor>();
			A->SetModel(M);
			A->SetSim((i - (Items.Num() - 1) * 0.5f) * 3.f, 4.f, 0, -PI / 2);
			const float Len = Anim.IsEmpty() ? 0.f : A->Play(Anim, true);
			UE_LOG(LogTemp, Display, TEXT("Showcase %s anim=%s len=%.2f bounds=%s r=%.0f"), *Key, *Anim, Len, *A->Mesh->Bounds.Origin.ToCompactString(), A->Mesh->Bounds.SphereRadius);
		}
	}
	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
		if (ABSCameraPawn* Cam = Cast<ABSCameraPawn>(PC->GetPawn()))
		{
			Cam->SetGame(Game);
			Cam->SetArena(Mode == EBSMode::Arena);
			float Z;
			if (FParse::Value(Cmd, TEXT("BSZoom="), Z)) Cam->SetZoom(Z, true);
		}
}

void ABSGameMode::Restart(bool bSameMode)
{
	FString Opt;
	if (bSameMode && Game) Opt = Game->Mode == EBSMode::Arena ? TEXT("mode=arena") : TEXT("mode=lane");
	UGameplayStatics::OpenLevel(this, FName(*GetWorld()->GetMapName().Replace(*GetWorld()->StreamingLevelsPrefix, TEXT(""))), true, Opt);
}

void ABSGameMode::Tick(float Dt)
{
	Super::Tick(Dt);
	Elapsed += Dt;

	// Pixel Streaming sets the resolution to the browser's CSS-pixel viewport (e.g. 393x695 on a phone). Hardware
	// video encoders reject odd sizes, and that is also blurry, so keep the aspect but scale small viewports up to
	// about 720p and round to multiples of 16.
	if (GEngine && GEngine->GameViewport && FParse::Param(FCommandLine::Get(), TEXT("RenderOffscreen")))
	{
		FVector2D Size;
		GEngine->GameViewport->GetViewportSize(Size);
		const int32 W = FMath::RoundToInt(Size.X), H = FMath::RoundToInt(Size.Y);
		if (W > 0 && H > 0)
		{
			const float Short = (float)FMath::Min(W, H), Long = (float)FMath::Max(W, H);
			float Scale = Short < 720.f ? 720.f / Short : 1.f;
			if (Long * Scale > 1920.f) Scale = 1920.f / Long;
			const int32 TW = FMath::Max(16, FMath::RoundToInt(W * Scale / 16.f) * 16);
			const int32 TH = FMath::Max(16, FMath::RoundToInt(H * Scale / 16.f) * 16);
			if (TW != W || TH != H) GEngine->Exec(GetWorld(), *FString::Printf(TEXT("r.SetRes %dx%dw"), TW, TH));
		}
	}
	if (ShotEvery > 0 && !ShotDir.IsEmpty() && Elapsed >= NextShot)
	{
		NextShot += ShotEvery;
		const FString File = FPaths::Combine(ShotDir, FString::Printf(TEXT("shot_%02d.png"), ShotIndex++));
		FScreenshotRequest::RequestScreenshot(File, true, false);
		FString CamInfo = TEXT("no pawn");
		if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
		{
			FVector Loc; FRotator Rot; PC->GetPlayerViewPoint(Loc, Rot);
			const ABSCameraPawn* Cam = Cast<ABSCameraPawn>(PC->GetPawn());
			CamInfo = FString::Printf(TEXT("pawn=%s view=(%.0f,%.0f,%.0f) rot=(%.1f,%.1f) fov=%.1f zoom=%.2f"),
				PC->GetPawn() ? *PC->GetPawn()->GetClass()->GetName() : TEXT("none"), Loc.X, Loc.Y, Loc.Z, Rot.Pitch, Rot.Yaw,
				PC->PlayerCameraManager ? PC->PlayerCameraManager->GetFOVAngle() : -1.f, Cam ? Cam->GetZoom() : -1.f);
		}
		if (Game) UE_LOG(LogTemp, Display, TEXT("BSShot %s %s | %s"), *File, *Game->DebugState(), *CamInfo);
	}
	// Automated test games: stop on game over, or when the bot outlasts the time cap (sim seconds).
	if (Game && Game->bStarted && !bTestDone)
	{
		static const bool bExitOnOver = FParse::Param(FCommandLine::Get(), TEXT("BSExitOnOver"));
		float MaxTime = 0;
		FParse::Value(FCommandLine::Get(), TEXT("BSMaxTime="), MaxTime);
		if (MaxTime > 0 && !Game->bOver && Game->Time >= MaxTime)
		{
			Game->EndReason = TEXT("timecap");
			Game->WriteTelemetry(TEXT("wave"));
			Game->WriteTelemetry(TEXT("final"));
			bTestDone = true;
		}
		else if (bExitOnOver && Game->bOver) bTestDone = true;
		if (bTestDone) UKismetSystemLibrary::QuitGame(this, nullptr, EQuitPreference::Quit, false);
	}
	if (QuitAfter > 0 && Elapsed >= QuitAfter)
	{
		QuitAfter = 0;
		if (Game) UE_LOG(LogTemp, Display, TEXT("BSFinal %s"), *Game->DebugState());
		UKismetSystemLibrary::QuitGame(this, nullptr, EQuitPreference::Quit, false);
	}
}

// ---- Player controller ---------------------------------------------------------------------------------
ABSPlayerController::ABSPlayerController()
{
	bShowMouseCursor = true;
	bEnableClickEvents = false;
	bEnableTouchEvents = false;
}

void ABSPlayerController::BeginPlay()
{
	Super::BeginPlay();
	if (!IsLocalController() || !GEngine || !GEngine->GameViewport) return;
	if (FParse::Param(FCommandLine::Get(), TEXT("BSNoHud"))) return;
	ABSGameMode* GM = GetWorld()->GetAuthGameMode<ABSGameMode>();
	Hud = SNew(SBSHud).GameMode(GM).PlayerController(this);
	GEngine->GameViewport->AddViewportWidgetContent(SNew(SWeakWidget).PossiblyNullContent(Hud.ToSharedRef()), 10);
	FInputModeUIOnly Mode;
	Mode.SetWidgetToFocus(Hud);
	Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(Mode);
}

void ABSPlayerController::EndPlay(const EEndPlayReason::Type Reason)
{
	if (Hud.IsValid() && GEngine && GEngine->GameViewport) GEngine->GameViewport->RemoveAllViewportWidgets();
	Hud.Reset();
	Super::EndPlay(Reason);
}

void ABSPlayerController::BSGive(int32 Amount)
{
	if (ABSGameMode* GM = GetWorld()->GetAuthGameMode<ABSGameMode>()) if (GM->GetGame()) GM->GetGame()->Minerals += Amount;
}

void ABSPlayerController::BSSpeed(float Scale)
{
	if (ABSGameMode* GM = GetWorld()->GetAuthGameMode<ABSGameMode>()) if (GM->GetGame()) GM->GetGame()->TimeScale = Scale;
}

void ABSPlayerController::BSZoom(float Z)
{
	if (ABSCameraPawn* Cam = Cast<ABSCameraPawn>(GetPawn())) Cam->SetZoom(Z, true);
}

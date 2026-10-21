#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "BSGame.h"
#include "BSGameMode.generated.h"

class UBSAssets;
class SBSHud;

UCLASS()
class ABSGameMode : public AGameModeBase
{
	GENERATED_BODY()
public:
	ABSGameMode();
	virtual void StartPlay() override;
	virtual void Tick(float Dt) override;

	void StartGame(EBSMode Mode);
	void Restart(bool bSameMode);
	ABSGame* GetGame() const { return Game; }
	UBSAssets* GetAssets() const { return Assets; }

private:
	UPROPERTY() TObjectPtr<UBSAssets> Assets;
	UPROPERTY() TObjectPtr<ABSGame> Game;
	// Test / demo flags from the command line.
	float ShotEvery = 0, NextShot = 0, QuitAfter = 0, Elapsed = 0;
	FString ShotDir;
	int32 ShotIndex = 0;
	bool bTestDone = false;
};

UCLASS()
class ABSPlayerController : public APlayerController
{
	GENERATED_BODY()
public:
	ABSPlayerController();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	// Console helpers (enable with the console or via -ExecCmds).
	UFUNCTION(Exec) void BSGive(int32 Amount);
	UFUNCTION(Exec) void BSSpeed(float Scale);
	UFUNCTION(Exec) void BSZoom(float Z);

private:
	TSharedPtr<SBSHud> Hud;
};

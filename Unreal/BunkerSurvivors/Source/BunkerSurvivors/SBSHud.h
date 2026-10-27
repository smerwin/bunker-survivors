// HUD: start screen, command strip, squad card, ability tiles, armoury, banners, world overlay and all player
// input (touch, mouse, keyboard). Pure Slate with custom painting; the layout adapts to portrait and landscape.
#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SLeafWidget.h"

class ABSGameMode;
class ABSGame;
class APlayerController;
class ABSCameraPawn;

// Leaf widget that paints with a lambda; the backbone of the custom-drawn HUD.
class SBSPaint : public SLeafWidget
{
public:
	using FPaintFn = TFunction<int32(const FGeometry&, FSlateWindowElementList&, int32)>;
	SLATE_BEGIN_ARGS(SBSPaint) : _Size(FVector2D(16, 16)) {}
		SLATE_ATTRIBUTE(FVector2D, Size)
		SLATE_ARGUMENT(FPaintFn, Paint)
	SLATE_END_ARGS()
	void Construct(const FArguments& Args) { Size = Args._Size; PaintFn = Args._Paint; }
	virtual FVector2D ComputeDesiredSize(float) const override { return Size.Get(); }
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geo, const FSlateRect& Cull, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const override
	{
		return PaintFn ? PaintFn(Geo, Out, Layer) : Layer;
	}
private:
	TAttribute<FVector2D> Size;
	FPaintFn PaintFn;
};

class SBSHud : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SBSHud) {}
		SLATE_ARGUMENT(ABSGameMode*, GameMode)
		SLATE_ARGUMENT(APlayerController*, PlayerController)
	SLATE_END_ARGS()

	void Construct(const FArguments& Args);
	virtual void Tick(const FGeometry& Geo, const double Now, const float Dt) override;
	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& Geo, const FKeyEvent& Key) override;
	virtual FReply OnMouseButtonDown(const FGeometry& Geo, const FPointerEvent& E) override;
	virtual FReply OnMouseButtonUp(const FGeometry& Geo, const FPointerEvent& E) override;
	virtual FReply OnMouseMove(const FGeometry& Geo, const FPointerEvent& E) override;
	virtual FReply OnMouseWheel(const FGeometry& Geo, const FPointerEvent& E) override;
	virtual FReply OnTouchStarted(const FGeometry& Geo, const FPointerEvent& E) override;
	virtual FReply OnTouchMoved(const FGeometry& Geo, const FPointerEvent& E) override;
	virtual FReply OnTouchEnded(const FGeometry& Geo, const FPointerEvent& E) override;

private:
	ABSGame* Game() const;                 // the running match (null on the start screen)
	ABSCameraPawn* Cam() const;
	bool Portrait() const { return LocalSize.Y > LocalSize.X; }
	void Tap(const FGeometry& Geo, const FVector2D& ScreenPos);
	bool TapSquadCard(const FVector2D& ScreenPos);    // eject / board a trooper; true if the tap landed on the card
	void ToggleBunkerView();                          // the bunker badge on the squad card switches sky / bunker view
	bool WorldToLocal(const FVector& World, FVector2D& Out) const;
	void TogglePause();

	TSharedRef<SWidget> BuildStartScreen();
	TSharedRef<SWidget> BuildCommandStrip();
	TSharedRef<SWidget> BuildSquadCard();
	TSharedRef<SWidget> BuildAbilityRow();
	TSharedRef<SWidget> BuildArmouryStrip();     // portrait: horizontal cards
	TSharedRef<SWidget> BuildArmouryColumn();    // landscape: vertical build list
	TSharedRef<SWidget> BuildModal(bool bGameOver);
	TSharedRef<SWidget> MakeAbility(int32 Index);
	TSharedRef<SWidget> MakeArmouryCard(int32 Index);
	TSharedRef<SWidget> MakeArmouryRow(int32 Index);
	TSharedRef<SWidget> MakeButton(const FString& Label, bool bPrimary, TFunction<void()> OnClick, float Width = 150);
	int32 PaintWorld(const FGeometry& Geo, FSlateWindowElementList& Out, int32 Layer) const;
	int32 PaintBanner(const FGeometry& Geo, FSlateWindowElementList& Out, int32 Layer) const;

	TWeakObjectPtr<ABSGameMode> GM;
	TWeakObjectPtr<APlayerController> PC;
	FVector2D LocalSize = FVector2D(1280, 720);
	float DpiScale = 1.f;
	double NowS = 0;

	// Animated readouts.
	float ShownMinerals = 0, MineralFlash = 0, MineralFail = 0;
	int32 SeenToast = 0, SeenWave = 0, LastLitSegments = 10;
	FString ToastText; double ToastAt = -10;
	double BannerAt = -10; FString BannerText;
	TArray<double> SegmentLostAt;
	float BcLastHp = 0; double BcHitAt = -10;     // battlecruiser hull bar: flashes when the ship is hit
	bool bArmouryOpen = true;
	TSharedPtr<SWidget> SquadCard;

	// Input state.
	struct FPtr { FVector2D Start, Last; double Down = 0; };
	TMap<int32, FPtr> Touches;
	bool bMouseDown = false, bDragging = false;
	FVector2D MouseStart, MouseLast; double MouseDownAt = 0, LastTapAt = -1;
	float PinchDist = 0, PinchAngle = 0;
};

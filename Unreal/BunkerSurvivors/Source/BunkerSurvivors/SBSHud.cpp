#include "SBSHud.h"
#include "BSGameMode.h"
#include "BSGame.h"
#include "BSAssets.h"
#include "BSCameraPawn.h"
#include "BSModelActor.h"
#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Fonts/CompositeFont.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Input/SButton.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerController.h"
#include "Misc/ConfigCacheIni.h"

// =====================================================================================================
// Design tokens
// =====================================================================================================
namespace C
{
	const FLinearColor Glass(0.010f, 0.016f, 0.026f, 0.78f);
	const FLinearColor GlassHi(0.045f, 0.07f, 0.10f, 0.88f);
	const FLinearColor Hair(0.62f, 0.80f, 0.95f, 0.18f);
	const FLinearColor Ink(0.93f, 0.96f, 1.0f, 1.f);
	const FLinearColor Muted(0.56f, 0.64f, 0.73f, 1.f);
	const FLinearColor Faint(1, 1, 1, 0.07f);
	const FLinearColor Cyan(0.36f, 0.86f, 1.0f, 1.f);
	const FLinearColor Amber(1.0f, 0.74f, 0.26f, 1.f);
	const FLinearColor Red(1.0f, 0.30f, 0.26f, 1.f);
	const FLinearColor Green(0.45f, 0.95f, 0.58f, 1.f);
	const FLinearColor Violet(0.74f, 0.42f, 1.0f, 1.f);
	const FLinearColor Dark(0.02f, 0.035f, 0.05f, 1.f);
	FLinearColor A(const FLinearColor& Col, float Alpha) { FLinearColor R = Col; R.A *= Alpha; return R; }
}

namespace D
{
	using FPts = TArray<FVector2f>;

	// StarCraft II's own UI typeface (Eurostile, plus Eurostile Extended for titles and numbers), exported from the
	// player's install into Content/SC2/Fonts. Falls back to the engine font if the files are missing.
	TSharedPtr<const FCompositeFont> Sc2Font()
	{
		static TSharedPtr<FCompositeFont> Font;
		static bool bTried = false;
		if (bTried) return Font;
		bTried = true;
		const FString Dir = FPaths::Combine(FPaths::ProjectContentDir(), TEXT("SC2/Fonts"));
		const TPair<const TCHAR*, const TCHAR*> Faces[] = {
			{ TEXT("Regular"), TEXT("eurostile-reg.otf") }, { TEXT("Bold"), TEXT("eurostile-bol.otf") },
			{ TEXT("Wide"), TEXT("eurostileext-med.otf") }, { TEXT("WideLight"), TEXT("eurostileext-reg.otf") } };
		TSharedPtr<FCompositeFont> C = MakeShared<FCompositeFont>();
		for (const auto& Face : Faces)
		{
			const FString Path = FPaths::Combine(Dir, Face.Value);
			if (!IFileManager::Get().FileExists(*Path)) { UE_LOG(LogTemp, Warning, TEXT("BS: HUD font missing (%s), using the engine font"), *Path); return Font; }
			C->DefaultTypeface.AppendFont(Face.Key, Path, EFontHinting::Default, EFontLoadingPolicy::LazyLoad);
		}
		Font = C;
		return Font;
	}

	// Face: Wide (titles, numbers), Bold (labels), Regular (body). Engine-font names are the fallback.
	FSlateFontInfo Font(const TCHAR* Face, const char* Fallback, float Size, int32 Spacing = 0)
	{
		const int32 Px = FMath::Max(1, FMath::RoundToInt(Size));
		FSlateFontInfo F = Sc2Font().IsValid() ? FSlateFontInfo(Sc2Font(), Px, FName(Face)) : FCoreStyle::GetDefaultFontStyle(Fallback, Px);
		F.LetterSpacing = Spacing;
		return F;
	}
	FSlateFontInfo Num(float Size) { return Font(TEXT("Wide"), "Black", Size, 10); }
	FSlateFontInfo Label(float Size = 9) { return Font(TEXT("Bold"), "BoldCondensed", Size, 110); }
	FSlateFontInfo Title(float Size) { return Font(TEXT("Wide"), "Black", Size, 60); }
	FSlateFontInfo Body(float Size) { return Font(TEXT("Regular"), "Regular", Size); }
	FSlateFontInfo Bold(float Size) { return Font(TEXT("Bold"), "Bold", Size); }

	// Text is measured at the real render scale: glyph advances round up per character at high DPI, so a
	// scale-1 measurement under-reports the drawn width by 10-15% on phones.
	float GMeasureScale = 1.f;
	FVector2f Measure(const FString& S, const FSlateFontInfo& F)
	{
		const float Sc = FMath::Max(GMeasureScale, 0.1f);
		return FVector2f(FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(S, F, Sc)) / Sc;
	}

	// Shrinks a font until the text fits the width (never below MinSize).
	// Slack > 1 leaves headroom for faces whose drawn width runs wider than measured (the regular body face).
	FSlateFontInfo Fit(const FString& S, TFunction<FSlateFontInfo(float)> Make, float MaxSize, float MinSize, float MaxW, float Slack = 1.f)
	{
		for (float Sz = MaxSize; Sz > MinSize; Sz -= 0.5f)
		{
			FSlateFontInfo F = Make(Sz);
			if (Measure(S, F).X * Slack <= MaxW) return F;
		}
		return Make(MinSize);
	}
	constexpr float BodySlack = 1.2f;

	// Splits text into two lines at the space nearest the middle.
	void SplitTwo(const FString& S, FString& A, FString& B)
	{
		int32 Best = INDEX_NONE;
		for (int32 i = 0; i < S.Len(); i++)
			if (S[i] == ' ' && (Best == INDEX_NONE || FMath::Abs(i - S.Len() / 2) < FMath::Abs(Best - S.Len() / 2))) Best = i;
		if (Best == INDEX_NONE) { A = S; B.Reset(); return; }
		A = S.Left(Best); B = S.Mid(Best + 1);
	}

	const FSlateBrush* WhiteBrush() { return FCoreStyle::Get().GetBrush("WhiteBrush"); }

	void Lines(FSlateWindowElementList& Out, int32 L, const FGeometry& G, FPts P, const FLinearColor& Col, float Thick = 1.f, bool bClosed = false)
	{
		if (bClosed && P.Num()) { const FVector2f First = P[0]; P.Add(First); }
		FSlateDrawElement::MakeLines(Out, L, G.ToPaintGeometry(), P, ESlateDrawEffect::None, Col, true, Thick);
	}
	void Line(FSlateWindowElementList& Out, int32 L, const FGeometry& G, FVector2f A, FVector2f B, const FLinearColor& Col, float Thick = 1.f) { Lines(Out, L, G, { A, B }, Col, Thick); }

	// Rounded fills use Slate's SDF rounded box (tinted by the draw colour). Outlines are drawn as paths
	// instead, because the rounded-box outline colour does not take the draw tint.
	const FSlateBrush* RoundBrush(float Radius)
	{
		static TMap<int32, TSharedPtr<FSlateBrush>> Cache;
		const int32 Key = FMath::RoundToInt(Radius * 10);
		if (TSharedPtr<FSlateBrush>* B = Cache.Find(Key)) return B->Get();
		TSharedPtr<FSlateBrush> B = MakeShared<FSlateRoundedBoxBrush>(FLinearColor::White, Radius);
		Cache.Add(Key, B);
		return B.Get();
	}

	FPts RoundedPath(FVector2f P, FVector2f S, float R)
	{
		R = FMath::Min(R, FMath::Min(S.X, S.Y) * 0.5f);
		FPts Out;
		const FVector2f Ctr[4] = { P + FVector2f(S.X - R, R), P + FVector2f(S.X - R, S.Y - R), P + FVector2f(R, S.Y - R), P + FVector2f(R, R) };
		const float Start[4] = { -PI / 2, 0, PI / 2, PI };
		for (int32 c = 0; c < 4; c++)
			for (int32 i = 0; i <= 6; i++) { const float A = Start[c] + (PI / 2) * i / 6; Out.Add(Ctr[c] + FVector2f(FMath::Cos(A), FMath::Sin(A)) * R); }
		return Out;
	}

	FSlateResourceHandle WhiteHandle()
	{
		static FSlateResourceHandle Handle;
		if (!Handle.IsValid()) Handle = FSlateApplication::Get().GetRenderer()->GetResourceHandle(*WhiteBrush());
		return Handle;
	}

	void Verts(FSlateWindowElementList& Out, int32 L, const FGeometry& G, const FPts& P, const TArray<FLinearColor>& Cols)
	{
		if (P.Num() < 3) return;
		const FSlateRenderTransform& T = G.GetAccumulatedRenderTransform();
		TArray<FSlateVertex> V; TArray<SlateIndex> I;
		for (int32 i = 0; i < P.Num(); i++) V.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(T, P[i], FVector2f(0.5f, 0.5f), Cols[FMath::Min(i, Cols.Num() - 1)].ToFColor(true)));
		for (int32 i = 1; i + 1 < P.Num(); i++) { I.Add(0); I.Add(i); I.Add(i + 1); }
		FSlateDrawElement::MakeCustomVerts(Out, L, WhiteHandle(), V, I, nullptr, 0, 0);
	}
	void Poly(FSlateWindowElementList& Out, int32 L, const FGeometry& G, const FPts& P, const FLinearColor& Col) { Verts(Out, L, G, P, { Col }); }

	void GradV(FSlateWindowElementList& Out, int32 L, const FGeometry& G, FVector2f P, FVector2f S, const FLinearColor& Top, const FLinearColor& Bottom)
	{
		Verts(Out, L, G, { P, P + FVector2f(S.X, 0), P + S, P + FVector2f(0, S.Y) }, { Top, Top, Bottom, Bottom });
	}
	void GradH(FSlateWindowElementList& Out, int32 L, const FGeometry& G, FVector2f P, FVector2f S, const FLinearColor& Left, const FLinearColor& Right)
	{
		Verts(Out, L, G, { P, P + FVector2f(S.X, 0), P + S, P + FVector2f(0, S.Y) }, { Left, Right, Right, Left });
	}

	void Rect(FSlateWindowElementList& Out, int32 L, const FGeometry& G, FVector2f Pos, FVector2f Size, const FLinearColor& Col)
	{
		FSlateDrawElement::MakeBox(Out, L, G.ToPaintGeometry(Size, FSlateLayoutTransform(Pos)), WhiteBrush(), ESlateDrawEffect::None, Col);
	}
	void RFill(FSlateWindowElementList& Out, int32 L, const FGeometry& G, FVector2f Pos, FVector2f Size, float R, const FLinearColor& Col)
	{
		FSlateDrawElement::MakeBox(Out, L, G.ToPaintGeometry(Size, FSlateLayoutTransform(Pos)), RoundBrush(R), ESlateDrawEffect::None, Col);
	}
	void RLine(FSlateWindowElementList& Out, int32 L, const FGeometry& G, FVector2f Pos, FVector2f Size, float R, const FLinearColor& Col, float W = 1.f)
	{
		const float H = W * 0.5f;
		Lines(Out, L, G, RoundedPath(Pos + FVector2f(H, H), Size - FVector2f(W, W), FMath::Max(0.f, R - H)), Col, W, true);
	}
	void Image(FSlateWindowElementList& Out, int32 L, const FGeometry& G, const FSlateBrush* B, FVector2f Pos, FVector2f Size, const FLinearColor& Tint = FLinearColor::White)
	{
		if (B) FSlateDrawElement::MakeBox(Out, L, G.ToPaintGeometry(Size, FSlateLayoutTransform(Pos)), B, ESlateDrawEffect::None, Tint);
	}

	// Align: 0 left, 0.5 centre, 1 right of Pos.X; Pos.Y is the text top.
	void Text(FSlateWindowElementList& Out, int32 L, const FGeometry& G, const FString& S, const FSlateFontInfo& F, FVector2f Pos, const FLinearColor& Col, float Align = 0.f, bool bShadow = false)
	{
		const FVector2f Sz = Measure(S, F);
		const FVector2f P(Pos.X - Sz.X * Align, Pos.Y);
		if (bShadow) FSlateDrawElement::MakeText(Out, L, G.ToPaintGeometry(Sz, FSlateLayoutTransform(P + FVector2f(0, 1.5f))), S, F, ESlateDrawEffect::None, C::A(FLinearColor::Black, 0.75f * Col.A));
		FSlateDrawElement::MakeText(Out, L + 1, G.ToPaintGeometry(Sz, FSlateLayoutTransform(P)), S, F, ESlateDrawEffect::None, Col);
	}

	void Circle(FSlateWindowElementList& Out, int32 L, const FGeometry& G, FVector2f Ctr, float R, const FLinearColor& Col, float Thick = 1.f, int32 Seg = 40, float From = 0, float To = 2 * PI)
	{
		FPts P;
		for (int32 i = 0; i <= Seg; i++) { const float A = From + (To - From) * i / Seg; P.Add(Ctr + FVector2f(FMath::Cos(A), FMath::Sin(A)) * R); }
		Lines(Out, L, G, P, Col, Thick);
	}
	void Disc(FSlateWindowElementList& Out, int32 L, const FGeometry& G, FVector2f Ctr, float R, const FLinearColor& Col, int32 Seg = 32)
	{
		FPts P;
		for (int32 i = 0; i < Seg; i++) { const float A = 2 * PI * i / Seg; P.Add(Ctr + FVector2f(FMath::Cos(A), FMath::Sin(A)) * R); }
		Poly(Out, L, G, P, Col);
	}
	void Pie(FSlateWindowElementList& Out, int32 L, const FGeometry& G, FVector2f Ctr, float R, float Frac, const FLinearColor& Col)
	{
		if (Frac <= 0) return;
		const int32 Seg = FMath::Max(3, FMath::CeilToInt(40 * Frac));
		FPts P{ Ctr };
		for (int32 i = 0; i <= Seg; i++) { const float A = -PI / 2 + 2 * PI * Frac * i / Seg; P.Add(Ctr + FVector2f(FMath::Cos(A), FMath::Sin(A)) * R); }
		Poly(Out, L, G, P, Col);
	}

	// Mineral crystal glyph.
	void Crystal(FSlateWindowElementList& Out, int32 L, const FGeometry& G, FVector2f C0, float S, const FLinearColor& Col)
	{
		const FVector2f T(C0.X, C0.Y - S), Tr(C0.X + S * 0.55f, C0.Y - S * 0.35f), Br(C0.X + S * 0.55f, C0.Y + S * 0.45f), B(C0.X, C0.Y + S), Bl(C0.X - S * 0.55f, C0.Y + S * 0.45f), Tl(C0.X - S * 0.55f, C0.Y - S * 0.35f);
		Poly(Out, L, G, { T, Tr, Br, B, Bl, Tl }, C::A(Col, 0.55f));
		Poly(Out, L + 1, G, { T, C0, Bl, Tl }, C::A(FLinearColor::White, 0.55f * Col.A));
		Poly(Out, L + 1, G, { T, Tr, Br, C0 }, C::A(Col, 0.9f));
	}

	void PauseGlyph(FSlateWindowElementList& Out, int32 L, const FGeometry& G, FVector2f C0, const FLinearColor& Col)
	{
		RFill(Out, L, G, C0 + FVector2f(-6, -7), FVector2f(4, 14), 1.5f, Col);
		RFill(Out, L, G, C0 + FVector2f(2, -7), FVector2f(4, 14), 1.5f, Col);
	}

	void SpeakerGlyph(FSlateWindowElementList& Out, int32 L, const FGeometry& G, FVector2f C0, bool bMuted, const FLinearColor& Col)
	{
		Poly(Out, L, G, { C0 + FVector2f(-8, -3), C0 + FVector2f(-4, -3), C0 + FVector2f(1, -8), C0 + FVector2f(1, 8), C0 + FVector2f(-4, 3), C0 + FVector2f(-8, 3) }, Col);
		if (bMuted)
		{
			Line(Out, L, G, C0 + FVector2f(4, -4), C0 + FVector2f(10, 4), Col, 2);
			Line(Out, L, G, C0 + FVector2f(10, -4), C0 + FVector2f(4, 4), Col, 2);
		}
		else
		{
			Circle(Out, L, G, C0 + FVector2f(1, 0), 5, Col, 1.5f, 10, -0.9f, 0.9f);
			Circle(Out, L, G, C0 + FVector2f(1, 0), 9, C::A(Col, 0.6f), 1.5f, 12, -0.9f, 0.9f);
		}
	}

	void Chevron(FSlateWindowElementList& Out, int32 L, const FGeometry& G, FVector2f C0, bool bUp, const FLinearColor& Col)
	{
		const float S = bUp ? -1.f : 1.f;
		Lines(Out, L, G, { C0 + FVector2f(-5, -2.5f * S), C0 + FVector2f(0, 2.5f * S), C0 + FVector2f(5, -2.5f * S) }, Col, 2);
	}

	// Mineral cost pill: crystal + number, or an arbitrary short string.
	void CostPill(FSlateWindowElementList& Out, int32 L, const FGeometry& G, FVector2f Ctr, const FString& S, bool bCrystal, const FLinearColor& Col)
	{
		const FSlateFontInfo F = Bold(10);
		const FVector2f Ts = Measure(S, F);
		const float W = Ts.X + (bCrystal ? 26 : 16), H = 17;
		const FVector2f P(Ctr.X - W * 0.5f, Ctr.Y - H * 0.5f);
		RFill(Out, L, G, P, FVector2f(W, H), H * 0.5f, C::A(FLinearColor::Black, 0.45f));
		float X = P.X + 8;
		if (bCrystal) { Crystal(Out, L + 1, G, FVector2f(X + 3, Ctr.Y), 5, Col); X += 11; }
		Text(Out, L + 1, G, S, F, FVector2f(X, Ctr.Y - Ts.Y * 0.5f), Col);
	}

	FString Int(float V) { return FString::FromInt(FMath::FloorToInt(V)); }
}

// =====================================================================================================
// Buttons
// =====================================================================================================
static const FButtonStyle& ClearButton()
{
	static FButtonStyle S = FButtonStyle()
		.SetNormal(FSlateNoResource()).SetHovered(FSlateNoResource()).SetPressed(FSlateNoResource()).SetDisabled(FSlateNoResource())
		.SetNormalPadding(FMargin(0)).SetPressedPadding(FMargin(0));
	return S;
}

// A button whose look is a paint lambda; the painter receives hover/press state.
static TSharedRef<SWidget> PaintedButton(TAttribute<FVector2D> Size, TFunction<void(const FGeometry&, FSlateWindowElementList&, int32, bool, bool)> Paint, TFunction<void()> OnClick)
{
	TSharedRef<TWeakPtr<SButton>> Holder = MakeShared<TWeakPtr<SButton>>();
	TSharedRef<SButton> Btn = SNew(SButton)
		.ButtonStyle(&ClearButton())
		.ContentPadding(0)
		.IsFocusable(false)
		// Inside scroll strips a touch that moves is a swipe, not a purchase.
		.TouchMethod(EButtonTouchMethod::PreciseTap)
		.ClickMethod(EButtonClickMethod::PreciseClick)
		.OnClicked_Lambda([OnClick]() { OnClick(); return FReply::Handled(); })
		[
			SNew(SBSPaint).Size(Size)
			.Paint([Holder, Paint](const FGeometry& G, FSlateWindowElementList& Out, int32 L)
			{
				TSharedPtr<SButton> B = Holder->Pin();
				Paint(G, Out, L, B && B->IsHovered(), B && B->IsPressed());
				return L + 8;
			})
		];
	*Holder = Btn;
	return Btn;
}

ABSGame* SBSHud::Game() const
{
	ABSGame* G = GM.IsValid() ? GM->GetGame() : nullptr;
	return (G && G->bStarted) ? G : nullptr;
}
ABSCameraPawn* SBSHud::Cam() const { return PC.IsValid() ? Cast<ABSCameraPawn>(PC->GetPawn()) : nullptr; }

void SBSHud::TogglePause()
{
	if (ABSGame* G = Game()) { G->bPaused = !G->bPaused; UGameplayStatics::SetGamePaused(G, G->bPaused); }
}

TSharedRef<SWidget> SBSHud::MakeButton(const FString& Label, bool bPrimary, TFunction<void()> OnClick, float Width)
{
	return PaintedButton(FVector2D(Width, 44), [Label, bPrimary, Width](const FGeometry& G, FSlateWindowElementList& Out, int32 L, bool bHover, bool bPress)
	{
		const FVector2f S(Width, 44), P(0, bPress ? 1.f : 0.f);
		if (bPrimary)
		{
			D::RFill(Out, L, G, P, S - FVector2f(0, 2), 12, bHover ? FLinearColor(0.55f, 0.92f, 1.f) : C::Cyan);
			D::Text(Out, L + 1, G, Label, D::Label(12), FVector2f(S.X * 0.5f, P.Y + 13), C::Dark, 0.5f);
		}
		else
		{
			D::RFill(Out, L, G, P, S - FVector2f(0, 2), 12, bHover ? C::GlassHi : C::A(C::Glass, 0.6f));
			D::RLine(Out, L + 1, G, P, S - FVector2f(0, 2), 12, C::A(C::Cyan, bHover ? 0.9f : 0.45f), 1.f);
			D::Text(Out, L + 2, G, Label, D::Label(12), FVector2f(S.X * 0.5f, P.Y + 13), C::Ink, 0.5f);
		}
	}, OnClick);
}

// =====================================================================================================
// Construction
// =====================================================================================================
void SBSHud::Construct(const FArguments& Args)
{
	GM = Args._GameMode;
	PC = Args._PlayerController;
	SegmentLostAt.Init(-10.0, 10);
	SetCanTick(true);

	auto InGame = [this]() { return Game() ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed; };
	auto PortraitOnly = [this]() { return Portrait() ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed; };
	auto LandscapeOnly = [this]() { return Portrait() ? EVisibility::Collapsed : EVisibility::SelfHitTestInvisible; };

	auto BottomFade = SNew(SBSPaint).Visibility(EVisibility::HitTestInvisible).Paint([this](const FGeometry& G, FSlateWindowElementList& Out, int32 L)
	{
		const FVector2f S = FVector2f(G.GetLocalSize());
		const float H = Portrait() ? (bArmouryOpen ? 290.f : 170.f) : 150.f;
		D::GradV(Out, L, G, FVector2f(0, S.Y - H), FVector2f(S.X, H), C::A(C::Dark, 0), C::A(C::Dark, 0.88f));
		D::GradV(Out, L, G, FVector2f(0, 0), FVector2f(S.X, 150), C::A(C::Dark, 0.55f), C::A(C::Dark, 0));
		return L + 1;
	});

	ChildSlot
	[
		SNew(SOverlay)
		+ SOverlay::Slot()[ SNew(SBSPaint).Visibility(EVisibility::HitTestInvisible).Paint([this](const FGeometry& G, FSlateWindowElementList& Out, int32 L) { return PaintWorld(G, Out, L); }) ]
		+ SOverlay::Slot()
		[
			SNew(SOverlay).Visibility_Lambda(InGame)
			+ SOverlay::Slot()[ BottomFade ]
			// Top: command strip and squad card.
			+ SOverlay::Slot().VAlign(VAlign_Top)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(10, 8, 10, 0)[ BuildCommandStrip() ]
				+ SVerticalBox::Slot().AutoHeight().Padding(10, 8, 10, 0).HAlign(HAlign_Left)[ BuildSquadCard() ]
			]
			// Portrait bottom: abilities above the armoury strip.
			+ SOverlay::Slot().VAlign(VAlign_Bottom)
			[
				SNew(SVerticalBox).Visibility_Lambda(PortraitOnly)
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 0, 0, 6)[ BuildAbilityRow() ]
				+ SVerticalBox::Slot().AutoHeight()[ BuildArmouryStrip() ]
			]
			// Landscape: abilities bottom centre, armoury build list on the right.
			+ SOverlay::Slot().VAlign(VAlign_Bottom).HAlign(HAlign_Center).Padding(0, 0, 0, 10)
			[
				SNew(SBox).Visibility_Lambda(LandscapeOnly)[ BuildAbilityRow() ]
			]
			+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Fill).Padding(0, 188, 10, 10)
			[
				SNew(SBox).Visibility_Lambda(LandscapeOnly).WidthOverride(236)[ BuildArmouryColumn() ]
			]
		]
		+ SOverlay::Slot()[ SNew(SBSPaint).Visibility(EVisibility::HitTestInvisible).Paint([this](const FGeometry& G, FSlateWindowElementList& Out, int32 L) { return PaintBanner(G, Out, L); }) ]
		+ SOverlay::Slot()[ BuildModal(false) ]
		+ SOverlay::Slot()[ BuildModal(true) ]
		+ SOverlay::Slot()[ BuildStartScreen() ]
	];
}

// =====================================================================================================
// Command strip: minerals, wave, kills, score, sound and pause
// =====================================================================================================
TSharedRef<SWidget> SBSHud::BuildCommandStrip()
{
	auto Strip = SNew(SBSPaint).Size(FVector2D(10, 50)).Paint([this](const FGeometry& G, FSlateWindowElementList& Out, int32 L)
	{
		ABSGame* Gm = Game(); if (!Gm) return L;
		const float W = G.GetLocalSize().X - 96;      // the two icon buttons sit to the right
		const bool bWide = W > 460;
		// Tapered glass strip with a cyan accent.
		D::RFill(Out, L, G, FVector2f(0, 0), FVector2f(W, 50), 12, C::Glass);
		D::RLine(Out, L + 1, G, FVector2f(0, 0), FVector2f(W, 50), 12, C::Hair);
		D::Rect(Out, L + 1, G, FVector2f(14, 0), FVector2f(36, 2), C::Cyan);

		auto Section = [&](float X, float Wd, const FString& Label, const FString& Value, const FLinearColor& Col, float Shake = 0)
		{
			D::Text(Out, L + 2, G, Label, D::Label(8.5f), FVector2f(X, 8), C::Muted);
			D::Text(Out, L + 2, G, Value, D::Fit(Value, [](float S) { return D::Num(S); }, 20, 12, Wd - 8), FVector2f(X + Shake, 19), Col);
		};
		const int32 N = bWide ? 4 : 3;
		const float Sw = W / N;
		// Minerals.
		{
			const float Pulse = MineralFlash;
			D::Crystal(Out, L + 2, G, FVector2f(20, 30), 9, FMath::Lerp(C::Cyan, FLinearColor::White, Pulse * 0.6f));
			const FLinearColor Col = FMath::Lerp(FMath::Lerp(C::Ink, C::Cyan, Pulse), C::Red, MineralFail);
			Section(36, Sw - 36, TEXT("MINERALS"), D::Int(ShownMinerals), Col, MineralFail * FMath::Sin(NowS * 60) * 3);
		}
		// Wave with a ring counting down to the next one.
		{
			const float X = Sw;
			D::Line(Out, L + 2, G, FVector2f(X, 12), FVector2f(X, 38), C::Hair);
			Section(X + 14, Sw - 50, TEXT("WAVE"), FString::Printf(TEXT("%02d"), Gm->Wave), C::Amber);
			const FVector2f Rc(X + Sw - 24, 25);
			const float Frac = FMath::Clamp(Gm->WaveTimer / FMath::Max(1.f, Gm->WaveLen), 0.f, 1.f);
			D::Circle(Out, L + 2, G, Rc, 13, C::Faint, 2.5f);
			D::Circle(Out, L + 3, G, Rc, 13, Frac < 0.2f ? C::Red : C::Cyan, 2.5f, 32, -PI / 2, -PI / 2 + 2 * PI * Frac);
			D::Text(Out, L + 4, G, FString::FromInt(FMath::CeilToInt(Gm->WaveTimer)), D::Bold(9), FVector2f(Rc.X, Rc.Y - 7), C::Ink, 0.5f);
		}
		// Kills.
		{
			const float X = Sw * 2;
			D::Line(Out, L + 2, G, FVector2f(X, 12), FVector2f(X, 38), C::Hair);
			Section(X + 14, Sw - 18, TEXT("KILLS"), D::Int(Gm->Kills), C::Ink);
		}
		if (bWide)
		{
			const float X = Sw * 3;
			D::Line(Out, L + 2, G, FVector2f(X, 12), FVector2f(X, 38), C::Hair);
			Section(X + 14, Sw - 18, TEXT("SCORE"), D::Int(Gm->Score + Gm->Kills * 10 + Gm->Wave * 250), C::Ink);
		}
		return L + 8;
	});

	auto IconBtn = [this](bool bPause)
	{
		return PaintedButton(FVector2D(42, 50), [this, bPause](const FGeometry& G, FSlateWindowElementList& Out, int32 L, bool bHover, bool bPress)
		{
			D::RFill(Out, L, G, FVector2f(0, 0), FVector2f(42, 50), 12, bHover ? C::GlassHi : C::Glass);
			D::RLine(Out, L + 1, G, FVector2f(0, 0), FVector2f(42, 50), 12, bHover ? C::A(C::Cyan, 0.6f) : C::Hair);
			const FLinearColor Col = bPress ? C::Cyan : C::Ink;
			if (bPause) D::PauseGlyph(Out, L + 2, G, FVector2f(21, 25), Col);
			else D::SpeakerGlyph(Out, L + 2, G, FVector2f(20, 25), FApp::GetVolumeMultiplier() <= 0, Col);
		}, [this, bPause]()
		{
			if (bPause) TogglePause();
			else FApp::SetVolumeMultiplier(FApp::GetVolumeMultiplier() > 0 ? 0.f : 1.f);
		});
	};

	return SNew(SOverlay)
		+ SOverlay::Slot()[ Strip ]
		+ SOverlay::Slot().HAlign(HAlign_Right)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)[ IconBtn(false) ]
			+ SHorizontalBox::Slot().AutoWidth()[ IconBtn(true) ]
		];
}

// =====================================================================================================
// Squad card: bunker badge, 10-segment armour bar, fireteam portraits
// =====================================================================================================
// Troops standing outside: a row of small portraits under the fireteam (tap one to send it back in).
// Past 24 troopers the portraits shrink so the card stays compact.
namespace SquadRow
{
	constexpr float X0 = 70, Top = 70;
	float Size(int32 Count) { return Count > 24 ? 11.f : 15.f; }
	float Gap(int32 Count) { return Count > 24 ? 2.f : 3.f; }
	int32 PerRow(float CardW, int32 Count) { return FMath::Max(1, FMath::FloorToInt((CardW - X0 - 12 + Gap(Count)) / (Size(Count) + Gap(Count)))); }
	FVector2f At(int32 i, float CardW, int32 Count)
	{
		const int32 N = PerRow(CardW, Count);
		return FVector2f(X0 + (i % N) * (Size(Count) + Gap(Count)), Top + (i / N) * (Size(Count) + Gap(Count) + 1));
	}
}

TSharedRef<SWidget> SBSHud::BuildSquadCard()
{
	return SAssignNew(SquadCard, SBSPaint)
		.Size_Lambda([this]()
		{
			const float Wd = Portrait() ? FMath::Max(200.f, (float)LocalSize.X - 20 - 104) : 400.f;
			const ABSGame* Gm = Game();
			const int32 N = Gm ? Gm->Squad.Num() : 0;
			const int32 Rows = N ? (N + SquadRow::PerRow(Wd, N) - 1) / SquadRow::PerRow(Wd, N) : 0;
			return FVector2D(Wd, 70 + (Rows ? Rows * (SquadRow::Size(N) + SquadRow::Gap(N) + 1) + 4 : 0));
		})
		.Paint([this](const FGeometry& G, FSlateWindowElementList& Out, int32 L)
		{
			ABSGame* Gm = Game(); if (!Gm) return L;
			const FVector2f S = FVector2f(G.GetLocalSize());
			const FBSBunker& B = Gm->Bunker;
			const float Pct = FMath::Clamp(B.Hp / FMath::Max(1.f, B.MaxHp), 0.f, 1.f);
			const bool bCritical = Pct < 0.25f && !B.bDead;
			const float Pulse = bCritical ? 0.5f + 0.5f * FMath::Sin(NowS * 8) : 0.f;
			D::RFill(Out, L, G, FVector2f(0, 0), S, 12, C::Glass);
			D::RLine(Out, L + 1, G, FVector2f(0, 0), S, 12, bCritical ? C::A(C::Red, 0.4f + 0.5f * Pulse) : C::Hair, bCritical ? 1.5f : 1.f);
			// Bunker badge.
			UBSAssets* As = Gm->GetAssets();
			D::Image(Out, L + 2, G, As->IconBrush(TEXT("btn-building-terran-bunker")), FVector2f(10, 11), FVector2f(48, 48));
			const ABSCameraPawn* Cv = Cam();
			const bool bFp = Cv && Cv->IsBunkerView();
			D::RLine(Out, L + 3, G, FVector2f(9, 10), FVector2f(50, 50), 8, bFp ? C::A(C::Amber, 0.75f + 0.25f * FMath::Sin(NowS * 3)) : C::A(C::Ink, 0.25f), bFp ? 2.f : 1.f);
			const float X0 = 70, Bw = S.X - X0 - 12;
			// Header line.
			D::Text(Out, L + 2, G, bCritical ? TEXT("INTEGRITY CRITICAL") : TEXT("BUNKER"), D::Label(8.5f), FVector2f(X0, 9), bCritical ? C::A(C::Red, 0.65f + 0.35f * Pulse) : C::Muted);
			D::Text(Out, L + 2, G, FString::Printf(TEXT("%d / %d"), FMath::CeilToInt(FMath::Max(0.f, B.Hp)), FMath::RoundToInt(B.MaxHp)), D::Bold(10), FVector2f(X0 + Bw, 7), bCritical ? C::Red : C::Ink, 1.f);
			// Armour segments.
			const int32 NSeg = 10;
			const float Gap = 3, Sgw = (Bw - Gap * (NSeg - 1)) / NSeg;
			const int32 Lit = FMath::CeilToInt(Pct * NSeg - 0.001f);
			const FLinearColor Col = Pct > 0.5f ? C::Green : Pct > 0.25f ? C::Amber : C::Red;
			for (int32 i = 0; i < NSeg; i++)
			{
				const FVector2f P(X0 + i * (Sgw + Gap), 25);
				if (i < Lit) D::RFill(Out, L + 2, G, P, FVector2f(Sgw, 9), 2, Col);
				else
				{
					D::RFill(Out, L + 2, G, P, FVector2f(Sgw, 9), 2, C::Faint);
					const double Since = NowS - SegmentLostAt[i];
					if (Since < 0.5) D::RFill(Out, L + 3, G, P, FVector2f(Sgw, 9), 2, C::A(FLinearColor::White, 0.9f * (1 - (float)Since / 0.5f)));
				}
			}
			// Fireteam portraits.
			const bool bStim = Gm->Ab.StimUntil > Gm->Time;
			const float Ps = 22, Pg = 5;
			for (int32 i = 0; i < B.Slots; i++)
			{
				const FVector2f P(X0 + i * (Ps + Pg), 41);
				if (i < B.Units.Num())
				{
					const FBSInfantryUnit& U = B.Units[i];
					D::Image(Out, L + 2, G, As->IconBrush(U.Def->Icon), P, FVector2f(Ps, Ps));
					D::RLine(Out, L + 3, G, P - FVector2f(1, 1), FVector2f(Ps + 2, Ps + 2), 5, bStim ? C::A(C::Amber, 0.6f + 0.4f * FMath::Sin(NowS * 12 + i)) : C::A(C::Ink, 0.22f), bStim ? 1.5f : 1.f);
					const float Hp = FMath::Clamp(U.Hp / FMath::Max(1.f, U.MaxHp), 0.f, 1.f);
					D::Rect(Out, L + 3, G, P + FVector2f(0, Ps + 2), FVector2f(Ps, 2), C::Faint);
					D::Rect(Out, L + 4, G, P + FVector2f(0, Ps + 2), FVector2f(Ps * Hp, 2), Hp > 0.5f ? C::Green : Hp > 0.25f ? C::Amber : C::Red);
				}
				else D::RLine(Out, L + 2, G, P, FVector2f(Ps, Ps), 5, C::A(C::Ink, 0.12f));
			}
			if (bStim) D::Text(Out, L + 2, G, TEXT("STIMMED"), D::Label(8), FVector2f(X0 + Bw, 47), C::Amber, 1.f);
			for (int32 i = 0; i < Gm->Squad.Num(); i++)
			{
				const FBSTrooper& M = *Gm->Squad[i];
				const int32 N = Gm->Squad.Num();
				const float Ps1 = SquadRow::Size(N);
				const FVector2f P = SquadRow::At(i, S.X, N), Sz(Ps1, Ps1);
				D::Image(Out, L + 2, G, As->IconBrush(M.Def->Icon), P, Sz, FLinearColor(1, 1, 1, 0.85f));
				D::RLine(Out, L + 3, G, P - FVector2f(1, 1), Sz + FVector2f(2, 2), 4, M.bBoarding ? C::Green : bStim ? C::A(C::Amber, 0.7f) : C::A(C::Cyan, 0.45f), 1.f);
				const float Hp = FMath::Clamp(M.Hp / FMath::Max(1.f, M.MaxHp), 0.f, 1.f);
				D::Rect(Out, L + 4, G, P + FVector2f(0, Ps1 - 2), FVector2f(Ps1 * Hp, 2), Hp > 0.5f ? C::Green : Hp > 0.25f ? C::Amber : C::Red);
			}
			return L + 8;
		});
}

// =====================================================================================================
// Abilities
// =====================================================================================================
struct FBSAbility { FString Name, Icon, Key; FLinearColor Accent; int32 Cost; float MaxCd; };
static const TArray<FBSAbility>& Abilities()
{
	static TArray<FBSAbility> A = {
		{ TEXT("STIM"), TEXT("btn-ability-terran-stimpack"), TEXT("S"), C::Amber, 0, BSDefs::StimCooldown },
		{ TEXT("AIR STRIKE"), TEXT("btn-unit-terran-banshee"), TEXT("A"), FLinearColor(1.f, 0.48f, 0.28f), BSDefs::BansheeCost, BSDefs::BansheeCooldown },
		{ TEXT("REPAIR"), TEXT("btn-ability-terran-repair"), TEXT("R"), C::Cyan, BSDefs::RepairCost, BSDefs::RepairCooldown },
		{ TEXT("YAMATO"), TEXT("btn-unit-terran-battlecruiser"), TEXT("Y"), C::Violet, BSDefs::YamatoCost, BSDefs::YamatoCooldown },
	};
	return A;
}

TSharedRef<SWidget> SBSHud::MakeAbility(int32 Index)
{
	const FVector2D Size(84, 100);
	return PaintedButton(Size, [this, Index, Size](const FGeometry& G, FSlateWindowElementList& Out, int32 L, bool bHover, bool bPress)
	{
		ABSGame* Gm = Game(); if (!Gm) return;
		const FBSAbility& Ab = Abilities()[Index];
		const float Cd = Index == 0 ? Gm->Ab.StimCd : Index == 1 ? Gm->Ab.StrikeCd : Index == 2 ? Gm->Ab.RepairCd : Gm->Ab.YamatoCd;
		const bool bLocked = Index == 0 && !Gm->Up.bStim;
		const bool bPoor = Ab.Cost > 0 && Gm->Minerals < Ab.Cost;
		const bool bActive = Index == 0 && Gm->Ab.StimUntil > Gm->Time;
		const bool bReady = !bLocked && !bPoor && Cd <= 0;
		const float W = (float)Size.X, Is = 58;
		const FVector2f Ip((W - Is) * 0.5f, 4 + (bPress ? 1.f : 0.f));
		// Icon tile.
		if (bReady)
		{
			const float Glow = 0.5f + 0.5f * FMath::Sin(NowS * 3.5 + Index);
			D::RLine(Out, L, G, Ip - FVector2f(4, 4), FVector2f(Is + 8, Is + 8), 16, C::A(Ab.Accent, 0.18f + 0.3f * Glow), 2);
		}
		D::RFill(Out, L + 1, G, Ip - FVector2f(2, 2), FVector2f(Is + 4, Is + 4), 13, C::Glass);
		const FLinearColor Tint = (bLocked || bPoor) ? FLinearColor(0.32f, 0.34f, 0.38f) : FLinearColor::White;
		D::Image(Out, L + 2, G, Gm->GetAssets()->IconBrush(Ab.Icon), Ip + FVector2f(3, 3), FVector2f(Is - 6, Is - 6), Tint);
		if (Cd > 0)
		{
			D::Pie(Out, L + 3, G, Ip + FVector2f(Is, Is) * 0.5f, Is * 0.5f - 3, Cd / Ab.MaxCd, C::A(FLinearColor::Black, 0.68f));
			D::Text(Out, L + 4, G, FString::FromInt(FMath::CeilToInt(Cd)), D::Num(18), Ip + FVector2f(Is * 0.5f, Is * 0.5f - 13), C::Ink, 0.5f, true);
		}
		if (bActive)
		{
			const float Left = FMath::Clamp((Gm->Ab.StimUntil - Gm->Time) / BSDefs::StimDuration, 0.f, 1.f);
			D::RLine(Out, L + 4, G, Ip - FVector2f(2, 2), FVector2f(Is + 4, Is + 4), 13, C::A(Ab.Accent, 0.5f + 0.5f * FMath::Sin(NowS * 10)), 2);
			D::Rect(Out, L + 4, G, Ip + FVector2f(6, Is - 7), FVector2f((Is - 12) * Left, 3), Ab.Accent);
		}
		D::RLine(Out, L + 4, G, Ip - FVector2f(2, 2), FVector2f(Is + 4, Is + 4), 13, bHover ? C::A(Ab.Accent, 0.95f) : C::A(Ab.Accent, bReady ? 0.6f : 0.25f), bHover ? 1.5f : 1.f);
		if (!Portrait())
		{
			D::RFill(Out, L + 5, G, Ip + FVector2f(-4, -2), FVector2f(15, 15), 4, C::Dark);
			D::Text(Out, L + 6, G, Ab.Key, D::Bold(8.5f), Ip + FVector2f(3.5f, -1), C::Muted, 0.5f);
		}
		// Name (auto-fitted) and cost.
		const FSlateFontInfo Nf = D::Fit(Ab.Name, [](float S) { return D::Label(S); }, 10, 7, W - 4);
		D::Text(Out, L + 2, G, Ab.Name, Nf, FVector2f(W * 0.5f, Is + 10), bLocked ? C::Muted : C::Ink, 0.5f, true);
		if (bLocked) D::CostPill(Out, L + 2, G, FVector2f(W * 0.5f, Is + 33), TEXT("LOCKED"), false, C::Muted);
		else if (Ab.Cost > 0) D::CostPill(Out, L + 2, G, FVector2f(W * 0.5f, Is + 33), FString::FromInt(Ab.Cost), true, bPoor ? C::Red : C::Cyan);
		else D::CostPill(Out, L + 2, G, FVector2f(W * 0.5f, Is + 33), TEXT("-10 HP"), false, C::Amber);
	},
	[this, Index]()
	{
		ABSGame* Gm = Game(); if (!Gm) return;
		if (Index == 0) Gm->UseStim(); else if (Index == 1) Gm->CallAirStrike(); else if (Index == 2) Gm->EmergencyRepair(); else Gm->UseYamato();
	});
}

TSharedRef<SWidget> SBSHud::BuildAbilityRow()
{
	TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
	for (int32 i = 0; i < Abilities().Num(); i++) Row->AddSlot().AutoWidth().Padding(6, 0)[ MakeAbility(i) ];
	return Row;
}

// =====================================================================================================
// Armoury items
// =====================================================================================================
struct FBSArmItem
{
	FString Id, Name, Short;
	TFunction<FString(ABSGame*)> Icon;
	TFunction<int32(ABSGame*)> Cost;
	TFunction<bool(ABSGame*)> Avail;
	TFunction<FString(ABSGame*)> Tag;       // shown when unavailable
	TFunction<int32(ABSGame*)> Level;       // -1 = no level pips
	int32 MaxLevel = 0;
	TFunction<void(ABSGame*)> Buy;
};

static const TArray<FBSArmItem>& ArmItems()
{
	static TArray<FBSArmItem> Items = []()
	{
		TArray<FBSArmItem> Out;
		for (const FBSInfantryDef& Inf : BSDefs::Infantry())
		{
			const FString Key = Inf.Key;
			FBSArmItem I; I.Id = Key; I.Name = Inf.Name; I.Short = Key == TEXT("marine") ? TEXT("Gauss rifle infantry") : Key == TEXT("firebat") ? TEXT("Flamethrower, anti-swarm") : TEXT("Slows, shreds armour");
			I.Icon = [Key](ABSGame*) { return BSDefs::FindInfantry(Key)->Icon; };
			I.Cost = [Key](ABSGame*) { return BSDefs::FindInfantry(Key)->Cost; };
			I.Avail = [](ABSGame* G) { return !G->TroopsFull(); };
			I.Tag = [](ABSGame*) { return FString(TEXT("FULL")); };
			I.Level = [](ABSGame*) { return -1; };
			I.Buy = [Key](ABSGame* G) { G->AddInfantry(Key); };
			Out.Add(I);
		}
		auto Simple = [&Out](const TCHAR* Id, const TCHAR* Name, const TCHAR* Short, const TCHAR* Icon, int32 Cost, TFunction<bool(ABSGame*)> Avail, TFunction<void(ABSGame*)> Buy)
		{
			FBSArmItem I; I.Id = Id; I.Name = Name; I.Short = Short;
			const FString Ic = Icon;
			I.Icon = [Ic](ABSGame*) { return Ic; };
			I.Cost = [Cost](ABSGame* G) { return Cost > 0 ? Cost : G->TankCost(); };
			I.Avail = Avail; I.Buy = Buy;
			I.Tag = [](ABSGame*) { return FString(TEXT("MAX")); };
			I.Level = [](ABSGame*) { return -1; };
			Out.Add(I);
		};
		Simple(TEXT("scv"), TEXT("SCV"), TEXT("Repairs bunker and tanks"), TEXT("btn-unit-terran-scv"), BSDefs::ScvCost,
			[](ABSGame* G) { return G->Scvs.Num() < BSDefs::ScvMax; }, [](ABSGame* G) { G->AddScv(); });
		Simple(TEXT("sam"), TEXT("Missile Turret"), TEXT("Anti-air, up to three"), TEXT("btn-building-terran-missileturret"), BSDefs::SamCost,
			[](ABSGame* G) { return G->SamCount() < BSDefs::SamMax; }, [](ABSGame* G) { G->BuildStructure(true); });
		Simple(TEXT("sensor"), TEXT("Sensor Tower"), TEXT("Powers the radar"), TEXT("btn-building-terran-sensortower-silver"), BSDefs::SensorCost,
			[](ABSGame* G) { return !G->HasSensor(); }, [](ABSGame* G) { G->BuildStructure(false); });
		Simple(TEXT("tank"), TEXT("Siege Tank"), TEXT("Air-dropped, long-range splash"), TEXT("btn-unit-terran-siegetank"), 0,
			[](ABSGame* G) { return G->Tanks.Num() < G->TankMax; }, [](ABSGame* G) { G->DropTank(); });
		for (const FBSUpgradeDef& U : BSDefs::Upgrades())
		{
			const FString Key = U.Key;
			FBSArmItem I; I.Id = Key; I.Name = U.Name; I.MaxLevel = U.Costs.Num();
			I.Short = Key == TEXT("stim") ? TEXT("Unlocks STIM ability") : Key == TEXT("weapons") ? TEXT("+1 infantry damage") : Key == TEXT("neosteel") ? TEXT("Bunker holds six")
				: Key == TEXT("plating") ? TEXT("+150 HP per level") : Key == TEXT("vehicle") ? TEXT("+5 tank damage") : Key == TEXT("hisec") ? TEXT("+1 range, bunker and tanks")
				: Key == TEXT("rails") ? TEXT("+2 bunker range") : TEXT("Heals troops and SCVs");
			I.Icon = [Key](ABSGame* G) { const FBSUpgradeDef* D0 = BSDefs::FindUpgrade(Key); return D0->Icons[FMath::Min(G->UpgradeLevel(Key), D0->Icons.Num() - 1)]; };
			I.Cost = [Key](ABSGame* G) { return G->CostOfUpgrade(Key); };
			I.Avail = [Key](ABSGame* G) { return G->UpgradeAvailable(Key); };
			I.Tag = [Key](ABSGame* G)
			{
				if (Key == TEXT("medivac") && !G->Up.bStim) return FString(TEXT("NEEDS STIM"));
				if (Key == TEXT("vehicle") && G->Tanks.Num() == 0 && G->Up.Vehicle < 3) return FString(TEXT("NEEDS TANK"));
				if (Key == TEXT("rails") && !G->Up.bHiSec) return FString(TEXT("NEEDS HI-SEC"));
				return FString(BSDefs::FindUpgrade(Key)->Costs.Num() > 1 ? TEXT("MAX") : TEXT("DONE"));
			};
			I.Level = [Key](ABSGame* G) { return BSDefs::FindUpgrade(Key)->Costs.Num() > 1 ? G->UpgradeLevel(Key) : -1; };
			I.Buy = [Key](ABSGame* G) { G->BuyUpgrade(Key); };
			Out.Add(I);
		}
		return Out;
	}();
	return Items;
}

// Portrait card: icon, two-line name, cost pill; unavailable items get a corner tag.
TSharedRef<SWidget> SBSHud::MakeArmouryCard(int32 Index)
{
	const FVector2D Size(76, 104);
	return PaintedButton(Size, [this, Index, Size](const FGeometry& G, FSlateWindowElementList& Out, int32 L, bool bHover, bool bPress)
	{
		ABSGame* Gm = Game(); if (!Gm) return;
		const FBSArmItem& I = ArmItems()[Index];
		const bool bAvail = I.Avail(Gm);
		const int32 Cost = I.Cost(Gm);
		const bool bPoor = bAvail && Gm->Minerals < Cost;
		const FVector2f S((float)Size.X, (float)Size.Y), P(0, bPress ? 1.f : 0.f);
		D::RFill(Out, L, G, P, S, 12, bHover ? C::GlassHi : C::Glass);
		D::RLine(Out, L + 1, G, P, S, 12, bHover ? C::A(C::Cyan, 0.7f) : C::Hair);
		const float Is = 44;
		const FVector2f Ip(P.X + (S.X - Is) * 0.5f, P.Y + 8);
		D::Image(Out, L + 2, G, Gm->GetAssets()->IconBrush(I.Icon(Gm)), Ip, FVector2f(Is, Is), (bAvail && !bPoor) ? FLinearColor::White : FLinearColor(0.3f, 0.32f, 0.36f));
		D::RLine(Out, L + 3, G, Ip - FVector2f(1, 1), FVector2f(Is + 2, Is + 2), 7, C::A(C::Ink, 0.2f));
		const int32 Lv = I.Level(Gm);
		if (Lv >= 0)
			for (int32 k = 0; k < I.MaxLevel; k++)
				D::RFill(Out, L + 3, G, FVector2f(S.X * 0.5f - (I.MaxLevel * 8 - 3) * 0.5f + k * 8, Ip.Y + Is + 3), FVector2f(5, 3), 1.5f, k < Lv ? C::Amber : C::Faint);
		// Name: up to two lines, auto-fitted.
		FString L1 = I.Name, L2;
		const float MaxW = S.X - 8;
		if (D::Measure(L1, D::Label(9.5f)).X > MaxW) { int32 Sp; if (L1.FindLastChar(' ', Sp)) { L2 = L1.Mid(Sp + 1); L1.LeftInline(Sp); } }
		const FSlateFontInfo Nf = D::Fit(L1.Len() > L2.Len() ? L1 : L2, [](float Sz) { return D::Label(Sz); }, 9.5f, 7, MaxW);
		const float Ny = L2.IsEmpty() ? P.Y + 64 : P.Y + 59;
		const FLinearColor Nc = bAvail ? C::Ink : C::Muted;
		D::Text(Out, L + 2, G, L1.ToUpper(), Nf, FVector2f(S.X * 0.5f, Ny), Nc, 0.5f);
		if (!L2.IsEmpty()) D::Text(Out, L + 2, G, L2.ToUpper(), Nf, FVector2f(S.X * 0.5f, Ny + 11), Nc, 0.5f);
		if (bAvail) D::CostPill(Out, L + 3, G, FVector2f(S.X * 0.5f, P.Y + S.Y - 13), FString::FromInt(Cost), true, bPoor ? C::Red : C::Cyan);
		else
		{
			const FString Tag = I.Tag(Gm);
			const FSlateFontInfo Tf = D::Fit(Tag, [](float Sz) { return D::Label(Sz); }, 8.5f, 6.5f, S.X - 14);
			const FVector2f Ts = D::Measure(Tag, Tf);
			const FVector2f Tp(S.X * 0.5f - Ts.X * 0.5f - 6, P.Y + S.Y - 21);
			D::RFill(Out, L + 3, G, Tp, FVector2f(Ts.X + 12, 16), 8, C::A(C::Amber, 0.16f));
			D::Text(Out, L + 4, G, Tag, Tf, FVector2f(Tp.X + 6, Tp.Y + 8 - Ts.Y * 0.5f), C::Amber);
		}
	},
	[this, Index]() { if (ABSGame* Gm = Game()) ArmItems()[Index].Buy(Gm); });
}

// Landscape build-list row: icon, name, one-line description, cost.
TSharedRef<SWidget> SBSHud::MakeArmouryRow(int32 Index)
{
	const FVector2D Size(236, 54);
	return PaintedButton(Size, [this, Index, Size](const FGeometry& G, FSlateWindowElementList& Out, int32 L, bool bHover, bool bPress)
	{
		ABSGame* Gm = Game(); if (!Gm) return;
		const FBSArmItem& I = ArmItems()[Index];
		const bool bAvail = I.Avail(Gm);
		const int32 Cost = I.Cost(Gm);
		const bool bPoor = bAvail && Gm->Minerals < Cost;
		const FVector2f S((float)Size.X, (float)Size.Y - 4), P(bPress ? 1.f : 0.f, 0);
		D::RFill(Out, L, G, P, S, 10, bHover ? C::GlassHi : C::Glass);
		D::RLine(Out, L + 1, G, P, S, 10, bHover ? C::A(C::Cyan, 0.7f) : C::Hair);
		const float Is = 36;
		D::Image(Out, L + 2, G, Gm->GetAssets()->IconBrush(I.Icon(Gm)), P + FVector2f(8, 7), FVector2f(Is, Is), (bAvail && !bPoor) ? FLinearColor::White : FLinearColor(0.3f, 0.32f, 0.36f));
		D::RLine(Out, L + 3, G, P + FVector2f(7, 6), FVector2f(Is + 2, Is + 2), 6, C::A(C::Ink, 0.2f));
		const float Tx = P.X + 54, RightW = 62, TextW = S.X - 54 - RightW;
		D::Text(Out, L + 2, G, I.Name.ToUpper(), D::Fit(I.Name.ToUpper(), [](float Sz) { return D::Label(Sz); }, 10, 7, TextW), FVector2f(Tx, P.Y + 9), bAvail ? C::Ink : C::Muted);
		D::Text(Out, L + 2, G, I.Short, D::Fit(I.Short, [](float Sz) { return D::Body(Sz); }, 9, 6.5f, TextW, D::BodySlack), FVector2f(Tx, P.Y + 26), C::Muted);
		const int32 Lv = I.Level(Gm);
		if (Lv >= 0) for (int32 k = 0; k < I.MaxLevel; k++) D::RFill(Out, L + 3, G, FVector2f(Tx + k * 8, P.Y + 41), FVector2f(5, 3), 1.5f, k < Lv ? C::Amber : C::Faint);
		const FVector2f Pc(P.X + S.X - RightW * 0.5f - 4, P.Y + S.Y * 0.5f);
		if (bAvail) D::CostPill(Out, L + 3, G, Pc, FString::FromInt(Cost), true, bPoor ? C::Red : C::Cyan);
		else
		{
			const FString Tag = I.Tag(Gm);
			const FSlateFontInfo Tf = D::Fit(Tag, [](float Sz) { return D::Label(Sz); }, 8.5f, 6.5f, RightW - 10);
			const FVector2f Ts = D::Measure(Tag, Tf);
			D::RFill(Out, L + 3, G, Pc - FVector2f(Ts.X * 0.5f + 6, 8), FVector2f(Ts.X + 12, 16), 8, C::A(C::Amber, 0.16f));
			D::Text(Out, L + 4, G, Tag, Tf, Pc - FVector2f(0, Ts.Y * 0.5f), C::Amber, 0.5f);
		}
	},
	[this, Index]() { if (ABSGame* Gm = Game()) ArmItems()[Index].Buy(Gm); });
}

TSharedRef<SWidget> SBSHud::BuildArmouryStrip()
{
	TSharedRef<SScrollBox> Strip = SNew(SScrollBox).Orientation(Orient_Horizontal).ScrollBarVisibility(EVisibility::Collapsed).ConsumeMouseWheel(EConsumeMouseWheel::Always);
	for (int32 i = 0; i < ArmItems().Num(); i++) Strip->AddSlot().Padding(i == 0 ? 10 : 4, 0, 4, 0)[ MakeArmouryCard(i) ];

	auto Header = PaintedButton(TAttribute<FVector2D>::CreateLambda([this]() { return FVector2D(LocalSize.X, 26); }),
		[this](const FGeometry& G, FSlateWindowElementList& Out, int32 L, bool bHover, bool)
	{
		const float W = G.GetLocalSize().X;
		D::Text(Out, L, G, TEXT("ARMOURY"), D::Label(9), FVector2f(12, 6), bHover ? C::Cyan : C::Muted);
		const float Tw = D::Measure(TEXT("ARMOURY"), D::Label(9)).X;
		D::Line(Out, L, G, FVector2f(20 + Tw, 13), FVector2f(W - 40, 13), C::Hair);
		D::Chevron(Out, L + 1, G, FVector2f(W - 24, 13), !bArmouryOpen, bHover ? C::Cyan : C::Muted);
	}, [this]() { bArmouryOpen = !bArmouryOpen; });

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()[ Header ]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 2, 0, 10)
		[
			SNew(SBox).Visibility_Lambda([this]() { return bArmouryOpen ? EVisibility::Visible : EVisibility::Collapsed; })[ Strip ]
		];
}

TSharedRef<SWidget> SBSHud::BuildArmouryColumn()
{
	TSharedRef<SScrollBox> List = SNew(SScrollBox).Orientation(Orient_Vertical).ScrollBarVisibility(EVisibility::Collapsed);
	for (int32 i = 0; i < ArmItems().Num(); i++) List->AddSlot()[ MakeArmouryRow(i) ];
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(2, 0, 0, 6)
		[
			SNew(SBSPaint).Size(FVector2D(236, 18)).Paint([](const FGeometry& G, FSlateWindowElementList& Out, int32 L)
			{
				D::Text(Out, L, G, TEXT("ARMOURY"), D::Label(9), FVector2f(2, 2), C::Muted);
				const float Tw = D::Measure(TEXT("ARMOURY"), D::Label(9)).X;
				D::Line(Out, L, G, FVector2f(10 + Tw, 9), FVector2f(234, 9), C::Hair);
				return L + 2;
			})
		]
		+ SVerticalBox::Slot().FillHeight(1.f)[ List ];
}

// =====================================================================================================
// Start screen and modals
// =====================================================================================================
TSharedRef<SWidget> SBSHud::BuildStartScreen()
{
	auto ModeCard = [this](bool bArena)
	{
		const FVector2D Size(340, 96);
		return PaintedButton(Size, [this, bArena, Size](const FGeometry& G, FSlateWindowElementList& Out, int32 L, bool bHover, bool bPress)
		{
			const FVector2f S((float)Size.X, (float)Size.Y), P(0, bPress ? 1.f : 0.f);
			const bool bRec = bArena != Portrait();
			const FLinearColor Acc = bArena ? C::Violet : C::Cyan;
			D::RFill(Out, L, G, P, S, 16, bHover ? C::GlassHi : C::Glass);
			D::RLine(Out, L + 1, G, P, S, 16, bHover ? C::A(Acc, 0.9f) : C::A(Acc, bRec ? 0.5f : 0.22f), bHover ? 1.5f : 1.f);
			D::RFill(Out, L + 1, G, P + FVector2f(0, 22), FVector2f(3, S.Y - 44), 1.5f, Acc);
			UBSAssets* As = GM.IsValid() ? GM->GetAssets() : nullptr;
			if (As) D::Image(Out, L + 2, G, As->IconBrush(bArena ? TEXT("btn-unit-zerg-ultralisk") : TEXT("btn-unit-terran-marine")), P + FVector2f(18, 18), FVector2f(60, 60));
			if (As) D::RLine(Out, L + 3, G, P + FVector2f(17, 17), FVector2f(62, 62), 10, C::A(C::Ink, 0.2f));
			D::Text(Out, L + 2, G, bArena ? TEXT("360 ARENA") : TEXT("LANE DEFENCE"), D::Fit(bArena ? TEXT("360 ARENA") : TEXT("LANE DEFENCE"), [](float Sz) { return D::Title(Sz); }, 14.5f, 10, S.X - 94 - 14), P + FVector2f(94, 20), C::Ink);
			const FString Desc = bArena ? TEXT("Waves from every side. Bigger, faster, deadlier.") : TEXT("Hold the lane as the Swarm pours down from the north.");
			FString D1, D2;
			D::SplitTwo(Desc, D1, D2);
			const FSlateFontInfo Df = D::Fit(D1.Len() > D2.Len() ? D1 : D2, [](float Sz) { return D::Body(Sz); }, 10.5f, 8, S.X - 110, D::BodySlack);
			D::Text(Out, L + 2, G, D1, Df, P + FVector2f(94, 44), C::Muted);
			D::Text(Out, L + 2, G, D2, Df, P + FVector2f(94, 58), C::Muted);
			if (bRec)
			{
				const FString R = Portrait() ? TEXT("BEST ON PHONE") : TEXT("BEST ON DESKTOP");
				const FVector2f Rs = D::Measure(R, D::Label(7.5f));
				D::RFill(Out, L + 2, G, P + FVector2f(S.X - Rs.X - 26, 14), FVector2f(Rs.X + 12, 15), 7.5f, C::A(Acc, 0.16f));
				D::Text(Out, L + 3, G, R, D::Label(7.5f), P + FVector2f(S.X - Rs.X - 20, 15.5f), Acc);
			}
		}, [this, bArena]() { if (GM.IsValid()) GM->StartGame(bArena ? EBSMode::Arena : EBSMode::Lane); });
	};

	auto Backdrop = SNew(SBSPaint).Visibility(EVisibility::HitTestInvisible).Paint([this](const FGeometry& G, FSlateWindowElementList& Out, int32 L)
	{
		const FVector2f S = FVector2f(G.GetLocalSize());
		const bool bPortrait = S.Y > S.X;
		// Let the live scene show through: darken top and bottom (portrait) or the left side (landscape).
		if (bPortrait)
		{
			D::GradV(Out, L, G, FVector2f(0, 0), FVector2f(S.X, S.Y * 0.4f), C::A(C::Dark, 0.92f), C::A(C::Dark, 0));
			D::GradV(Out, L, G, FVector2f(0, S.Y * 0.5f), FVector2f(S.X, S.Y * 0.5f), C::A(C::Dark, 0), C::A(C::Dark, 0.95f));
		}
		else
		{
			D::GradH(Out, L, G, FVector2f(0, 0), FVector2f(S.X * 0.62f, S.Y), C::A(C::Dark, 0.95f), C::A(C::Dark, 0));
			D::GradV(Out, L, G, FVector2f(0, S.Y * 0.7f), FVector2f(S.X, S.Y * 0.3f), C::A(C::Dark, 0), C::A(C::Dark, 0.7f));
		}
		const float MaxTitleW = bPortrait ? S.X - 48 : S.X * 0.5f;
		const FSlateFontInfo Tf0 = D::Fit(TEXT("SURVIVORS"), [](float Sz) { return D::Title(Sz); }, bPortrait ? 56.f : 60.f, 24.f, MaxTitleW);
		const float Big = (float)Tf0.Size;
		const FVector2f Tp = bPortrait ? FVector2f(S.X * 0.5f, S.Y * 0.07f) : FVector2f(56, S.Y * 0.12f);
		const float Al = bPortrait ? 0.5f : 0.f;
		const float LineH = D::Measure(TEXT("SURVIVORS"), Tf0).Y * 0.86f;
		D::Text(Out, L + 1, G, TEXT("BUNKER"), Tf0, Tp, C::Ink, Al);
		D::Text(Out, L + 1, G, TEXT("SURVIVORS"), Tf0, Tp + FVector2f(0, LineH), C::Cyan, Al);
		const FString Tag = TEXT("ONE FIRETEAM.  ONE BUNKER.  THE SWARM.");
		const FSlateFontInfo Tf = D::Fit(Tag, [](float Sz) { return D::Label(Sz); }, 10, 7, MaxTitleW);
		const float Ty = Tp.Y + LineH * 2.f + 18;
		D::Rect(Out, L + 1, G, FVector2f(Tp.X - (bPortrait ? 24 : 0), Ty - 9), FVector2f(48, 2), C::Amber);
		D::Text(Out, L + 1, G, Tag, Tf, FVector2f(Tp.X, Ty), C::Muted, Al);
		(void)Big;
		// Footer.
		int32 BL = 0, BA = 0;
		GConfig->GetInt(TEXT("BunkerSurvivors"), TEXT("Best.Lane"), BL, GGameUserSettingsIni);
		GConfig->GetInt(TEXT("BunkerSurvivors"), TEXT("Best.Arena"), BA, GGameUserSettingsIni);
		const FVector2f Fp = bPortrait ? FVector2f(S.X * 0.5f, S.Y - 46) : FVector2f(56, S.Y - 46);
		D::Text(Out, L + 1, G, FString::Printf(TEXT("BEST  LANE %d   ARENA %d"), BL, BA), D::Label(8.5f), Fp, C::Muted, Al);
		if (GM.IsValid() && GM->GetAssets())
			{
				const FString Line = TEXT("STARCRAFT II ART LINKED  ") + GM->GetAssets()->Summary.ToUpper();
				D::Text(Out, L + 1, G, Line, D::Fit(Line, [](float Sz) { return D::Label(Sz); }, 7.5f, 5.f, G.GetLocalSize().X - 28), Fp + FVector2f(0, 16), C::A(C::Green, 0.7f), Al);
			}
		return L + 4;
	});

	auto Cards = [ModeCard]()
	{
		return SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)[ ModeCard(false) ]
			+ SVerticalBox::Slot().AutoHeight()[ ModeCard(true) ];
	};

	return SNew(SOverlay)
		.Visibility_Lambda([this]() { return Game() ? EVisibility::Collapsed : EVisibility::Visible; })
		+ SOverlay::Slot()[ Backdrop ]
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Bottom).Padding(0, 0, 0, 92)
		[ SNew(SBox).Visibility_Lambda([this]() { return Portrait() ? EVisibility::Visible : EVisibility::Collapsed; })[ Cards() ] ]
		+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Center).Padding(56, 120, 0, 0)
		[ SNew(SBox).Visibility_Lambda([this]() { return Portrait() ? EVisibility::Collapsed : EVisibility::Visible; })[ Cards() ] ];
}

TSharedRef<SWidget> SBSHud::BuildModal(bool bGameOver)
{
	auto Panel = SNew(SBSPaint).Size(FVector2D(340, 236)).Paint([this, bGameOver](const FGeometry& G, FSlateWindowElementList& Out, int32 L)
	{
		ABSGame* Gm = Game(); if (!Gm) return L;
		const FVector2f S(340, 236);
		D::RFill(Out, L, G, FVector2f(0, 0), S, 18, C::A(C::Dark, 0.94f));
		D::RLine(Out, L + 1, G, FVector2f(0, 0), S, 18, bGameOver ? C::A(C::Red, 0.5f) : C::Hair);
		D::Rect(Out, L + 1, G, FVector2f(S.X * 0.5f - 24, 0), FVector2f(48, 2), bGameOver ? C::Red : C::Cyan);
		D::Text(Out, L + 2, G, bGameOver ? TEXT("BUNKER LOST") : TEXT("PAUSED"), D::Fit(TEXT("BUNKER LOST"), [](float Sz) { return D::Title(Sz); }, 26, 14, S.X - 36), FVector2f(S.X * 0.5f, 22), bGameOver ? C::Red : C::Ink, 0.5f);
		const int32 Score = bGameOver ? Gm->FinalScore : FMath::RoundToInt(Gm->Score + Gm->Kills * 10 + Gm->Wave * 250);
		const TArray<TPair<FString, FString>> Stats = {
			{ TEXT("WAVE"), FString::FromInt(Gm->Wave) }, { TEXT("KILLS"), FString::FromInt(Gm->Kills) }, { TEXT("SCORE"), FString::FromInt(Score) } };
		for (int32 i = 0; i < Stats.Num(); i++)
		{
			const float ColW = (S.X - 24) / Stats.Num(), X = 12 + ColW * (i + 0.5f);
			D::Text(Out, L + 2, G, Stats[i].Key, D::Label(8.5f), FVector2f(X, 72), C::Muted, 0.5f);
			D::Text(Out, L + 2, G, Stats[i].Value, D::Fit(Stats[i].Value, [](float Sz) { return D::Num(Sz); }, 20, 11, ColW - 16), FVector2f(X, 86), C::Ink, 0.5f);
		}
		if (bGameOver)
		{
			const FString B = Gm->bNewBest ? TEXT("NEW BEST") : FString::Printf(TEXT("BEST %d"), Gm->Best);
			const FLinearColor Bc = Gm->bNewBest ? C::Amber : C::Muted;
			const FVector2f Bs = D::Measure(B, D::Label(9));
			D::RFill(Out, L + 2, G, FVector2f(S.X * 0.5f - Bs.X * 0.5f - 10, 124), FVector2f(Bs.X + 20, 20), 10, C::A(Bc, Gm->bNewBest ? 0.18f + 0.1f * FMath::Sin(NowS * 5) : 0.1f));
			D::Text(Out, L + 3, G, B, D::Label(9), FVector2f(S.X * 0.5f, 127), Bc, 0.5f);
		}
		return L + 8;
	});

	auto Vis = [this, bGameOver]()
	{
		ABSGame* Gm = Game();
		if (!Gm) return EVisibility::Collapsed;
		return (bGameOver ? Gm->bOver : Gm->bPaused) ? EVisibility::Visible : EVisibility::Collapsed;
	};

	TSharedRef<SHorizontalBox> Buttons = SNew(SHorizontalBox);
	if (bGameOver)
	{
		Buttons->AddSlot().AutoWidth().Padding(5, 0)[ MakeButton(TEXT("REDEPLOY"), true, [this]() { if (GM.IsValid()) GM->Restart(true); }, 140) ];
		Buttons->AddSlot().AutoWidth().Padding(5, 0)[ MakeButton(TEXT("MAIN MENU"), false, [this]() { if (GM.IsValid()) GM->Restart(false); }, 140) ];
	}
	else
	{
		Buttons->AddSlot().AutoWidth().Padding(5, 0)[ MakeButton(TEXT("RESUME"), true, [this]() { TogglePause(); }, 140) ];
		Buttons->AddSlot().AutoWidth().Padding(5, 0)[ MakeButton(TEXT("MAIN MENU"), false, [this]() { if (GM.IsValid()) GM->Restart(false); }, 140) ];
	}

	return SNew(SOverlay).Visibility_Lambda(Vis)
		+ SOverlay::Slot()[ SNew(SBSPaint).Paint([](const FGeometry& G, FSlateWindowElementList& Out, int32 L) { D::Rect(Out, L, G, FVector2f(0, 0), FVector2f(G.GetLocalSize()), C::A(FLinearColor::Black, 0.55f)); return L + 1; }) ]
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
		[
			SNew(SOverlay)
			+ SOverlay::Slot()[ Panel ]
			+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Bottom).Padding(0, 0, 0, 20)[ Buttons ]
		];
}

// =====================================================================================================
// Per-frame state, banners, world overlay
// =====================================================================================================
void SBSHud::Tick(const FGeometry& Geo, const double Now, const float Dt)
{
	SCompoundWidget::Tick(Geo, Now, Dt);
	NowS = Now;
	DpiScale = Geo.Scale;
	D::GMeasureScale = Geo.Scale;
	LocalSize = Geo.GetLocalSize();
	ABSGame* Gm = Game();
	if (!Gm) return;
	const float Diff = Gm->Minerals - ShownMinerals;
	if (Diff > 1) MineralFlash = FMath::Min(1.f, MineralFlash + Dt * 4 * FMath::Min(1.f, Diff / 20));
	ShownMinerals += Diff * FMath::Min(1.f, Dt * 8.f);
	if (FMath::Abs(Gm->Minerals - ShownMinerals) < 0.5f) ShownMinerals = Gm->Minerals;
	MineralFlash = FMath::Max(0.f, MineralFlash - Dt * 2);
	MineralFail = FMath::Max(0.f, MineralFail - Dt * 2.5f);
	if (Gm->ToastSerial != SeenToast)
	{
		SeenToast = Gm->ToastSerial; ToastText = Gm->LastToast; ToastAt = Now;
		if (ToastText.Contains(TEXT("minerals"))) MineralFail = 1;
	}
	if (Gm->WaveSerial != SeenWave) { SeenWave = Gm->WaveSerial; BannerText = Gm->WaveBanner; BannerAt = Now; }
	const int32 Lit = FMath::CeilToInt(FMath::Clamp(Gm->Bunker.Hp / FMath::Max(1.f, Gm->Bunker.MaxHp), 0.f, 1.f) * 10 - 0.001f);
	for (int32 i = FMath::Max(0, Lit); i < LastLitSegments && i < 10; i++) SegmentLostAt[i] = Now;
	LastLitSegments = Lit;
}

int32 SBSHud::PaintBanner(const FGeometry& G, FSlateWindowElementList& Out, int32 L) const
{
	if (!Game()) return L;
	const FVector2f S = FVector2f(G.GetLocalSize());
	// Toast: a small glass pill under the squad card.
	const double Ts = NowS - ToastAt;
	if (Ts < 2.4 && !ToastText.IsEmpty())
	{
		const float In = FMath::Clamp((float)Ts / 0.15f, 0.f, 1.f), Alpha = In * (Ts > 1.9 ? 1 - (float)(Ts - 1.9) / 0.5f : 1.f);
		const FString T = ToastText.ToUpper();
		const FSlateFontInfo F = D::Fit(T, [](float Sz) { return D::Label(Sz); }, 10, 7.5f, S.X - 80);
		const FVector2f Sz = D::Measure(T, F);
		const float W = Sz.X + 34, Y = 150 + (1 - In) * 6;
		const FVector2f P(S.X * 0.5f - W * 0.5f, Y);
		D::RFill(Out, L, G, P, FVector2f(W, 26), 13, C::A(C::Dark, 0.88f * Alpha));
		D::RLine(Out, L + 1, G, P, FVector2f(W, 26), 13, C::A(C::Cyan, 0.35f * Alpha));
		D::Disc(Out, L + 1, G, P + FVector2f(14, 13), 3, C::A(C::Cyan, Alpha), 12);
		D::Text(Out, L + 2, G, T, F, FVector2f(P.X + 24, P.Y + 13 - Sz.Y * 0.5f), C::A(C::Ink, Alpha));
	}
	// Wave banner: a glass band whose hairlines draw outward from the centre.
	const double Bs = NowS - BannerAt;
	if (Bs < 2.6 && !BannerText.IsEmpty())
	{
		const float In = FMath::Clamp((float)Bs / 0.35f, 0.f, 1.f), OutK = FMath::Clamp((float)(Bs - 2.1) / 0.5f, 0.f, 1.f);
		const float Ease = 1 - FMath::Pow(1 - In, 3), Alpha = Ease * (1 - OutK);
		const float Y = S.Y * 0.32f, H = 76;
		D::GradH(Out, L, G, FVector2f(0, Y), FVector2f(S.X * 0.5f, H), C::A(C::Dark, 0), C::A(C::Dark, 0.82f * Alpha));
		D::GradH(Out, L, G, FVector2f(S.X * 0.5f, Y), FVector2f(S.X * 0.5f, H), C::A(C::Dark, 0.82f * Alpha), C::A(C::Dark, 0));
		const bool bHeavy = BannerText.Contains(TEXT("//"));
		const FLinearColor Acc = bHeavy ? C::Red : C::Amber;
		const float Half = S.X * 0.42f * Ease;
		D::Line(Out, L + 1, G, FVector2f(S.X * 0.5f - Half, Y), FVector2f(S.X * 0.5f + Half, Y), C::A(Acc, 0.8f * Alpha), 1.5f);
		D::Line(Out, L + 1, G, FVector2f(S.X * 0.5f - Half, Y + H), FVector2f(S.X * 0.5f + Half, Y + H), C::A(Acc, 0.8f * Alpha), 1.5f);
		FString Main = BannerText, Sub = TEXT("INCOMING");
		const int32 Sep = BannerText.Find(TEXT("//"));
		if (Sep != INDEX_NONE) { Main = BannerText.Left(Sep).TrimEnd(); Sub = BannerText.Mid(Sep + 2).TrimStart(); }
		int32 Num = 0;
		if (Main.StartsWith(TEXT("WAVE "))) { Num = FCString::Atoi(*Main.Mid(5)); Main = FString::Printf(TEXT("WAVE %02d"), Num); }
		D::Text(Out, L + 2, G, Sub, D::Label(9.5f), FVector2f(S.X * 0.5f, Y + 12), C::A(Acc, Alpha), 0.5f);
		D::Text(Out, L + 2, G, Main, D::Fit(Main, [](float Sz) { return D::Title(Sz); }, 30, 16, S.X - 40), FVector2f(S.X * 0.5f, Y + 28), C::A(C::Ink, Alpha), 0.5f);
	}
	return L + 4;
}

bool SBSHud::WorldToLocal(const FVector& World, FVector2D& Out) const
{
	if (!PC.IsValid()) return false;
	FVector2D Px;
	if (!PC->ProjectWorldLocationToScreen(World, Px, false)) return false;
	Out = Px / FMath::Max(DpiScale, 0.01f);
	return true;
}

int32 SBSHud::PaintWorld(const FGeometry& G, FSlateWindowElementList& Out, int32 L) const
{
	ABSGame* Gm = Game();
	if (!Gm) return L;
	const FVector2f S = FVector2f(G.GetLocalSize());
	if (const ABSCameraPawn* Cv = Cam(); Cv && Cv->IsBunkerView())
	{
		// Looking out of the bunker: dark steel above and below, as if through the firing slit.
		const FLinearColor Steel(0.03f, 0.04f, 0.055f);
		const int32 N = 12;
		for (int32 k = 0; k < N; k++)
		{
			const float Top = 0.17f * S.Y / N, Bot = 0.2f * S.Y / N;
			D::Rect(Out, L, G, FVector2f(0, k * Top), FVector2f(S.X, Top + 1), C::A(Steel, 0.92f * (1 - (float)k / N)));
			D::Rect(Out, L, G, FVector2f(0, S.Y - (k + 1) * Bot), FVector2f(S.X, Bot + 1), C::A(Steel, 0.7f * (1 - (float)k / N)));
		}
	}
	auto Bar = [&](const FVector& W, float Pct, float Width, const FLinearColor& Col)
	{
		FVector2D P; if (!WorldToLocal(W, P)) return;
		const FVector2f Pf(P);
		if (Pf.X < -40 || Pf.X > S.X + 40 || Pf.Y < -40 || Pf.Y > S.Y + 40) return;
		D::RFill(Out, L, G, Pf - FVector2f(Width * 0.5f + 1, 3), FVector2f(Width + 2, 5), 2.5f, C::A(FLinearColor::Black, 0.7f));
		D::RFill(Out, L + 1, G, Pf - FVector2f(Width * 0.5f, 2), FVector2f(FMath::Max(3.f, Width * Pct), 3), 1.5f, Col);
	};
	for (const TSharedPtr<FBSEnemy>& E : Gm->Enemies)
		if (E->Hp < E->MaxHp) Bar(BSSpace::ToWorld(E->X, E->Y, E->Z + E->Def->Height + 0.5f), E->Hp / E->MaxHp, 18 + E->Radius * 14, C::Violet);
	for (const TSharedPtr<FBSTank>& K : Gm->Tanks)
		if (K->State == FBSTank::Ready && K->Hp < K->MaxHp) Bar(BSSpace::ToWorld(K->X, K->Y, 2.4f), K->Hp / K->MaxHp, 32, C::Green);
	for (const TSharedPtr<FBSScv>& Sc : Gm->Scvs)
		if (Sc->Hp < Sc->MaxHp) Bar(BSSpace::ToWorld(Sc->X, Sc->Y, 1.7f), Sc->Hp / Sc->MaxHp, 18, C::Green);

	// Target lock: four corner brackets that settle in when a target is chosen.
	if (TSharedPtr<FBSEnemy> F = Gm->Focus.Pin())
	{
		FVector2D P, P2;
		if (WorldToLocal(BSSpace::ToWorld(F->X, F->Y, F->Z + 0.5f), P) && WorldToLocal(BSSpace::ToWorld(F->X, F->Y + F->Radius + 0.4f, F->Z + 0.5f), P2))
		{
			const float R = FMath::Max(14.f, (float)FVector2D::Distance(P, P2)) * (1.f + 0.08f * FMath::Sin(NowS * 6));
			const FVector2f Ctr(P);
			for (int32 k = 0; k < 4; k++)
			{
				const FVector2f Sg(k % 2 ? 1.f : -1.f, k / 2 ? 1.f : -1.f);
				const FVector2f Cn = Ctr + Sg * R;
				D::Lines(Out, L + 3, G, { Cn - FVector2f(Sg.X * 7, 0), Cn, Cn - FVector2f(0, Sg.Y * 7) }, C::Red, 2.f);
			}
			const FVector2f Ts = D::Measure(TEXT("TARGET"), D::Label(7.5f));
			D::RFill(Out, L + 3, G, FVector2f(Ctr.X - Ts.X * 0.5f - 5, Ctr.Y + R + 5), FVector2f(Ts.X + 10, 14), 7, C::A(C::Red, 0.85f));
			D::Text(Out, L + 4, G, TEXT("TARGET"), D::Label(7.5f), FVector2f(Ctr.X, Ctr.Y + R + 6), C::Dark, 0.5f);
		}
	}

	for (const FBSFloater& Fl : Gm->Floaters)
	{
		FVector2D P;
		if (!WorldToLocal(BSSpace::ToWorld(Fl.Pos.X, Fl.Pos.Y, Fl.Pos.Z + Fl.T * 1.5f), P)) continue;
		D::Text(Out, L + 5, G, Fl.Text.ToUpper(), D::Label(12), FVector2f(P), C::A(Fl.Color, 1 - Fl.T / 1.3f), 0.5f, true);
	}

	// Radar, top-right below the command strip: only while a sensor tower stands.
	const bool bPortrait = S.Y > S.X;
	const bool bRadar = Gm->SensorUp();
	const float Rr = bPortrait ? 44 : 56;
	const FVector2f Rc(S.X - 10 - Rr, 66 + Rr + 4);
	if (bRadar) D::Disc(Out, L + 6, G, Rc, Rr, C::Glass, 48);
	if (bRadar) D::Circle(Out, L + 7, G, Rc, Rr * 0.5f, C::Faint, 1, 36);
	const float Sweep = (float)FMath::Fmod(NowS * 1.8, 2 * PI);
	if (bRadar) for (int32 k = 0; k < 6; k++)
	{
		const float A0 = Sweep - k * 0.09f;
		D::Poly(Out, L + 7, G, { Rc, Rc + FVector2f(FMath::Cos(A0), FMath::Sin(A0)) * Rr, Rc + FVector2f(FMath::Cos(A0 - 0.09f), FMath::Sin(A0 - 0.09f)) * Rr }, C::A(C::Cyan, 0.14f * (1 - k / 6.f)));
	}
	if (bRadar) D::Circle(Out, L + 8, G, Rc, Rr, C::A(C::Cyan, 0.35f), 1.f, 48);
	ABSCameraPawn* Cm = Cam();
	const float Yaw = Cm ? FMath::DegreesToRadians(Cm->GetYaw()) : 0;
	const float RangeM = Gm->Mode == EBSMode::Lane ? 32.f : 30.f;
	auto ToRadar = [&](float X, float Y) -> FVector2f
	{
		const float Fx = FMath::Cos(Yaw), Fy = FMath::Sin(Yaw);
		const float Wx = Y, Wy = X;
		const float Up = Wx * Fx + Wy * Fy, Right = -Wx * Fy + Wy * Fx;
		FVector2f P(Right, -Up);
		P *= Rr / RangeM;
		if (P.Size() > Rr - 3) P = P.GetSafeNormal() * (Rr - 3);
		return Rc + P;
	};
	if (bRadar) for (const TSharedPtr<FBSEnemy>& E : Gm->Enemies)
		D::Disc(Out, L + 9, G, ToRadar(E->X, E->Y), E->Def->Budget >= 16 ? 2.6f : 1.6f, E->Def->bFlying ? C::Amber : C::Violet, 8);
	if (bRadar) for (const TSharedPtr<FBSTank>& K : Gm->Tanks) D::Disc(Out, L + 9, G, ToRadar(K->X, K->Y), 2.2f, C::Cyan, 8);
	if (bRadar) for (const TSharedPtr<FBSStructure>& St : Gm->Structures) D::Disc(Out, L + 9, G, ToRadar(St->X, St->Y), 2.2f, C::Cyan, 8);
	if (bRadar) D::Disc(Out, L + 9, G, Rc, 3, C::Green, 10);

	// Threat markers on the screen edge for off-screen zerg, kept clear of the HUD.
	const float TopY = 150, BottomY = S.Y - (bPortrait ? (bArmouryOpen ? 300.f : 190.f) : 130.f);
	const float RightX = S.X - (bPortrait ? 16.f : 256.f);
	const FVector2f Mid((16 + RightX) * 0.5f, (TopY + BottomY) * 0.5f);
	TMap<int32, int32> Buckets;
	for (const TSharedPtr<FBSEnemy>& E : Gm->Enemies)
	{
		FVector2D P;
		if (WorldToLocal(BSSpace::ToWorld(E->X, E->Y, 0), P) && P.X >= 0 && P.Y >= 0 && P.X <= S.X && P.Y <= S.Y) continue;
		const FVector2f Rp = ToRadar(E->X, E->Y) - Rc;
		Buckets.FindOrAdd(FMath::RoundToInt(FMath::Atan2(Rp.Y, Rp.X) / (PI / 8)))++;
	}
	for (const auto& B : Buckets)
	{
		const float A0 = B.Key * PI / 8;
		const FVector2f Dir(FMath::Cos(A0), FMath::Sin(A0));
		const float Hx = (RightX - 16) * 0.5f - 14, Hy = (BottomY - TopY) * 0.5f - 14;
		const float T = FMath::Min(Dir.X != 0 ? Hx / FMath::Abs(Dir.X) : 1e9f, Dir.Y != 0 ? Hy / FMath::Abs(Dir.Y) : 1e9f);
		const FVector2f P = Mid + Dir * T;
		const FVector2f N(-Dir.Y, Dir.X);
		const float Pulse = 0.65f + 0.35f * FMath::Sin(NowS * 5 + B.Key);
		D::Poly(Out, L + 10, G, { P + Dir * 9, P - Dir * 4 + N * 7, P - Dir * 1 , P - Dir * 4 - N * 7 }, C::A(C::Red, Pulse));
		const FString Cnt = FString::FromInt(B.Value);
		const FVector2f Cs = D::Measure(Cnt, D::Bold(8.5f));
		const FVector2f Cp = P - Dir * 18;
		D::RFill(Out, L + 10, G, Cp - FVector2f(Cs.X * 0.5f + 5, 8), FVector2f(Cs.X + 10, 16), 8, C::A(C::Dark, 0.85f));
		D::Text(Out, L + 11, G, Cnt, D::Bold(8.5f), Cp - FVector2f(0, Cs.Y * 0.5f), C::Ink, 0.5f);
	}
	return L + 12;
}

// =====================================================================================================
// Input
// =====================================================================================================
void SBSHud::Tap(const FGeometry& Geo, const FVector2D& ScreenPos)
{
	ABSGame* Gm = Game();
	if (!Gm || !PC.IsValid()) return;
	if (TapSquadCard(ScreenPos)) return;
	const double Now = FPlatformTime::Seconds();
	if (Now - LastTapAt < 0.3) { if (ABSCameraPawn* C0 = Cam()) C0->ResetView(); LastTapAt = -1; return; }
	LastTapAt = Now;
	const FVector2D Px = Geo.AbsoluteToLocal(ScreenPos) * Geo.Scale;
	FVector Origin, Dir;
	if (!PC->DeprojectScreenPositionToWorld(Px.X, Px.Y, Origin, Dir) || FMath::Abs(Dir.Z) < 1e-4) return;
	const float T = -Origin.Z / Dir.Z;
	if (T < 0) return;
	const FVector Hit = Origin + Dir * T;
	if (Gm->FocusAt(Hit.Y / BSSpace::UnitsPerMetre, Hit.X / BSSpace::UnitsPerMetre)) Gm->Toast(TEXT("Focus fire"));
}

void SBSHud::ToggleBunkerView()
{
	ABSGame* Gm = Game(); ABSCameraPawn* Cv = Cam();
	if (!Gm || !Cv) return;
	const bool bOn = !Cv->IsBunkerView();
	Cv->SetBunkerView(bOn);
	Gm->bBunkerView = bOn;
	// Looking out from inside it: hide the bunker itself.
	if (Gm->Bunker.Actor && !Gm->Bunker.bDead) Gm->Bunker.Actor->SetActorHiddenInGame(bOn);
}

// Tap a trooper in the bunker to send them outside; tap one outside to bring them back in.
bool SBSHud::TapSquadCard(const FVector2D& ScreenPos)
{
	ABSGame* Gm = Game();
	if (!Gm || !SquadCard.IsValid()) return false;
	const FGeometry& CG = SquadCard->GetCachedGeometry();
	const FVector2f P = FVector2f(CG.AbsoluteToLocal(ScreenPos)), S = FVector2f(CG.GetLocalSize());
	if (P.X < 0 || P.Y < 0 || P.X > S.X || P.Y > S.Y) return false;
	auto In = [&P](const FVector2f& At, float Sz) { return P.X >= At.X - 3 && P.X <= At.X + Sz + 3 && P.Y >= At.Y - 3 && P.Y <= At.Y + Sz + 3; };
	if (In(FVector2f(10, 11), 48)) { ToggleBunkerView(); return true; }
	for (int32 i = 0; i < Gm->Bunker.Units.Num(); i++)
		if (In(FVector2f(SquadRow::X0 + i * 27.f, 41), 22)) { Gm->Eject(i); return true; }
	for (int32 i = 0; i < Gm->Squad.Num(); i++)
		if (In(SquadRow::At(i, S.X, Gm->Squad.Num()), SquadRow::Size(Gm->Squad.Num()))) { Gm->Board(i); return true; }
	return true;                                      // taps on the card never focus-fire
}

FReply SBSHud::OnMouseButtonDown(const FGeometry& Geo, const FPointerEvent& E)
{
	if (E.IsTouchEvent() || !Game()) return FReply::Unhandled();
	bMouseDown = true; bDragging = false;
	MouseStart = MouseLast = E.GetScreenSpacePosition();
	MouseDownAt = FPlatformTime::Seconds();
	return FReply::Handled().CaptureMouse(SharedThis(this)).SetUserFocus(SharedThis(this), EFocusCause::Mouse);
}

FReply SBSHud::OnMouseMove(const FGeometry& Geo, const FPointerEvent& E)
{
	if (E.IsTouchEvent() || !bMouseDown) return FReply::Unhandled();
	const FVector2D P = E.GetScreenSpacePosition();
	if (!bDragging && FVector2D::Distance(P, MouseStart) > 6) bDragging = true;
	if (bDragging) if (ABSCameraPawn* C0 = Cam()) C0->PanPixels((P - MouseLast).X, (P - MouseLast).Y, LocalSize.Y * DpiScale);
	MouseLast = P;
	return FReply::Handled();
}

FReply SBSHud::OnMouseButtonUp(const FGeometry& Geo, const FPointerEvent& E)
{
	if (E.IsTouchEvent() || !bMouseDown) return FReply::Unhandled();
	bMouseDown = false;
	if (!bDragging && FPlatformTime::Seconds() - MouseDownAt < 0.35) Tap(Geo, E.GetScreenSpacePosition());
	bDragging = false;
	return FReply::Handled().ReleaseMouseCapture();
}

FReply SBSHud::OnMouseWheel(const FGeometry& Geo, const FPointerEvent& E)
{
	if (!Game()) return FReply::Unhandled();
	if (ABSCameraPawn* C0 = Cam()) C0->ZoomBy(E.GetWheelDelta() < 0 ? 1.12f : 0.89f);
	return FReply::Handled();
}

FReply SBSHud::OnTouchStarted(const FGeometry& Geo, const FPointerEvent& E)
{
	if (!Game()) return FReply::Unhandled();
	FPtr P; P.Start = P.Last = E.GetScreenSpacePosition(); P.Down = FPlatformTime::Seconds();
	Touches.Add(E.GetPointerIndex(), P);
	if (Touches.Num() == 2)
	{
		TArray<FPtr> T; Touches.GenerateValueArray(T);
		PinchDist = FVector2D::Distance(T[0].Last, T[1].Last);
		PinchAngle = FMath::Atan2(T[1].Last.Y - T[0].Last.Y, T[1].Last.X - T[0].Last.X);
	}
	return FReply::Handled().CaptureMouse(SharedThis(this));
}

FReply SBSHud::OnTouchMoved(const FGeometry& Geo, const FPointerEvent& E)
{
	FPtr* P = Touches.Find(E.GetPointerIndex());
	if (!P) return FReply::Unhandled();
	const FVector2D NowP = E.GetScreenSpacePosition();
	const FVector2D Delta = NowP - P->Last;
	P->Last = NowP;
	ABSCameraPawn* C0 = Cam();
	if (Touches.Num() == 1)
	{
		if (FVector2D::Distance(NowP, P->Start) > 8 && C0) C0->PanPixels(Delta.X, Delta.Y, LocalSize.Y * DpiScale);
	}
	else if (Touches.Num() == 2 && C0)
	{
		TArray<FPtr> T; Touches.GenerateValueArray(T);
		const float Dd = FVector2D::Distance(T[0].Last, T[1].Last);
		const float Aa = FMath::Atan2(T[1].Last.Y - T[0].Last.Y, T[1].Last.X - T[0].Last.X);
		if (PinchDist > 0 && Dd > 0) C0->ZoomBy(PinchDist / Dd);
		float DA = Aa - PinchAngle;
		if (DA > PI) DA -= 2 * PI;
		if (DA < -PI) DA += 2 * PI;
		C0->Orbit(FMath::RadiansToDegrees(DA));
		PinchDist = Dd; PinchAngle = Aa;
		for (auto& Pair : Touches) Pair.Value.Start = FVector2D(-1e6, -1e6);     // a pinch is never a tap
	}
	return FReply::Handled();
}

FReply SBSHud::OnTouchEnded(const FGeometry& Geo, const FPointerEvent& E)
{
	FPtr P;
	if (!Touches.RemoveAndCopyValue(E.GetPointerIndex(), P)) return FReply::Unhandled();
	if (Touches.Num() == 0 && FVector2D::Distance(P.Last, P.Start) <= 8 && FPlatformTime::Seconds() - P.Down < 0.35) Tap(Geo, P.Last);
	if (Touches.Num() < 2) PinchDist = 0;
	return Touches.Num() ? FReply::Handled() : FReply::Handled().ReleaseMouseCapture();
}

FReply SBSHud::OnKeyDown(const FGeometry& Geo, const FKeyEvent& Key)
{
	ABSGame* Gm = Game();
	ABSCameraPawn* C0 = Cam();
	const FKey K = Key.GetKey();
	if (!Gm)
	{
		if (K == EKeys::One && GM.IsValid()) { GM->StartGame(EBSMode::Lane); return FReply::Handled(); }
		if (K == EKeys::Two && GM.IsValid()) { GM->StartGame(EBSMode::Arena); return FReply::Handled(); }
		return FReply::Unhandled();
	}
	const float H = LocalSize.Y * DpiScale;
	if (K == EKeys::P || K == EKeys::Escape) TogglePause();
	else if (K == EKeys::V) ToggleBunkerView();
	else if (K == EKeys::S) Gm->UseStim();
	else if (K == EKeys::A) Gm->CallAirStrike();
	else if (K == EKeys::R) Gm->EmergencyRepair();
	else if (K == EKeys::Y) Gm->UseYamato();
	else if (!C0) return FReply::Unhandled();
	else if (K == EKeys::Q) C0->Orbit(-15);
	else if (K == EKeys::E) C0->Orbit(15);
	else if (K == EKeys::Equals || K == EKeys::Add) C0->ZoomBy(0.85f);
	else if (K == EKeys::Hyphen || K == EKeys::Subtract) C0->ZoomBy(1.18f);
	else if (K == EKeys::Up) C0->PanPixels(0, 40, H);
	else if (K == EKeys::Down) C0->PanPixels(0, -40, H);
	else if (K == EKeys::Left) C0->PanPixels(40, 0, H);
	else if (K == EKeys::Right) C0->PanPixels(-40, 0, H);
	else if (K == EKeys::Home) C0->ResetView();
	else return FReply::Unhandled();
	return FReply::Handled();
}

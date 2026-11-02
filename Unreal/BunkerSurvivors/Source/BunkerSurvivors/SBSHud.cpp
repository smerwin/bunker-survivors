#include "SBSHud.h"
#include "BSGameMode.h"
#include "BSGame.h"
#include "BSAssets.h"
#include "BSCameraPawn.h"
#include "BSModelActor.h"
#include "BSMeta.h"
#include "BSTerrain.h"
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
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Engine/Texture2D.h"
#include "TextureResource.h"

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

	// Embossed display text (big titles, the wave banner): a dark edge below with a soft shadow, a faint lit edge above.
	void EText(FSlateWindowElementList& Out, int32 L, const FGeometry& G, const FString& S, const FSlateFontInfo& F, FVector2f Pos, const FLinearColor& Col, float Align = 0.f)
	{
		const FVector2f Sz = Measure(S, F);
		const FVector2f P(Pos.X - Sz.X * Align, Pos.Y);
		auto Ghost = [&](FVector2f Off, const FLinearColor& Gc) { FSlateDrawElement::MakeText(Out, L, G.ToPaintGeometry(Sz, FSlateLayoutTransform(P + Off)), S, F, ESlateDrawEffect::None, Gc); };
		const FLinearColor Shade = C::A(FLinearColor::Black, Col.A);
		Ghost(FVector2f(-1, 2), C::A(Shade, 0.16f)); Ghost(FVector2f(1, 2), C::A(Shade, 0.16f)); Ghost(FVector2f(0, 3), C::A(Shade, 0.2f));
		Ghost(FVector2f(0, 1), C::A(Shade, 0.85f));
		Ghost(FVector2f(0, -1), C::A(FLinearColor::White, 0.12f * Col.A));
		FSlateDrawElement::MakeText(Out, L + 1, G.ToPaintGeometry(Sz, FSlateLayoutTransform(P)), S, F, ESlateDrawEffect::None, Col);
	}

	// ---- Wear: a weathered, gently sandblasted finish for the big panels --------------------------------------------
	// One 256px tile generated at startup, seamless (everything wraps): fine light sand grain, faint long diagonal
	// scratches and small dark pits with a lit rim on their lower-right edge (tiny impact craters). RGB is the colour and
	// alpha the strength; it is tiled at one texel per screen pixel whatever the UI scale.
	constexpr int32 GritSize = 256;
	UTexture2D* GritTexture()
	{
		static UTexture2D* Tex = nullptr;
		static bool bTried = false;
		if (bTried) return Tex;
		bTried = true;
		const int32 N = GritSize;
		auto Wrap = [N](float V) { return V - N * FMath::RoundToFloat(V / N); };       // toroidal offset in [-N/2, N/2]
		auto Idx = [N](int32 X, int32 Y) { return ((Y % N + N) % N) * N + ((X % N + N) % N); };
		TArray<FLinearColor> Px; Px.Init(FLinearColor::Transparent, N * N);
		auto Over = [](FLinearColor& Dst, const FLinearColor& Col, float A)
		{
			if (A <= 0.f) return;
			const float Oa = A + Dst.A * (1 - A);
			Dst = FLinearColor((Col.R * A + Dst.R * Dst.A * (1 - A)) / Oa, (Col.G * A + Dst.G * Dst.A * (1 - A)) / Oa, (Col.B * A + Dst.B * Dst.A * (1 - A)) / Oa, Oa);
		};
		FRandomStream Rs(1977);
		// Sand grain: sparse, since any lift averages out to a grey wash over the dark glass.
		for (FLinearColor& P : Px) Over(P, FLinearColor::White, FMath::Clamp((Rs.FRand() - 0.72f) * 0.4f, 0.f, 0.11f));
		// Scratches, mostly running up to the right.
		for (int32 k = 0; k < 16; k++)
		{
			const FVector2f S0(Rs.FRand() * N, Rs.FRand() * N);
			const float Ang = -0.3f + Rs.FRandRange(-0.12f, 0.12f), Len = Rs.FRandRange(24.f, 120.f), Str = Rs.FRandRange(0.05f, 0.12f);
			const FVector2f Dir(FMath::Cos(Ang), FMath::Sin(Ang));
			for (int32 y = 0; y < N; y++)
				for (int32 x = 0; x < N; x++)
				{
					const float Dx = Wrap(x - S0.X), Dy = Wrap(y - S0.Y);
					const float T = Dx * Dir.X + Dy * Dir.Y;
					if (T < 0 || T > Len) continue;
					const float Dist = FMath::Abs(Dx * Dir.Y - Dy * Dir.X);
					if (Dist < 1.f) Over(Px[y * N + x], FLinearColor::White, Str * (1 - Dist) * FMath::Sqrt(FMath::Sin(PI * T / Len)));
				}
		}
		// Pits: mostly tiny, a few up to ~5px across, slightly oval.
		TArray<float> Pit; Pit.Init(0.f, N * N);
		for (int32 k = 0; k < 180; k++)
		{
			const FVector2f Ctr(Rs.FRand() * N, Rs.FRand() * N);
			const float R = 0.45f + 2.f * FMath::Pow(Rs.FRand(), 2.5f), E = Rs.FRandRange(0.7f, 1.3f);
			const int32 Ext = FMath::CeilToInt(R * 1.4f) + 1;
			for (int32 y = FMath::FloorToInt(Ctr.Y) - Ext; y <= FMath::FloorToInt(Ctr.Y) + Ext; y++)
				for (int32 x = FMath::FloorToInt(Ctr.X) - Ext; x <= FMath::FloorToInt(Ctr.X) + Ext; x++)
				{
					const float Dx = (x + 0.5f - Ctr.X) * E, Dy = (y + 0.5f - Ctr.Y) / E;
					float& P = Pit[Idx(x, y)];
					P = FMath::Max(P, FMath::Clamp(R - FMath::Sqrt(Dx * Dx + Dy * Dy) + 0.5f, 0.f, 1.f));
				}
		}
		for (int32 y = 0; y < N; y++)
			for (int32 x = 0; x < N; x++)
			{
				const float P = Pit[y * N + x], Rim = Pit[Idx(x - 1, y - 1)] * (1 - P);
				Over(Px[y * N + x], FLinearColor::White, 0.45f * Rim);
				Over(Px[y * N + x], FLinearColor(0.f, 0.02f, 0.04f), 0.7f * P);
			}
		Tex = UTexture2D::CreateTransient(N, N, PF_B8G8R8A8, TEXT("BSGrit"));
		if (!Tex) return nullptr;
		Tex->SRGB = true;
		Tex->Filter = TF_Bilinear;
		Tex->AddressX = TA_Wrap; Tex->AddressY = TA_Wrap;
		Tex->NeverStream = true;
		auto& Mip = Tex->GetPlatformData()->Mips[0];
		FColor* Dst = static_cast<FColor*>(Mip.BulkData.Lock(LOCK_READ_WRITE));
		for (int32 i = 0; i < N * N; i++) Dst[i] = Px[i].ToFColor(true);
		Mip.BulkData.Unlock();
		Tex->UpdateResource();
		Tex->AddToRoot();      // lives as long as the game; the HUD's Slate brush points at it
		return Tex;
	}
	const FSlateBrush* GritBrush()
	{
		static FSlateBrush B;
		static bool bInit = false;
		if (!bInit)
		{
			bInit = true;
			B.DrawAs = ESlateBrushDrawType::Image;
			B.Tiling = ESlateBrushTileType::Both;
			B.ImageSize = FVector2D(GritSize, GritSize);
			if (UTexture2D* T = GritTexture()) B.SetResourceObject(T); else B.DrawAs = ESlateBrushDrawType::NoDrawType;
		}
		return &B;
	}
	// The grit tile over a panel, kept inside its rounded corners (inset 0.3R keeps the rectangle's corners inside the arcs).
	void Grit(FSlateWindowElementList& Out, int32 L, const FGeometry& G, FVector2f Pos, FVector2f Size, float R, float Strength = 1.f)
	{
		const FSlateBrush* B = GritBrush();
		if (B->DrawAs == ESlateBrushDrawType::NoDrawType || Strength <= 0) return;
		const float In = R * 0.3f;
		const FVector2f P = Pos + FVector2f(In, In), S = Size - FVector2f(In, In) * 2;
		if (S.X <= 0 || S.Y <= 0) return;
		// Tiles count in local units, so draw at the pixel scale: a local size of S * scale shrunk back by 1 / scale.
		const float K = FMath::Max(GMeasureScale, 0.1f);
		FSlateDrawElement::MakeBox(Out, L, G.ToPaintGeometry(S * K, FSlateLayoutTransform(1.f / K, P)), B, ESlateDrawEffect::None, FLinearColor(1, 1, 1, Strength));
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

	// 10810 -> "10,810".
	FString Thousands(int64 V)
	{
		FString Digits = FString::Printf(TEXT("%lld"), V < 0 ? -V : V), R;
		for (int32 i = 0; i < Digits.Len(); i++)
		{
			if (i > 0 && (Digits.Len() - i) % 3 == 0) R.AppendChar(',');
			R.AppendChar(Digits[i]);
		}
		return V < 0 ? TEXT("-") + R : R;
	}

	// A little 3D: a faint lit inner top edge and a darker inner bottom edge.
	void Bevel(FSlateWindowElementList& Out, int32 L, const FGeometry& G, FVector2f Pos, FVector2f Size, float R, float Strength = 1.f)
	{
		const FPts P = RoundedPath(Pos + FVector2f(1, 1), Size - FVector2f(2, 2), FMath::Max(0.f, R - 1));
		FPts Top, Bottom;
		for (int32 i = 21; i < 28; i++) Top.Add(P[i]);
		for (int32 i = 0; i < 7; i++) Top.Add(P[i]);
		for (int32 i = 7; i < 21; i++) Bottom.Add(P[i]);
		Lines(Out, L, G, Top, C::A(FLinearColor::White, 0.08f * Strength), 1.f);
		Lines(Out, L, G, Bottom, C::A(FLinearColor::Black, 0.3f * Strength), 1.5f);
	}

	// A large worn panel: soft drop shadow, the fill, then (one layer up) the grit and the bevel.
	void Panel(FSlateWindowElementList& Out, int32 L, const FGeometry& G, FVector2f Pos, FVector2f Size, float R, const FLinearColor& Col, float GritK = 0.5f)
	{
		for (int32 i = 1; i <= 3; i++)
			RFill(Out, L, G, Pos + FVector2f(-i * 0.5f, 1.f + i), Size + FVector2f(i, 0), R + i * 0.5f, C::A(FLinearColor::Black, 0.05f * Col.A));
		RFill(Out, L, G, Pos, Size, R, Col);
		Grit(Out, L + 1, G, Pos, Size, R, GritK * Col.A);
		Bevel(Out, L + 1, G, Pos, Size, R, 0.8f);
	}
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
			D::Bevel(Out, L, G, P, S - FVector2f(0, 2), 12, 1.5f);
			D::Text(Out, L + 1, G, Label, D::Label(12), FVector2f(S.X * 0.5f, P.Y + 13), C::Dark, 0.5f);
		}
		else
		{
			D::RFill(Out, L, G, P, S - FVector2f(0, 2), 12, bHover ? C::GlassHi : C::A(C::Glass, 0.6f));
			D::RLine(Out, L + 1, G, P, S - FVector2f(0, 2), 12, C::A(C::Cyan, bHover ? 0.9f : 0.45f), 1.f);
			D::Bevel(Out, L + 1, G, P, S - FVector2f(0, 2), 12, 0.6f);
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
	bHyperion = FParse::Param(FCommandLine::Get(), TEXT("BSHyperion"));     // playtesting: open on the Hyperion

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
			// Swarm mode: the brood button, top right under the command strip.
			+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Top).Padding(0, 64, 10, 0)
			[
				SNew(SBox).Visibility_Lambda([this]() { ABSGame* Gm = Game(); return Gm && Gm->bSwarm ? EVisibility::Visible : EVisibility::Collapsed; })[ BuildBroodButton() ]
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
		+ SOverlay::Slot()[ BuildShop() ]
		+ SOverlay::Slot()[ BuildModal(false) ]
		+ SOverlay::Slot()[ BuildModal(true) ]
		+ SOverlay::Slot()[ BuildStartScreen() ]
		+ SOverlay::Slot()[ BuildHyperion() ]
		+ SOverlay::Slot()[ BuildZeratul() ]
	];
}

// =====================================================================================================
// Command strip: minerals, wave, the Hyperion's ETA, kills, score, sound and pause
// =====================================================================================================
TSharedRef<SWidget> SBSHud::BuildCommandStrip()
{
	auto Strip = SNew(SBSPaint).Size(FVector2D(10, 50)).Paint([this](const FGeometry& G, FSlateWindowElementList& Out, int32 L)
	{
		ABSGame* Gm = Game(); if (!Gm) return L;
		const float W = G.GetLocalSize().X - 96;      // the two icon buttons sit to the right
		const bool bWide = W > 460;
		// Tapered glass strip with a cyan accent.
		D::Panel(Out, L, G, FVector2f(0, 0), FVector2f(W, 50), 12, C::Glass);
		D::RLine(Out, L + 1, G, FVector2f(0, 0), FVector2f(W, 50), 12, C::Hair);
		D::Rect(Out, L + 1, G, FVector2f(14, 0), FVector2f(36, 2), C::Cyan);

		auto Section = [&](float X, float Wd, const FString& Label, const FString& Value, const FLinearColor& Col, float Shake = 0)
		{
			D::Text(Out, L + 2, G, Label, D::Label(8.5f), FVector2f(X, 8), C::Muted);
			D::Text(Out, L + 2, G, Value, D::Fit(Value, [](float S) { return D::Num(S); }, 20, 12, Wd - 8), FVector2f(X + Shake, 19), Col);
		};
		// Swarm mode: the swarm's minerals, its vespene, and its kills; a narrow strip drops the wave and kills (as the
		// browser's phone layout does).
		const bool bSwm = Gm->bSwarm;
		const bool bWaveKills = !bSwm || bWide;
		const int32 N = bSwm ? (bWide ? 5 : 3) : bWide ? 5 : 4;
		const float Sw = W / N;
		int32 Slot = 0;
		// Minerals.
		{
			const float Pulse = MineralFlash;
			D::Crystal(Out, L + 2, G, FVector2f(20, 30), 9, FMath::Lerp(C::Cyan, FLinearColor::White, Pulse * 0.6f));
			const FLinearColor Col = FMath::Lerp(FMath::Lerp(C::Ink, C::Cyan, Pulse), C::Red, MineralFail);
			Section(36, Sw - 36, TEXT("MINERALS"), D::Int(ShownMinerals), Col, MineralFail * FMath::Sin(NowS * 60) * 3);
			Slot++;
		}
		if (bSwm)
		{
			const float X = Sw * Slot++;
			D::Line(Out, L + 2, G, FVector2f(X, 12), FVector2f(X, 38), C::Hair);
			D::Crystal(Out, L + 2, G, FVector2f(X + 18, 30), 9, C::Green);
			Section(X + 34, Sw - 36, TEXT("VESPENE"), D::Int(FMath::FloorToFloat(Gm->Sw.Gas)), C::Ink);
		}
		// Wave with a ring counting down to the next one.
		if (bWaveKills)
		{
			const float X = Sw * Slot++;
			D::Line(Out, L + 2, G, FVector2f(X, 12), FVector2f(X, 38), C::Hair);
			Section(X + 14, Sw - 50, TEXT("WAVE"), FString::Printf(TEXT("%02d"), Gm->Wave), C::Amber);
			const FVector2f Rc(X + Sw - 24, 25);
			const float Frac = FMath::Clamp(Gm->WaveTimer / FMath::Max(1.f, Gm->WaveLen), 0.f, 1.f);
			D::Circle(Out, L + 2, G, Rc, 13, C::Faint, 2.5f);
			D::Circle(Out, L + 3, G, Rc, 13, Frac < 0.2f ? C::Red : C::Cyan, 2.5f, 32, -PI / 2, -PI / 2 + 2 * PI * Frac);
			D::Text(Out, L + 4, G, FString::FromInt(FMath::Max(0, FMath::CeilToInt(Gm->WaveTimer))), D::Bold(9), FVector2f(Rc.X, Rc.Y - 7), C::Ink, 0.5f);
		}
		// Time left until the Hyperion takes everyone off (amber from the finale on).
		{
			const float X = Sw * Slot++;
			const int32 Ev = FMath::CeilToInt(Gm->EvacIn());
			D::Line(Out, L + 2, G, FVector2f(X, 12), FVector2f(X, 38), C::Hair);
			Section(X + 14, Sw - 18, TEXT("HYPERION"), FString::Printf(TEXT("%02d:%02d"), Ev / 60, Ev % 60), Ev <= BSDefs::EvacAt - BSDefs::FinaleAt ? C::Amber : C::Ink);
		}
		// Kills.
		if (bWaveKills)
		{
			const float X = Sw * Slot++;
			D::Line(Out, L + 2, G, FVector2f(X, 12), FVector2f(X, 38), C::Hair);
			Section(X + 14, Sw - 18, TEXT("KILLS"), D::Int(bSwm ? Gm->Sw.Kills : Gm->Kills), C::Ink);
		}
		if (bWide && !bSwm)
		{
			const float X = Sw * Slot++;
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
			D::Bevel(Out, L, G, FVector2f(0, 0), FVector2f(42, 50), 12, 0.6f);
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
			const float BcRow = Gm && Gm->Bc.IsValid() ? 18.f : 0.f;       // the battlecruiser's hull bar, while it is deployed
			return FVector2D(Wd, 70 + (Rows ? Rows * (SquadRow::Size(N) + SquadRow::Gap(N) + 1) + 4 : 0) + BcRow);
		})
		.Paint([this](const FGeometry& G, FSlateWindowElementList& Out, int32 L)
		{
			ABSGame* Gm = Game(); if (!Gm) return L;
			const FVector2f S = FVector2f(G.GetLocalSize());
			const FBSBunker& B = Gm->Bunker;
			const float Pct = FMath::Clamp(B.Hp / FMath::Max(1.f, B.MaxHp), 0.f, 1.f);
			const bool bCritical = Pct < 0.25f && !B.bDead;
			const float Pulse = bCritical ? 0.5f + 0.5f * FMath::Sin(NowS * 8) : 0.f;
			D::Panel(Out, L, G, FVector2f(0, 0), S, 12, C::Glass);
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
			// Battlecruiser hull, only while it is deployed: a slim bar along the bottom of the card that flashes when hit.
			if (Gm->Bc.IsValid())
			{
				const FBSBattlecruiser& Bcr = *Gm->Bc;
				const float Bf = FMath::Clamp(Bcr.Hp / FMath::Max(1.f, Bcr.MaxHp), 0.f, 1.f);
				const bool bHit = NowS - BcHitAt < 0.25;
				const FLinearColor BcCol = Bf > 0.5f ? C::Cyan : Bf > 0.25f ? C::Amber : C::Red;
				const float Y = S.Y - 18, Bx = X0 + 96, Bw2 = FMath::Max(20.f, Bw - 96 - 62);
				D::Text(Out, L + 2, G, Bf <= 0.25f ? TEXT("BC HULL CRITICAL") : TEXT("BATTLECRUISER"), D::Label(7.5f), FVector2f(X0, Y), Bf <= 0.25f ? C::Red : C::Cyan);
				D::RFill(Out, L + 2, G, FVector2f(Bx, Y + 3), FVector2f(Bw2, 5), 2, C::Faint);
				D::RFill(Out, L + 3, G, FVector2f(Bx, Y + 3), FVector2f(Bw2 * Bf, 5), 2, BcCol);
				if (bHit) D::RLine(Out, L + 4, G, FVector2f(Bx - 1, Y + 2), FVector2f(Bw2 + 2, 7), 3, C::Red, 1.f);
				D::Text(Out, L + 2, G, FString::Printf(TEXT("%d / %d"), FMath::CeilToInt(FMath::Max(0.f, Bcr.Hp)), FMath::RoundToInt(Bcr.MaxHp)), D::Bold(8.5f), FVector2f(X0 + Bw, Y - 1), C::Ink, 1.f);
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
			const float MaxCd = Index == 1 ? Gm->StrikeCooldown() : Ab.MaxCd;
			D::Pie(Out, L + 3, G, Ip + FVector2f(Is, Is) * 0.5f, Is * 0.5f - 3, FMath::Min(1.f, Cd / MaxCd), C::A(FLinearColor::Black, 0.68f));
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

// Swarm mode: red light, green light. HOLDING (massed / cap) while the broods wait at the hive; ATTACK (massed) once
// released with some still there; ATTACKING otherwise. A tap (or Space) toggles.
TSharedRef<SWidget> SBSHud::BuildBroodButton()
{
	const FVector2D Size(96, 46);       // compact: it sits in the strip the squad card leaves free on the right
	return PaintedButton(Size, [this, Size](const FGeometry& G, FSlateWindowElementList& Out, int32 L, bool bHover, bool bPress)
	{
		ABSGame* Gm = Game(); if (!Gm) return;
		const int32 N = Gm->HeldCount();
		const bool bHold = Gm->Sw.bHold;
		const FLinearColor Acc = bHold ? C::Amber : N ? C::Red : C::Green;
		const FVector2f S((float)Size.X, (float)Size.Y), P(0, bPress ? 1.f : 0.f);
		D::RFill(Out, L, G, P, S, 12, bHover ? C::GlassHi : C::Glass);
		D::RLine(Out, L + 1, G, P, S, 12, C::A(Acc, bHover ? 1.f : 0.6f + (N && !bHold ? 0.3f * FMath::Sin(NowS * 6) : 0.f)), 1.5f);
		const FString Big = bHold ? TEXT("HOLDING") : N ? TEXT("ATTACK") : TEXT("ATTACKING");
		const FString Small = bHold ? FString::Printf(TEXT("%d / %d MASSED"), N, Gm->HoldCap()) : N ? FString::Printf(TEXT("%d MASSED"), N) : FString(TEXT("TAP TO HOLD"));
		D::Text(Out, L + 2, G, Big, D::Fit(Big, [](float Sz) { return D::Title(Sz); }, 12, 7, S.X - 10), P + FVector2f(S.X * 0.5f, 7), Acc, 0.5f);
		D::Text(Out, L + 2, G, Small, D::Fit(Small, [](float Sz) { return D::Label(Sz); }, 7.5f, 5.5f, S.X - 8), P + FVector2f(S.X * 0.5f, 28), C::Muted, 0.5f);
	}, [this]() { if (ABSGame* Gm = Game()) Gm->BroodHold(); });
}

// The Protoss campaign, in full: Zeratul on a loop, YTMND style. Over the live model (ABSGame::SetupZeratul): a turning,
// breathing wallpaper of his portrait, huge outlined words cycling in rainbow colours, and EN TARO ADUN to leave.
TSharedRef<SWidget> SBSHud::BuildZeratul()
{
	auto Vis = [this]() { return GM.IsValid() && GM->IsZeratul() ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed; };
	auto Page = SNew(SBSPaint).Visibility(EVisibility::HitTestInvisible).Paint([this](const FGeometry& G, FSlateWindowElementList& Out, int32 L)
	{
		const FVector2f S = FVector2f(G.GetLocalSize());
		const float T = (float)FMath::Fmod(NowS, 10000.0);
		// The wallpaper: his portrait tiled, turning and breathing.
		if (const FSlateBrush* B = GM.IsValid() && GM->GetAssets() ? GM->GetAssets()->IconBrush(TEXT("btn-unit-protoss-zeratul")) : nullptr)
		{
			const float K = 1 + 0.25f * FMath::Sin(T * 1.7f), Tile = 96 * K, R = S.Size() * 0.5f;
			// The whole wallpaper turns about the middle of the screen.
			const FGeometry Wall = G.MakeChild(S, FSlateLayoutTransform(), FSlateRenderTransform(FQuat2D(T * 0.6f)), FVector2f(0.5f, 0.5f));
			for (float X = S.X * 0.5f - R; X < S.X * 0.5f + R; X += Tile)
				for (float Y = S.Y * 0.5f - R; Y < S.Y * 0.5f + R; Y += Tile)
					FSlateDrawElement::MakeBox(Out, L, Wall.ToPaintGeometry(FVector2f(Tile, Tile), FSlateLayoutTransform(FVector2f(X, Y))), B, ESlateDrawEffect::None, FLinearColor(1, 1, 1, 0.16f));
		}
		static const TCHAR* Words[] = { TEXT("ZERATUL"), TEXT("YOU HAVE COME FAR"), TEXT("THE PROTOSS CAMPAIGN"), TEXT("THIS IS IT"), TEXT("ADUN TORO"), TEXT("HE BLINKED"),
			TEXT("BEHIND YOU"), TEXT("VOID SHADES"), TEXT("NO SUPPLY DEPOT"), TEXT("ZERATUL") };
		const FString Word = Words[FMath::FloorToInt(T / 2.2f) % UE_ARRAY_COUNT(Words)];
		const float Pulse = 1 + 0.12f * FMath::Sin(T * 9);
		for (int32 k = 0; k < 2; k++)
		{
			const FString Txt = k == 0 ? Word : Word == TEXT("ZERATUL") ? FString(TEXT("EN TARO ZERATUL")) : FString(TEXT("ZERATUL"));
			const float Y = S.Y * (k == 0 ? 0.18f : 0.84f);
			FSlateFontInfo F = D::Fit(Txt, [](float Sz) { return D::Title(Sz); }, S.Y / 7, 8, S.X * 0.86f / Pulse);
			F.Size *= Pulse;
			F.OutlineSettings.OutlineSize = FMath::Max(3, FMath::RoundToInt(F.Size / 9));
			F.OutlineSettings.OutlineColor = FLinearColor::Black;
			const FLinearColor Col = FLinearColor::MakeFromHSV8((uint8)FMath::Fmod(T * 85 + k * 128, 256.f), 255, 255);
			const FVector2f Ts = D::Measure(Txt, F);
			const FGeometry Child = G.MakeChild(Ts, FSlateLayoutTransform(FVector2f(S.X * 0.5f - Ts.X * 0.5f, Y - Ts.Y * 0.5f)), FSlateRenderTransform(FQuat2D(FMath::Sin(T * 3) * 0.05f)), FVector2f(0.5f, 0.5f));
			FSlateDrawElement::MakeText(Out, L + 2, Child.ToPaintGeometry(), Txt, F, ESlateDrawEffect::None, Col);
		}
		return L + 4;
	});
	return SNew(SOverlay).Visibility_Lambda(Vis)
		+ SOverlay::Slot()[ Page ]
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Bottom).Padding(0, 0, 0, 24)
		[
			MakeButton(TEXT("EN TARO ADUN"), true, [this]() { if (GM.IsValid()) GM->Restart(false); }, 200)
		];
}

// Swarm mode: Kerrigan's button first (summon her; once she is out, send her back to advancing with the swarm), then her
// spells.
void SBSHud::HeroButton()
{
	ABSGame* Gm = Game(); if (!Gm) return;
	if (Gm->HeroAlive()) Gm->HeroAdvance(); else Gm->SwarmBuy(TEXT("hero"), FString());
}

TSharedRef<SWidget> SBSHud::MakeSwarmAbility(int32 Index)
{
	// Seven tiles, narrower than the Terran four so the bar fits a phone's width.
	const FVector2D Size(56, 92);
	return PaintedButton(Size, [this, Index](const FGeometry& G, FSlateWindowElementList& Out, int32 L, bool bHover, bool bPress)
	{
		ABSGame* Gm = Game(); if (!Gm) return;
		const FBSEnemy* E = Gm->HeroAlive();
		FString Name, Icon, Key, Sub;
		FLinearColor Accent = FLinearColor(1.f, 0.48f, 0.85f);
		float Cd = 0, Full = 1;
		bool bLocked = false, bPoor = false, bActive = false;
		if (Index < 0)
		{
			Icon = TEXT("btn-unit-zerg-kerriganinfested"); Key = TEXT("K");
			const FString St = Gm->SwarmState(TEXT("hero"), FString());
			if (E) { Sub = FString::Printf(TEXT("%d%%"), FMath::CeilToInt(E->Hp / FMath::Max(1.f, E->MaxHp) * 100)); bActive = !E->bOrder && !E->bHold; }
			else if (St == TEXT("locked")) { Sub = TEXT("HIVE"); bLocked = true; }
			else if (St == TEXT("wait")) { Sub = TEXT("RECOVERING"); bLocked = true; Cd = Gm->Sw.HeroAt - Gm->Time; Full = BSSwarm::HeroRespawn; }
			else { const BSSwarm::FCost& C0 = BSSwarm::HeroCost; Sub = FString::Printf(TEXT("%d + %dg"), C0.M, C0.G); bPoor = Gm->Sw.Minerals < C0.M || Gm->Sw.Gas < C0.G; }
			Name = E ? (E->bOrder || E->bHold ? TEXT("ADVANCE") : TEXT("KERRIGAN")) : TEXT("SUMMON");
		}
		else
		{
			const BSSwarm::FAbility& A = BSSwarm::Abilities()[Index];
			Name = A.Name; Icon = A.Icon; Key = A.Key; Accent = A.Accent;
			const FString Id = A.Id;
			Full = Id == TEXT("leap") ? BSDefs::LeapCd : Id == TEXT("apocalypse") ? BSDefs::ApocCd : Id == TEXT("kinetic") ? BSDefs::KineticCd : Id == TEXT("grip") ? BSDefs::GripCd
				: Id == TEXT("chain") ? BSDefs::ChainCd : BSDefs::MutationCd;
			if (!E) bLocked = true;
			else { Cd = Gm->HeroCd(Id); Sub = A.Key; }
		}
		const bool bReady = !bLocked && !bPoor && Cd <= 0;
		const float W = G.GetLocalSize().X, Is = 44;
		const FVector2f Ip((W - Is) * 0.5f, 4 + (bPress ? 1.f : 0.f));
		if (bReady)
		{
			const float Glow = 0.5f + 0.5f * FMath::Sin(NowS * 3.5 + Index);
			D::RLine(Out, L, G, Ip - FVector2f(4, 4), FVector2f(Is + 8, Is + 8), 16, C::A(Accent, 0.18f + 0.3f * Glow), 2);
		}
		D::RFill(Out, L + 1, G, Ip - FVector2f(2, 2), FVector2f(Is + 4, Is + 4), 13, C::Glass);
		const FLinearColor Tint = (bLocked || bPoor) ? FLinearColor(0.32f, 0.34f, 0.38f) : FLinearColor::White;
		if (const FSlateBrush* B = Gm->GetAssets()->IconBrush(Icon)) D::Image(Out, L + 2, G, B, Ip + FVector2f(3, 3), FVector2f(Is - 6, Is - 6), Tint);
		else D::Text(Out, L + 2, G, Name.Left(2), D::Title(18), Ip + FVector2f(Is * 0.5f, Is * 0.5f - 12), C::A(Accent, bLocked ? 0.4f : 0.9f), 0.5f);   // icon not exported
		if (Cd > 0)
		{
			D::Pie(Out, L + 3, G, Ip + FVector2f(Is, Is) * 0.5f, Is * 0.5f - 3, FMath::Min(1.f, Cd / FMath::Max(0.01f, Full)), C::A(FLinearColor::Black, 0.68f));
			D::Text(Out, L + 4, G, FString::FromInt(FMath::CeilToInt(Cd)), D::Num(18), Ip + FVector2f(Is * 0.5f, Is * 0.5f - 13), C::Ink, 0.5f, true);
		}
		if (bActive) D::RLine(Out, L + 4, G, Ip - FVector2f(2, 2), FVector2f(Is + 4, Is + 4), 13, C::A(Accent, 0.5f + 0.5f * FMath::Sin(NowS * 6)), 2);
		D::RLine(Out, L + 4, G, Ip - FVector2f(2, 2), FVector2f(Is + 4, Is + 4), 13, bHover ? C::A(Accent, 0.95f) : C::A(Accent, bReady ? 0.6f : 0.25f), bHover ? 1.5f : 1.f);
		if (!Portrait())
		{
			D::RFill(Out, L + 5, G, Ip + FVector2f(-4, -2), FVector2f(15, 15), 4, C::Dark);
			D::Text(Out, L + 6, G, Key, D::Bold(8.5f), Ip + FVector2f(3.5f, -1), C::Muted, 0.5f);
		}
		const FSlateFontInfo Nf = D::Fit(Name, [](float S) { return D::Label(S); }, 9, 5, W - 2);
		D::Text(Out, L + 2, G, Name, Nf, FVector2f(W * 0.5f, Is + 10), bLocked ? C::Muted : C::Ink, 0.5f, true);
		if (!Sub.IsEmpty() && D::Measure(Sub, D::Label(8)).X > W - 8 && !(Index < 0 && !E && !bLocked))
			D::Text(Out, L + 2, G, Sub, D::Fit(Sub, [](float Sz) { return D::Label(Sz); }, 8, 5.5f, W - 2), FVector2f(W * 0.5f, Is + 27), bLocked ? C::Muted : Accent, 0.5f);
		else if (!Sub.IsEmpty())
		{
			// (Kerrigan's price is too long for a pill on these tiles: plain text, fitted.)
			if (Index < 0 && !E && !bLocked) D::Text(Out, L + 2, G, Sub, D::Fit(Sub, [](float Sz) { return D::Bold(Sz); }, 8.5f, 5.5f, W - 2), FVector2f(W * 0.5f, Is + 27), bPoor ? C::Red : C::Cyan, 0.5f);
			else D::CostPill(Out, L + 2, G, FVector2f(W * 0.5f, Is + 33), Sub, false, bLocked ? C::Muted : Accent);
		}
	},
	[this, Index]()
	{
		ABSGame* Gm = Game(); if (!Gm) return;
		if (Index < 0) HeroButton(); else Gm->HeroCast(BSSwarm::Abilities()[Index].Id);
	});
}

TSharedRef<SWidget> SBSHud::BuildAbilityRow()
{
	TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
	// Swarm mode: Kerrigan and her spells instead of the Terran abilities.
	auto SwarmVis = [this](bool bSwarmItem) { ABSGame* Gm = Game(); return (Gm && Gm->bSwarm == bSwarmItem) ? EVisibility::Visible : EVisibility::Collapsed; };
	for (int32 i = -1; i < BSSwarm::Abilities().Num(); i++) Row->AddSlot().AutoWidth().Padding(1, 0)[ SNew(SBox).Visibility_Lambda([SwarmVis]() { return SwarmVis(true); })[ MakeSwarmAbility(i) ] ];
	for (int32 i = 0; i < Abilities().Num(); i++) Row->AddSlot().AutoWidth().Padding(6, 0)[ SNew(SBox).Visibility_Lambda([SwarmVis]() { return SwarmVis(false); })[ MakeAbility(i) ] ];
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
	TFunction<bool(ABSGame*)> Hidden;       // high tech stays hidden until a shop card unlocks it
	bool bTech = false;
	// Swarm mode's hive cluster (bSwarm items show only there, the rest only outside it): Price ("m + gg"), Poor (short of
	// either), Note (a count or level, shown in place of pips).
	bool bSwarm = false;
	TFunction<FString(ABSGame*)> Price;
	TFunction<bool(ABSGame*)> Poor;
	TFunction<FString(ABSGame*)> Note;
	bool IsHidden(ABSGame* G) const { return G->bSwarm != bSwarm || (Hidden && Hidden(G)); }
};

static const TArray<FBSArmItem>& ArmItems()
{
	static TArray<FBSArmItem> Items = []()
	{
		TArray<FBSArmItem> Out;
		// High tech first: hidden until a shop card puts it here. Research comes from the shop, not the armoury.
		for (const FBSTechCard& Tc : BSDefs::Tech())
		{
			const FString Id = Tc.Id, Ic = Tc.Icon;
			FBSArmItem I; I.Id = Id; I.Name = Tc.Name; I.bTech = true;
			I.Short = Id == TEXT("goliath") ? TEXT("Walker: autocannons, AA missiles") : Id == TEXT("nuke") ? TEXT("Ghost-called nuke on the densest pack")
				: Id == TEXT("vikings") ? TEXT("Fighter wing hunts air")
				: Id == TEXT("ghost") ? TEXT("Cloaked sniper; each makes nukes cheaper") : Id == TEXT("wraiths") ? TEXT("Lasers on the swarm, missiles at air")
				: Id == TEXT("vessel") ? TEXT("Irradiate and Defensive Matrix") : Id == TEXT("thor") ? TEXT("Heavy walker holds the lane")
				: Id == TEXT("bc") ? TEXT("Hovers overhead: lasers, missiles, Yamato") : TEXT("The Odin");
			I.Icon = [Ic](ABSGame*) { return Ic; };
			I.Cost = [Id](ABSGame* G) { int32 C = 0; FString T; G->TechReady(Id, C, T); return C; };
			I.Avail = [Id](ABSGame* G) { int32 C = 0; FString T; return G->TechReady(Id, C, T); };
			I.Tag = [Id](ABSGame* G) { int32 C = 0; FString T; G->TechReady(Id, C, T); return T; };
			I.Level = [](ABSGame*) { return -1; };
			I.Hidden = [Id](ABSGame* G) { return !G->TechUnlocked.Contains(Id); };
			I.Buy = [Id](ABSGame* G) { G->BuyTech(Id); };
			Out.Add(I);
		}
		for (const FBSInfantryDef& Inf : BSDefs::Infantry())
		{
			const FString Key = Inf.Key;
			FBSArmItem I; I.Id = Key; I.Name = Inf.Name; I.Short = Key == TEXT("marine") ? TEXT("Gauss rifle infantry") : Key == TEXT("firebat") ? TEXT("Flamethrower, anti-swarm") : TEXT("Slows, shreds armour");
			I.Icon = [Key](ABSGame*) { return BSDefs::FindInfantry(Key)->Icon; };
			I.Cost = [Key](ABSGame* G) { return G->TroopCost(BSDefs::FindInfantry(Key)); };
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
		// Swarm mode: the hive cluster. Economy, the tiers, strains to breed, evolutions; two prices (minerals + vespene).
		auto Swarm = [&Out](const FString& Kind, const FString& Id, const FString& Name, TFunction<FString(ABSGame*)> Icon, TFunction<FString(ABSGame*)> Note, const FString& MaxLabel)
		{
			FBSArmItem I; I.Id = Kind + TEXT(":") + Id; I.Name = Name; I.bSwarm = true;
			I.Icon = Icon; I.Note = Note;
			I.Cost = [Kind, Id](ABSGame* G) { BSSwarm::FCost C; G->SwarmCost(Kind, Id, C); return C.M; };
			I.Price = [Kind, Id](ABSGame* G) { BSSwarm::FCost C; G->SwarmCost(Kind, Id, C); return C.G ? FString::Printf(TEXT("%d + %dg"), C.M, C.G) : FString::FromInt(C.M); };
			I.Poor = [Kind, Id](ABSGame* G) { BSSwarm::FCost C; return G->SwarmCost(Kind, Id, C) && (G->Sw.Minerals < C.M || G->Sw.Gas < C.G); };
			I.Avail = [Kind, Id](ABSGame* G) { return G->SwarmState(Kind, Id).IsEmpty(); };
			I.Tag = [Kind, Id, MaxLabel](ABSGame* G)
			{
				const FString St = G->SwarmState(Kind, Id);
				if (Kind == TEXT("chamber") && St == TEXT("max")) return FString(G->Sw.bAutoEvo ? TEXT("AUTO") : TEXT("MANUAL"));   // (a tap switches)
				if (St == TEXT("busy")) { const auto* R = G->Researching(Id); return FString::Printf(TEXT("%ds"), R ? FMath::CeilToInt(R->Dur - R->T) : 0); }
				return St == TEXT("max") ? MaxLabel : St == TEXT("locked") ? FString(BSSwarm::Tiers()[G->SwarmNeeds(Kind, Id)].Name).ToUpper()
					: St == TEXT("nochamber") ? FString(TEXT("CHAMBER")) : St == TEXT("full") ? FString(TEXT("BUSY")) : FString(TEXT("LOCKED"));
			};
			I.Level = [](ABSGame*) { return -1; };
			I.Buy = [Kind, Id](ABSGame* G) { G->SwarmBuy(Kind, Id); };
			Out.Add(I);
		};
		auto Fixed = [](const TCHAR* Icon) { const FString Ic = Icon; return TFunction<FString(ABSGame*)>([Ic](ABSGame*) { return Ic; }); };
		Swarm(TEXT("drone"), FString(), TEXT("Drone"), Fixed(TEXT("btn-unit-zerg-drone")), [](ABSGame* G) { return FString::FromInt(G->Sw.Drones); }, TEXT("MAX"));
		Swarm(TEXT("extractor"), FString(), TEXT("Extractor"), Fixed(TEXT("btn-building-zerg-extractor")), [](ABSGame* G) { return FString::FromInt(G->Sw.Extractors); }, TEXT("MAX"));
		Swarm(TEXT("hatch"), FString(), TEXT("Hatchery"), Fixed(TEXT("btn-building-zerg-hatchery")), [](ABSGame* G) { return FString::Printf(TEXT("%d  %.1f/s"), G->Sw.Hatcheries, G->LarvaRate()); }, TEXT("MAX"));
		Swarm(TEXT("queen"), FString(), TEXT("Spawn Larva"), Fixed(TEXT("btn-unit-zerg-queen")), [](ABSGame* G) { return G->Sw.Queens ? FString::Printf(TEXT("LV %d"), G->Sw.Queens) : FString(); }, TEXT("MAX"));
		Swarm(TEXT("chamber"), FString(), TEXT("Evolution Chamber"), Fixed(TEXT("btn-building-zerg-evolutionchamber")), [](ABSGame* G) { return G->Sw.Chambers ? FString::FromInt(G->Sw.Chambers) : FString(); }, TEXT("AUTO"));
		Swarm(TEXT("tier"), FString(), TEXT("Evolve Tier"), [](ABSGame* G) { const auto& T = BSSwarm::Tiers(); return FString(T[FMath::Min(G->Sw.Tier + 1, T.Num() - 1)].Icon); },
			[](ABSGame* G) { return FString(BSSwarm::Tiers()[G->Sw.Tier].Name).ToUpper(); }, TEXT("HIVE"));
		for (const BSSwarm::FStrain& S : BSSwarm::Strains()) Swarm(TEXT("strain"), S.Id, S.Name, Fixed(S.Icon), [](ABSGame*) { return FString(); }, TEXT("BRED"));
		for (const BSSwarm::FEvo& V : BSSwarm::Evos())
		{
			const FString Id = V.Id;
			Swarm(TEXT("evo"), Id, V.Name, [Id](ABSGame* G) { const BSSwarm::FEvo* E = BSSwarm::FindEvo(Id); return FString(E->Icons[FMath::Min(G->EvoLevel(Id), E->Icons.Num() - 1)]); },
				[Id](ABSGame* G) { return BSSwarm::FindEvo(Id)->Levels() && G->EvoLevel(Id) ? FString::Printf(TEXT("LV %d"), G->EvoLevel(Id)) : FString(); }, TEXT("DONE"));
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
		const bool bPoor = bAvail && (I.Poor ? I.Poor(Gm) : Gm->Minerals < Cost);
		const FVector2f S((float)Size.X, (float)Size.Y), P(0, bPress ? 1.f : 0.f);
		D::RFill(Out, L, G, P, S, 12, bHover ? C::GlassHi : C::Glass);
		D::RLine(Out, L + 1, G, P, S, 12, bHover ? C::A(C::Cyan, 0.7f) : I.bTech ? C::A(C::Violet, 0.8f) : C::Hair, I.bTech ? 1.5f : 1.f);
		const float Is = 44;
		const FVector2f Ip(P.X + (S.X - Is) * 0.5f, P.Y + 8);
		const FSlateBrush* Ib = Gm->GetAssets()->IconBrush(I.Icon(Gm));
		D::Image(Out, L + 2, G, Ib, Ip, FVector2f(Is, Is), (bAvail && !bPoor) ? FLinearColor::White : FLinearColor(0.3f, 0.32f, 0.36f));
		if (!Ib) D::Text(Out, L + 2, G, I.Name.Left(2).ToUpper(), D::Title(16), Ip + FVector2f(Is * 0.5f, Is * 0.5f - 10), C::A(C::Violet, 0.8f), 0.5f);   // icon not exported
		D::RLine(Out, L + 3, G, Ip - FVector2f(1, 1), FVector2f(Is + 2, Is + 2), 7, C::A(C::Ink, 0.2f));
		if (I.Note) { const FString Nt = I.Note(Gm); if (!Nt.IsEmpty()) { const FVector2f Ns = D::Measure(Nt, D::Bold(7.5f)); D::RFill(Out, L + 4, G, FVector2f(Ip.X + Is - Ns.X - 6, Ip.Y + Is - 13), FVector2f(Ns.X + 6, 13), 4, C::A(C::Dark, 0.85f)); D::Text(Out, L + 5, G, Nt, D::Bold(7.5f), FVector2f(Ip.X + Is - 3, Ip.Y + Is - 13), C::Amber, 1.f); } }
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
		if (bAvail) D::CostPill(Out, L + 3, G, FVector2f(S.X * 0.5f, P.Y + S.Y - 13), I.Price ? I.Price(Gm) : FString::FromInt(Cost), true, bPoor ? C::Red : C::Cyan);
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
		const bool bPoor = bAvail && (I.Poor ? I.Poor(Gm) : Gm->Minerals < Cost);
		const FVector2f S((float)Size.X, (float)Size.Y - 4), P(bPress ? 1.f : 0.f, 0);
		D::RFill(Out, L, G, P, S, 10, bHover ? C::GlassHi : C::Glass);
		D::RLine(Out, L + 1, G, P, S, 10, bHover ? C::A(C::Cyan, 0.7f) : C::Hair);
		const float Is = 36;
		const FSlateBrush* Ib = Gm->GetAssets()->IconBrush(I.Icon(Gm));
		D::Image(Out, L + 2, G, Ib, P + FVector2f(8, 7), FVector2f(Is, Is), (bAvail && !bPoor) ? FLinearColor::White : FLinearColor(0.3f, 0.32f, 0.36f));
		if (!Ib) D::Text(Out, L + 2, G, I.Name.Left(2).ToUpper(), D::Title(14), P + FVector2f(8 + Is * 0.5f, 7 + Is * 0.5f - 9), C::A(C::Violet, 0.8f), 0.5f);   // icon not exported
		D::RLine(Out, L + 3, G, P + FVector2f(7, 6), FVector2f(Is + 2, Is + 2), 6, C::A(C::Ink, 0.2f));
		const float Tx = P.X + 54, RightW = 62, TextW = S.X - 54 - RightW;
		D::Text(Out, L + 2, G, I.Name.ToUpper(), D::Fit(I.Name.ToUpper(), [](float Sz) { return D::Label(Sz); }, 10, 7, TextW), FVector2f(Tx, P.Y + 9), bAvail ? C::Ink : C::Muted);
		const FString Line2 = I.Note ? I.Note(Gm) : I.Short;
		D::Text(Out, L + 2, G, Line2, D::Fit(Line2, [](float Sz) { return D::Body(Sz); }, 9, 6.5f, TextW, D::BodySlack), FVector2f(Tx, P.Y + 26), I.Note ? C::Amber : C::Muted);
		const int32 Lv = I.Level(Gm);
		if (Lv >= 0) for (int32 k = 0; k < I.MaxLevel; k++) D::RFill(Out, L + 3, G, FVector2f(Tx + k * 8, P.Y + 41), FVector2f(5, 3), 1.5f, k < Lv ? C::Amber : C::Faint);
		const FVector2f Pc(P.X + S.X - RightW * 0.5f - 4, P.Y + S.Y * 0.5f);
		if (bAvail)
		{
			const FString Pr = I.Price ? I.Price(Gm) : FString::FromInt(Cost);
			if (D::Measure(Pr, D::Bold(9)).X > RightW - 22) D::Text(Out, L + 3, G, Pr, D::Fit(Pr, [](float Sz) { return D::Bold(Sz); }, 9, 6.5f, RightW + 6), Pc - FVector2f(0, 6), bPoor ? C::Red : C::Cyan, 0.5f);
			else D::CostPill(Out, L + 3, G, Pc, Pr, true, bPoor ? C::Red : C::Cyan);
		}
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
	auto Shown = [this](int32 i) { ABSGame* Gm = Game(); return (Gm && ArmItems()[i].IsHidden(Gm)) ? EVisibility::Collapsed : EVisibility::Visible; };
	for (int32 i = 0; i < ArmItems().Num(); i++) Strip->AddSlot().Padding(4, 0, 4, 0)[ SNew(SBox).Visibility_Lambda([Shown, i]() { return Shown(i); })[ MakeArmouryCard(i) ] ];

	auto Header = PaintedButton(TAttribute<FVector2D>::CreateLambda([this]() { return FVector2D(LocalSize.X, 26); }),
		[this](const FGeometry& G, FSlateWindowElementList& Out, int32 L, bool bHover, bool)
	{
		const float W = G.GetLocalSize().X;
		const FString Lab = Game() && Game()->bSwarm ? TEXT("HIVE CLUSTER") : TEXT("ARMOURY");
		D::Text(Out, L, G, Lab, D::Label(9), FVector2f(12, 6), bHover ? C::Cyan : C::Muted);
		const float Tw = D::Measure(Lab, D::Label(9)).X;
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
	for (int32 i = 0; i < ArmItems().Num(); i++)
		List->AddSlot()[ SNew(SBox).Visibility_Lambda([this, i]() { ABSGame* Gm = Game(); return (Gm && ArmItems()[i].IsHidden(Gm)) ? EVisibility::Collapsed : EVisibility::Visible; })[ MakeArmouryRow(i) ] ];
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(2, 0, 0, 6)
		[
			SNew(SBSPaint).Size(FVector2D(236, 18)).Paint([this](const FGeometry& G, FSlateWindowElementList& Out, int32 L)
			{
				const FString Lab = Game() && Game()->bSwarm ? TEXT("HIVE CLUSTER") : TEXT("ARMOURY");
				D::Text(Out, L, G, Lab, D::Label(9), FVector2f(2, 2), C::Muted);
				const float Tw = D::Measure(Lab, D::Label(9)).X;
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
			D::Panel(Out, L, G, P, S, 16, bHover ? C::GlassHi : C::Glass);
			D::RLine(Out, L + 1, G, P, S, 16, bHover ? C::A(Acc, 0.9f) : C::A(Acc, bRec ? 0.5f : 0.22f), bHover ? 1.5f : 1.f);
			D::RFill(Out, L + 1, G, P + FVector2f(0, 22), FVector2f(3, S.Y - 44), 1.5f, Acc);
			UBSAssets* As = GM.IsValid() ? GM->GetAssets() : nullptr;
			if (As) D::Image(Out, L + 2, G, As->IconBrush(bArena ? TEXT("btn-unit-zerg-ultralisk") : TEXT("btn-unit-terran-marine")), P + FVector2f(18, 18), FVector2f(60, 60));
			if (As) D::RLine(Out, L + 3, G, P + FVector2f(17, 17), FVector2f(62, 62), 10, C::A(C::Ink, 0.2f));
			D::Text(Out, L + 2, G, bArena ? TEXT("360 ARENA") : TEXT("LANE DEFENCE"), D::Fit(bArena ? TEXT("360 ARENA") : TEXT("LANE DEFENCE"), [](float Sz) { return D::Title(Sz); }, 14.5f, 10, S.X - 94 - 14), P + FVector2f(94, 20), C::Ink);
			const FString Desc = bArena ? TEXT("Waves from every side for 30 minutes. Bigger, faster, deadlier.") : TEXT("Hold the lane for 30 minutes as the Swarm pours down from the north.");
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
		const FVector2f Tp = bPortrait ? FVector2f(S.X * 0.5f, S.Y * 0.07f) : FVector2f(56, S.Y * 0.12f);
		const float Al = bPortrait ? 0.5f : 0.f;
		const float LineH = D::Measure(TEXT("SURVIVORS"), Tf0).Y * 0.86f;
		D::EText(Out, L + 1, G, TEXT("BUNKER"), Tf0, Tp, C::Ink, Al);
		D::EText(Out, L + 1, G, TEXT("SURVIVORS"), Tf0, Tp + FVector2f(0, LineH), C::Cyan, Al);
		const FString Tag = TEXT("ONE FIRETEAM.  ONE BUNKER.  THE SWARM.");
		const FSlateFontInfo Tf = D::Fit(Tag, [](float Sz) { return D::Label(Sz); }, 10, 7, MaxTitleW);
		const float Ty = Tp.Y + LineH * 2.f + 18;
		D::Rect(Out, L + 1, G, FVector2f(Tp.X - (bPortrait ? 24 : 0), Ty - 9), FVector2f(48, 2), C::Amber);
		D::Text(Out, L + 1, G, Tag, Tf, FVector2f(Tp.X, Ty), C::Muted, Al);
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

	// The Hyperion: perks between runs.
	auto HyperionCard = [this]()
	{
		const FVector2D Size(340, 52);
		return PaintedButton(Size, [this, Size](const FGeometry& G, FSlateWindowElementList& Out, int32 L, bool bHover, bool bPress)
		{
			const FVector2f S((float)Size.X, (float)Size.Y), P(0, bPress ? 1.f : 0.f);
			D::Panel(Out, L, G, P, S, 14, bHover ? C::GlassHi : C::Glass);
			D::RLine(Out, L + 1, G, P, S, 14, C::A(C::Amber, bHover ? 0.9f : 0.35f), bHover ? 1.5f : 1.f);
			D::RFill(Out, L + 1, G, P + FVector2f(0, 12), FVector2f(3, S.Y - 24), 1.5f, C::Amber);
			UBSAssets* As = GM.IsValid() ? GM->GetAssets() : nullptr;
			if (As) D::Image(Out, L + 2, G, As->IconBrush(TEXT("btn-unit-terran-battlecruiser")), P + FVector2f(14, 8), FVector2f(36, 36));
			D::Text(Out, L + 2, G, TEXT("HYPERION"), D::Title(13), P + FVector2f(62, 9), C::Ink);
			D::Text(Out, L + 2, G, FString::Printf(TEXT("%d VESPENE  //  PERKS AND CARDS"), BSMeta::Gas()), D::Label(8), P + FVector2f(62, 30), C::Green);
		}, [this]() { bHyperion = true; });
	};

	// The Swarm: open once a run has escaped Kerrigan (-BSSwarm opens it for playtesting); greyed out until then.
	auto SwarmCard = [this]()
	{
		const FVector2D Size(340, 96);
		return PaintedButton(Size, [this, Size](const FGeometry& G, FSlateWindowElementList& Out, int32 L, bool bHover, bool bPress)
		{
			static const bool bForce = FParse::Param(FCommandLine::Get(), TEXT("BSSwarm"));
			const bool bOpen = bForce || BSMeta::SwarmOpen();
			const FVector2f S((float)Size.X, (float)Size.Y), P(0, bPress && bOpen ? 1.f : 0.f);
			const FLinearColor Acc(1.f, 0.48f, 0.85f);
			const bool bHot = bHover && bOpen;
			D::Panel(Out, L, G, P, S, 16, bHot ? C::GlassHi : C::Glass);
			D::RLine(Out, L + 1, G, P, S, 16, bHot ? C::A(Acc, 0.9f) : C::A(Acc, bOpen ? 0.3f : 0.12f), bHot ? 1.5f : 1.f);
			D::RFill(Out, L + 1, G, P + FVector2f(0, 22), FVector2f(3, S.Y - 44), 1.5f, C::A(Acc, bOpen ? 1.f : 0.35f));
			UBSAssets* As = GM.IsValid() ? GM->GetAssets() : nullptr;
			const FSlateBrush* Ib = As ? As->IconBrush(TEXT("btn-unit-zerg-kerriganinfested")) : nullptr;
			if (!Ib && As) Ib = As->IconBrush(TEXT("btn-unit-zerg-zergling"));      // (until her icon is exported)
			if (Ib) D::Image(Out, L + 2, G, Ib, P + FVector2f(18, 18), FVector2f(60, 60), bOpen ? FLinearColor::White : FLinearColor(0.3f, 0.32f, 0.36f));
			D::RLine(Out, L + 3, G, P + FVector2f(17, 17), FVector2f(62, 62), 10, C::A(C::Ink, 0.2f));
			D::Text(Out, L + 2, G, TEXT("THE SWARM"), D::Title(14.5f), P + FVector2f(94, 20), bOpen ? C::Ink : C::Muted);
			const FString Desc = bOpen ? TEXT("Play the zerg: feed the hives, summon Kerrigan and raze the bunker before the Hyperion gets them out.") : TEXT("Escape Kerrigan on the Hyperion to unlock.");
			FString D1, D2;
			D::SplitTwo(Desc, D1, D2);
			const FSlateFontInfo Df = D::Fit(D1.Len() > D2.Len() ? D1 : D2, [](float Sz) { return D::Body(Sz); }, 10.5f, 7, S.X - 110, D::BodySlack);
			D::Text(Out, L + 2, G, D1, Df, P + FVector2f(94, 44), C::Muted);
			D::Text(Out, L + 2, G, D2, Df, P + FVector2f(94, 58), C::Muted);
		}, [this]()
		{
			if (GM.IsValid() && (FParse::Param(FCommandLine::Get(), TEXT("BSSwarm")) || BSMeta::SwarmOpen())) GM->StartGame(EBSMode::Lane, true);
		});
	};

	// The Protoss campaign: hidden until the swarm has razed the bunker (-BSZeratul shows it).
	auto ProtossCard = [this]()
	{
		const FVector2D Size(340, 96);
		return SNew(SBox).Visibility_Lambda([]() { return BSMeta::ProtossOpen() || FParse::Param(FCommandLine::Get(), TEXT("BSZeratul")) ? EVisibility::Visible : EVisibility::Collapsed; })
		[
			PaintedButton(Size, [this, Size](const FGeometry& G, FSlateWindowElementList& Out, int32 L, bool bHover, bool bPress)
			{
				const FVector2f S((float)Size.X, (float)Size.Y), P(0, bPress ? 1.f : 0.f);
				const FLinearColor Acc(0.35f, 0.95f, 0.55f);
				D::Panel(Out, L, G, P, S, 16, bHover ? C::GlassHi : C::Glass);
				D::RLine(Out, L + 1, G, P, S, 16, C::A(Acc, bHover ? 0.9f : 0.3f), bHover ? 1.5f : 1.f);
				D::RFill(Out, L + 1, G, P + FVector2f(0, 22), FVector2f(3, S.Y - 44), 1.5f, Acc);
				UBSAssets* As = GM.IsValid() ? GM->GetAssets() : nullptr;
				if (const FSlateBrush* Ib = As ? As->IconBrush(TEXT("btn-unit-protoss-zeratul")) : nullptr) D::Image(Out, L + 2, G, Ib, P + FVector2f(18, 18), FVector2f(60, 60));
				D::RLine(Out, L + 3, G, P + FVector2f(17, 17), FVector2f(62, 62), 10, C::A(C::Ink, 0.2f));
				D::Text(Out, L + 2, G, TEXT("THE PROTOSS CAMPAIGN"), D::Fit(TEXT("THE PROTOSS CAMPAIGN"), [](float Sz) { return D::Title(Sz); }, 14.5f, 9, S.X - 108), P + FVector2f(94, 20), C::Ink);
				D::Text(Out, L + 2, G, TEXT("The whole campaign. Every last second of it."), D::Body(10), P + FVector2f(94, 48), C::Muted);
			}, [this]() { if (GM.IsValid()) GM->StartZeratul(); })
		];
	};

	auto Cards = [ModeCard, SwarmCard, ProtossCard, HyperionCard]()
	{
		return SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)[ ModeCard(false) ]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)[ ModeCard(true) ]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)[ ProtossCard() ]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)[ SwarmCard() ]
			+ SVerticalBox::Slot().AutoHeight()[ HyperionCard() ];
	};

	return SNew(SOverlay)
		.Visibility_Lambda([this]() { return Game() || (GM.IsValid() && GM->IsZeratul()) ? EVisibility::Collapsed : EVisibility::Visible; })
		+ SOverlay::Slot()[ Backdrop ]
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Bottom).Padding(0, 0, 0, 80)
		[ SNew(SBox).Visibility_Lambda([this]() { return Portrait() ? EVisibility::Visible : EVisibility::Collapsed; })[ Cards() ] ]
		+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Center).Padding(56, 120, 0, 0)
		[ SNew(SBox).Visibility_Lambda([this]() { return Portrait() ? EVisibility::Collapsed : EVisibility::Visible; })[ Cards() ] ];
}

TSharedRef<SWidget> SBSHud::BuildModal(bool bGameOver)
{
	// Stats run one per row (label left, figure right, hairlines between) so big kill and score counts never collide.
	// Game over: title, three rows, the best pill, vespene banked, any card unlocked, then the buttons.
	const float PanelH = bGameOver ? 366.f : 286.f;
	auto PanelW = [this]() { return FMath::Clamp((float)LocalSize.X - 32.f, 320.f, 380.f); };
	auto Panel = SNew(SBSPaint).Size_Lambda([PanelW, PanelH]() { return FVector2D(PanelW(), PanelH); }).Paint([this, bGameOver, PanelH](const FGeometry& G, FSlateWindowElementList& Out, int32 L)
	{
		ABSGame* Gm = Game(); if (!Gm) return L;
		constexpr float RowsTop = 66, RowH = 44;
		const FVector2f S(G.GetLocalSize().X, PanelH);
		D::Panel(Out, L, G, FVector2f(0, 0), S, 18, C::A(C::Dark, 0.94f));
		const FLinearColor EndCol = Gm->bWon ? C::Green : C::Red;     // evacuated by the Hyperion, or overrun
		D::RLine(Out, L + 1, G, FVector2f(0, 0), S, 18, bGameOver ? C::A(EndCol, 0.5f) : C::Hair);
		D::Rect(Out, L + 1, G, FVector2f(S.X * 0.5f - 24, 0), FVector2f(48, 2), bGameOver ? EndCol : C::Cyan);
		const TCHAR* EndTitle = Gm->bSwarm ? (Gm->bWon ? TEXT("BUNKER RAZED") : Gm->bHiveDown ? TEXT("HIVE DESTROYED") : TEXT("THEY GOT AWAY")) : Gm->bWon ? TEXT("EVACUATED") : TEXT("BUNKER LOST");
		D::EText(Out, L + 2, G, bGameOver ? EndTitle : TEXT("PAUSED"), D::Fit(TEXT("BUNKER LOST"), [](float Sz) { return D::Title(Sz); }, 28, 14, S.X - 40), FVector2f(S.X * 0.5f, 18), bGameOver ? EndCol : C::Ink, 0.5f);
		const int64 Score = bGameOver ? Gm->FinalScore : FMath::RoundToInt(Gm->Score + Gm->Kills * 10 + Gm->Wave * 250);
		const TArray<TPair<FString, FString>> Stats = {
			{ TEXT("WAVE"), D::Thousands(Gm->Wave) }, { TEXT("KILLS"), D::Thousands(Gm->bSwarm ? Gm->Sw.Kills : Gm->Kills) }, { TEXT("SCORE"), D::Thousands(Score) } };
		const float Pad = 24;
		for (int32 i = 0; i < Stats.Num(); i++)
		{
			const float Y = RowsTop + i * RowH;
			if (i > 0) D::Rect(Out, L + 2, G, FVector2f(Pad - 4, Y), FVector2f(S.X - 2 * Pad + 8, 1), C::Hair);
			const FSlateFontInfo Lf = D::Label(9);
			D::Text(Out, L + 2, G, Stats[i].Key, Lf, FVector2f(Pad, Y + RowH * 0.5f - D::Measure(Stats[i].Key, Lf).Y * 0.5f), C::Muted);
			const FSlateFontInfo Vf = D::Fit(Stats[i].Value, [](float Sz) { return D::Num(Sz); }, 24, 12, S.X * 0.62f);
			D::Text(Out, L + 2, G, Stats[i].Value, Vf, FVector2f(S.X - Pad, Y + RowH * 0.5f - D::Measure(Stats[i].Value, Vf).Y * 0.5f), C::Ink, 1.f);
		}
		if (bGameOver)
		{
			const float Y0 = RowsTop + Stats.Num() * RowH + 12;
			const FString B = Gm->bNewBest ? TEXT("NEW BEST") : TEXT("BEST ") + D::Thousands(Gm->Best);
			const FLinearColor Bc = Gm->bNewBest ? C::Amber : C::Muted;
			const FVector2f Bs = D::Measure(B, D::Label(9));
			D::RFill(Out, L + 2, G, FVector2f(S.X * 0.5f - Bs.X * 0.5f - 10, Y0), FVector2f(Bs.X + 20, 20), 10, C::A(Bc, Gm->bNewBest ? 0.18f + 0.1f * FMath::Sin(NowS * 5) : 0.1f));
			D::Text(Out, L + 3, G, B, D::Label(9), FVector2f(S.X * 0.5f, Y0 + 10 - Bs.Y * 0.5f), Bc, 0.5f);
			// What the run banked at the Hyperion, and any card it unlocked.
			const BSMeta::FRunResult& Mr = Gm->MetaResult;
			D::Text(Out, L + 2, G, FString::Printf(TEXT("+%d VESPENE FOR THE HYPERION"), Mr.Gas), D::Label(9), FVector2f(S.X * 0.5f, Y0 + 32), C::Green, 0.5f);
			FString U;
			for (const FBSCardDef* Cd : Mr.Unlocked) U += (U.IsEmpty() ? TEXT("NEW CARD: ") : TEXT(", ")) + BSMeta::CardName(*Cd).ToUpper();
			if (!U.IsEmpty()) D::Text(Out, L + 2, G, U, D::Fit(U, [](float Sz) { return D::Label(Sz); }, 9, 6, S.X - 24), FVector2f(S.X * 0.5f, Y0 + 50), C::Amber, 0.5f);
			if (Mr.bSwarm) D::Text(Out, L + 2, G, TEXT("NEW MODE: THE SWARM"), D::Label(9), FVector2f(S.X * 0.5f, Y0 + (U.IsEmpty() ? 50 : 68)), C::Amber, 0.5f);
			if (Mr.bProtoss) D::Text(Out, L + 2, G, TEXT("NEW: THE PROTOSS CAMPAIGN"), D::Label(9), FVector2f(S.X * 0.5f, Y0 + (U.IsEmpty() ? 50 : 68)), C::Violet, 0.5f);
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
// The shop (between waves) and the Hyperion (between runs)
// =====================================================================================================
// One offered card: a tall card in landscape, a row in portrait. Rarity colours the frame; bought cards fade.
TSharedRef<SWidget> SBSHud::MakeOffer(int32 Index, bool bRow)
{
	const TAttribute<FVector2D> Size = TAttribute<FVector2D>::CreateLambda([this, bRow]() { return bRow ? FVector2D(FMath::Min(440.f, (float)LocalSize.X - 44), 62) : FVector2D(150, 192); });
	return PaintedButton(Size, [this, Index, bRow](const FGeometry& G, FSlateWindowElementList& Out, int32 L, bool bHover, bool bPress)
	{
		ABSGame* Gm = Game(); if (!Gm || !Gm->Shop.Offers.IsValidIndex(Index)) return;
		const bool bBought = Gm->Shop.Bought[Index];
		// The Terrans' shop cards, or (swarm mode) the hive's mutations.
		FString RarityKey, Name, Desc, LvText, Price, IconName;
		bool bPoor = false;
		if (Gm->Shop.bZerg)
		{
			const BSSwarm::FZCard* Zc = BSSwarm::FindCard(Gm->Shop.Offers[Index]);
			if (!Zc) return;
			const int32 Lv = Gm->ZCardLevel(Zc->Id), Max = Zc->Max ? Zc->Max : 99;
			const BSSwarm::FCost Cost = Gm->ZCardCost(*Zc);
			RarityKey = Zc->Rarity; Name = FString(Zc->Name).ToUpper(); Desc = Zc->Desc; IconName = Zc->Icon;
			LvText = Max > 1 && Max < 99 ? FString::Printf(TEXT("LV %d / %d"), bBought ? Lv : Lv + 1, Max) : FString();
			bPoor = !bBought && (Gm->Sw.Minerals < Cost.M || Gm->Sw.Gas < Cost.G);
			Price = bBought ? FString(TEXT("BOUGHT")) : Cost.M || Cost.G ? (Cost.G ? FString::Printf(TEXT("%d + %dg"), Cost.M, Cost.G) : FString::FromInt(Cost.M)) : FString(TEXT("FREE"));
		}
		else
		{
			const FBSCardDef* Cd = BSDefs::FindCard(Gm->Shop.Offers[Index]);
			if (!Cd) return;
			const int32 Lv = Gm->CardLevel(*Cd), Max = Gm->CardMax(*Cd), Cost = Gm->CardCost(*Cd);
			RarityKey = Cd->Rarity; Name = BSMeta::CardName(*Cd).ToUpper(); Desc = Gm->CardDesc(*Cd); IconName = Gm->CardIcon(*Cd);
			LvText = Max <= 1 ? FString() : Cd->Grow ? FString::Printf(TEXT("LV %d"), bBought ? Lv : Lv + 1) : FString::Printf(TEXT("LV %d / %d"), bBought ? Lv : Lv + 1, Max);
			bPoor = !bBought && Gm->Minerals < Cost;
			Price = bBought ? FString(TEXT("BOUGHT")) : FString::FromInt(Cost);
		}
		const FBSRarity& R = BSDefs::FindRarity(RarityKey);
		const float Fade = bBought ? 0.45f : 1.f;
		const FVector2f S = FVector2f(G.GetLocalSize()), P(0, bPress && !bBought ? 1.f : 0.f);
		D::RFill(Out, L, G, P, S, 14, C::A(bHover && !bBought ? C::GlassHi : C::Glass, 0.95f));
		D::RLine(Out, L + 1, G, P, S, 14, C::A(R.Color, (bHover && !bBought ? 1.f : 0.75f) * Fade), 1.5f);
		const FLinearColor PriceCol = bBought ? C::Green : bPoor ? C::Red : C::Cyan;
		const FLinearColor Tint = FLinearColor(Fade, Fade, Fade);
		if (bRow)
		{
			const float Is = 44, RightW = 76, Tx = 64, TextW = S.X - Tx - RightW;
			D::Image(Out, L + 2, G, Gm->GetAssets()->IconBrush(IconName), P + FVector2f(10, 9), FVector2f(Is, Is), Tint);
			D::RLine(Out, L + 3, G, P + FVector2f(9, 8), FVector2f(Is + 2, Is + 2), 7, C::A(C::Ink, 0.2f));
			D::Text(Out, L + 2, G, R.Name, D::Label(7.5f), P + FVector2f(Tx, 7), C::A(R.Color, Fade));
			D::Text(Out, L + 2, G, Name, D::Fit(Name, [](float Sz) { return D::Label(Sz); }, 11, 7.5f, TextW), P + FVector2f(Tx, 19), C::A(C::Ink, Fade));
			D::Text(Out, L + 2, G, Desc, D::Fit(Desc, [](float Sz) { return D::Body(Sz); }, 9.5f, 6.5f, TextW, D::BodySlack), P + FVector2f(Tx, 37), C::A(C::Muted, Fade));
			if (!LvText.IsEmpty()) D::Text(Out, L + 2, G, LvText, D::Label(7.5f), P + FVector2f(S.X - 12, 9), C::A(C::Amber, Fade), 1.f);
			D::CostPill(Out, L + 3, G, P + FVector2f(S.X - RightW * 0.5f - 6, S.Y * 0.5f + 7), Price, !bBought, PriceCol);
		}
		else
		{
			const float Is = 52;
			const FVector2f Ip(P.X + (S.X - Is) * 0.5f, P.Y + 12);
			D::Image(Out, L + 2, G, Gm->GetAssets()->IconBrush(IconName), Ip, FVector2f(Is, Is), Tint);
			D::RLine(Out, L + 3, G, Ip - FVector2f(1, 1), FVector2f(Is + 2, Is + 2), 8, C::A(C::Ink, 0.2f));
			D::Text(Out, L + 2, G, R.Name, D::Label(7.5f), FVector2f(S.X * 0.5f, P.Y + 70), C::A(R.Color, Fade), 0.5f);
			D::Text(Out, L + 2, G, Name, D::Fit(Name, [](float Sz) { return D::Label(Sz); }, 10.5f, 7, S.X - 14), FVector2f(S.X * 0.5f, P.Y + 84), C::A(C::Ink, Fade), 0.5f);
			if (!LvText.IsEmpty()) D::Text(Out, L + 2, G, LvText, D::Label(7.5f), FVector2f(S.X * 0.5f, P.Y + 101), C::A(C::Amber, Fade), 0.5f);
			FString D1, D2;
			if (D::Measure(Desc, D::Body(9.5f)).X * D::BodySlack > S.X - 16) D::SplitTwo(Desc, D1, D2); else D1 = Desc;
			const FSlateFontInfo Df = D::Fit(D1.Len() > D2.Len() ? D1 : D2, [](float Sz) { return D::Body(Sz); }, 9.5f, 6.5f, S.X - 16, D::BodySlack);
			D::Text(Out, L + 2, G, D1, Df, FVector2f(S.X * 0.5f, P.Y + 117), C::A(C::Ink, 0.85f * Fade), 0.5f);
			if (!D2.IsEmpty()) D::Text(Out, L + 2, G, D2, Df, FVector2f(S.X * 0.5f, P.Y + 131), C::A(C::Ink, 0.85f * Fade), 0.5f);
			D::CostPill(Out, L + 3, G, FVector2f(S.X * 0.5f, P.Y + S.Y - 18), Price, !bBought, PriceCol);
		}
	},
	[this, Index]() { if (ABSGame* Gm = Game()) Gm->BuyCard(Index); });
}

TSharedRef<SWidget> SBSHud::BuildShop()
{
	constexpr int32 MaxOffers = 5;
	auto Open = [this]() { ABSGame* Gm = Game(); return Gm && Gm->Shop.bOpen && !Gm->bOver; };
	auto OfferVis = [this](int32 i, bool bRow)
	{
		ABSGame* Gm = Game();
		return Gm && i < Gm->Shop.Offers.Num() && Portrait() == bRow ? EVisibility::Visible : EVisibility::Collapsed;
	};
	TSharedRef<SHorizontalBox> Cards = SNew(SHorizontalBox);
	TSharedRef<SVerticalBox> Rows = SNew(SVerticalBox);
	for (int32 i = 0; i < MaxOffers; i++)
	{
		Cards->AddSlot().AutoWidth().Padding(5, 0)[ SNew(SBox).Visibility_Lambda([OfferVis, i]() { return OfferVis(i, false); })[ MakeOffer(i, false) ] ];
		Rows->AddSlot().AutoHeight().Padding(0, 3)[ SNew(SBox).Visibility_Lambda([OfferVis, i]() { return OfferVis(i, true); })[ MakeOffer(i, true) ] ];
	}

	auto Header = [this]() { return SNew(SBSPaint).Size(TAttribute<FVector2D>::CreateLambda([this]() { return FVector2D(Portrait() ? FMath::Min(440.f, (float)LocalSize.X - 44) : 420.f, 64); }))
		.Paint([this](const FGeometry& G, FSlateWindowElementList& Out, int32 L)
	{
		ABSGame* Gm = Game(); if (!Gm) return L;
		const FVector2f S = FVector2f(G.GetLocalSize());
		const bool bZerg = Gm->Shop.bZerg;          // swarm mode: the hive's mutations
		const FString T = bZerg ? FString::Printf(TEXT("WAVE %d  //  MUTATIONS"), Gm->Shop.Wave) : FString::Printf(TEXT("WAVE %d CLEARED"), Gm->Shop.Wave);
		D::EText(Out, L, G, T, D::Fit(T, [](float Sz) { return D::Title(Sz); }, 22, 13, S.X), FVector2f(S.X * 0.5f, 2), C::Ink, 0.5f);
		FString B = bZerg ? FString(TEXT("CHOOSE ONE")) : Gm->Shop.Bonus ? FString::Printf(TEXT("+%d CLEAR BONUS"), Gm->Shop.Bonus) : FString(TEXT("WAVE HELD"));
		if (!bZerg && Gm->Shop.Interest) B += FString::Printf(TEXT("   +%d INTEREST"), Gm->Shop.Interest);
		D::Text(Out, L, G, B, D::Label(9), FVector2f(S.X * 0.5f, 46), C::Amber, 0.5f);
		return L + 2;
	}); };

	auto Reroll = [this](float W)
	{
		return PaintedButton(FVector2D(W, 44), [this, W](const FGeometry& G, FSlateWindowElementList& Out, int32 L, bool bHover, bool bPress)
		{
			ABSGame* Gm = Game(); if (!Gm) return;
			const int32 Cost = Gm->Shop.bZerg ? Gm->ZergRerollCost() : Gm->RerollCost();
			const float Have = Gm->Shop.bZerg ? Gm->Sw.Minerals : Gm->Minerals;
			const FVector2f S(W, 42), P(0, bPress ? 1.f : 0.f);
			D::RFill(Out, L, G, P, S, 12, bHover ? C::GlassHi : C::A(C::Glass, 0.6f));
			D::RLine(Out, L + 1, G, P, S, 12, C::A(C::Cyan, bHover ? 0.9f : 0.45f));
			const FString T = Cost ? FString::Printf(TEXT("REROLL  %d"), Cost) : FString(TEXT("FREE REROLL"));
			D::Text(Out, L + 2, G, T, D::Label(12), FVector2f(S.X * 0.5f, P.Y + 13), Have < Cost ? C::Red : C::Ink, 0.5f);
		}, [this]() { if (ABSGame* Gm = Game()) Gm->Reroll(); });
	};
	auto NextWave = [this](float W) { return MakeButton(TEXT("NEXT WAVE"), true, [this]() { if (ABSGame* Gm = Game()) Gm->CloseShop(); }, W); };
	auto Peek = [this](const TCHAR* Label, float W) { return MakeButton(Label, false, [this]() { bShopPeek = !bShopPeek; }, W); };
	// (Swarm mode: the hive cluster.)
	auto PeekHive = [this, Peek](float W)
	{
		return SNew(SOverlay)
			+ SOverlay::Slot()[ SNew(SBox).Visibility_Lambda([this]() { ABSGame* Gm = Game(); return Gm && Gm->Shop.bZerg ? EVisibility::Collapsed : EVisibility::Visible; })[ Peek(TEXT("ARMOURY"), W) ] ]
			+ SOverlay::Slot()[ SNew(SBox).Visibility_Lambda([this]() { ABSGame* Gm = Game(); return Gm && Gm->Shop.bZerg ? EVisibility::Visible : EVisibility::Collapsed; })[ Peek(TEXT("HIVE"), W) ] ];
	};

	auto Hint = [this](){ return SNew(SBSPaint).Size(FVector2D(330, 16)).Paint([this](const FGeometry& G, FSlateWindowElementList& Out, int32 L)
	{
		ABSGame* Gm = Game();
		D::Text(Out, L, G, Gm && Gm->Shop.bZerg ? TEXT("The hive cluster stays below.") : TEXT("Troops, tanks, SCVs and turrets stay in the armoury."), D::Body(9), FVector2f(G.GetLocalSize().X * 0.5f, 2), C::Muted, 0.5f);
		return L + 2;
	}); };
	auto Backdrop = [](float Radius, float Alpha)
	{
		return SNew(SBSPaint).Paint([Radius, Alpha](const FGeometry& G, FSlateWindowElementList& Out, int32 L)
		{
			const FVector2f S = FVector2f(G.GetLocalSize());
			if (Radius > 0) D::Panel(Out, L, G, FVector2f(0, 0), S, Radius, C::A(C::Dark, Alpha));
			else { D::Rect(Out, L, G, FVector2f(0, 0), S, C::A(C::Dark, Alpha)); D::Grit(Out, L + 1, G, FVector2f(0, 0), S, 0, 0.35f * Alpha); }
			if (Radius > 0) D::RLine(Out, L + 1, G, FVector2f(0, 0), S, Radius, C::Hair);
			D::Rect(Out, L + 1, G, FVector2f(S.X * 0.5f - 24, 0), FVector2f(48, 2), C::Cyan);
			return L + 2;
		});
	};

	// Landscape: a panel under the command bar; only the panel takes the pointer, so the armoury stays usable.
	auto Panel = SNew(SOverlay)
		+ SOverlay::Slot()[ Backdrop(18, 0.92f) ]
		+ SOverlay::Slot().Padding(12, 14, 12, 12)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[ Header() ]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 6)[ Cards ]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 10, 0, 0)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(5, 0)[ Reroll(160) ]
				+ SHorizontalBox::Slot().AutoWidth().Padding(5, 0)[ NextWave(160) ]
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 8, 0, 0)[ Hint() ]
		];

	// Portrait (phones): the whole screen, the offers scrolling between the header (with the minerals, as the command bar
	// is covered) and the buttons. ARMOURY folds it into a bar under the bunker card.
	auto Minerals = SNew(SBSPaint).Size(FVector2D(300, 18)).Paint([this](const FGeometry& G, FSlateWindowElementList& Out, int32 L)
	{
		ABSGame* Gm = Game(); if (!Gm) return L;
		const FString M = Gm->Shop.bZerg ? FString::Printf(TEXT("%d MINERALS   %d VESPENE"), FMath::FloorToInt(Gm->Sw.Minerals), FMath::FloorToInt(Gm->Sw.Gas)) : FString::Printf(TEXT("%d MINERALS"), FMath::FloorToInt(Gm->Minerals));
		D::Text(Out, L, G, M, D::Label(11), FVector2f(G.GetLocalSize().X * 0.5f, 1), C::Cyan, 0.5f);
		return L + 1;
	});
	auto Full = SNew(SOverlay)
		+ SOverlay::Slot()[ Backdrop(0, 1.f) ]
		+ SOverlay::Slot().Padding(12, 18, 12, 14)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[ Header() ]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 0, 0, 8)[ Minerals ]
			+ SVerticalBox::Slot().FillHeight(1)[ SNew(SScrollBox).Orientation(Orient_Vertical).ScrollBarVisibility(EVisibility::Collapsed) + SScrollBox::Slot().HAlign(HAlign_Center)[ Rows ] ]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 10, 0, 0)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0)[ PeekHive(100) ]
				+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0)[ Reroll(120) ]
				+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0)[ NextWave(120) ]
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 8, 0, 0)[ Hint() ]
		];
	auto Bar = SNew(SOverlay)
		+ SOverlay::Slot()[ Backdrop(14, 0.92f) ]
		+ SOverlay::Slot().Padding(8)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0)[ Peek(TEXT("SHOP"), 150) ]
			+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0)[ NextWave(170) ]
		];

	auto Landscape = [this, Open]() { return Open() && !Portrait() ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed; };
	auto PortraitFull = [this, Open]()
	{
		if (!Open()) return EVisibility::Collapsed;
		if (ABSGame* Gm = Game()) if (Gm->Shop.Wave != ShopPeekWave) { ShopPeekWave = Gm->Shop.Wave; bShopPeek = false; }   // each shop opens unfolded
		return Portrait() && !bShopPeek ? EVisibility::Visible : EVisibility::Collapsed;
	};
	auto PortraitBar = [this, Open]() { return Open() && Portrait() && bShopPeek ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed; };
	return SNew(SOverlay)
		+ SOverlay::Slot()
		[
			SNew(SOverlay).Visibility_Lambda(Landscape)
			+ SOverlay::Slot()[ SNew(SBSPaint).Visibility(EVisibility::HitTestInvisible).Paint([](const FGeometry& G, FSlateWindowElementList& Out, int32 L)
			{
				D::Rect(Out, L, G, FVector2f(0, 0), FVector2f(G.GetLocalSize()), C::A(FLinearColor::Black, 0.3f));
				return L + 1;
			}) ]
			+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Top).Padding(0, 70, 0, 0)[ Panel ]
		]
		+ SOverlay::Slot()[ SNew(SBox).Visibility_Lambda(PortraitFull)[ Full ] ]
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Top).Padding(0, 150, 0, 0)[ SNew(SBox).Visibility_Lambda(PortraitBar)[ Bar ] ];
}

// A Hyperion perk: icon, name, what it does, level pips, vespene cost.
TSharedRef<SWidget> SBSHud::MakePerk(int32 Index)
{
	const TAttribute<FVector2D> Size = TAttribute<FVector2D>::CreateLambda([this]() { return FVector2D(FMath::Min(480.f, (float)LocalSize.X - 56), 58); });
	return PaintedButton(Size, [this, Index](const FGeometry& G, FSlateWindowElementList& Out, int32 L, bool bHover, bool bPress)
	{
		const FBSPerkDef& Pk = BSDefs::Perks()[Index];
		const int32 Lv = BSMeta::Perk(Pk.Id), Cost = BSMeta::PerkCost(Pk.Id);
		const bool bMax = Cost == INDEX_NONE, bPoor = !bMax && BSMeta::Gas() < Cost;
		const FVector2f S = FVector2f(G.GetLocalSize()) - FVector2f(0, 4), P(0, bPress ? 1.f : 0.f);
		D::RFill(Out, L, G, P, S, 12, bHover ? C::GlassHi : C::Glass);
		D::RLine(Out, L + 1, G, P, S, 12, bHover ? C::A(C::Green, 0.7f) : C::Hair);
		UBSAssets* As = GM.IsValid() ? GM->GetAssets() : nullptr;
		if (As) D::Image(Out, L + 2, G, As->IconBrush(Pk.Icon), P + FVector2f(9, 8), FVector2f(38, 38));
		const float Tx = 58, RightW = 70, TextW = S.X - Tx - RightW;
		D::Text(Out, L + 2, G, Pk.Name.ToUpper(), D::Fit(Pk.Name.ToUpper(), [](float Sz) { return D::Label(Sz); }, 10.5f, 7.5f, TextW), P + FVector2f(Tx, 8), C::Ink);
		D::Text(Out, L + 2, G, Pk.Desc, D::Fit(Pk.Desc, [](float Sz) { return D::Body(Sz); }, 9.5f, 6.5f, TextW, D::BodySlack), P + FVector2f(Tx, 24), C::Muted);
		if (Pk.Costs.Num() > 1)
			for (int32 k = 0; k < Pk.Costs.Num(); k++) D::RFill(Out, L + 2, G, P + FVector2f(Tx + k * 17, 42), FVector2f(14, 4), 2, k < Lv ? C::Amber : C::Faint);
		const FString T = bMax ? FString(TEXT("MAX")) : FString::FromInt(Cost);
		D::CostPill(Out, L + 3, G, P + FVector2f(S.X - RightW * 0.5f - 4, S.Y * 0.5f), T, false, bMax ? C::Amber : bPoor ? C::Red : C::Green);
	}, [Index]() { BSMeta::BuyPerk(BSDefs::Perks()[Index].Id); });
}

TSharedRef<SWidget> SBSHud::BuildHyperion()
{
	auto Width = [this]() { return FMath::Min(480.f, (float)LocalSize.X - 56); };
	auto Head = SNew(SBSPaint).Size(TAttribute<FVector2D>::CreateLambda([Width]() { return FVector2D(Width(), 104); }))
		.Paint([](const FGeometry& G, FSlateWindowElementList& Out, int32 L)
	{
		const FVector2f S = FVector2f(G.GetLocalSize());
		D::EText(Out, L, G, TEXT("HYPERION"), D::Title(24), FVector2f(S.X * 0.5f, 4), C::Ink, 0.5f);
		D::Text(Out, L, G, FString::Printf(TEXT("%d VESPENE"), BSMeta::Gas()), D::Label(11), FVector2f(S.X * 0.5f, 48), C::Green, 0.5f);
		const FString H = TEXT("Every run banks vespene: one a wave cleared, five more every tenth wave, 25 if the Hyperion takes you off, half as much again in the arena.");
		FString H1, H2; D::SplitTwo(H, H1, H2);
		const FSlateFontInfo Hf = D::Fit(H1.Len() > H2.Len() ? H1 : H2, [](float Sz) { return D::Body(Sz); }, 9.5f, 6.5f, S.X, D::BodySlack);
		D::Text(Out, L, G, H1, Hf, FVector2f(S.X * 0.5f, 70), C::Muted, 0.5f);
		D::Text(Out, L, G, H2, Hf, FVector2f(S.X * 0.5f, 84), C::Muted, 0.5f);
		return L + 2;
	});
	// Every shop card; locked ones say what unlocks them.
	auto Codex = SNew(SBSPaint).Size(TAttribute<FVector2D>::CreateLambda([Width]()
	{
		const int32 Cols = Width() > 400 ? 3 : 2;
		return FVector2D(Width(), 26 + FMath::DivideAndRoundUp(BSDefs::Cards().Num(), Cols) * 42);
	})).Paint([this](const FGeometry& G, FSlateWindowElementList& Out, int32 L)
	{
		const FVector2f S = FVector2f(G.GetLocalSize());
		const int32 Cols = S.X > 400 ? 3 : 2;
		const float Cw = (S.X - (Cols - 1) * 6) / Cols;
		UBSAssets* As = GM.IsValid() ? GM->GetAssets() : nullptr;
		D::Text(Out, L, G, TEXT("CARDS"), D::Label(9), FVector2f(S.X * 0.5f, 4), C::Muted, 0.5f);
		const TArray<FBSCardDef>& All = BSDefs::Cards();
		for (int32 i = 0; i < All.Num(); i++)
		{
			const FBSCardDef& Cd = All[i];
			const bool bOpen = BSMeta::Unlocked(Cd);
			const FBSRarity& R = BSDefs::FindRarity(Cd.Rarity);
			const FVector2f P((i % Cols) * (Cw + 6), 26 + (i / Cols) * 42);
			D::RFill(Out, L, G, P, FVector2f(Cw, 36), 8, C::Faint);
			D::Rect(Out, L + 1, G, P + FVector2f(0, 4), FVector2f(2, 28), R.Color);
			FString Ic = Cd.Icon;
			if (!Cd.Up.IsEmpty()) Ic = BSDefs::FindUpgrade(Cd.Up)->Icons[0];
			if (As) D::Image(Out, L + 1, G, As->IconBrush(Ic), P + FVector2f(8, 4), FVector2f(28, 28), bOpen ? FLinearColor::White : FLinearColor(0.25f, 0.25f, 0.28f));
			const FString Name = BSMeta::CardName(Cd).ToUpper();
			const int32 N = BSMeta::Buys(Cd.Id);
			const FString Sub = !bOpen ? BSMeta::UnlockText(Cd) : N ? FString::Printf(TEXT("Bought %dx"), N) : R.Name.ToLower();
			D::Text(Out, L + 1, G, Name, D::Fit(Name, [](float Sz) { return D::Label(Sz); }, 8.5f, 6, Cw - 46), P + FVector2f(42, 5), bOpen ? C::Ink : C::Muted);
			D::Text(Out, L + 1, G, Sub, D::Fit(Sub, [](float Sz) { return D::Body(Sz); }, 8.5f, 6, Cw - 46, D::BodySlack), P + FVector2f(42, 19), C::Muted);
		}
		return L + 3;
	});

	TSharedRef<SVerticalBox> Perks = SNew(SVerticalBox);
	for (int32 i = 0; i < BSDefs::Perks().Num(); i++) Perks->AddSlot().AutoHeight()[ MakePerk(i) ];

	auto Panel = SNew(SOverlay)
		+ SOverlay::Slot()[ SNew(SBSPaint).Paint([](const FGeometry& G, FSlateWindowElementList& Out, int32 L)
		{
			const FVector2f S = FVector2f(G.GetLocalSize());
			D::Panel(Out, L, G, FVector2f(0, 0), S, 18, C::A(C::Dark, 0.95f));
			D::RLine(Out, L + 1, G, FVector2f(0, 0), S, 18, C::Hair);
			D::Rect(Out, L + 1, G, FVector2f(S.X * 0.5f - 24, 0), FVector2f(48, 2), C::Amber);
			return L + 2;
		}) ]
		+ SOverlay::Slot().Padding(14, 16, 14, 14)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().FillHeight(1.f)
			[
				SNew(SScrollBox).Orientation(Orient_Vertical).ScrollBarVisibility(EVisibility::Collapsed)
				+ SScrollBox::Slot()[ Head ]
				+ SScrollBox::Slot().Padding(0, 6)[ Perks ]
				+ SScrollBox::Slot().Padding(0, 8)[ Codex ]
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 10, 0, 0)[ MakeButton(TEXT("BACK"), true, [this]() { bHyperion = false; }, 160) ]
		];

	return SNew(SOverlay)
		.Visibility_Lambda([this]() { return bHyperion && !Game() ? EVisibility::Visible : EVisibility::Collapsed; })
		+ SOverlay::Slot()[ SNew(SBSPaint).Paint([](const FGeometry& G, FSlateWindowElementList& Out, int32 L)
		{
			D::Rect(Out, L, G, FVector2f(0, 0), FVector2f(G.GetLocalSize()), C::A(C::Dark, 0.8f));
			return L + 1;
		}) ]
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Fill).Padding(0, 20)
		[
			SNew(SBox).WidthOverride_Lambda([Width]() { return Width() + 28; })[ Panel ]
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
	const float Have = Gm->bSwarm ? Gm->Sw.Minerals : Gm->Minerals;      // swarm mode: the swarm's
	const float Diff = Have - ShownMinerals;
	if (Diff > 1) MineralFlash = FMath::Min(1.f, MineralFlash + Dt * 4 * FMath::Min(1.f, Diff / 20));
	ShownMinerals += Diff * FMath::Min(1.f, Dt * 8.f);
	if (FMath::Abs(Have - ShownMinerals) < 0.5f) ShownMinerals = Have;
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
	if (Gm->Bc.IsValid()) { if (Gm->Bc->Hp < BcLastHp) BcHitAt = Now; BcLastHp = Gm->Bc->Hp; }
	else BcLastHp = 0;
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
	// The finale: a long health bar per boss under the toast.
	{
		const TArray<TSharedPtr<FBSEnemy>> Bosses = Game()->Bosses();
		const float W = FMath::Min(420.f, S.X - 40), X0 = S.X * 0.5f - W * 0.5f;
		const FSlateFontInfo F = D::Label(9);
		for (int32 i = 0; i < Bosses.Num(); i++)
		{
			const FBSEnemy& E = *Bosses[i];
			const float Y = 184 + i * 34, Pct = FMath::Clamp(E.Hp / FMath::Max(1.f, E.MaxHp), 0.f, 1.f);
			const bool bRun = E.Def->bUntouchable;   // Kerrigan: no health to whittle down, only the Hyperion's countdown
			const FString Hp = bRun ? FString(TEXT("UNSTOPPABLE  //  RUN")) : FString::Printf(TEXT("%d / %d"), FMath::CeilToInt(E.Hp), FMath::RoundToInt(E.MaxHp));
			D::RFill(Out, L, G, FVector2f(X0 - 8, Y - 4), FVector2f(W + 16, 30), 8, C::A(C::Dark, 0.85f));
			D::Text(Out, L + 1, G, E.Def->Name.ToUpper(), F, FVector2f(X0, Y), C::Ink);
			D::Text(Out, L + 1, G, Hp, F, FVector2f(X0 + W - D::Measure(Hp, F).X, Y), bRun ? C::Red : C::Violet);
			D::RFill(Out, L + 1, G, FVector2f(X0, Y + 15), FVector2f(W, 6), 3, C::A(FLinearColor::Black, 0.6f));
			D::RFill(Out, L + 2, G, FVector2f(X0, Y + 15), FVector2f(FMath::Max(3.f, W * Pct), 6), 3, bRun || Pct < 0.25f ? C::Red : C::Violet);
		}
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
		const FLinearColor Acc = BannerText.StartsWith(TEXT("VICTORY")) ? C::Green : bHeavy ? C::Red : C::Amber;
		const float Half = S.X * 0.42f * Ease;
		D::Line(Out, L + 1, G, FVector2f(S.X * 0.5f - Half, Y), FVector2f(S.X * 0.5f + Half, Y), C::A(Acc, 0.8f * Alpha), 1.5f);
		D::Line(Out, L + 1, G, FVector2f(S.X * 0.5f - Half, Y + H), FVector2f(S.X * 0.5f + Half, Y + H), C::A(Acc, 0.8f * Alpha), 1.5f);
		FString Main = BannerText, Sub = TEXT("INCOMING");
		const int32 Sep = BannerText.Find(TEXT("//"));
		if (Sep != INDEX_NONE) { Main = BannerText.Left(Sep).TrimEnd(); Sub = BannerText.Mid(Sep + 2).TrimStart(); }
		int32 Num = 0;
		if (Main.StartsWith(TEXT("WAVE "))) { Num = FCString::Atoi(*Main.Mid(5)); Main = FString::Printf(TEXT("WAVE %02d"), Num); }
		D::Text(Out, L + 2, G, Sub, D::Label(9.5f), FVector2f(S.X * 0.5f, Y + 12), C::A(Acc, Alpha), 0.5f);
		D::EText(Out, L + 2, G, Main, D::Fit(Main, [](float Sz) { return D::Title(Sz); }, 30, 16, S.X - 40), FVector2f(S.X * 0.5f, Y + 28), C::A(C::Ink, Alpha), 0.5f);
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

	// Swarm mode: the base's taps. Hatcheries ready to inject glow green, evolution chambers show their research (amber ring:
	// idle), the chrysalis pulses pink while Kerrigan can be summoned.
	if (Gm->bSwarm && Gm->Mode == EBSMode::Lane)
	{
		const float Pulse = 0.55f + 0.45f * FMath::Sin(NowS * 3.85);
		auto Ring = [&](const FVector2D& At, float R, const FLinearColor& Col, float Frac)
		{
			const float Gz = BSTerrain::Height(At.X, At.Y) + 0.1f;
			const int32 Seg = 32, N = FMath::Max(2, FMath::CeilToInt(Seg * Frac));
			D::FPts Pts;
			for (int32 k = 0; k <= N; k++)
			{
				const float A = -PI / 2 + 2 * PI * Frac * k / N;
				FVector2D P;
				if (!WorldToLocal(BSSpace::ToWorld(At.X + FMath::Cos(A) * R, At.Y + FMath::Sin(A) * R, Gz), P)) return;
				Pts.Add(FVector2f(P));
			}
			D::Lines(Out, L + 3, G, Pts, Col, 3.f, Frac >= 1);
		};
		const TArray<ABSGame::FHatch> Hs = Gm->HatchSpots();
		for (int32 i = 0; i < Hs.Num(); i++) if (Gm->InjectReady(i)) Ring(Hs[i].P, i ? 2.2f : 3.f, FLinearColor(0.55f, 0.88f, 0.54f, Pulse), 1);
		for (int32 i = 0; i < BSSwarm::ChamberMax; i++)
		{
			if (!Gm->Sw.ChamberAt[i]) continue;
			const auto& R = Gm->Sw.Research[i];
			if (!R.Id.IsEmpty()) Ring(BSSwarm::ChamberSpots[i], 1.8f, FLinearColor(0.37f, 0.89f, 1.f), FMath::Clamp(R.T / FMath::Max(0.01f, R.Dur), 0.02f, 1.f));
			else Ring(BSSwarm::ChamberSpots[i], 1.8f, FLinearColor(1.f, 0.75f, 0.24f, Pulse), 1);
		}
		BSSwarm::FCost Hc;
		if (Gm->Sw.Parts.Contains(TEXT("chrysalis")) && Gm->SwarmState(TEXT("hero"), FString()).IsEmpty() && Gm->SwarmCost(TEXT("hero"), FString(), Hc) && Gm->Sw.Minerals >= Hc.M && Gm->Sw.Gas >= Hc.G)
			Ring(BSSwarm::Chrysalis, 1.6f, FLinearColor(1.f, 0.48f, 0.85f, Pulse), 1);
	}

	// Swarm mode: where Kerrigan was sent (bright while she is on her way, dim while she holds it).
	if (const FBSEnemy* He = Gm->bSwarm ? Gm->HeroAlive() : nullptr; He && (He->bOrder || He->bHold))
	{
		const FVector2D Spot = He->bOrder ? He->Order : He->Hold;
		const float Gz = BSTerrain::Height(Spot.X, Spot.Y) + 0.05f;
		D::FPts Ring;
		for (int32 k = 0; k < 28; k++)
		{
			const float A = k * 2 * PI / 28;
			FVector2D P;
			if (!WorldToLocal(BSSpace::ToWorld(Spot.X + FMath::Cos(A) * 0.8f, Spot.Y + FMath::Sin(A) * 0.8f, Gz), P)) { Ring.Reset(); break; }
			Ring.Add(FVector2f(P));
		}
		if (Ring.Num()) D::Lines(Out, L + 3, G, Ring, He->bOrder ? FLinearColor(1.f, 0.48f, 0.85f) : FLinearColor(1.f, 0.48f, 0.85f, 0.45f), 2.f, true);
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
	if (bRadar)
	{
		// Troopers outside: small dots; goliaths, thors and the odin: discs by size; the battlecruiser and air support: green
		// on cyan.
		for (const TSharedPtr<FBSTrooper>& M : Gm->Squad) D::Disc(Out, L + 9, G, ToRadar(M->X, M->Y), 1.1f, C::Green, 6);
		for (const TSharedPtr<FBSMech>& M : Gm->Mechs)
			D::Disc(Out, L + 9, G, ToRadar(M->X, M->Y), M->Def->Key == TEXT("odin") ? 4.5f : M->Def->Key == TEXT("thor") ? 3.5f : 2.5f, C::Green, 10);
		if (Gm->Bc.IsValid()) { D::Disc(Out, L + 9, G, ToRadar(Gm->Bc->X, Gm->Bc->Y), 6.f, C::Cyan, 12); D::Disc(Out, L + 10, G, ToRadar(Gm->Bc->X, Gm->Bc->Y), 3.f, C::Green, 10); }
		for (const TSharedPtr<FBSBanshee>& V : Gm->VikingWing) D::Disc(Out, L + 9, G, ToRadar(V->X, V->Y), 2.2f, C::Cyan, 8);
		for (const TSharedPtr<FBSBanshee>& V : Gm->Banshees) D::Disc(Out, L + 9, G, ToRadar(V->X, V->Y), 2.2f, C::Cyan, 8);
		for (const TSharedPtr<FBSBanshee>& V : Gm->Wraiths) D::Disc(Out, L + 9, G, ToRadar(V->X, V->Y), 2.2f, C::Cyan, 8);
		if (Gm->Vessel.IsValid()) D::Disc(Out, L + 9, G, ToRadar(Gm->Vessel->X, Gm->Vessel->Y), 4.f, C::Cyan, 10);
	}
	if (bRadar) D::Disc(Out, L + 9, G, Rc, 3, C::Green, 10);

	// Threat markers on the screen edge for off-screen zerg, kept clear of the HUD (not in swarm mode: they are the
	// player's own).
	if (Gm->bSwarm) return L + 12;
	const float TopY = 150, BottomY = S.Y - (bPortrait ? (bArmouryOpen ? 300.f : 190.f) : 130.f);
	const float RightX = S.X - (bPortrait ? 16.f : 256.f);
	const FVector2f Mid((16 + RightX) * 0.5f, (TopY + BottomY) * 0.5f);
	TMap<int32, int32> Buckets;
	for (const TSharedPtr<FBSEnemy>& E : Gm->Enemies)
	{
		if (E->Def->bTumor || E->Def->bAmbient) continue;   // creep tumors stay put, ambient overlords never attack: not threats to point at
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
	// Onto the terrain (cliffs, plateaus and the high open ground in the north), not just the plane z = 0: a few
	// refinements of the height under the hit.
	float T = -Origin.Z / Dir.Z;
	for (int32 k = 0; k < 4 && T >= 0; k++)
	{
		const FVector P = Origin + Dir * T;
		T = (BSTerrain::Height(P.Y / BSSpace::UnitsPerMetre, P.X / BSSpace::UnitsPerMetre) * BSSpace::UnitsPerMetre - Origin.Z) / Dir.Z;
	}
	if (T < 0) return;
	const FVector Hit = Origin + Dir * T;
	// Swarm mode: a tap on the base injects, evolves or summons; anywhere else it sends Kerrigan there.
	if (Gm->bSwarm)
	{
		const float Sx = Hit.Y / BSSpace::UnitsPerMetre, Sy = Hit.X / BSSpace::UnitsPerMetre;
		if (!Gm->BaseTap(Sx, Sy)) Gm->HeroOrder(Sx, Sy);
		return;
	}
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
	if (Gm->bSwarm) return true;                      // (in swarm mode the squad card is the enemy's)
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
		if (bHyperion && K == EKeys::Escape) { bHyperion = false; return FReply::Handled(); }
		if (K == EKeys::One && GM.IsValid()) { GM->StartGame(EBSMode::Lane); return FReply::Handled(); }
		if (K == EKeys::Two && GM.IsValid()) { GM->StartGame(EBSMode::Arena); return FReply::Handled(); }
		if (K == EKeys::Three && GM.IsValid() && (FParse::Param(FCommandLine::Get(), TEXT("BSSwarm")) || BSMeta::SwarmOpen())) { GM->StartGame(EBSMode::Lane, true); return FReply::Handled(); }
		return FReply::Unhandled();
	}
	const float H = LocalSize.Y * DpiScale;
	// Shop: 1-5 buy, X rerolls, Enter or Space starts the next wave.
	if (Gm->Shop.bOpen)
	{
		const FKey Nums[] = { EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five };
		for (int32 i = 0; i < 5; i++) if (K == Nums[i]) { Gm->BuyCard(i); return FReply::Handled(); }
		if (K == EKeys::X) { Gm->Reroll(); return FReply::Handled(); }
		if (K == EKeys::Enter || K == EKeys::SpaceBar) { Gm->CloseShop(); return FReply::Handled(); }
	}
	// Swarm mode: K summons Kerrigan (or sends her back to advancing), Q W E R T Y her spells; the Terrans' S A R are not
	// the player's.
	if (Gm->bSwarm)
	{
		if (K == EKeys::K) { HeroButton(); return FReply::Handled(); }
		if (K == EKeys::SpaceBar && !Gm->Shop.bOpen) { Gm->BroodHold(); return FReply::Handled(); }
		for (const BSSwarm::FAbility& A : BSSwarm::Abilities()) if (K == FKey(A.Key)) { Gm->HeroCast(A.Id); return FReply::Handled(); }
		if (K == EKeys::S || K == EKeys::A || K == EKeys::R) return FReply::Handled();
	}
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

#include "BSTerrain.h"

namespace
{
	bool bActive = false;

	float Smooth(float A, float B, float X) { const float T = FMath::Clamp((X - A) / (B - A), 0.f, 1.f); return T * T * (3 - 2 * T); }

	float SegDist(float Px, float Py, float Ax, float Ay, float Bx, float By)
	{
		const float Dx = Bx - Ax, Dy = By - Ay, T = FMath::Clamp(((Px - Ax) * Dx + (Py - Ay) * Dy) / (Dx * Dx + Dy * Dy), 0.f, 1.f);
		return FMath::Sqrt(FMath::Square(Px - Ax - Dx * T) + FMath::Square(Py - Ay - Dy * T));
	}
	// Cheap smooth noise for ragged cliff edges.
	float Noise(float X, float Y)
	{
		return 0.5f * FMath::Sin(X * 1.7f + FMath::Sin(Y * 0.9f) * 2) * FMath::Sin(Y * 1.3f + FMath::Sin(X * 0.7f) * 2) + 0.25f * FMath::Sin(X * 3.1f - Y * 2.3f);
	}

	// Ramps: from the arm floor (A) up to the plateau (B).
	struct FRamp { FVector2D A, B; float W; };
	const FRamp Ramps[2] = { { FVector2D(-7.2f, 13.7f), FVector2D(-11.1f, 10.9f), 1.3f }, { FVector2D(7.2f, 13.7f), FVector2D(11.1f, 10.9f), 1.3f } };
	// The arms run from the fork (2, 9.5) out to (14.5, 27), mirrored.
	constexpr float ArmAx = 2.0f, ArmAy = 9.5f, ArmBx = 14.5f, ArmBy = 27.f, ArmR = 3.6f;

	// Ramp influence K (0 = none, 1 = on the ramp) and progress T along it (0 at the bottom, 1 at the top).
	bool RampAt(float X, float Y, float& K, float& T)
	{
		for (const FRamp& R : Ramps)
		{
			const float Dx = R.B.X - R.A.X, Dy = R.B.Y - R.A.Y, L2 = Dx * Dx + Dy * Dy, L = FMath::Sqrt(L2);
			const float Tt = ((X - R.A.X) * Dx + (Y - R.A.Y) * Dy) / L2;
			if (Tt < -0.25f || Tt > 1.25f) continue;
			const float Perp = FMath::Abs(((X - R.A.X) * Dy - (Y - R.A.Y) * Dx) / L);
			const float Kk = 1 - Smooth(R.W, R.W + 0.6f, Perp);
			if (Kk > 0) { K = Kk * (1 - Smooth(1, 1.25f, Tt)) * (1 - Smooth(0, -0.25f, Tt)); T = FMath::Clamp(Tt, 0.f, 1.f); return true; }
		}
		return false;
	}
}

namespace BSTerrain
{
	void SetMode(bool bLane) { bActive = bLane; }
	bool Active() { return bActive; }

	float LowSdf(float X, float Y)
	{
		const float Ax = FMath::Abs(X);
		const float W = 3.9f + 3.9f * FMath::Clamp((3 - Y) / 2.5f, 0.f, 1.f);     // a narrow stem that widens into a pad around the bunker and tanks
		const float Stem = FMath::Max(Ax - W, Y - 10.5f);
		const float Arm = SegDist(Ax, Y, ArmAx, ArmAy, ArmBx, ArmBy) - ArmR;     // the two arms (mirrored)
		const float North = 26.5f - Y;                                         // open ground to the north where waves gather
		return FMath::Min3(Stem, Arm, North) + Noise(X, Y) * 0.35f;
	}

	float Height(float X, float Y)
	{
		if (!bActive) return 0;
		float Hh = H * Smooth(0.f, 1.15f, LowSdf(X, Y));
		float K = 0, T = 0;
		if (RampAt(X, Y, K, T) && K > 0) Hh = Hh + (H * T - Hh) * K;
		return Hh;
	}

	float Slope(float X, float Y)
	{
		const float E = 0.15f;
		return FMath::Sqrt(FMath::Square(Height(X + E, Y) - Height(X - E, Y)) + FMath::Square(Height(X, Y + E) - Height(X, Y - E))) / (2 * E);
	}

	bool Walkable(float X, float Y) { return !bActive || Slope(X, Y) < 1.05f; }
	bool OnHigh(float X, float Y) { return bActive && Height(X, Y) > H * 0.6f; }

	TArray<FVector2D> Route(float X, float Y, bool bHigh)
	{
		TArray<FVector2D> Pts;
		if (!bActive) return Pts;
		const float S = X == 0 ? (FMath::RandBool() ? -1.f : 1.f) : FMath::Sign(X);
		// From the open ground in the north: skirt the mesa's tip, then drop into the arm.
		if (Y > 24.5f)
		{
			Pts.Add(FVector2D(12.8f * S + FMath::FRand() - 0.5f, 27.4f));
			Pts.Add(FVector2D(11.8f * S + (FMath::FRand() - 0.5f) * 0.8f, 22.5f));
		}
		if (bHigh)
		{
			const FRamp& R = Ramps[S < 0 ? 0 : 1];
			Pts.Add(FVector2D(R.A.X - 0.4f * S, R.A.Y + 0.6f));
			Pts.Add(FVector2D(R.B.X + 0.3f * S, R.B.Y - 0.4f));
			// Perch on the cliff edge overlooking the bunker.
			Pts.Add(FVector2D((5.7f + FMath::FRand() * 0.5f) * S, 3.3f + FMath::FRand() * 1.0f));
		}
		else Pts.Add(FVector2D(1.6f * S + (FMath::FRand() - 0.5f) * 1.5f, 9));
		return Pts;
	}

	TArray<FVector2D> LedgeRoute(int32 Side)
	{
		const float S = Side < 0 ? -1.f : 1.f;
		const FRamp& R = Ramps[Side < 0 ? 0 : 1];
		return { FVector2D(2.2f * S, 5.5f), FVector2D(2.9f * S, 10.4f), FVector2D(R.A.X - 0.4f * S, R.A.Y + 0.6f), FVector2D(R.B.X + 0.3f * S, R.B.Y - 0.4f) };
	}

	FVector2D ArmPoint(int32 Side, float T, float Off)
	{
		const float Dx = ArmBx - ArmAx, Dy = ArmBy - ArmAy, L = FMath::Sqrt(Dx * Dx + Dy * Dy);
		return FVector2D(Side * (ArmAx + Dx * T + Off * Dy / L), ArmAy + Dy * T - Off * Dx / L);
	}
}

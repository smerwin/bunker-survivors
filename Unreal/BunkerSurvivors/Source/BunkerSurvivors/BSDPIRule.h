// UI scale: portrait screens get a ~410-unit-wide layout (phone-like); landscape screens a ~620-unit-tall one.
#pragma once

#include "CoreMinimal.h"
#include "Engine/DPICustomScalingRule.h"
#include "BSDPIRule.generated.h"

UCLASS()
class UBSDPIRule : public UDPICustomScalingRule
{
	GENERATED_BODY()
public:
	virtual float GetDPIScaleBasedOnSize(FIntPoint Size) const override
	{
		if (Size.X <= 0 || Size.Y <= 0) return 1.f;
		const float S = Size.Y > Size.X ? Size.X / 410.f : Size.Y / 620.f;
		return FMath::Clamp(S, 0.6f, 4.f);
	}
};

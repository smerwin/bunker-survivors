#include "BSMeta.h"
#include "Misc/ConfigCacheIni.h"

namespace
{
	const TCHAR* Section = TEXT("BunkerSurvivors.Meta");
	int32 Get(const FString& Key) { int32 V = 0; GConfig->GetInt(Section, *Key, V, GGameUserSettingsIni); return V; }
	void Set(const FString& Key, int32 V) { GConfig->SetInt(Section, *Key, V, GGameUserSettingsIni); }
	void Save() { GConfig->Flush(false, GGameUserSettingsIni); }
	TArray<const FBSCardDef*> Locked()
	{
		TArray<const FBSCardDef*> Out;
		for (const FBSCardDef& C : BSDefs::Cards()) if (!BSMeta::Unlocked(C)) Out.Add(&C);
		return Out;
	}
}

namespace BSMeta
{
	int32 Gas() { return Get(TEXT("Gas")); }
	int32 Best() { return Get(TEXT("Best")); }
	int32 Perk(const FString& Id) { return Get(TEXT("Perk.") + Id); }
	int32 Buys(const FString& CardId) { return Get(TEXT("Buy.") + CardId); }

	int32 PerkCost(const FString& Id)
	{
		const FBSPerkDef* P = BSDefs::FindPerk(Id);
		const int32 L = Perk(Id);
		return P && L < P->Costs.Num() ? P->Costs[L] : INDEX_NONE;
	}

	bool BuyPerk(const FString& Id)
	{
		const int32 Cost = PerkCost(Id);
		if (Cost == INDEX_NONE || Gas() < Cost) return false;
		Set(TEXT("Gas"), Gas() - Cost);
		Set(TEXT("Perk.") + Id, Perk(Id) + 1);
		Save();
		return true;
	}

	bool Unlocked(const FBSCardDef& C)
	{
		if (C.UnlockBest && Best() < C.UnlockBest) return false;
		if (!C.UnlockBuys.IsEmpty() && Buys(C.UnlockBuys) < C.UnlockBuysN) return false;
		return true;
	}

	FString CardName(const FBSCardDef& C)
	{
		if (!C.Name.IsEmpty()) return C.Name;
		const FBSUpgradeDef* U = BSDefs::FindUpgrade(C.Up);
		return U ? U->Name : C.Id;
	}

	FString UnlockText(const FBSCardDef& C)
	{
		if (C.UnlockBest) return FString::Printf(TEXT("Clear wave %d"), C.UnlockBest);
		if (C.UnlockBuys.IsEmpty()) return FString();
		const FBSCardDef* By = BSDefs::FindCard(C.UnlockBuys);
		return FString::Printf(TEXT("Buy %s %d time%s"), By ? *CardName(*By) : *C.UnlockBuys, C.UnlockBuysN, C.UnlockBuysN > 1 ? TEXT("s") : TEXT(""));
	}

	TArray<const FBSCardDef*> RecordBuy(const FString& CardId)
	{
		const TArray<const FBSCardDef*> Was = Locked();
		Set(TEXT("Buy.") + CardId, Buys(CardId) + 1);
		Save();
		return Was.FilterByPredicate([](const FBSCardDef* C) { return Unlocked(*C); });
	}

	FRunResult EndRun(bool bArena, int32 WavesCleared, bool bEvacuated)
	{
		const TArray<const FBSCardDef*> Was = Locked();
		FRunResult R;
		R.Gas = FMath::RoundToInt((WavesCleared * BSDefs::GasPerWave + (WavesCleared / 10) * BSDefs::GasMilestone + (bEvacuated ? BSDefs::GasEvac : 0.f)) * (bArena ? BSDefs::GasArena : 1.f));
		Set(TEXT("Gas"), Gas() + R.Gas);
		Set(TEXT("Best"), FMath::Max(Best(), WavesCleared));
		Save();
		R.Unlocked = Was.FilterByPredicate([](const FBSCardDef* C) { return Unlocked(*C); });
		return R;
	}
}

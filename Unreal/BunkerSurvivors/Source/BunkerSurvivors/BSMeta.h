// Progress between runs: vespene earned by every run, perks bought with it at the Hyperion (BSDefs::Perks), and how
// many times each shop card has been bought over all runs, which unlocks new cards (FBSCardDef Unlock*). Kept in the
// user settings ini next to the best scores; matches client/js/meta.js.
#pragma once

#include "CoreMinimal.h"
#include "BSTypes.h"

namespace BSMeta
{
	int32 Gas();
	int32 Best();                                       // most waves cleared in one run, either mode
	int32 Perk(const FString& Id);
	int32 PerkCost(const FString& Id);                  // INDEX_NONE once maxed
	bool BuyPerk(const FString& Id);
	int32 Buys(const FString& CardId);
	bool Unlocked(const FBSCardDef& C);
	FString UnlockText(const FBSCardDef& C);
	FString CardName(const FBSCardDef& C);
	// A card bought in a run; returns the cards this purchase unlocked.
	TArray<const FBSCardDef*> RecordBuy(const FString& CardId);
	// The ladder rung reached (BSDefs::Ladder): forced (playtesting, -BSRung=N or -BSTechMax; INDEX_NONE: not forced) or earned.
	extern int32 Force;
	int32 Rung();
	// The rung on which a Terran unit or ability (an armoury item, ability or card id) or a zerg strain joins the fight.
	int32 TerranRung(const FString& Id);
	int32 ZergRung(const FString& Key);
	// Waves a run on rung I (0 is the first) must clear to open the next.
	inline int32 LadderWaves(int32 I) { return BSDefs::LadderWaves + I; }
	// The run is over (bEvacuated: the Hyperion took you off): bank its vespene and best wave. A run that clears enough
	// waves opens the next rung of the ladder (Rung: that rung's index, else INDEX_NONE).
	struct FRunResult { int32 Gas = 0; TArray<const FBSCardDef*> Unlocked; int32 Rung = INDEX_NONE; };
	FRunResult EndRun(bool bArena, int32 WavesCleared, bool bEvacuated);
}

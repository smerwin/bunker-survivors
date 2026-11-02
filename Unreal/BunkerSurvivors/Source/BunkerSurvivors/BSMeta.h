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
	// Swarm mode: open once a run has escaped Kerrigan on the Hyperion.
	bool SwarmOpen();
	// The run is over (bEvacuated: the Hyperion took you off; in swarm mode, the bunker razed): bank its vespene and best
	// wave (not for a swarm run). bSwarm in the result: this run opened swarm mode.
	struct FRunResult { int32 Gas = 0; TArray<const FBSCardDef*> Unlocked; bool bSwarm = false; };
	FRunResult EndRun(bool bArena, bool bSwarmRun, int32 WavesCleared, bool bEvacuated);
}

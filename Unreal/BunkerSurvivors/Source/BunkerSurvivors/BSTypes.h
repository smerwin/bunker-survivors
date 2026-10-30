// Unit, upgrade and sound definitions. Stats follow StarCraft II (Faster speed); the simulation runs in
// SC2 world units (1 unit = 1 m = 100 Unreal units). Ported from client/js/units.js.
#pragma once

#include "CoreMinimal.h"

enum class EBSProjectile : uint8 { None, Acid, Spine, Bullet, Glaive, Spines, Brood, Guardian, Spores, Bile };

struct FBSZergDef
{
	FString Key, Name, Model, Death, Icon;
	float Scale = 1.f;
	float Hp = 35, Armor = 0, Dmg = 5, DmgLight = 0, DmgArmored = 0, DmgStructure = 0;
	float Cd = 1, Range = 0.1f, Speed = 3, Radius = 0.375f;
	bool bLight = false, bArmored = false, bFlying = false, bSuicide = false, bBurrows = false, bBroodlings = false;
	float Fly = 0, Splash = 0, Bounce = 0, Cleave = 0, Life = 0;
	int32 Bounty = 5, Budget = 2, Unlock = 1;
	float Weight = 1, HitFrac = 0.4f, Height = 1, DeathHold = 3.5f;
	TArray<FString> AttackAnims;      // candidate sequence names, best first ("" = no attack anim)
	TArray<FString> Variants;         // alternative models (strains and skins) mixed in for variety
	// With VariantFrom the variety dials up slowly: the first variant joins at that wave, one more every VariantStep waves,
	// and the share of variants grows a little each wave up to the usual 45%.
	int32 VariantFrom = 0, VariantStep = 1;
	FString ImpactFx;                 // SC2 effect played where a melee hit lands
	EBSProjectile Projectile = EBSProjectile::None;
	FString SoundAttack, SoundDeath;
	// Late game (joins once Late tech cards are unlocked): winged zerglings hop over cliffs, scourge and devourers hunt
	// Terran air, guardians single out mechs, defilers and vipers cast; overlords and nydus worms are special events.
	int32 Late = 0;
	bool bHops = false, bAntiAir = false, bPreferMech = false, bTransport = false, bNydus = false;
	float Acid = 0;                   // devourer: seconds the target fires slower
	FString Spell;                    // "defiler" (dark swarm / plague), "abduct" (viper) or "broodling" (brood queen)
	int32 Max = 0;                    // rare units (brood queen): never more than Max on the field and on their way
	// Creep (see BSCreep): queens plant tumors, tumors spread creep, creep overlords generate it from the sky.
	bool bTumors = false, bTumor = false, bCreeper = false;
	// Wave 100 finale bosses: "kerrigan" (Leaping Strike, Apocalypse) or "leviathan" (bombards, spawns mutalisks). Bosses
	// keep their own hit points (no late-wave toughening) and always spawn, whatever the alive cap.
	FString Boss;
	float Bombard = 0;                // leviathan bile: splash radius of each shot
};

// Creep: a coarse grid (Cell metres) of coverage 0..1 that grows where a source reaches and slowly fades (lingers) where
// none does. Zerg ground units on creep move faster (SC2: +30%). Firebat flames burn it back and scorch the ground for a
// while, and singe tumors. Queens join from FromWave, creep overlords two waves later. The swarm may keep more tumors
// alive each wave (TumorBase + TumorPerWave per creep wave, up to TumorMax), so the creep spreads further as the waves go
// on. Port of BS.CREEP in units.js.
namespace BSCreep
{
	constexpr float Cell = 1, Update = 0.25f, On = 0.35f, Grow = 0.6f, Decay = 0.012f, Speed = 1.3f;
	constexpr int32 FromWave = 8;
	constexpr float TumorRadius = 4.5f, TumorGrowth = 0.45f, TumorSpread = 12, TumorHop0 = 3, TumorHop1 = 4.5f, TumorMinBunker = 6, TumorGap = 2;
	constexpr int32 TumorBase = 4, TumorPerWave = 3, TumorMax = 30;
	constexpr float QueenEvery = 9, QueenFirst0 = 3, QueenFirst1 = 6;
	constexpr float OverlordRadius = 3.2f, OverlordLife = 50, OverlordDrift = 0.3f, OverlordMinDist = 6;
	constexpr float BurnRadius = 1.6f, BurnRegrow = 5, BurnTumorDmg = 10;
}

struct FBSInfantryDef
{
	FString Key, Name, Icon, Sound, Desc, Death;
	float Hp = 45, Dmg = 6, DmgLight = 0, DmgArmored = 0, Cd = 0.61f, Range = 5, Splash = 0, Slow = 0;
	bool bGroundOnly = false;
	int32 Cost = 50;
	float StimHp = 10;
};

// High-tech walkers (goliath, thor, odin). G* = anti-ground weapon, A* = anti-air; Shots per volley.
struct FBSMechDef
{
	FString Key, Name;
	float Hp = 150, Armor = 1, Radius = 0.7f, Speed = 3;
	int32 Cost = 250, Max = 4;
	float GDmg = 12, GCd = 0.9f, GRange = 6, GSplash = 0; int32 GShots = 2;
	float ADmg = 10, ACd = 1.5f, ARange = 8, ASplash = 0; int32 AShots = 2;
};

// High tech for the armoury: a shop card puts each there (see FBSCardDef), where it is bought like a trooper.
struct FBSTechCard { FString Id, Name, Icon, Banner; };

struct FBSUpgradeDef
{
	FString Key, Name, Desc;
	TArray<FString> Icons;            // one per level (or a single icon)
	TArray<int32> Costs;              // one per level
};

// A shop card (see ABSGame::EndWave). Up: one level of that research (its costs, icons and text); Tech: puts that
// BSDefs::Tech entry in the armoury; otherwise a passive with Max levels at Costs[level]. Req: what this run needs
// before it is offered ("tank", "stim", "hisec", "medivac", "bc", or a research or "wave" and a number). Unlock: what
// it takes over all runs before the card joins the pool at all: UnlockBuys bought UnlockBuysN times, or wave
// UnlockBest cleared (see BSMeta).
struct FBSCardDef
{
	FString Id, Up, Tech, Name, Icon, Desc, Rarity, Req, UnlockBuys;
	int32 Max = 1, UnlockBuysN = 0, UnlockBest = 0;
	TArray<int32> Costs;
};

// Rarer cards turn up later (From: first wave they can be offered) and less often (Weight).
struct FBSRarity { FString Key, Name; float Weight = 1; int32 From = 1; FLinearColor Color; };

// A perk at the Hyperion, bought with vespene between runs (see BSMeta); Costs: one per level.
struct FBSPerkDef { FString Id, Name, Icon, Desc; TArray<int32> Costs; };

namespace BSDefs
{
	const TArray<FBSZergDef>& Zerg();
	const FBSZergDef* FindZerg(const FString& Key);
	const TArray<FBSInfantryDef>& Infantry();
	const FBSInfantryDef* FindInfantry(const FString& Key);
	const TArray<FBSUpgradeDef>& Upgrades();
	const FBSUpgradeDef* FindUpgrade(const FString& Key);
	const TArray<FBSMechDef>& Mechs();
	const FBSMechDef* FindMech(const FString& Key);
	const TArray<FBSTechCard>& Tech();
	const FBSTechCard* FindTech(const FString& Id);
	const TArray<FBSCardDef>& Cards();
	const FBSCardDef* FindCard(const FString& Id);
	const FBSRarity& FindRarity(const FString& Key);
	const TArray<FBSPerkDef>& Perks();
	const FBSPerkDef* FindPerk(const FString& Id);

	// The shop. Every cleared wave pauses the fight and offers ShopOffers random cards, bought with banked minerals;
	// troops, tanks, SCVs, turrets and repairs stay in the armoury throughout. A reroll costs RerollCost, RerollStep more
	// each time in the same visit. ShopLinger: seconds after a wave's last spawn before the shop opens anyway (stragglers
	// wait, frozen, while you shop); flyers still aloft ShopPressIn seconds after it close in. Clear bonus for a wave wiped out: ClearBonusA + ClearBonusB x wave. Research cards
	// weigh ShopResearchWeight times their rarity, and every offer starts with one while there is research left.
	constexpr int32 ShopOffers = 4, RerollCost = 25, RerollStep = 25, ClearBonusA = 25, ClearBonusB = 10;
	constexpr float ShopLinger = 40, ShopPressIn = 10, ShopResearchWeight = 2;
	// Between runs: vespene per wave cleared, Milestone more every tenth, x GasArena in the arena.
	constexpr float GasPerWave = 1, GasMilestone = 5, GasArena = 1.5f;

	// Terran constants.
	constexpr float BunkerHp = 400, BunkerArmor = 1, BunkerRangeBonus = 1, BunkerHalfSize = 1.5f;
	constexpr int32 BunkerSlots = 4, BunkerSlotsUpgraded = 6;
	constexpr float ScvHp = 45, ScvRepairRate = 13.8f, ScvSpeed = 3.94f, ScvRadius = 0.375f;
	constexpr int32 ScvCost = 50, ScvMax = 4;
	constexpr float TankHp = 175, TankArmor = 1, TankDmg = 40, TankDmgArmored = 30, TankCd = 2.14f, TankRange = 13, TankMinRange = 2, TankRadius = 0.9f;
	constexpr float TankSplash1 = 0.47f, TankSplash2 = 0.78f, TankSplash3 = 1.25f;
	constexpr int32 TankCost = 300, TankCostStep = 100, TankMaxLane = 4, TankMaxArena = 4;
	// Air strike: four banshees hover over the lane (and its ledges) and fire missiles at passing zerg.
	constexpr float BansheeDmg = 12, BansheeCd = 1.1f, BansheeRange = 6, BansheeSpeed = 9, BansheeFly = 2.4f, BansheeHover = 14, BansheeCooldown = 32;
	constexpr int32 BansheeShots = 2, BansheeCost = 200, BansheeCount = 4;
	// Troops standing behind the bunker (ejected, or recruited while it is full).
	constexpr int32 SquadMax = 48;
	// Missile turrets: anti-air only, like SC2 (12 x2, range 7). Sensor tower: the radar only works while one stands.
	constexpr float SamHp = 250, SamDmg = 12, SamCd = 0.86f, SamRange = 7, SamRadius = 1.0f, SensorHp = 200, SensorRadius = 0.8f, BuildTime = 3;
	constexpr int32 SamShots = 2, SamCost = 100, SamMax = 3, SensorCost = 125;
	// Forward base (lane): a maxed-out army takes the plateau east of the stem and an SCV builds a command center there,
	// which mines CcIncome minerals/s. ExpandTroopers go up with the Odin and the goliaths; below ExpandMinSquad they fall
	// back. ExpandClear: the plateau counts as taken with at most two ground zerg this close to the post.
	constexpr float CcHp = 1500, CcArmor = 1, CcRadius = 1.5f, CcScale = 0.6f, CcBuild = 12, CcIncome = 3, ExpandClear = 7;
	constexpr int32 ExpandTroopers = 16, ExpandMinSquad = 24;
	constexpr float RailsRange = 2.f;
	// Battlecruiser on station: hovers over the lane; laser batteries (ATS 8 against ground, ATA 5 against air, one bolt
	// every 0.16 s along six hardpoints), anti-air missile pods, and a Yamato cannon that charges before it fires.
	constexpr float BcLaserG = 8, BcLaserA = 5, BcLaserCd = 0.16f, BcRange = 10, BcYamato = 240, BcYamatoCd = 20, BcYamatoCharge = 2, BcMissileCd = 6, BcMissileDmg = 30, BcMissileSplash = 1.2f;
	constexpr int32 BcCost = 1500, BcMissiles = 6;
	// Point defence reaches a little further against scourge and devourers; seconds between "under attack" warnings.
	constexpr float BcPdRange = 4, BcAlertGap = 8;
	constexpr float BcHp = 1200, BcArmor = 3, BansheeHp = 140, VikingHp = 135;   // scourge and devourers can shoot air support down
	// A ghost calls a tactical nuke on the densest pack well away from the bunker.
	constexpr float NukeDmg = 300, NukeRadius = 7, NukeDelay = 3, NukeMinDist = 9, NukeCooldown = 45;
	constexpr int32 NukeCost = 400;
	// Four viking fighters patrol overhead hunting air.
	constexpr float VikingDmg = 10, VikingCd = 1.4f, VikingRange = 9, VikingHover = 20, VikingFly = 4.2f, VikingSpeed = 9, VikingCooldown = 35;
	constexpr int32 VikingShots = 2, VikingCost = 300, VikingCount = 4;
	// Yamato strike: a Battlecruiser fires on the densest cluster.
	constexpr float YamatoDmg = 240, YamatoSplash = 120, YamatoRadius = 3.f, YamatoCooldown = 40;
	constexpr int32 YamatoCost = 300;
	constexpr float HiSecRange = 1.f;
	constexpr float StimDuration = 11, StimCooldown = 13, RepairCooldown = 20, RepairAmount = 150;
	constexpr int32 RepairCost = 75;
	// The difficulty curve, waves 1-100 (BS.WAVES): short early waves that ramp fast; past the knee the budget, the zergling
	// swarm and the zerg's extra hit points grow linearly, so a maxed-out player meets a steady climb rather than a wall.
	constexpr float WaveLenStart = 30, WaveLenEnd = 30, WavePerWaveA = 16, WavePerWaveB = 0.12f, SwarmPerWave = 5, WaveHp = 0.02f;
	// Alive/AliveHard: zerg on the field at once, rising from [0] at wave AliveFrom to [1] by AliveBy (the rest wait their
	// turn). Past ShadowCrowd alive, small zerg spawn without dynamic shadows: a crowd of zerglings reads fine without
	// them and their shadow passes are most of the cost.
	constexpr int32 Alive[2] = { 160, 200 }, AliveHard[2] = { 200, 250 }, AliveFrom = 20, AliveBy = 60, ShadowCrowd = 100;
	// The swarm evolves on its own as well as answering your tech: from TierFrom, every TierEvery waves count as one more
	// tech card for the late-game strains (ABSGame::LateTier).
	constexpr int32 TierFrom = 20, TierEvery = 10;
	// A named assault every ten waves from 30 (BS.WAVES.milestones), on top of the budget: Zerg is "type:count,...".
	struct FMilestone { int32 Wave; const TCHAR* Name; const TCHAR* Toast; const TCHAR* Zerg; int32 Pods, Nydus; };
	inline constexpr FMilestone Milestones[] = {
		{ 30, TEXT("ULTRALISK STAMPEDE"), TEXT("Ultralisks stampeding down the lane!"), TEXT("ultralisk:6"), 0, 0 },
		{ 40, TEXT("AIR ARMADA"), TEXT("A swarm of fliers darkens the sky!"), TEXT("mutalisk:12,guardian:4,devourer:3,broodlord:2"), 0, 0 },
		{ 50, TEXT("THE LEVIATHAN"), TEXT("A leviathan looms over the field!"), TEXT("leviathan:1"), 0, 0 },
		{ 60, TEXT("DROP POD ASSAULT"), TEXT("Drop pods landing all round the base!"), TEXT(""), 6, 0 },
		{ 70, TEXT("NYDUS BREACH"), TEXT("Nydus worms breaching behind our lines!"), TEXT(""), 0, 3 },
		{ 80, TEXT("DARK SWARM"), TEXT("Defilers lead a lurker siege!"), TEXT("defiler:4,lurker:8,ultralisk:4"), 0, 0 },
		{ 90, TEXT("BROOD WAR"), TEXT("The old brood returns in force!"), TEXT("broodqueen:2,guardian:6,defiler:2,ultralisk:4"), 0, 0 },
	};
	constexpr int32 WaveLenBy = 12, WaveKnee = 16, SwarmKnee = 12, SwarmMax = 360, WaveHpFrom = 10;
	// Wave 100 finale (ABSGame::StartFinale): Kerrigan leads a leviathan and a maxed-out wave (budget x FinaleBudget, every
	// late-game strain) while zerg drop pods land all round the base. A pod hits the ground PodLand s after it appears (its
	// Birth sequence), crushing what it lands on, then lets out PodCargo zerg. FinaleVictory: score for killing both bosses.
	constexpr int32 FinaleWave = 100, FinalePods = 8, PodCargo = 4;   // every other pod brings an ultralisk
	constexpr float FinaleBudget = 1.5f, PodStart = 6, PodEvery = 2, PodLand = 2.7f, PodDmg = 60, PodRadius = 2, PodMinR = 7, PodMaxR = 13;
	constexpr float LeapCd = 8, LeapMin = 3, LeapMax = 9, LeapDur = 0.7f, LeapDmg = 90, LeapRadius = 1.8f;           // Leaping Strike onto a defender out of reach
	constexpr float ApocCd = 13, ApocRange = 9, ApocDelay = 1.2f, ApocDmg = 70, ApocRadius = 2;                      // a psionic blast on the thickest knot of defenders
	constexpr float BroodCd = 15; constexpr int32 BroodCount = 3;                                                    // the leviathan spawns mutalisks
	constexpr float FinaleVictory = 50000;
}

// Sound keys -> exported sound files (S_<key>_<n> in /Game/SC2/Sounds).
namespace BSSounds
{
	extern const TCHAR* MarineFire; extern const TCHAR* Stim; extern const TCHAR* FirebatFire; extern const TCHAR* MarauderFire;
	extern const TCHAR* TankFire; extern const TCHAR* TankHit; extern const TCHAR* TankDeath; extern const TCHAR* BansheeFire;
	extern const TCHAR* BansheeHit; extern const TCHAR* ScvRepair; extern const TCHAR* MedivacUnload; extern const TCHAR* BanelingDeath;
	extern const TCHAR* Explosion; extern const TCHAR* SamFire;
}

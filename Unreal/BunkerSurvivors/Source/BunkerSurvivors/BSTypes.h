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
	// Overlords (BSOverlords): watchers glare from a vantage point (ambient: they never hold up the end of a wave); front
	// drops unload zerg up the lane. No overlord counts toward the alive cap (bUncounted).
	bool bWatcher = false, bFront = false, bAmbient = false, bUncounted = false;
	// Overseer (BSOverseers): hunts cloaked ghosts and reveals them (and cloaked wraiths) within BSOverseers::Detect.
	bool bDetector = false;
	// Finale bosses: "kerrigan" (Leaping Strike, Apocalypse and the rest of her kit) or "leviathan" (bombards, spawns mutalisks). Bosses
	// keep their own hit points (no late-wave toughening) and always spawn, whatever the alive cap.
	FString Boss;
	// Kerrigan: nothing aims at her or hurts her (you hold on until the Hyperion comes), and she makes for the bunker.
	bool bUntouchable = false, bPreferBunker = false;
	float Bombard = 0;                // leviathan bile: splash radius of each shot
};

// Creep: a coarse grid (Cell metres) of coverage 0..1 that grows where a source reaches and slowly fades (lingers) where
// none does. Zerg ground units on creep move faster (SC2: +30%). Firebat flames burn it back and scorch the ground for a
// while, and singe tumors. Queens join from FromWave, creep overlords two waves later. The swarm may keep more tumors
// alive each wave (TumorBase + TumorPerWave per creep wave, up to TumorMax), so the creep spreads further as the waves go
// on. Port of BS.CREEP in units.js.
// Overlords from wave From: watchers (Watch0 a wave plus Watch1 every 10 waves, at most WatchMax), each staying
// Life0..Life1 seconds; and drops (Chance a wave, plus ChanceGrow a wave up to ChanceMax; a second one from Second)
// unloading up the lane N zerg picked from the latest cargo tier the wave has reached. Port of BS.OVERLORDS in units.js.
namespace BSOverlords
{
	constexpr int32 From = 3, WatchMax = 3, Second = 15;
	constexpr float Watch0 = 0.7f, Watch1 = 0.4f, Life0 = 25, Life1 = 45, Chance = 0.35f, ChanceGrow = 0.02f, ChanceMax = 0.8f;
	struct FCargo { int32 From, N; TArray<const TCHAR*> Units; };
	inline const FCargo Cargo[] = {
		{ 3, 4, { TEXT("zergling") } },
		{ 8, 4, { TEXT("zergling"), TEXT("zergling"), TEXT("baneling") } },
		{ 14, 3, { TEXT("baneling"), TEXT("roach"), TEXT("hydralisk") } },
		{ 22, 4, { TEXT("roach"), TEXT("hydralisk"), TEXT("infested") } },
		{ 30, 1, { TEXT("ultralisk") } },
	};
}

// Overseers: from wave From, while there are ghosts on the field, Base + PerGhost per ghost a wave. A ghost within
// Detect metres of one is revealed. Port of BS.OVERSEERS in units.js.
namespace BSOverseers
{
	constexpr int32 From = 18, Base = 1;
	constexpr float PerGhost = 0.5f, Detect = 5.5f;
}

namespace BSCreep
{
	constexpr float Cell = 1, Update = 0.25f, On = 0.35f, Grow = 0.6f, Decay = 0.012f, Speed = 1.3f;
	constexpr int32 FromWave = 8;
	constexpr float TumorRadius = 4.5f, TumorGrowth = 0.45f, TumorSpread = 12, TumorHop0 = 3, TumorHop1 = 4.5f, TumorMinBunker = 6, TumorGap = 2;
	constexpr int32 TumorBase = 4, TumorPerWave = 3, TumorMax = 30;
	constexpr float QueenEvery = 9, QueenFirst0 = 3, QueenFirst1 = 6;
	constexpr float OverlordRadius = 3.2f, OverlordLife = 50, OverlordDrift = 0.3f, OverlordMinDist = 6;
	constexpr float OverlordDrip = 1.0f;   // how far under the overlord's middle its creep drips from
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
	float Hp = 150, Armor = 1, Radius = 0.7f, Speed = 3, Scale = 1;
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
// before it is offered ("tank", "stim", "hisec", "medivac", "bc", "highground", or a research or "wave" and a number;
// several, comma-separated, all needed). bLane: offered in the lane only. Unlock: what it takes over all runs before the
// card joins the pool at all: UnlockBuys bought UnlockBuysN times, or wave UnlockBest cleared (see BSMeta).
struct FBSCardDef
{
	FString Id, Up, Tech, Name, Icon, Desc, Rarity, Req, UnlockBuys;
	int32 Max = 1, UnlockBuysN = 0, UnlockBest = 0;
	bool bLane = false;
	TArray<int32> Costs;
	int32 Grow = 0;                       // endless cards: Costs[0] + Grow x level
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
	// wait, frozen, while you shop). Clear bonus for a wave wiped out: ClearBonusA + ClearBonusB x wave. Research cards
	// weigh ShopResearchWeight times their rarity, and every offer starts with one while there is research left.
	constexpr int32 ShopOffers = 4, RerollCost = 25, RerollStep = 25, ClearBonusA = 25, ClearBonusB = 10;
	constexpr float ShopLinger = 40, ShopResearchWeight = 2;
	// Between runs: vespene per wave cleared, Milestone more every tenth, GasEvac when the Hyperion took you off, x GasArena
	// in the arena.
	constexpr float GasPerWave = 1, GasMilestone = 5, GasEvac = 25, GasArena = 1.5f;

	// Terran constants.
	constexpr float BunkerHp = 400, BunkerArmor = 1, BunkerRangeBonus = 1, BunkerHalfSize = 1.5f;
	constexpr int32 BunkerSlots = 4, BunkerSlotsUpgraded = 6;
	constexpr float ScvHp = 45, ScvRepairRate = 13.8f, ScvSpeed = 3.94f, ScvRadius = 0.375f;
	constexpr int32 ScvCost = 50, ScvMax = 4;
	constexpr float TankHp = 175, TankArmor = 1, TankDmg = 40, TankDmgArmored = 30, TankCd = 2.14f, TankRange = 13, TankMinRange = 2, TankRadius = 0.9f;
	constexpr float TankSplash1 = 0.47f, TankSplash2 = 0.78f, TankSplash3 = 1.25f;
	constexpr int32 TankCost = 300, TankCostStep = 100, TankMaxLane = 4, TankMaxArena = 4, TankHigh = 2;   // TankHigh: more with the High Ground card
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
	// Reapers (the Reaper Squad card: ReaperSquad[level] of them, losses replaced every wave): fast, light, hit-and-run
	// pistoleers that hop cliffs both ways. Two fire teams walk a beat round the plateaus (the arena's ring) at ReaperPatrol
	// m/s, strung out ReaperGap m apart, and hunt ground zerg within ReaperHunt m that stay within ReaperLeash m of their
	// beat; every Kd8Cd s a KD8 charge on a pack (damage and a knock back). They regenerate out of combat, and below
	// ReaperFallBack of their hit points keep to the beat to do so.
	constexpr float ReaperHp = 60, ReaperDmg = 4, ReaperDmgLight = 5, ReaperCd = 1.1f, ReaperRange = 5, ReaperSpeed = 5.25f, ReaperKite = 4;
	constexpr float ReaperPatrol = 2.2f, ReaperGap = 1.1f, ReaperHunt = 7, ReaperLeash = 9;
	constexpr float ReaperRegen = 3, ReaperRegenAfter = 4, ReaperFallBack = 0.4f;
	constexpr int32 ReaperSquad[3] = { 0, 4, 6 }, ReaperShots = 2;
	constexpr float Kd8Cd = 12, Kd8Dmg = 10, Kd8Radius = 1.6f, Kd8Knock = 2.2f, Kd8Range = 5;
	constexpr int32 Kd8MinPack = 3;
	// Ghosts: cloaked snipers on the high ground (lane: over the ramp tops, then the cliffs over the stem). C-10 rifle at
	// anything climbing to them, Steady Targeting (Snipe*) on anything big and armoured. Overseers reveal them. Each ghost on
	// the field takes GhostNukeCut off the tactical nuke's price and reload (up to 4). Revealed, a ghost turns its guns on the
	// overseer first and sidesteps GhostEvade metres away from it along the plateau.
	constexpr float GhostArmor = 1, GhostEvade = 3;
	constexpr float GhostHp = 150, GhostDmg = 10, GhostDmgLight = 10, GhostCd = 1.07f, GhostRange = 6, GhostSpeed = 3.94f, GhostNukeCut = 0.15f;
	constexpr int32 GhostCost = 200, GhostMax = 4;
	constexpr float SnipeDmg = 170, SnipeRange = 10, SnipeCd = 8, SnipeChannel = 1.43f, SnipeMinHp = 300;
	// Vikings and wraiths fly close air support: long strafing passes up and down the lane (round the arena) in a spread
	// wedge, always flying straight on, firing on whatever lies within *Cone radians of their nose and staying on that
	// target while it does. Ship Weapons adds to every shot (ABSGame::AirBonus).
	// Four viking fighters hunt air (VikingPatrol: metres a second round the patrol loop).
	constexpr float VikingDmg = 7, VikingCd = 0.7f, VikingRange = 9, VikingCone = 1.0f, VikingHover = 20, VikingFly = 4.2f, VikingSpeed = 9, VikingCooldown = 35, VikingPatrol = 6.5f;
	constexpr int32 VikingShots = 2, VikingCost = 300, VikingCount = 4;
	// Three wraiths sweep the lane for a while: burst lasers at ground zerg (GDmg every GCd), Gemini missiles at air.
	// Wraiths cloak for WraithCloakDur s when fired upon (an overseer still sees them), then not again for WraithCloakCd s.
	constexpr float WraithHp = 120, WraithGDmg = 5, WraithGCd = 0.3f, WraithADmg = 7, WraithACd = 0.8f, WraithRange = 7, WraithCone = 1.0f, WraithHover = 20, WraithFly = 3.4f, WraithSpeed = 9, WraithCooldown = 40;
	constexpr float WraithPatrol = 7.5f, WraithCloakDur = 6, WraithCloakCd = 12;
	constexpr int32 WraithCost = 350, WraithCount = 3, WraithAShots = 2;
	// A science vessel on station for VesselStay seconds, casting from VesselRange: Irradiate on the densest pack (dps to
	// the target and everything within radius, for its duration), Defensive Matrix on the most battered of the bunker,
	// tanks and mechs (absorbs MatrixHp damage for MatrixDur s).
	constexpr float VesselHp = 200, VesselArmor = 1, VesselCooldown = 50, VesselStay = 30, VesselFly = 5.2f, VesselSpeed = 4, VesselScale = 1.f, VesselRange = 11;
	constexpr int32 VesselCost = 400;
	// Fired upon, the vessel pulls back behind the bunker and returns once it has gone VesselCalm seconds unhit.
	constexpr float VesselRetreatY = -5, VesselRetreatSpeed = 6, VesselCalm = 3;
	constexpr float IrradiateCd = 7, IrradiateDur = 6, IrradiateDps = 18, IrradiateRadius = 1.8f;
	constexpr float MatrixCd = 10, MatrixHp = 250, MatrixDur = 12;
	// Yamato strike: a Battlecruiser fires on the densest cluster.
	constexpr float YamatoDmg = 240, YamatoSplash = 120, YamatoRadius = 3.f, YamatoCooldown = 40;
	constexpr int32 YamatoCost = 300;
	constexpr float HiSecRange = 1.f;
	constexpr float StimDuration = 11, StimCooldown = 13, RepairCooldown = 20, RepairAmount = 150;
	constexpr int32 RepairCost = 75;
	// The difficulty curve over a 30-minute run, about 40 waves (BS.WAVES): short early waves that ramp fast; past the knee
	// the budget, the zergling swarm and the zerg's extra hit points climb steadily rather than hitting a wall.
	constexpr float WaveLenStart = 30, WaveLenEnd = 30, WavePerWaveA = 16, WavePerWaveB = 0.4f, SwarmPerWave = 5, WaveHp = 0.02f;
	// Alive/AliveHard: zerg on the field at once, rising from [0] at wave AliveFrom to [1] by AliveBy (the rest wait their
	// turn). Past ShadowCrowd alive, small zerg spawn without dynamic shadows: a crowd of zerglings reads fine without
	// them and their shadow passes are most of the cost.
	constexpr int32 Alive[2] = { 160, 220 }, AliveHard[2] = { 200, 250 }, AliveFrom = 20, AliveBy = 40, ShadowCrowd = 100;
	// The swarm evolves on its own as well as answering your tech: from TierFrom, every TierEvery waves count as one more
	// tech card for the late-game strains (ABSGame::LateTier).
	constexpr int32 TierFrom = 20, TierEvery = 5;
	// A named assault every three waves from 25 (BS.WAVES.milestones), on top of the budget: Zerg is "type:count,...".
	struct FMilestone { int32 Wave; const TCHAR* Name; const TCHAR* Toast; const TCHAR* Zerg; int32 Pods, Nydus; };
	inline constexpr FMilestone Milestones[] = {
		{ 25, TEXT("HYDRALISK SWARM"), TEXT("A hydralisk swarm pours down the lane!"), TEXT("hydralisk:36"), 0, 0 },
		{ 28, TEXT("ULTRALISK STAMPEDE"), TEXT("Ultralisks stampeding down the lane!"), TEXT("ultralisk:6"), 0, 0 },
		{ 31, TEXT("AIR ARMADA"), TEXT("A swarm of fliers darkens the sky!"), TEXT("mutalisk:12,guardian:4,devourer:3,broodlord:2"), 0, 0 },
		{ 34, TEXT("THE LEVIATHAN"), TEXT("A leviathan looms over the field!"), TEXT("leviathan:1"), 0, 0 },
		{ 37, TEXT("DROP POD ASSAULT"), TEXT("Drop pods landing all round the base!"), TEXT(""), 6, 0 },
		{ 40, TEXT("NYDUS BREACH"), TEXT("Nydus worms breaching behind our lines!"), TEXT(""), 0, 3 },
		{ 43, TEXT("DARK SWARM"), TEXT("Defilers lead a lurker siege!"), TEXT("defiler:4,lurker:8,ultralisk:4"), 0, 0 },
		{ 46, TEXT("BROOD WAR"), TEXT("The old brood returns in force!"), TEXT("broodqueen:2,guardian:6,defiler:2,ultralisk:4"), 0, 0 },
	};
	constexpr int32 WaveLenBy = 12, WaveKnee = 16, SwarmKnee = 12, SwarmMax = 360, WaveHpFrom = 10;
	// Past wave BountyFrom a kill pays 1 / (1 + BountyFall x waves past it) of its bounty (a third by wave 40; bosses pay
	// in full), so a strong army stops snowballing minerals and has to make every rebuy count.
	constexpr int32 BountyFrom = 28; constexpr float BountyFall = 0.15f;
	// Strains and skins (FBSZergDef::Variants) only mix in once the plain strain has had the field to itself: VariantsAfter
	// waves past its unlock wave, primal ones VariantsPrimalAfter (BS.VARIANTS).
	constexpr int32 VariantsAfter = 3, VariantsPrimalAfter = 10;
	// The run lasts EvacAt seconds of fighting (the clock stops while you shop), then the Hyperion evacuates everyone left:
	// the run is won (+EvacScore). She arrives EvacArrive seconds before and comes down over the bunker from EvacFrom to
	// EvacHover (metres) while the fight goes on (BS.EVAC).
	constexpr float EvacAt = 1800, EvacArrive = 10, EvacScale = 1.2f, EvacScore = 50000;
	inline const FVector EvacFrom(0, -40, 14), EvacHover(0, 2.5f, 6.5f);
	// The finale (ABSGame::StartFinale), the swarm's last push: the first wave to start FinaleAt seconds in (two minutes
	// before the evac). Kerrigan, who cannot be stopped, comes for the bunker with a leviathan, and from then until the evac every wave is maxed out (budget x FinaleBudget, every late-game strain),
	// while zerg drop pods land all round the base. A pod hits the ground PodLand s after it appears (its Birth sequence),
	// crushing what it lands on, then lets out PodCargo zerg.
	constexpr float FinaleAt = 1680;
	constexpr int32 FinalePods = 8, PodCargo = 4;   // every other pod brings an ultralisk
	constexpr float FinaleBudget = 1.5f, PodStart = 6, PodEvery = 2, PodLand = 2.7f, PodDmg = 60, PodRadius = 2, PodMinR = 7, PodMaxR = 13;
	constexpr float LeapCd = 8, LeapMin = 3, LeapMax = 9, LeapDur = 0.7f, LeapDmg = 90, LeapRadius = 1.8f;           // Leaping Strike onto a defender out of reach
	constexpr float ApocCd = 13, ApocRange = 9, ApocDelay = 1.2f, ApocDmg = 70, ApocRadius = 2;                      // a psionic blast on the thickest knot of defenders
	constexpr float BossStructure = 0.4f;                                                                            // what Apocalypse and Leaping Strike do to buildings
	constexpr float BroodCd = 15; constexpr int32 BroodCount = 3;                                                    // the leviathan spawns mutalisks
	// The rest of a maxed-out Heart of the Swarm kit (ABSGame::KerriganKit). Her swings use every attack she has and sweep
	// the wings (cleave); every FuryEvery-th swing sends her into a Fury (half again the attack speed for FuryDur s). Kinetic Blast
	// hits the biggest machine in reach, Crushing Grip and the Immobilization Wave crush defenders and pin them (no firing
	// for the stun s), Chain Lightning arcs through troops, Wild Mutation halves the damage the zerg round her take for
	// MutationDur s, and she spawns banelings.
	constexpr float KineticCd = 9, KineticRange = 9, KineticDmg = 160;
	constexpr float GripCd = 14, GripRange = 7, GripRadius = 2.4f, GripDmg = 50, GripStun = 2.5f;
	constexpr float ChainCd = 8, ChainRange = 8, ChainHop = 4, ChainDmg = 45; constexpr int32 ChainJumps = 5;
	constexpr float WaveCd = 22, WaveRadius = 5, WaveDmg = 60, WaveStun = 2; constexpr int32 WaveMin = 4;
	constexpr float MutationCd = 20, MutationRadius = 9, MutationDur = 8; constexpr int32 MutationMin = 4;
	constexpr float BanesCd = 16; constexpr int32 BanesCount = 6;
	constexpr float FuryDur = 3; constexpr int32 FuryEvery = 8;
	// A corpse plays its death until the body has fallen (CorpseFall: seconds, measured per death model), lies CorpseLie s,
	// then sinks away over CorpseSink s. SC2's death sequences run ten seconds, but the body is still for nearly all of it.
	// Past CorpseCap of them the oldest go early.
	constexpr float CorpseLie = 0.4f, CorpseSink = 0.7f; constexpr int32 CorpseCap = 60;
	float CorpseFall(const FString& Key, float Hold);
}

// Sound keys -> exported sound files (S_<key>_<n> in /Game/SC2/Sounds).
namespace BSSounds
{
	extern const TCHAR* MarineFire; extern const TCHAR* Stim; extern const TCHAR* FirebatFire; extern const TCHAR* MarauderFire;
	extern const TCHAR* TankFire; extern const TCHAR* TankHit; extern const TCHAR* TankDeath; extern const TCHAR* BansheeFire;
	extern const TCHAR* BansheeHit; extern const TCHAR* ScvRepair; extern const TCHAR* MedivacUnload; extern const TCHAR* BanelingDeath;
	extern const TCHAR* Explosion; extern const TCHAR* SamFire;
}

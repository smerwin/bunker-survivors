// Unit, upgrade and sound definitions. Stats follow StarCraft II (Faster speed); the simulation runs in
// SC2 world units (1 unit = 1 m = 100 Unreal units). Ported from client/js/units.js.
#pragma once

#include "CoreMinimal.h"

enum class EBSProjectile : uint8 { None, Acid, Spine, Bullet, Glaive, Spines, Brood };

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
	EBSProjectile Projectile = EBSProjectile::None;
	FString SoundAttack, SoundDeath;
};

struct FBSInfantryDef
{
	FString Key, Name, Icon, Sound, Desc;
	float Hp = 45, Dmg = 6, DmgLight = 0, DmgArmored = 0, Cd = 0.61f, Range = 5, Splash = 0, Slow = 0;
	bool bGroundOnly = false;
	int32 Cost = 50;
	float StimHp = 10;
};

struct FBSUpgradeDef
{
	FString Key, Name, Desc;
	TArray<FString> Icons;            // one per level (or a single icon)
	TArray<int32> Costs;              // one per level
};

namespace BSDefs
{
	const TArray<FBSZergDef>& Zerg();
	const FBSZergDef* FindZerg(const FString& Key);
	const TArray<FBSInfantryDef>& Infantry();
	const FBSInfantryDef* FindInfantry(const FString& Key);
	const TArray<FBSUpgradeDef>& Upgrades();
	const FBSUpgradeDef* FindUpgrade(const FString& Key);

	// Terran constants.
	constexpr float BunkerHp = 400, BunkerArmor = 1, BunkerRangeBonus = 1, BunkerHalfSize = 1.5f;
	constexpr int32 BunkerSlots = 4, BunkerSlotsUpgraded = 6;
	constexpr float ScvHp = 45, ScvRepairRate = 13.8f, ScvSpeed = 3.94f, ScvRadius = 0.375f;
	constexpr int32 ScvCost = 50, ScvMax = 4;
	constexpr float TankHp = 175, TankArmor = 1, TankDmg = 40, TankDmgArmored = 30, TankCd = 2.14f, TankRange = 13, TankMinRange = 2, TankRadius = 0.9f;
	constexpr float TankSplash1 = 0.47f, TankSplash2 = 0.78f, TankSplash3 = 1.25f;
	constexpr int32 TankCost = 300, TankCostStep = 100, TankMaxLane = 4, TankMaxArena = 4;
	constexpr float BansheeDmg = 12, BansheeCd = 0.55f, BansheeRange = 7, BansheeSpeed = 9, BansheeFly = 4.5f, BansheeCooldown = 25;
	constexpr int32 BansheeShots = 2, BansheeCost = 150;
	// Yamato strike: a Battlecruiser fires on the densest cluster.
	constexpr float YamatoDmg = 240, YamatoSplash = 120, YamatoRadius = 3.f, YamatoCooldown = 40;
	constexpr int32 YamatoCost = 300;
	constexpr float HiSecRange = 1.f;
	constexpr float StimDuration = 11, StimCooldown = 13, RepairCooldown = 20, RepairAmount = 150;
	constexpr int32 RepairCost = 75;
}

// Sound keys -> exported sound files (S_<key>_<n> in /Game/SC2/Sounds).
namespace BSSounds
{
	extern const TCHAR* MarineFire; extern const TCHAR* Stim; extern const TCHAR* FirebatFire; extern const TCHAR* MarauderFire;
	extern const TCHAR* TankFire; extern const TCHAR* TankHit; extern const TCHAR* TankDeath; extern const TCHAR* BansheeFire;
	extern const TCHAR* BansheeHit; extern const TCHAR* ScvRepair; extern const TCHAR* MedivacUnload; extern const TCHAR* BanelingDeath;
	extern const TCHAR* Explosion;
}

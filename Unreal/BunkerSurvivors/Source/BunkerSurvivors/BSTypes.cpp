#include "BSTypes.h"

namespace BSSounds
{
	const TCHAR* MarineFire = TEXT("marineFire"); const TCHAR* Stim = TEXT("stim"); const TCHAR* FirebatFire = TEXT("firebatFire");
	const TCHAR* MarauderFire = TEXT("marauderFire"); const TCHAR* TankFire = TEXT("tankFire"); const TCHAR* TankHit = TEXT("tankHit");
	const TCHAR* TankDeath = TEXT("tankDeath"); const TCHAR* BansheeFire = TEXT("bansheeFire"); const TCHAR* BansheeHit = TEXT("bansheeHit");
	const TCHAR* ScvRepair = TEXT("scvRepair"); const TCHAR* MedivacUnload = TEXT("medivacUnload"); const TCHAR* BanelingDeath = TEXT("banelingDeath");
	const TCHAR* Explosion = TEXT("explosion"); const TCHAR* SamFire = TEXT("samFire");
}

namespace
{
	TArray<FBSZergDef> MakeZerg()
	{
		TArray<FBSZergDef> Out;
		auto Add = [&Out](const TCHAR* Key, const TCHAR* Name, const TCHAR* Model, const TCHAR* Death, const TCHAR* Icon) -> FBSZergDef&
		{
			FBSZergDef& D = Out.AddDefaulted_GetRef();
			D.Key = Key; D.Name = Name; D.Model = Model; D.Death = Death; D.Icon = Icon;
			return D;
		};
		{
			FBSZergDef& D = Add(TEXT("zergling"), TEXT("Zergling"), TEXT("zergling"), TEXT("zerglingDeath"), TEXT("btn-unit-zerg-zergling"));
			D.Variants = { TEXT("zerglingRaptor"), TEXT("zerglingSwarmling"), TEXT("zerglingPrimal") };      // winged ones are the hoppers below
			D.ImpactFx = TEXT("zerglingImpact");
			D.Hp = 30; D.Dmg = 5; D.Cd = 0.497f; D.Range = 0.1f; D.Speed = 4.13f; D.Radius = 0.375f; D.bLight = true;
			D.Bounty = 4; D.Budget = 2; D.Unlock = 1; D.Weight = 12; D.HitFrac = 0.45f; D.AttackAnims = { TEXT("attack"), TEXT("attack 01") };
			D.SoundAttack = TEXT("zerglingAttack"); D.SoundDeath = TEXT("zerglingDeath"); D.Height = 0.8f;
		}
		{
			FBSZergDef& D = Add(TEXT("baneling"), TEXT("Baneling"), TEXT("baneling"), TEXT("banelingDeath"), TEXT("btn-unit-zerg-baneling"));
			D.Variants = { TEXT("banelingSplitter"), TEXT("banelingHunter"), TEXT("banelingPrimal") };
			D.Hp = 30; D.Dmg = 16; D.DmgLight = 19; D.DmgStructure = 80; D.Cd = 1; D.Range = 0.25f; D.Speed = 3.5f; D.Radius = 0.375f; D.bLight = true;
			D.bSuicide = true; D.Splash = 2.2f; D.Bounty = 10; D.Budget = 4; D.Unlock = 3; D.Weight = 5; D.SoundDeath = TEXT("banelingDeath"); D.Height = 0.8f;
		}
		{
			FBSZergDef& D = Add(TEXT("roach"), TEXT("Roach"), TEXT("roach"), TEXT("roachDeath"), TEXT("btn-unit-zerg-roach"));
			D.Variants = { TEXT("roachGlial"), TEXT("roachVile"), TEXT("roachCorpser"), TEXT("roachPrimal") };
			D.Hp = 145; D.Armor = 1; D.Dmg = 16; D.Cd = 1.43f; D.Range = 4; D.Speed = 3.15f; D.Radius = 0.625f; D.bArmored = true;
			D.Bounty = 22; D.Budget = 7; D.Unlock = 5; D.Weight = 5; D.HitFrac = 0.35f; D.AttackAnims = { TEXT("attack") };
			D.Projectile = EBSProjectile::Acid; D.SoundAttack = TEXT("roachFire"); D.SoundDeath = TEXT("roachDeath"); D.Height = 0.9f;
		}
		{
			FBSZergDef& D = Add(TEXT("hydralisk"), TEXT("Hydralisk"), TEXT("hydralisk"), TEXT("hydraliskDeath"), TEXT("btn-unit-zerg-hydralisk"));
			D.Variants = { TEXT("hydraliskImpaler"), TEXT("hydraliskLurkerStrain"), TEXT("hydraliskRemastered"), TEXT("hydraliskPrimal") };
			D.Hp = 90; D.Dmg = 12; D.Cd = 0.59f; D.Range = 5; D.Speed = 3.15f; D.Radius = 0.625f; D.bLight = true;
			D.Bounty = 28; D.Budget = 8; D.Unlock = 6; D.Weight = 5; D.HitFrac = 0.3f; D.AttackAnims = { TEXT("attack") };
			D.Projectile = EBSProjectile::Spine; D.SoundAttack = TEXT("hydraliskFire"); D.SoundDeath = TEXT("hydraliskDeath"); D.Height = 1.6f;
		}
		{
			FBSZergDef& D = Add(TEXT("infested"), TEXT("Infested Marine"), TEXT("infested"), TEXT("infestedDeath"), TEXT("btn-unit-zerg-zergling"));
			D.Hp = 50; D.Dmg = 8; D.Cd = 0.61f; D.Range = 5; D.Speed = 2.25f; D.Radius = 0.375f; D.bLight = true;
			D.Bounty = 8; D.Budget = 3; D.Unlock = 7; D.Weight = 6; D.HitFrac = 0.3f; D.AttackAnims = { TEXT("attack"), TEXT("attack 01") };
			D.Projectile = EBSProjectile::Bullet; D.SoundAttack = TEXT("marineFire"); D.Height = 1.2f;
		}
		{
			FBSZergDef& D = Add(TEXT("mutalisk"), TEXT("Mutalisk"), TEXT("mutalisk"), TEXT("mutaliskDeath"), TEXT("btn-unit-zerg-mutalisk"));
			D.Variants = { TEXT("mutaliskPrimal"), TEXT("mutaliskPrimalSkin") };
			D.Hp = 120; D.Dmg = 9; D.Bounce = 3; D.Cd = 1.09f; D.Range = 3; D.Speed = 5.6f; D.Radius = 0.75f; D.bLight = true; D.bFlying = true; D.Fly = 2.6f;
			D.Bounty = 30; D.Budget = 9; D.Unlock = 9; D.Weight = 4; D.HitFrac = 0.3f; D.AttackAnims = { TEXT("attack") };
			D.Projectile = EBSProjectile::Glaive; D.SoundAttack = TEXT("mutaliskFire"); D.SoundDeath = TEXT("mutaliskDeath"); D.Height = 1.2f;
		}
		{
			FBSZergDef& D = Add(TEXT("lurker"), TEXT("Lurker"), TEXT("lurker"), TEXT("lurkerDeath"), TEXT("btn-unit-zerg-lurker"));
			D.Variants = { TEXT("lurkerSeeker"), TEXT("lurkerSpine") };
			D.Hp = 200; D.Armor = 1; D.Dmg = 20; D.DmgArmored = 10; D.Cd = 1.43f; D.Range = 8; D.Speed = 4.13f; D.Radius = 0.75f; D.bArmored = true; D.bBurrows = true;
			D.Bounty = 50; D.Budget = 16; D.Unlock = 12; D.Weight = 2; D.HitFrac = 0.35f; D.Projectile = EBSProjectile::Spines; D.Height = 1.0f;
		}
		{
			FBSZergDef& D = Add(TEXT("ultralisk"), TEXT("Ultralisk"), TEXT("ultralisk"), TEXT("ultraliskDeath"), TEXT("btn-unit-zerg-ultralisk"));
			D.Variants = { TEXT("ultraliskRemastered"), TEXT("ultraliskPrimal") };
			D.ImpactFx = TEXT("ultraliskImpact");
			D.Hp = 500; D.Armor = 2; D.Dmg = 35; D.Cd = 0.61f; D.Range = 1; D.Speed = 4.13f; D.Radius = 1.0f; D.bArmored = true; D.Cleave = 1.5f;
			D.Bounty = 100; D.Budget = 30; D.Unlock = 15; D.Weight = 1.5f; D.HitFrac = 0.45f; D.AttackAnims = { TEXT("attack"), TEXT("attack 01") };
			D.SoundAttack = TEXT("ultraliskAttack"); D.SoundDeath = TEXT("ultraliskDeath"); D.Height = 2.4f; D.DeathHold = 5;
		}
		{
			FBSZergDef& D = Add(TEXT("broodlord"), TEXT("Brood Lord"), TEXT("broodlord"), TEXT("broodlordDeath"), TEXT("btn-unit-zerg-broodlord"));
			D.Variants = { TEXT("broodlordEx1"), TEXT("broodlordPrimal") };
			D.Hp = 225; D.Armor = 1; D.Dmg = 20; D.Cd = 1.79f; D.Range = 6; D.Speed = 1.97f; D.Radius = 1.0f; D.bArmored = true; D.bFlying = true; D.Fly = 3.4f; D.bBroodlings = true;
			D.Bounty = 120; D.Budget = 34; D.Unlock = 18; D.Weight = 1.2f; D.HitFrac = 0.3f; D.Projectile = EBSProjectile::Brood; D.Height = 1.6f;
		}
		// Late game: these join the waves once high tech starts unlocking (Late = tech cards needed); the more cards, the
		// more of them. See ABSGame::StartWave.
		{
			FBSZergDef& D = Add(TEXT("wingling"), TEXT("Winged Zergling"), TEXT("wingling"), TEXT("winglingDeath"), TEXT("btn-unit-zerg-zergling"));
			D.ImpactFx = TEXT("zerglingImpact");
			D.Hp = 35; D.Dmg = 5; D.Cd = 0.4f; D.Range = 0.1f; D.Speed = 5.6f; D.Radius = 0.375f; D.bLight = true; D.bHops = true;
			D.Bounty = 5; D.Budget = 2; D.Late = 1; D.Weight = 5; D.HitFrac = 0.45f; D.AttackAnims = { TEXT("attack"), TEXT("attack 01") };
			D.SoundAttack = TEXT("zerglingAttack"); D.SoundDeath = TEXT("zerglingDeath"); D.Height = 0.8f;
		}
		{
			FBSZergDef& D = Add(TEXT("guardian"), TEXT("Guardian"), TEXT("guardian"), TEXT("guardianDeath"), TEXT("btn-unit-zerg-guardian"));
			D.Hp = 150; D.Armor = 2; D.Dmg = 20; D.Cd = 2; D.Range = 9; D.Speed = 2.2f; D.Radius = 1.0f; D.bArmored = true; D.bFlying = true; D.Fly = 3.8f; D.bPreferMech = true;
			D.Bounty = 60; D.Budget = 20; D.Late = 1; D.Weight = 2.5f; D.HitFrac = 0.4f; D.AttackAnims = { TEXT("attack") };
			D.Projectile = EBSProjectile::Guardian; D.SoundAttack = TEXT("guardianFire"); D.Height = 1.4f;
		}
		{
			FBSZergDef& D = Add(TEXT("devourer"), TEXT("Devourer"), TEXT("devourer"), TEXT("devourerDeath"), TEXT("btn-unit-zerg-devourer"));
			D.Hp = 250; D.Armor = 2; D.Dmg = 15; D.Cd = 2; D.Range = 6; D.Speed = 2.8f; D.Radius = 1.0f; D.bArmored = true; D.bFlying = true; D.Fly = 4.4f; D.bAntiAir = true; D.Acid = 6;
			D.Bounty = 60; D.Budget = 18; D.Late = 2; D.Weight = 2; D.HitFrac = 0.4f; D.AttackAnims = { TEXT("attack") };
			D.Projectile = EBSProjectile::Spores; D.SoundDeath = TEXT("devourerDeath"); D.Height = 1.4f;
		}
		{
			FBSZergDef& D = Add(TEXT("scourge"), TEXT("Scourge"), TEXT("scourge"), TEXT("scourgeDeath"), TEXT("btn-unit-zerg-scourge"));
			D.Hp = 25; D.Dmg = 110; D.Cd = 1; D.Range = 0.2f; D.Speed = 6.5f; D.Radius = 0.4f; D.bLight = true; D.bFlying = true; D.Fly = 4.6f; D.bAntiAir = true; D.bSuicide = true;
			D.Bounty = 8; D.Budget = 3; D.Late = 2; D.Weight = 4; D.HitFrac = 0; D.Height = 0.6f;
		}
		{
			FBSZergDef& D = Add(TEXT("defiler"), TEXT("Defiler"), TEXT("defiler"), TEXT("defilerDeath"), TEXT("btn-unit-zerg-defiler"));
			D.Hp = 80; D.Armor = 1; D.Dmg = 0; D.Cd = 8; D.Range = 9; D.Speed = 2.6f; D.Radius = 0.6f; D.Spell = TEXT("defiler");
			D.Bounty = 50; D.Budget = 14; D.Late = 2; D.Weight = 2; D.HitFrac = 0; D.Height = 1.2f;
		}
		{
			FBSZergDef& D = Add(TEXT("viper"), TEXT("Viper"), TEXT("viper"), TEXT("viperDeath"), TEXT("btn-unit-zerg-viper"));
			D.Hp = 150; D.Armor = 1; D.Dmg = 0; D.Cd = 11; D.Range = 9; D.Speed = 4; D.Radius = 0.75f; D.bFlying = true; D.Fly = 4; D.Spell = TEXT("abduct");
			D.Bounty = 50; D.Budget = 16; D.Late = 3; D.Weight = 1.5f; D.HitFrac = 0; D.Height = 1.4f;
		}
		// Not bought from the wave budget: special events scheduled by StartWave.
		{
			FBSZergDef& D = Add(TEXT("overlord"), TEXT("Overlord"), TEXT("overlord"), TEXT("overlordDeath"), TEXT("btn-unit-zerg-overlord"));
			D.Hp = 400; D.Armor = 1; D.Dmg = 0; D.Cd = 1; D.Range = 0; D.Speed = 3; D.Radius = 1.0f; D.bArmored = true; D.bFlying = true; D.Fly = 4.2f; D.bTransport = true;
			D.Bounty = 25; D.Budget = 0; D.Unlock = 999; D.Weight = 0; D.HitFrac = 0; D.SoundDeath = TEXT("overlordDeath"); D.Height = 2;
		}
		{
			FBSZergDef& D = Add(TEXT("nydus"), TEXT("Nydus Worm"), TEXT("nydus"), TEXT(""), TEXT("btn-building-zerg-nydusworm"));
			D.Hp = 450; D.Armor = 1; D.Dmg = 0; D.Cd = 1; D.Range = 0; D.Speed = 0; D.Radius = 1.3f; D.bArmored = true; D.bNydus = true;
			D.Bounty = 60; D.Budget = 0; D.Unlock = 999; D.Weight = 0; D.HitFrac = 0; D.Height = 3;
		}
		{
			FBSZergDef& D = Add(TEXT("broodling"), TEXT("Broodling"), TEXT("zergling"), TEXT("zerglingDeath"), TEXT("btn-unit-zerg-zergling"));
			D.ImpactFx = TEXT("zerglingImpact");
			D.Scale = 0.55f; D.Hp = 30; D.Dmg = 4; D.Cd = 0.46f; D.Range = 0.1f; D.Speed = 4.13f; D.Radius = 0.25f; D.bLight = true; D.Life = 8;
			D.Bounty = 1; D.Budget = 0; D.Unlock = 999; D.Weight = 0; D.HitFrac = 0.45f; D.AttackAnims = { TEXT("attack") }; D.Height = 0.5f;
		}
		return Out;
	}

	TArray<FBSInfantryDef> MakeInfantry()
	{
		TArray<FBSInfantryDef> Out;
		{
			FBSInfantryDef& D = Out.AddDefaulted_GetRef();
			D.Key = TEXT("marine"); D.Name = TEXT("Marine"); D.Icon = TEXT("btn-unit-terran-marine"); D.Sound = TEXT("marineFire");
			D.Desc = TEXT("Gauss rifle. Cheap, reliable, stims well."); D.Death = TEXT("marineDeath"); D.Hp = 45; D.Dmg = 6; D.Cd = 0.61f; D.Range = 5; D.Cost = 50; D.StimHp = 10;
		}
		{
			FBSInfantryDef& D = Out.AddDefaulted_GetRef();
			D.Key = TEXT("firebat"); D.Name = TEXT("Firebat"); D.Icon = TEXT("btn-unit-terran-firebat"); D.Sound = TEXT("firebatFire");
			D.Desc = TEXT("Flamethrower. Burns packs of lings and banelings."); D.Death = TEXT("firebatDeath"); D.Hp = 100; D.Dmg = 8; D.DmgLight = 6; D.Cd = 0.86f; D.Range = 2; D.Splash = 1.3f;
			D.bGroundOnly = true; D.Cost = 100; D.StimHp = 10;
		}
		{
			FBSInfantryDef& D = Out.AddDefaulted_GetRef();
			D.Key = TEXT("marauder"); D.Name = TEXT("Marauder"); D.Icon = TEXT("btn-unit-terran-marauder"); D.Sound = TEXT("marauderFire");
			D.Desc = TEXT("Punisher grenades. Slows and shreds armour."); D.Death = TEXT("marauderDeath"); D.Hp = 125; D.Dmg = 10; D.DmgArmored = 10; D.Cd = 1.07f; D.Range = 6; D.Slow = 1.5f;
			D.bGroundOnly = true; D.Cost = 100; D.StimHp = 20;
		}
		return Out;
	}

	TArray<FBSUpgradeDef> MakeUpgrades()
	{
		TArray<FBSUpgradeDef> Out;
		auto Add = [&Out](const TCHAR* Key, const TCHAR* Name, const TCHAR* Desc, TArray<FString> Icons, TArray<int32> Costs)
		{
			FBSUpgradeDef& D = Out.AddDefaulted_GetRef();
			D.Key = Key; D.Name = Name; D.Desc = Desc; D.Icons = MoveTemp(Icons); D.Costs = MoveTemp(Costs);
		};
		Add(TEXT("stim"), TEXT("Stimpack"), TEXT("Research, then hit STIM: +50% fire rate for 11s, costs HP."), { TEXT("btn-ability-terran-stimpack") }, { 100 });
		Add(TEXT("weapons"), TEXT("Infantry Weapons"), TEXT("+1 damage for every bunker unit."),
			{ TEXT("btn-upgrade-terran-infantryweaponslevel1"), TEXT("btn-upgrade-terran-infantryweaponslevel2"), TEXT("btn-upgrade-terran-infantryweaponslevel3") }, { 100, 175, 250, 350, 475, 600, 750, 900, 1100, 1300 });
		Add(TEXT("neosteel"), TEXT("Neosteel Frame"), TEXT("Bunker holds 6 units."), { TEXT("btn-upgrade-terran-neosteelframe") }, { 150 });
		Add(TEXT("plating"), TEXT("Building Armor"), TEXT("+150 bunker HP per level, +1 armour for the first three."), { TEXT("btn-upgrade-terran-buildingarmor") }, { 250, 350, 450, 600, 800, 1000, 1200, 1400 });
		Add(TEXT("hisec"), TEXT("Hi-Sec Auto Tracking"), TEXT("+1 range for the bunker and siege tanks."), { TEXT("btn-upgrade-terran-hisecautotracking") }, { 200 });
		Add(TEXT("vehicle"), TEXT("Vehicle Weapons"), TEXT("+5 (+3 armoured) siege tank damage."), { TEXT("btn-upgrade-terran-vehicleweaponslevel1") }, { 150, 225, 300, 400, 525, 650, 800, 950, 1150, 1350 });
		Add(TEXT("medivac"), TEXT("Medivac on Station"), TEXT("Heals bunker infantry 9 HP/s so they can stim again."), { TEXT("btn-ability-terran-heal") }, { 200 });
		Add(TEXT("rails"), TEXT("Magnetic Rails"), TEXT("+2 range for the troops in the bunker."), { TEXT("btn-upgrade-terran-magrailmunitions") }, { 300 });
		return Out;
	}
}

namespace
{
	TArray<FBSMechDef> MakeMechs()
	{
		TArray<FBSMechDef> Out;
		auto Add = [&Out](const TCHAR* Key, const TCHAR* Name) -> FBSMechDef& { FBSMechDef& D = Out.AddDefaulted_GetRef(); D.Key = Key; D.Name = Name; return D; };
		{
			FBSMechDef& D = Add(TEXT("goliath"), TEXT("Goliath"));
			D.Hp = 150; D.Armor = 1; D.Radius = 0.7f; D.Cost = 250; D.Max = 4; D.Speed = 3;
			D.GDmg = 12; D.GShots = 2; D.GCd = 0.9f; D.GRange = 6; D.ADmg = 10; D.AShots = 2; D.ACd = 1.5f; D.ARange = 8;
		}
		{
			FBSMechDef& D = Add(TEXT("thor"), TEXT("Thor"));
			D.Hp = 450; D.Armor = 1; D.Radius = 1.25f; D.Cost = 600; D.Max = 2; D.Speed = 2.2f;
			D.GDmg = 30; D.GShots = 2; D.GCd = 1.28f; D.GRange = 10; D.ADmg = 6; D.AShots = 4; D.ACd = 2.1f; D.ARange = 12; D.ASplash = 0.6f;
		}
		{
			FBSMechDef& D = Add(TEXT("odin"), TEXT("Odin"));
			D.Hp = 1500; D.Armor = 2; D.Radius = 2.2f; D.Cost = 2500; D.Max = 1; D.Speed = 2;
			D.GDmg = 60; D.GShots = 2; D.GCd = 1.1f; D.GRange = 16; D.GSplash = 1.2f; D.ADmg = 15; D.AShots = 6; D.ACd = 2; D.ARange = 16; D.ASplash = 0.8f;
		}
		return Out;
	}

	TArray<FBSTechCard> MakeTech()
	{
		return {
			{ TEXT("goliath"), TEXT("Goliath"), TEXT("btn-unit-terran-goliath"), TEXT("vehicle"), TEXT("GOLIATHS") },
			{ TEXT("nuke"), TEXT("Tactical Nuke"), TEXT("btn-ability-terran-nuclearstrike"), TEXT("weapons"), TEXT("TACTICAL NUKE") },
			{ TEXT("vikings"), TEXT("Viking Wing"), TEXT("btn-unit-terran-vikingfighter"), TEXT("systems"), TEXT("VIKING WING") },
			{ TEXT("thor"), TEXT("Thor"), TEXT("btn-unit-terran-thor"), TEXT("plating"), TEXT("THOR") },
			{ TEXT("bc"), TEXT("Battlecruiser"), TEXT("btn-unit-terran-battlecruiser"), TEXT("all"), TEXT("BATTLECRUISER ON STATION") },
			{ TEXT("odin"), TEXT("Odin"), TEXT("btn-unit-terran-odin"), TEXT("bc"), TEXT("THE ODIN") },
		};
	}
}

namespace BSDefs
{
	const TArray<FBSMechDef>& Mechs() { static TArray<FBSMechDef> V = MakeMechs(); return V; }
	const FBSMechDef* FindMech(const FString& Key) { return Mechs().FindByPredicate([&](const FBSMechDef& D) { return D.Key == Key; }); }
	const TArray<FBSTechCard>& Tech() { static TArray<FBSTechCard> V = MakeTech(); return V; }
	const FBSTechCard* FindTech(const FString& Id) { return Tech().FindByPredicate([&](const FBSTechCard& C) { return C.Id == Id; }); }
	const TArray<FBSZergDef>& Zerg() { static TArray<FBSZergDef> V = MakeZerg(); return V; }
	const FBSZergDef* FindZerg(const FString& Key) { return Zerg().FindByPredicate([&](const FBSZergDef& D) { return D.Key == Key; }); }
	const TArray<FBSInfantryDef>& Infantry() { static TArray<FBSInfantryDef> V = MakeInfantry(); return V; }
	const FBSInfantryDef* FindInfantry(const FString& Key) { return Infantry().FindByPredicate([&](const FBSInfantryDef& D) { return D.Key == Key; }); }
	const TArray<FBSUpgradeDef>& Upgrades() { static TArray<FBSUpgradeDef> V = MakeUpgrades(); return V; }
	const FBSUpgradeDef* FindUpgrade(const FString& Key) { return Upgrades().FindByPredicate([&](const FBSUpgradeDef& D) { return D.Key == Key; }); }
}

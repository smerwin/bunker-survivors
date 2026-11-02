// Swarm mode: the tables turned. The player is the zerg: minerals and vespene feed hatcheries, a lair and a hive that
// hatch ever-evolving broods on their own; the goal is a hive and the economy to summon Kerrigan (with drop pods, as in
// the Terran runs' finale), who then answers to the player directly. The Terrans play themselves (the commander below,
// the same one the balance bots use) and win by holding out until the Hyperion takes them off. Unlocked by escaping
// Kerrigan once (BSMeta::SwarmOpen). Data in BSSwarm (BSTypes). Port of client/js/swarm.js; keep the two in step.
#include "BSGame.h"
#include "BSTerrain.h"
#include "BSModelActor.h"
#include "Algo/Reverse.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

namespace
{
	float Rand01() { return FMath::FRand(); }
	float Dist(float Ax, float Ay, float Bx, float By) { return FMath::Sqrt((Ax - Bx) * (Ax - Bx) + (Ay - By) * (Ay - By)); }
	FBSTarget PointTarget(const FVector2D& P) { FBSTarget T; T.Kind = FBSTarget::Point; T.X = P.X; T.Y = P.Y; T.Radius = 0; return T; }
	int32 StrainBudget(const BSSwarm::FStrain& S) { return S.Budget ? S.Budget : BSDefs::FindZerg(S.Id)->Budget; }
	// Her spells' cooldown fields, by ability id.
	float* HeroCdField(FBSEnemy& E, const FString& Id)
	{
		if (Id == TEXT("leap")) return &E.LeapCd;
		if (Id == TEXT("apocalypse")) return &E.ApocCd;
		if (Id == TEXT("kinetic")) return &E.KineticCd;
		if (Id == TEXT("grip")) return &E.GripCd;
		if (Id == TEXT("chain")) return &E.ChainCd;
		if (Id == TEXT("wave")) return &E.WaveCd;
		if (Id == TEXT("mutation")) return &E.MutateCd;
		if (Id == TEXT("banelings")) return &E.BanesCd;
		return nullptr;
	}
}

void ABSGame::InitSwarm()
{
	Sw = FSwarmState();
	Sw.Strains.Add(TEXT("zergling"));
	Income *= BSSwarm::TerranIncome;
	SyncBase();
}

// ---- The swarm's economy --------------------------------------------------------------------------------------
void ABSGame::UpdateSwarm(float Dt)
{
	Sw.Minerals += SwarmIncome() * Dt;
	Sw.Gas += SwarmGasIncome() * Dt;
	Sw.Larva = FMath::Min(LarvaRate() * BSSwarm::Brood * BSSwarm::Bank, Sw.Larva + LarvaRate() * Dt);
	if ((Sw.BroodT -= Dt) <= 0) { Sw.BroodT = BSSwarm::Brood - ZCardLevel(TEXT("pulse")); HatchBrood(); }
	UpdateResearch(Dt * (1 + 0.25f * ZCardLevel(TEXT("haste"))));
	AutoInject();
	UpdateSunkens(Dt);
	// Kerrigan down: she can be summoned again once the hive has recovered (BSSwarm::HeroRespawn).
	if (Sw.Hero.IsValid() && Sw.Hero->Hp <= 0) { Sw.Hero.Reset(); Sw.HeroAt = Time + BSSwarm::HeroRespawn; Toast(TEXT("Kerrigan has fallen: summon her again when the hive recovers")); }
	// The Terran commander.
	if ((Sw.Think -= Dt) <= 0) { Sw.Think = 0.5f; TerranThink(); }
	if ((Sw.BaseT -= Dt) <= 0) { Sw.BaseT = 0.5f; SyncBase(); }
	// Playtesting: -BSRaidAt=N forces a Terran raid on the hive N s in.
	static float RaidAt = -1;
	if (RaidAt < 0) { RaidAt = 0; FParse::Value(FCommandLine::Get(), TEXT("BSRaidAt="), RaidAt); }
	if (RaidAt > 0 && Time >= RaidAt) { RaidAt = 0; TerranRaid(TerranRead(), true); }
}

// The strains the hives can hatch now (bought, and not at their cap; anti-air ones only while there is Terran air).
const BSSwarm::FStrain* ABSGame::SwarmPick() const
{
	TMap<FString, int32> Have;
	for (const TSharedPtr<FBSEnemy>& E : Enemies) Have.FindOrAdd(E->Def->Key)++;
	const bool bAir = AirPresence();
	TArray<const BSSwarm::FStrain*> List;
	for (const BSSwarm::FStrain& S : BSSwarm::Strains())
		if (Sw.Strains.Contains(S.Id) && !(S.Max && Have.FindRef(S.Id) >= S.Max) && !(BSDefs::FindZerg(S.Id)->bAntiAir && !bAir) && !(S.bLane && Mode != EBSMode::Lane)) List.Add(&S);
	auto Wt = [](const BSSwarm::FStrain* S) { const float W = S->Weight ? S->Weight : BSDefs::FindZerg(S->Id)->Weight; return W ? W : 1.f; };
	float Total = 0;
	for (const BSSwarm::FStrain* S : List) Total += Wt(S);
	float Roll = Rand01() * Total;
	for (const BSSwarm::FStrain* S : List) { Roll -= Wt(S); if (Roll <= 0) return S; }
	return List.Num() ? List.Last() : nullptr;
}

// A brood: everything the banked larva pays for, from one approach (mostly true north, sometimes down an arm).
void ABSGame::HatchBrood()
{
	const bool bLane = Mode == EBSMode::Lane;
	const float Ang = bLane ? (Rand01() < 0.35f ? (Rand01() < 0.5f ? 1.f : 2.f) : 0.f) : Rand01() * 2 * PI;
	for (int32 Guard = 0; Guard < 200; Guard++)
	{
		if (!Sw.Next) Sw.Next = SwarmPick();
		if (!Sw.Next || Sw.Larva < StrainBudget(*Sw.Next) || AliveCount() >= MaxAlive()) break;
		if (Sw.bHold && HeldCount() >= HoldCap()) break;         // the hive holds only so many (the larva banks)
		Sw.Larva -= StrainBudget(*Sw.Next);
		if (TSharedPtr<FBSEnemy> E = SpawnEnemy(Sw.Next->Id, Ang)) E->bHeld = Sw.bHold;
		Sw.Next = nullptr;
	}
}

// The Terrans' round: their wave clock still runs (income, the shop between), but no zerg come from it.
void ABSGame::SwarmRound(int32 W)
{
	SpawnTimer = 0; WaveTimer = WaveLen;
	Minerals += 20 + 8 * W;
}

// ---- Buying -----------------------------------------------------------------------------------------------------
bool ABSGame::SwarmCost(const FString& Kind, const FString& Id, BSSwarm::FCost& Out) const
{
	if (Kind == TEXT("drone")) { Out = { BSSwarm::DroneCost, 0 }; return Sw.Drones < BSSwarm::DroneMax; }
	if (Kind == TEXT("extractor")) { Out = { BSSwarm::ExtractorCost, 0 }; return Sw.Extractors < BSSwarm::ExtractorMax; }
	if (Kind == TEXT("hatch")) { if (Sw.Hatcheries - 1 >= UE_ARRAY_COUNT(BSSwarm::HatchCosts)) return false; Out = { BSSwarm::HatchCosts[Sw.Hatcheries - 1], 0 }; return true; }
	if (Kind == TEXT("queen")) { if (Sw.Queens >= UE_ARRAY_COUNT(BSSwarm::QueenCosts)) return false; Out = { BSSwarm::QueenCosts[Sw.Queens], 0 }; return true; }
	if (Kind == TEXT("chamber")) { Out = BSSwarm::ChamberCost; return Sw.Chambers < BSSwarm::ChamberMax; }
	if (Kind == TEXT("tier")) { if (Sw.Tier + 1 >= BSSwarm::Tiers().Num()) return false; Out = BSSwarm::Tiers()[Sw.Tier + 1].Cost; return true; }
	if (Kind == TEXT("hero"))
	{
		const float K = ZCardLevel(TEXT("favor")) ? 0.75f : 1.f;          // Kerrigan's Favor
		Out = { FMath::RoundToInt(BSSwarm::HeroCost.M * K), FMath::RoundToInt(BSSwarm::HeroCost.G * K) };
		return !HeroAlive();
	}
	if (Kind == TEXT("strain")) { const BSSwarm::FStrain* S = BSSwarm::FindStrain(Id); if (!S || Sw.Strains.Contains(Id)) return false; Out = S->Cost; return true; }
	if (Kind == TEXT("evo"))
	{
		const BSSwarm::FEvo* V = BSSwarm::FindEvo(Id);
		const int32 Lv = EvoLevel(Id);
		if (!V || Lv >= V->Costs.Num()) return false;
		Out = V->Costs[Lv];
		return true;
	}
	return false;
}

// The hive tier something needs (a strain, an evolution's next level), or 0.
int32 ABSGame::SwarmNeeds(const FString& Kind, const FString& Id) const
{
	if (Kind == TEXT("hero")) return BSSwarm::HeroTier;
	if (Kind == TEXT("strain")) { const BSSwarm::FStrain* S = BSSwarm::FindStrain(Id); return S ? S->Tier : 0; }
	if (Kind == TEXT("evo")) { const BSSwarm::FEvo* V = BSSwarm::FindEvo(Id); return V ? V->Tiers[FMath::Min(EvoLevel(Id), V->Tiers.Num() - 1)] : 0; }
	return 0;
}

// '' (buyable), 'max', 'locked' (the hive tier it needs), 'need' (a strain it improves is not bred yet), 'wait' (Kerrigan,
// until the hive recovers from her fall); chamber evolutions: 'nochamber', 'busy' (this line is being researched),
// 'full' (every chamber is busy).
FString ABSGame::SwarmState(const FString& Kind, const FString& Id) const
{
	BSSwarm::FCost C;
	if (!SwarmCost(Kind, Id, C)) return TEXT("max");
	if (SwarmNeeds(Kind, Id) > Sw.Tier) return TEXT("locked");
	if (Kind == TEXT("hero") && Time < Sw.HeroAt) return TEXT("wait");
	if (Kind == TEXT("evo"))
	{
		const BSSwarm::FEvo* V = BSSwarm::FindEvo(Id);
		if (V && V->Need && !Sw.Strains.Contains(V->Need)) return TEXT("need");
		if (V && V->Levels())
		{
			if (!Sw.Chambers) return TEXT("nochamber");
			if (Researching(Id)) return TEXT("busy");
			if (IdleChamber() < 0) return TEXT("full");
		}
	}
	return FString();
}

bool ABSGame::SwarmBuy(const FString& Kind, const FString& Id)
{
	const FString St = SwarmState(Kind, Id);
	BSSwarm::FCost C;
	SwarmCost(Kind, Id, C);
	auto FreeSpot = [](const bool* At, int32 N) { int32 i = 0; while (i < N && At[i]) i++; return i; };
	// The evolution chambers' item, at its max, switches them between evolving on their own and only when tapped.
	if (Kind == TEXT("chamber") && St == TEXT("max"))
	{
		Sw.bAutoEvo = !Sw.bAutoEvo;
		Toast(Sw.bAutoEvo ? TEXT("Evolution chambers: evolving by themselves each wave") : TEXT("Evolution chambers: evolving only when tapped"));
		return false;
	}
	if (St == TEXT("max") || St == TEXT("wait") || St == TEXT("busy")) return false;
	if (St == TEXT("nochamber")) { Toast(TEXT("Build an evolution chamber first")); return false; }
	if (St == TEXT("full")) { Toast(TEXT("Every evolution chamber is busy")); return false; }
	if (St == TEXT("locked")) { Toast(FString(TEXT("Needs a ")) + BSSwarm::Tiers()[SwarmNeeds(Kind, Id)].Name); return false; }
	if (St == TEXT("need")) { Toast(FString(TEXT("Breed ")) + BSDefs::FindZerg(BSSwarm::FindEvo(Id)->Need)->Name + TEXT("s first")); return false; }
	if (Sw.Minerals < C.M) { Toast(TEXT("Not enough minerals")); return false; }
	if (Sw.Gas < C.G) { Toast(TEXT("Not enough vespene")); return false; }
	Sw.Minerals -= C.M; Sw.Gas -= C.G;
	if (Kind == TEXT("drone")) Sw.Drones++;
	else if (Kind == TEXT("extractor")) { Sw.Extractors++; const int32 i = FreeSpot(Sw.ExtAt, UE_ARRAY_COUNT(Sw.ExtAt)); if (i < UE_ARRAY_COUNT(Sw.ExtAt)) Sw.ExtAt[i] = true; }
	else if (Kind == TEXT("hatch"))
	{
		Sw.Hatcheries++;
		const int32 i = FreeSpot(Sw.HatchAt, UE_ARRAY_COUNT(Sw.HatchAt)); if (i < UE_ARRAY_COUNT(Sw.HatchAt)) Sw.HatchAt[i] = true;
		Toast(FString::Printf(TEXT("Hatchery %d is up"), Sw.Hatcheries));
	}
	else if (Kind == TEXT("queen"))
	{
		Sw.Queens++;
		Toast(Sw.Queens <= Sw.Hatcheries ? FString::Printf(TEXT("A queen for hatchery %d: tap it to inject larva"), Sw.Queens) : FString::Printf(TEXT("Spawn Larva level %d"), Sw.Queens));
	}
	else if (Kind == TEXT("chamber"))
	{
		Sw.Chambers++;
		const int32 i = FreeSpot(Sw.ChamberAt, BSSwarm::ChamberMax); if (i < BSSwarm::ChamberMax) Sw.ChamberAt[i] = true;
		Toast(FString::Printf(TEXT("Evolution chamber %d: tap it to evolve"), Sw.Chambers));
	}
	else if (Kind == TEXT("tier"))
	{
		Sw.Tier++;
		WaveBanner = FString(TEXT("THE ")) + FString(BSSwarm::Tiers()[Sw.Tier].Name).ToUpper() + TEXT(" IS COMPLETE"); WaveBannerT = Time; WaveSerial++;
		Toast(Sw.Tier == BSSwarm::HeroTier ? TEXT("The hive can summon Kerrigan") : TEXT("New strains and evolutions open"));
	}
	else if (Kind == TEXT("strain")) { Sw.Strains.Add(Id); Toast(FString(BSSwarm::FindStrain(Id)->Name) + TEXT(" join the broods")); }
	else if (Kind == TEXT("evo"))
	{
		const BSSwarm::FEvo* V = BSSwarm::FindEvo(Id);
		const int32 Lv = EvoLevel(Id);
		if (V->Levels())
		{
			// Melee, missile and carapace: researched in an evolution chamber over ChamberTimes s.
			FSwarmState::FResearch& R = Sw.Research[IdleChamber()];
			R.Id = Id; R.T = 0; R.Dur = BSSwarm::ChamberTimes[FMath::Min(Lv, (int32)UE_ARRAY_COUNT(BSSwarm::ChamberTimes) - 1)];
			Toast(FString::Printf(TEXT("Evolving %s level %d"), V->Name, Lv + 1));
		}
		else { Sw.Evo.Add(Id, Lv + 1); Toast(FString(TEXT("Evolved: ")) + V->Name); }
	}
	else if (Kind == TEXT("hero")) SpawnHero();
	return true;
}

// The swarm's strains as evolved now (BSSwarm evolutions); the player's Kerrigan (mortal, BSSwarm::Hero*). Copies live
// as long as the game (EvoDefs), so zerg keep the def they hatched with.
const FBSZergDef* ABSGame::SwarmDef(const FString& Type)
{
	const FBSZergDef* Base = BSDefs::FindZerg(Type);
	if (!Base) return nullptr;
	FString Key = FString::Printf(TEXT("swarm:%s/%d"), *Type, Sw.Tier);
	for (const BSSwarm::FEvo& V : BSSwarm::Evos()) Key += FString::Printf(TEXT("/%d"), EvoLevel(V.Id));
	if (const TUniquePtr<FBSZergDef>* Have = EvoDefs.Find(Key)) return Have->Get();
	TUniquePtr<FBSZergDef> D = MakeUnique<FBSZergDef>(*Base);
	const int32 Melee = EvoLevel(TEXT("melee")), Missile = EvoLevel(TEXT("missile")), Carapace = EvoLevel(TEXT("carapace"));
	if (Type == TEXT("kerrigan"))
	{
		D->bUntouchable = false; D->bPreferBunker = false;
		D->Hp = BSSwarm::HeroHp * (1 + BSSwarm::EvoHp * Carapace);
		D->Dmg = BSSwarm::HeroDmg * (1 + BSSwarm::EvoDmg * Melee);
		D->DmgStructure = BSSwarm::HeroStructure * (1 + BSSwarm::EvoDmg * Melee);
		D->Armor = BSSwarm::HeroArmor + Carapace;
		D->Bounty = 300;
		return EvoDefs.Add(Key, MoveTemp(D)).Get();
	}
	const int32 Lv = Base->Range <= 1.5f ? Melee : Missile;
	if (Base->Dmg) D->Dmg = Base->Dmg * (1 + BSSwarm::EvoDmg * Lv);
	if (Base->DmgStructure) D->DmgStructure = Base->DmgStructure * (1 + BSSwarm::EvoDmg * Lv);
	D->Armor = Base->Armor + Carapace;
	D->Hp = Base->Hp * (1 + BSSwarm::EvoHp * Carapace);
	const bool bLing = Type == TEXT("zergling") || Type == TEXT("wingling");
	if (bLing && EvoLevel(TEXT("boost"))) D->Speed = Base->Speed * 1.4f;
	if (bLing && EvoLevel(TEXT("adrenal"))) D->Cd = Base->Cd * 0.7f;
	if (Type == TEXT("baneling") && EvoLevel(TEXT("hooks"))) { D->Speed = Base->Speed * 1.4f; D->Hp += 15; }
	if (Type == TEXT("roach") && EvoLevel(TEXT("glial"))) D->Speed = Base->Speed * 1.35f;
	if (Type == TEXT("hydralisk") && EvoLevel(TEXT("spines"))) D->Range = Base->Range + 1.5f;
	if (Type == TEXT("ultralisk") && EvoLevel(TEXT("chitin"))) D->Armor += 2;
	return EvoDefs.Add(Key, MoveTemp(D)).Get();
}

// ---- Kerrigan, the player's own -------------------------------------------------------------------------------
// Summoned: she comes down the lane while drop pods slam down all round the base.
void ABSGame::SpawnHero()
{
	// Out of her chrysalis at the base in the lane.
	TSharedPtr<FBSEnemy> E = SpawnEnemy(TEXT("kerrigan"), 0, Mode == EBSMode::Lane ? &BSSwarm::Chrysalis : nullptr);
	if (!E) return;
	E->bHero = true; E->bOrder = false; E->bHold = false; E->Power = BSSwarm::HeroPower;
	// Her spells start ready.
	E->LeapCd = E->ApocCd = E->KineticCd = E->GripCd = E->ChainCd = E->WaveCd = E->MutateCd = E->BanesCd = 0;
	Sw.Hero = E;
	SyncBase();
	DropPods(BSSwarm::HeroPods, 2, BSDefs::PodEvery);
	WaveBanner = TEXT("THE QUEEN OF BLADES TAKES THE FIELD"); WaveBannerT = Time; WaveSerial++;
	Toast(TEXT("Kerrigan answers the swarm: tap the ground to send her"));
	PlaySound(TEXT("finaleRoar"), 1.f);
	Shake = 1;
}

// Send her somewhere (a tap on the ground): she fights whatever she meets on the way, then holds that spot.
bool ABSGame::HeroOrder(float X, float Y)
{
	FBSEnemy* E = HeroAlive();
	if (!E) return false;
	if (Mode == EBSMode::Lane) X = FMath::Clamp(X, -21.f, 21.f);
	E->bOrder = true; E->Order = FVector2D(X, Y); E->bHold = false; E->Route.Reset(); E->bPerched = false; E->Retarget = 0;
	return true;
}

// Back to advancing with the swarm on her own.
bool ABSGame::HeroAdvance()
{
	FBSEnemy* E = HeroAlive();
	if (!E) return false;
	E->bOrder = false; E->bHold = false; E->Retarget = 0;
	return true;
}

void ABSGame::HeroTarget(FBSEnemy& E, float Dt)
{
	auto Near = [this](float X, float Y, float R) -> const FBSTarget*
	{
		const FBSTarget* Best = nullptr; float Bd = R;
		for (const FBSTarget& T : Targets) { const float D = Dist(X, Y, T.X, T.Y) - T.Radius; if (D < Bd) { Bd = D; Best = &T; } }
		return Best;
	};
	if (E.bOrder)
	{
		if (Dist(E.X, E.Y, E.Order.X, E.Order.Y) < 0.7f) { E.bHold = true; E.Hold = E.Order; E.bOrder = false; }
		else { const FBSTarget* T = Near(E.X, E.Y, BSSwarm::HeroEngage); E.Target = T ? *T : PointTarget(E.Order); return; }
	}
	if (E.bHold) { const FBSTarget* T = Near(E.Hold.X, E.Hold.Y, BSSwarm::HeroGuard); E.Target = T ? *T : PointTarget(E.Hold); return; }
	if ((E.Retarget -= Dt) <= 0 || !E.Target.IsValid(Bunker) || E.Target.Kind == FBSTarget::Point)
	{
		const FBSTarget* T = Near(E.X, E.Y, 1e9f);
		E.Target = T ? *T : FBSTarget();
		E.Retarget = 0.6f;
	}
}

// Her spells, on the player's word (a queued cast goes off as soon as she is free).
bool ABSGame::HeroKit(FBSEnemy& E, float Dt)
{
	E.KineticCd -= Dt; E.GripCd -= Dt; E.ChainCd -= Dt; E.WaveCd -= Dt; E.MutateCd -= Dt; E.BanesCd -= Dt;
	if (ZCardLevel(TEXT("psionic")))             // Psionic Link: every spell recharges a third faster
		for (float* Cd : { &E.KineticCd, &E.GripCd, &E.ChainCd, &E.WaveCd, &E.MutateCd, &E.BanesCd, &E.LeapCd, &E.ApocCd }) *Cd -= Dt / 3;
	if (!E.Queued.IsEmpty())
	{
		const FString Id = E.Queued;
		E.Queued.Reset();
		const FBSTarget* T = nullptr;
		if (Id == TEXT("leap"))
		{
			// Onto the defender out of reach with the most others round it (the bunker if nothing else).
			float Bn = -1;
			for (const FBSTarget& Tg : Targets)
			{
				const float R = Dist(E.X, E.Y, Tg.X, Tg.Y) - Tg.Radius;
				if (R > BSDefs::LeapMax || R < 1) continue;
				float N = Tg.bStructure ? -0.5f : 0.f;
				for (const FBSTarget& O : Targets) N += Dist(O.X, O.Y, Tg.X, Tg.Y) < 2.5f;
				if (N > Bn) { Bn = N; T = &Tg; }
			}
		}
		if (KerriganAbility(E, Id, T, true)) return true;
		const BSSwarm::FAbility* A = BSSwarm::FindAbility(Id);
		Toast(FString(TEXT("Nothing in range for ")) + (A ? FString(A->Name).ToLower() : Id));
	}
	// Advancing on her own: the same unsticking as the finale's.
	return !E.bOrder && !E.bHold ? KerriganUnstick(E, Dt) : false;
}

float ABSGame::HeroCd(const FString& Id) const
{
	FBSEnemy* E = HeroAlive();
	if (!E) return 0;
	const float* Cd = HeroCdField(*E, Id);
	return Cd ? FMath::Max(0.f, *Cd) : 0.f;
}

bool ABSGame::HeroCast(const FString& Id)
{
	FBSEnemy* E = HeroAlive();
	if (!E) { Toast(Sw.Tier < BSSwarm::HeroTier ? TEXT("Kerrigan answers only a hive") : TEXT("Summon Kerrigan first")); return false; }
	const float* Cd = HeroCdField(*E, Id);
	if (!Cd || *Cd > 0) return false;
	E->Queued = Id;
	return true;
}

// ---- The hive cluster on the field -------------------------------------------------------------------------------
// North of the lane, where the broods have always come from: the main hatchery (morphing to lair and hive) in the middle,
// scaled-down hatcheries and extractors round it, a tech building for each strain line and the evolution chamber, drones
// at work, larva by every hatchery, the queens, and Kerrigan's chrysalis once the hive can summon her. Everything grows in
// as it is bought (BSSwarm base data). Purely visual: the Terrans never reach it.
ABSGame::FSwarmState::FPart* ABSGame::BasePart(const FString& Key, const FString& Model, float X, float Y, float Scale, float Yaw)
{
	FSwarmState::FPart* Old = Sw.Parts.Find(Key);
	if (Old && Old->Model == Model) return Old;
	const bool bMorph = Old != nullptr;
	if (Old) ReleaseModel(Old->Actor);
	FSwarmState::FPart P;
	P.Model = Model; P.X = X; P.Y = Y; P.Scale = Scale; P.T = bMorph ? 0.6f : 0.f;
	P.Actor = AcquireModel(Model);                // (null if the model is not imported: the part is skipped quietly)
	if (P.Actor)
	{
		P.Actor->SetTeamColor(ZergColor);
		P.Actor->SetSim(X, Y, BSTerrain::Height(X, Y), Yaw);
		P.Actor->SetScale(0.01f);
		P.Actor->Play(TEXT("stand"), true);
	}
	return &Sw.Parts.Add(Key, P);
}

void ABSGame::DropPart(const FString& Key)
{
	if (FSwarmState::FPart* P = Sw.Parts.Find(Key)) { ReleaseModel(P->Actor); Sw.Parts.Remove(Key); }
}

// What the base should show for what has been bought.
void ABSGame::SyncBase()
{
	if (Mode != EBSMode::Lane) return;
	const FVector2D At = BSSwarm::BaseAt;
	static const TCHAR* Main[] = { TEXT("hatchery"), TEXT("lair"), TEXT("hive") };
	BaseEntity(TEXT("main"), BasePart(TEXT("main"), Main[FMath::Clamp(Sw.Tier, 0, 2)], At.X, At.Y, BSSwarm::BaseMain));
	TArray<TPair<FVector, FString>> Hatch = { { FVector(At.X, At.Y, BSSwarm::BaseMain), TEXT("main") } };
	for (int32 i = 0; i < UE_ARRAY_COUNT(BSSwarm::HatchSpots); i++)
	{
		if (!Sw.HatchAt[i]) continue;
		const FVector2D P = BSSwarm::HatchSpots[i];
		const FString Key = FString::Printf(TEXT("hatch%d"), i);
		BaseEntity(Key, BasePart(Key, TEXT("hatchery"), P.X, P.Y, BSSwarm::HatchScale));
		Hatch.Add({ FVector(P.X, P.Y, BSSwarm::HatchScale), Key });
	}
	for (int32 i = 0; i < UE_ARRAY_COUNT(BSSwarm::ExtractorSpots); i++)
		if (Sw.ExtAt[i]) { const FString Key = FString::Printf(TEXT("extractor%d"), i); BaseEntity(Key, BasePart(Key, TEXT("extractor"), BSSwarm::ExtractorSpots[i].X, BSSwarm::ExtractorSpots[i].Y, BSSwarm::ExtractorScale)); }
	for (int32 i = 0; i < ZCardLevel(TEXT("sunken")) && i < UE_ARRAY_COUNT(BSSwarm::SunkenSpots); i++)
		{ const FString Key = FString::Printf(TEXT("sunken%d"), i); BaseEntity(Key, BasePart(Key, TEXT("spinecrawler"), BSSwarm::SunkenSpots[i].X, BSSwarm::SunkenSpots[i].Y, 0.7f)); }
	for (int32 i = 0; i < BSSwarm::ChamberMax; i++)
		if (Sw.ChamberAt[i]) { const FString Key = FString::Printf(TEXT("chamber%d"), i); BaseEntity(Key, BasePart(Key, TEXT("evolutionchamber"), BSSwarm::ChamberSpots[i].X, BSSwarm::ChamberSpots[i].Y, BSSwarm::ChamberScale)); }
	// Tech: each building once anything it serves is bred, in the next free spot.
	for (const BSSwarm::FTech& T : BSSwarm::Tech())
	{
		bool bOn = false;
		for (const TCHAR* S : T.Serves) bOn |= Sw.Strains.Contains(S);
		if (!bOn) continue;
		if (!Sw.TechSpot.Contains(T.Model)) Sw.TechSpot.Add(T.Model, Sw.TechSpot.Num());
		const FVector2D P = BSSwarm::TechSpots[Sw.TechSpot[T.Model] % UE_ARRAY_COUNT(BSSwarm::TechSpots)];
		BasePart(FString(TEXT("tech:")) + T.Model, T.Model, P.X, P.Y, BSSwarm::TechScale);
	}
	// Larva wriggling at every hatchery's foot, the queens round the main one.
	for (const auto& H : Hatch)
		for (int32 k = 0; k < BSSwarm::BaseLarva; k++)
		{
			const float A = -PI / 2 + (k - (BSSwarm::BaseLarva - 1) / 2.f) * 0.5f, R = 3.1f * H.Key.Z / BSSwarm::BaseMain + 0.4f;
			BasePart(FString::Printf(TEXT("larva:%s:%d"), *H.Value, k), TEXT("larva"), H.Key.X + FMath::Cos(A) * R, H.Key.Y + FMath::Sin(A) * R, 0.9f, Rand01() * 2 * PI);
		}
	for (int32 i = 0; i < 6; i++)
	{
		const FString Key = FString::Printf(TEXT("queen%d"), i);
		if (i < Sw.Queens) { const float A = PI * (0.15f + 0.14f * i); BasePart(Key, TEXT("queen"), At.X + FMath::Cos(A) * 4.2f, At.Y - FMath::Sin(A) * 3.6f, 0.85f); }
		else DropPart(Key);
	}
	// Kerrigan's chrysalis: there from the moment the hive can summon her until she breaks out of it.
	const FVector2D Ch = BSSwarm::Chrysalis;
	if (Sw.Tier >= BSSwarm::HeroTier && !HeroAlive()) BasePart(TEXT("chrysalis"), TEXT("chrysalis"), Ch.X, Ch.Y, 0.9f);
	else if (Sw.Parts.Contains(TEXT("chrysalis"))) { PlayFx(TEXT("nydusBirth"), FVector(Ch.X, Ch.Y, BSTerrain::Height(Ch.X, Ch.Y)), 0, 1.6f); DropPart(TEXT("chrysalis")); }
	// The mineral line behind the main hatchery.
	for (int32 i = 0; i < UE_ARRAY_COUNT(BSSwarm::MineralSpots); i++)
		BasePart(FString::Printf(TEXT("mineral%d"), i), FString::Printf(TEXT("mineral%d"), i % 3), BSSwarm::MineralSpots[i].X, BSSwarm::MineralSpots[i].Y, BSSwarm::MineralScale, Rand01() * 2 * PI);
	// Drones: up to BaseDrones of them, three to an extractor and the rest mining the mineral line, back and forth from the
	// main hatchery.
	const int32 Want = FMath::Min(Sw.Drones, BSSwarm::BaseDrones);
	while (Sw.Drones2.Num() < Want)
	{
		const FVector H = Hatch[Sw.Drones2.Num() % Hatch.Num()].Key;
		FSwarmState::FDrone Dr;
		Dr.X = Dr.Tx = H.X; Dr.Y = Dr.Ty = H.Y - 2; Dr.Wait = Rand01() * 2;
		Dr.Actor = AcquireModel(TEXT("drone"));
		if (Dr.Actor) { Dr.Actor->SetTeamColor(ZergColor); Dr.Actor->SetScale(0.8f); Dr.Actor->SetSim(Dr.X, Dr.Y, BSTerrain::Height(Dr.X, Dr.Y), 0); Dr.Actor->Play(TEXT("stand"), true); }
		Sw.Drones2.Add(Dr);
	}
}

// A base building's hit points: an entity in Enemies (the Terrans' guns find it; zerg do not target it) sharing the part's
// model. Destroyed, it is gone from the base and the economy (BaseLost); the main hatchery is the run.
void ABSGame::BaseEntity(const FString& Key, const FSwarmState::FPart* Part)
{
	if (!Part) return;
	FString Kind = Key;
	while (Kind.Len() && FChar::IsDigit(Kind[Kind.Len() - 1])) Kind.LeftChopInline(1);
	const float Hide = 1 + 0.5f * ZCardLevel(TEXT("hide"));
	const float MainHp = BSSwarm::HpMain[FMath::Clamp(Sw.Tier, 0, 2)] * Hide;
	TSharedPtr<FBSEnemy>* Have = Sw.Ents.Find(Key);
	if (!Have)
	{
		const bool bMain = Kind == TEXT("main");
		const float Hp = (bMain ? BSSwarm::HpMain[FMath::Clamp(Sw.Tier, 0, 2)] : Kind == TEXT("hatch") ? BSSwarm::HpHatch : Kind == TEXT("extractor") ? BSSwarm::HpExtractor : Kind == TEXT("sunken") ? BSSwarm::HpSunken : BSSwarm::HpChamber) * Hide;
		TSharedPtr<FBSZergDef> D = MakeShared<FBSZergDef>();
		D->Key = TEXT("base:") + Key;
		D->Name = bMain ? BSSwarm::Tiers()[Sw.Tier].Name : Kind == TEXT("hatch") ? TEXT("Hatchery") : Kind == TEXT("extractor") ? TEXT("Extractor") : Kind == TEXT("sunken") ? TEXT("Sunken Colony") : TEXT("Evolution Chamber");
		D->Hp = Hp; D->Armor = 1; D->Dmg = 0; D->Cd = 1; D->Range = 0; D->Speed = 0; D->bArmored = true;
		D->Radius = bMain ? BSSwarm::RadiusMain : Kind == TEXT("hatch") ? BSSwarm::RadiusHatch : Kind == TEXT("extractor") ? BSSwarm::RadiusExtractor : Kind == TEXT("sunken") ? BSSwarm::RadiusSunken : BSSwarm::RadiusChamber;
		D->bBaseBuilding = true; D->bSunken = Kind == TEXT("sunken"); D->bUncounted = true; D->bAmbient = true;
		D->Bounty = BSSwarm::BaseBounty; D->Budget = 0; D->Unlock = 999; D->Weight = 0; D->HitFrac = 0; D->Height = 2;
		Sw.BaseDefs.Add(D);
		TSharedPtr<FBSEnemy> E = MakeShared<FBSEnemy>();
		E->Id = NextId++; E->Def = D.Get(); E->BaseKey = Key;
		E->X = Part->X; E->Y = Part->Y; E->Z = BSTerrain::Height(Part->X, Part->Y); E->Yaw = -PI / 2;
		E->Hp = E->MaxHp = Hp; E->Radius = D->Radius; E->Cd = 1;
		Enemies.Add(E);
		Have = &Sw.Ents.Add(Key, E);
	}
	FBSEnemy& E = **Have;
	E.Actor = Part->Actor;
	// The main hatchery morphing: tougher, healed by what it gains.
	if (Kind == TEXT("main") && E.MaxHp < MainHp - 1)
	{
		const float Gain = MainHp - E.MaxHp;
		E.MaxHp += Gain; E.Hp += Gain;
		const_cast<FBSZergDef*>(E.Def)->Name = BSSwarm::Tiers()[Sw.Tier].Name;
	}
}

void ABSGame::BaseLost(const FString& Key)
{
	FString Kind = Key;
	while (Kind.Len() && FChar::IsDigit(Kind[Kind.Len() - 1])) Kind.LeftChopInline(1);
	const int32 i = FCString::Atoi(*Key.Mid(Kind.Len()));
	Sw.Ents.Remove(Key); Sw.Parts.Remove(Key);            // (its model went with the entity)
	if (Kind == TEXT("main")) { HiveLost(); return; }
	if (Kind == TEXT("hatch")) { Sw.HatchAt[i] = false; Sw.Hatcheries--; Sw.Inject.Remove(Key); for (int32 k = 0; k < BSSwarm::BaseLarva; k++) DropPart(FString::Printf(TEXT("larva:%s:%d"), *Key, k)); }
	if (Kind == TEXT("extractor")) { Sw.ExtAt[i] = false; Sw.Extractors--; }
	if (Kind == TEXT("chamber")) { Sw.ChamberAt[i] = false; Sw.Chambers--; Sw.Research[i] = FSwarmState::FResearch(); }
	if (Kind == TEXT("sunken")) Sw.Cards.FindOrAdd(TEXT("sunken"))--;
	Toast(FString(Kind == TEXT("hatch") ? TEXT("A hatchery") : Kind == TEXT("extractor") ? TEXT("An extractor") : Kind == TEXT("sunken") ? TEXT("A sunken colony") : TEXT("An evolution chamber")) + TEXT(" has been destroyed!"));
	Shake = FMath::Min(1.f, Shake + 0.4f);
}

void ABSGame::HiveLost()
{
	if (bOver) return;
	bOver = true; bWon = false; bHiveDown = true;
	WaveBanner = TEXT("THE HIVE HAS FALLEN"); WaveBannerT = Time; WaveSerial++;
	PlaySound(BSSounds::Explosion, 0.9f);
	Shake = 1;
	EndReason = TEXT("hive");
	FinishRun();
}

// The Terran commander's raid: when the lane is empty and little is massed at the hive, a squad of stimmed marines charges
// up the lane to burn the base (BSSwarm Raid*). bForce (playtesting, -BSRaidAt): now, with whatever marines it has (and
// a fresh squad of them if it has none), stim or not.
void ABSGame::TerranRaid(const FTerranRead& R, bool bForce)
{
	if (!bSwarm || Mode != EBSMode::Lane) return;
	if (!bForce)
	{
		if (Time < BSSwarm::RaidFrom || Time < Sw.RaidAt || !Up.bStim) return;
		if (R.Total - R.Massed > BSSwarm::RaidMaxField || R.Massed > BSSwarm::RaidMaxMassed) return;
	}
	auto Marines = [this]()
	{
		TArray<TSharedPtr<FBSTrooper>> Out;
		for (const TSharedPtr<FBSTrooper>& M : Squad) if (!M->bDead && !M->bRaid && !M->Fwd.bOn && !M->bBoarding && M->Def->Key == TEXT("marine")) Out.Add(M);
		return Out;
	};
	TArray<TSharedPtr<FBSTrooper>> List = Marines();
	if (bForce && List.Num() < BSSwarm::RaidMin)
	{
		const FBSInfantryDef* Md = BSDefs::FindInfantry(TEXT("marine"));
		for (int32 k = List.Num(); k < BSSwarm::RaidMin; k++) JoinSquad(Md, TrooperHp(Md), FMath::FRandRange(-2.f, 2.f), -2.5f, nullptr);
		List = Marines();
	}
	if (List.Num() < (bForce ? 1 : BSSwarm::RaidMin)) return;
	// Up the lane: the zerg's own way down it, walked backwards.
	TArray<FVector2D> Route = BSTerrain::Route(BSSwarm::BaseAt.X, BSSwarm::BaseAt.Y - 4, false);
	Algo::Reverse(Route);
	for (int32 k = 0; k < List.Num() && k < BSSwarm::RaidMax; k++) { List[k]->bRaid = true; List[k]->RaidRoute = Route; }
	Sw.RaidAt = Time + BSSwarm::RaidCd;
	if (Ab.StimCd <= 0 && Up.bStim) UseStim();
	WaveBanner = TEXT("STIMMED MARINES RUSHING THE HIVE"); WaveBannerT = Time; WaveSerial++;
	Toast(TEXT("Marines are rushing the hive: hold the broods to defend it!"));
}

// Where a raider goes next: along its route up the lane, then at the nearest base building.
FVector2D ABSGame::RaidGoal(FBSTrooper& M)
{
	while (M.RaidRoute.Num() && Dist(M.RaidRoute[0].X, M.RaidRoute[0].Y, M.X, M.Y) < 1.2f) M.RaidRoute.RemoveAt(0);
	if (M.RaidRoute.Num()) return M.RaidRoute[0];
	const FBSEnemy* Pick = nullptr; float Bd = 1e9f;
	for (const TSharedPtr<FBSEnemy>& E : Enemies) if (E->Def->bBaseBuilding && E->Alive()) { const float D = Dist(M.X, M.Y, E->X, E->Y); if (D < Bd) { Bd = D; Pick = E.Get(); } }
	if (!Pick) return FVector2D(M.X, M.Y);
	const float A = FMath::Atan2(M.Y - Pick->Y, M.X - Pick->X), Rr = M.Def->Range * 0.85f + Pick->Radius;
	return FVector2D(Pick->X + FMath::Cos(A) * Rr, Pick->Y + FMath::Sin(A) * Rr);
}

// Every frame: growing in, drones at work.
void ABSGame::UpdateBase(float Dt)
{
	for (auto& Pr : Sw.Parts)
	{
		FSwarmState::FPart& P = Pr.Value;
		if (P.T >= 1) continue;
		P.T = FMath::Min(1.f, P.T + Dt / BSSwarm::BaseGrow);
		const float K = 1 - (1 - P.T) * (1 - P.T);
		if (P.Actor) P.Actor->SetScale(FMath::Max(0.01f, P.Scale * K));
	}
	TArray<FVector2D> Ext;
	for (int32 i = 0; i < UE_ARRAY_COUNT(BSSwarm::ExtractorSpots); i++) if (Sw.ExtAt[i]) Ext.Add(BSSwarm::ExtractorSpots[i]);
	for (int32 n = 0; n < Sw.Drones2.Num(); n++)
	{
		FSwarmState::FDrone& D = Sw.Drones2[n];
		if (D.bMoving)
		{
			const float Dx = D.Tx - D.X, Dy = D.Ty - D.Y, L = FMath::Sqrt(Dx * Dx + Dy * Dy), S = BSSwarm::DroneSpeed * Dt;
			if (L <= S)
			{
				D.X = D.Tx; D.Y = D.Ty; D.bMoving = false; D.Wait = D.bHome ? 0.4f : 1.5f + Rand01();
				if (D.Actor) D.Actor->Play(D.bHome ? TArray<FString>{ TEXT("stand") } : TArray<FString>{ TEXT("stand work"), TEXT("stand") }, true);
			}
			else { D.X += Dx / L * S; D.Y += Dy / L * S; D.Yaw = FMath::Atan2(Dy, Dx); }
			if (D.Actor) D.Actor->SetSim(D.X, D.Y, BSTerrain::Height(D.X, D.Y), D.Yaw);
		}
		else if ((D.Wait -= Dt) <= 0)
		{
			// Out to its mineral field or extractor, then back to the hatchery with the load.
			D.bHome = !D.bHome;
			const FVector2D Work = n < Ext.Num() * 3 ? Ext[n / 3] : BSSwarm::MineralSpots[n % UE_ARRAY_COUNT(BSSwarm::MineralSpots)], H = BSSwarm::BaseAt;
			const FVector2D P = D.bHome ? H + (Work - H) * 0.45f : Work;
			const float A = FMath::Atan2(H.Y - P.Y, H.X - P.X);
			D.Tx = P.X + (D.bHome ? 0 : FMath::Cos(A) * 0.9f) + (Rand01() - 0.5f) * 0.4f; D.Ty = P.Y + (D.bHome ? 0 : FMath::Sin(A) * 0.9f) + (Rand01() - 0.5f) * 0.4f;
			D.bMoving = true;
			if (D.Actor) D.Actor->Play(TEXT("walk"), true);
		}
	}
}

// Where a brood member comes out: at the foot of one of the hatcheries (the east or west ones for the arms).
FVector2D ABSGame::BroodSpawn(float Ang) const
{
	TArray<FVector2D> List;
	for (const FHatch& H : HatchSpots()) List.Add(H.P);
	const int32 Side = FMath::RoundToInt(Ang);         // 1 north-east arm, 2 north-west
	TArray<FVector2D> Pool = List.FilterByPredicate([Side](const FVector2D& P) { return Side == 1 ? P.X > 0.5f : Side == 2 ? P.X < -0.5f : true; });
	if (!Pool.Num()) Pool = List;
	const FVector2D P = Pool[FMath::RandRange(0, Pool.Num() - 1)];
	return FVector2D(P.X + FMath::FRandRange(-1.6f, 1.6f), P.Y - FMath::FRandRange(2.2f, 3.4f));
}

// ---- Mutations: the zerg's cards between rounds -------------------------------------------------------------------
// After every Terran round the fight pauses for the hive's own shop: ShopOffers random cards from BSSwarm::Cards
// (weighted by rarity as the Terrans' are), one of which may be bought with the swarm's minerals and vespene; rerolls
// cost minerals. Event cards strike the Terrans at once; the rest last the run.
BSSwarm::FCost ABSGame::ZCardCost(const BSSwarm::FZCard& C) const
{
	const int32 Lv = ZCardLevel(C.Id);
	return { C.Cost.M + C.Grow.M * Lv, C.Cost.G + C.Grow.G * Lv };
}

bool ABSGame::ZCardOpen(const BSSwarm::FZCard& C) const
{
	if (ZCardLevel(C.Id) >= (C.Max ? C.Max : 99) || Wave < FMath::Max(BSDefs::FindRarity(C.Rarity).From, C.From)) return false;
	if (FCString::Strcmp(C.Id, TEXT("dronedrop")) == 0 && Sw.Drones >= BSSwarm::DroneMax) return false;
	return true;
}

void ABSGame::OpenZergShop()
{
	if (bOver) return;
	Shop = FShop();
	Shop.bOpen = true; Shop.bZerg = true; Shop.Wave = Wave;
	Shop.Offers = RollZergOffers();
	Shop.Bought.Init(false, Shop.Offers.Num());
	UE_LOG(LogTemp, Log, TEXT("BSZergShop wave=%d offers=%s"), Wave, *FString::Join(Shop.Offers, TEXT(",")));
}

TArray<FString> ABSGame::RollZergOffers() const
{
	TArray<const BSSwarm::FZCard*> Pool;
	for (const BSSwarm::FZCard& C : BSSwarm::Cards()) if (ZCardOpen(C)) Pool.Add(&C);
	TArray<FString> Out;
	while (Out.Num() < BSSwarm::ShopOffers && Pool.Num())
	{
		float Total = 0;
		for (const BSSwarm::FZCard* C : Pool) Total += BSDefs::FindRarity(C->Rarity).Weight;
		float Roll = Rand01() * Total;
		int32 i = 0;
		for (; i < Pool.Num() - 1; i++) { Roll -= BSDefs::FindRarity(Pool[i]->Rarity).Weight; if (Roll <= 0) break; }
		Out.Add(Pool[i]->Id);
		Pool.RemoveAt(i);
	}
	return Out;
}

bool ABSGame::ZergReroll()
{
	const int32 Cost = ZergRerollCost();
	if (Sw.Minerals < Cost) { Toast(TEXT("Not enough minerals")); return false; }
	Sw.Minerals -= Cost; Shop.Rerolls++;
	Shop.Offers = RollZergOffers();
	Shop.Bought.Init(false, Shop.Offers.Num());
	return true;
}

bool ABSGame::ZergBuyCard(int32 I)
{
	if (!Shop.Offers.IsValidIndex(I) || Shop.Bought[I]) return false;
	const BSSwarm::FZCard* C = BSSwarm::FindCard(Shop.Offers[I]);
	if (!C) return false;
	const BSSwarm::FCost Cost = ZCardCost(*C);
	if (Sw.Minerals < Cost.M) { Toast(TEXT("Not enough minerals")); return false; }
	if (Sw.Gas < Cost.G) { Toast(TEXT("Not enough vespene")); return false; }
	Sw.Minerals -= Cost.M; Sw.Gas -= Cost.G;
	const int32 Lv = ++Sw.Cards.FindOrAdd(C->Id);
	const FString Id = C->Id;
	if (Id == TEXT("tithe")) { const int32 M = 100 + 5 * Wave; Sw.Minerals += M; Toast(FString::Printf(TEXT("+%d minerals"), M)); }
	if (Id == TEXT("gasdrop")) { const int32 V = 75 + 4 * Wave; Sw.Gas += V; Toast(FString::Printf(TEXT("+%d vespene"), V)); }
	if (Id == TEXT("dronedrop")) { const int32 N = FMath::Min(4, BSSwarm::DroneMax - Sw.Drones); Sw.Drones += N; Toast(FString::Printf(TEXT("+%d drones"), N)); }
	if (C->bEvent) ZergEvent(*C);
	if (Id == TEXT("sunken")) SyncBase();
	for (int32 k = 0; k < Shop.Bought.Num(); k++) Shop.Bought[k] = true;        // one card a round
	if (Id == TEXT("hide"))
		for (auto& Pr : Sw.Ents) { const float K = 1.5f / (1 + 0.5f * (Lv - 1)); Pr.Value->MaxHp *= K; Pr.Value->Hp *= K; }
	Toast(FString(C->Name) + ((C->Max ? C->Max : 99) > 1 && Id != TEXT("tithe") ? FString::Printf(TEXT(" level %d"), Lv) : FString()));
	return true;
}

// Event cards: the swarm strikes at once.
void ABSGame::ZergEvent(const BSSwarm::FZCard& C)
{
	const bool bLane = Mode == EBSMode::Lane;
	auto Ang = [bLane]() { static const float Lane[] = { 0, 0, 1, 2 }; return bLane ? Lane[FMath::RandRange(0, 3)] : Rand01() * 2 * PI; };
	const FString Id = C.Id;
	if (Id == TEXT("hydras")) for (int32 i = 0; i < 10 + 2 * Sw.Tier; i++) SpawnEnemy(TEXT("hydralisk"), Ang());
	else if (Id == TEXT("ultras")) for (int32 i = 0; i < 3 + Sw.Tier; i++) SpawnEnemy(TEXT("ultralisk"), Ang());
	else if (Id == TEXT("nydus")) for (int32 i = 0; i < 2; i++) SpawnEnemy(TEXT("nydus"), 0);
	else if (Id == TEXT("pods")) DropPods(4, 1, 2);
	else if (Id == TEXT("darkswarm"))
	{
		FBSSwarm Cloud; Cloud.X = 0; Cloud.Y = 0; Cloud.R = 4.5f; Cloud.Until = Time + 15;
		Cloud.Fx = PlayFx(TEXT("darkSwarm"), FVector(0, 0, BSTerrain::Height(0, 0)), 0, 2.f);
		Swarms.Add(Cloud);
	}
	else if (Id == TEXT("plague"))
	{
		BuildTargets();
		for (const FBSTarget& T : Targets)
		{
			if (T.bStructure || T.X * T.X + T.Y * T.Y >= 81) continue;
			const float Until = Time + 6;
			switch (T.Kind)
			{
			case FBSTarget::Tank: if (auto P = T.TankRef.Pin()) P->PlagueUntil = Until; break;
			case FBSTarget::Scv: if (auto P = T.ScvRef.Pin()) P->PlagueUntil = Until; break;
			case FBSTarget::Trooper: if (auto P = T.TrooperRef.Pin()) P->PlagueUntil = Until; break;
			case FBSTarget::Mech: if (auto P = T.MechRef.Pin()) P->PlagueUntil = Until; break;
			default: break;
			}
			PlayFx(TEXT("plague"), FVector(T.X, T.Y, BSTerrain::Height(T.X, T.Y) + 0.5f));
		}
		PlaySound(TEXT("plague"), 0.7f);
	}
	WaveBanner = FString(C.Name).ToUpper(); WaveBannerT = Time; WaveSerial++;
	Shake = FMath::Min(1.f, Shake + 0.4f);
}

// Sunken colonies: the base's guns, firing on any Terran in reach (raiders, mostly).
void ABSGame::UpdateSunkens(float Dt)
{
	for (auto& Pr : Sw.Ents)
	{
		FBSEnemy& E = *Pr.Value;
		if (!E.Def->bSunken || !E.Alive() || (E.Cd -= Dt) > 0) continue;
		const FBSTarget* Pick = nullptr; float Bd = BSSwarm::SunkenRange;
		for (const FBSTarget& T : Targets) { if (T.Kind == FBSTarget::Bunker) continue; const float D = Dist(E.X, E.Y, T.X, T.Y) - T.Radius; if (D < Bd) { Bd = D; Pick = &T; } }
		if (!Pick) { E.Cd = 0.3f; continue; }
		E.Cd = BSSwarm::SunkenCd;
		if (E.Actor) E.Actor->PlayRandomWithPrefix(TEXT("attack"), false);
		const FBSTarget Tg = *Pick;
		PlayFx(TEXT("ultraliskImpact"), FVector(Tg.X, Tg.Y, BSTerrain::Height(Tg.X, Tg.Y)), 0, 0.8f);
		DamageDefender(Tg, BSSwarm::SunkenDmg);
		PlaySound(TEXT("ultraliskAttack"), 0.4f, 0.2f);
	}
}

// Brood Mothers: the queens inject on their own, a little after each inject is ready (AutoInject of its cooldown late).
void ABSGame::AutoInject()
{
	if (!ZCardLevel(TEXT("autoinject"))) return;
	const float Late = BSSwarm::InjectCd * BSSwarm::AutoInject;
	const TArray<FHatch> Hs = HatchSpots();
	for (int32 i = 0; i < Hs.Num(); i++) if (InjectReady(i) && Time >= Sw.Inject.FindRef(Hs[i].Key) + Late) Inject(i);
}

// ---- Base micro ---------------------------------------------------------------------------------------------------
// Red light, green light: while the broods are held they mass at the base (counting against the alive cap); released,
// everything held charges together in a frenzy (BSSwarm Hold*).
void ABSGame::BroodHold(int32 On)
{
	const bool bOn = On < 0 ? !Sw.bHold : On > 0;
	Sw.bHold = bOn;
	if (bOn) { Toast(TEXT("Broods hold at the hive")); return; }
	int32 N = 0;
	for (const TSharedPtr<FBSEnemy>& E : Enemies)
		if (E->bHeld) { E->bHeld = false; E->FrenzyUntil = Time + BSSwarm::HoldFrenzy + 3 * ZCardLevel(TEXT("frenzy")); E->Retarget = 0; N++; }
	if (N)
	{
		WaveBanner = FString::Printf(TEXT("THE SWARM ATTACKS  //  %d STRONG"), N); WaveBannerT = Time; WaveSerial++;
		PlaySound(TEXT("finaleRoar"), FMath::Min(1.f, 0.4f + N / 80.f), 2.f);
	}
}

// A held brood member with a Terran raider close: it fights instead of waiting.
bool ABSGame::HeldWakes(const FBSEnemy& E) const
{
	for (const FBSTarget& T : Targets) if (T.Kind != FBSTarget::Bunker && Dist(E.X, E.Y, T.X, T.Y) < BSSwarm::RaidWake) return true;
	return false;
}

int32 ABSGame::HeldCount() const
{
	int32 N = 0;
	for (const TSharedPtr<FBSEnemy>& E : Enemies) N += E->bHeld && E->Alive();
	return N;
}

// Queen injects: a hatchery with a queen of its own (one per Spawn Larva level, main hatchery first) can be injected every
// InjectCd s for a burst of larva. Tap the hatchery when it glows.
TArray<ABSGame::FHatch> ABSGame::HatchSpots() const
{
	TArray<FHatch> Out = { { BSSwarm::BaseAt, TEXT("main") } };
	for (int32 i = 0; i < UE_ARRAY_COUNT(BSSwarm::HatchSpots); i++) if (Sw.HatchAt[i]) Out.Add({ BSSwarm::HatchSpots[i], FString::Printf(TEXT("hatch%d"), i) });
	return Out;
}

bool ABSGame::InjectReady(int32 I) const
{
	const TArray<FHatch> Hs = HatchSpots();
	return Hs.IsValidIndex(I) && I < Sw.Queens && Time >= Sw.Inject.FindRef(Hs[I].Key);
}

bool ABSGame::Inject(int32 I)
{
	if (I >= Sw.Queens) { Toast(TEXT("That hatchery needs a queen: Spawn Larva")); return false; }
	const TArray<FHatch> Hs = HatchSpots();
	if (!Hs.IsValidIndex(I)) return false;
	const FHatch& H = Hs[I];
	if (!InjectReady(I)) { Toast(FString::Printf(TEXT("Inject ready in %ds"), FMath::CeilToInt(Sw.Inject.FindRef(H.Key) - Time))); return false; }
	const int32 Gain = BSSwarm::InjectLarva + BSSwarm::InjectPerTier * Sw.Tier;
	Sw.Larva += Gain; Sw.Inject.Add(H.Key, Time + BSSwarm::InjectCd);
	PlayFx(TEXT("broodSpellImpact"), FVector(H.P.X, H.P.Y, BSTerrain::Height(H.P.X, H.P.Y)), 0, 1.4f);
	Floater(H.P.X, H.P.Y, 3, FString::Printf(TEXT("+%d LARVA"), Gain), FLinearColor(0.55f, 0.88f, 0.54f));
	return true;
}

// Evolution chambers: melee, missile and carapace levels are researched in one, a level at a time over ChamberTimes s.
// Each round (wave) idle chambers start the next affordable one on their own (unless AUTO is off); tapping a chamber
// starts one now.
const ABSGame::FSwarmState::FResearch* ABSGame::Researching(const FString& Id) const
{
	for (const FSwarmState::FResearch& R : Sw.Research) if (!R.Id.IsEmpty() && R.Id == Id) return &R;
	return nullptr;
}

int32 ABSGame::IdleChamber() const
{
	for (int32 i = 0; i < BSSwarm::ChamberMax; i++) if (Sw.ChamberAt[i] && Sw.Research[i].Id.IsEmpty()) return i;
	return INDEX_NONE;
}

// The evolution an idle chamber would take on next: the lowest level it can afford, melee before missile before carapace.
FString ABSGame::NextEvolution() const
{
	FString Pick;
	for (const BSSwarm::FEvo& V : BSSwarm::Evos())
	{
		if (!V.Levels() || !SwarmState(TEXT("evo"), V.Id).IsEmpty()) continue;
		BSSwarm::FCost C;
		SwarmCost(TEXT("evo"), V.Id, C);
		if (Sw.Minerals < C.M || Sw.Gas < C.G) continue;
		if (Pick.IsEmpty() || EvoLevel(V.Id) < EvoLevel(Pick)) Pick = V.Id;
	}
	return Pick;
}

bool ABSGame::ChamberPull(int32 Ci)
{
	const FSwarmState::FResearch& R = Sw.Research[Ci];
	if (!R.Id.IsEmpty()) { Toast(FString::Printf(TEXT("%s: %ds"), BSSwarm::FindEvo(R.Id)->Name, FMath::CeilToInt(R.Dur - R.T))); return false; }
	const FString Id = NextEvolution();
	if (Id.IsEmpty()) { Toast(TEXT("Nothing to evolve that the hive can afford")); return false; }
	return SwarmBuy(TEXT("evo"), Id);
}

void ABSGame::ChamberAuto()
{
	if (!Sw.bAutoEvo) return;
	for (int32 Guard = 0; Guard < 3 && IdleChamber() != INDEX_NONE; Guard++) { const FString Id = NextEvolution(); if (Id.IsEmpty()) break; SwarmBuy(TEXT("evo"), Id); }
}

void ABSGame::UpdateResearch(float Dt)
{
	for (FSwarmState::FResearch& R : Sw.Research)
	{
		if (R.Id.IsEmpty() || (R.T += Dt) < R.Dur) continue;
		const FString Id = R.Id;
		R = FSwarmState::FResearch();
		const int32 Lv = ++Sw.Evo.FindOrAdd(Id);
		Toast(FString::Printf(TEXT("Evolved: %s level %d"), BSSwarm::FindEvo(Id)->Name, Lv));
	}
}

// A tap on the base: a hatchery injects, a chamber evolves, the chrysalis summons Kerrigan. False when the tap hit none of
// them (it sends Kerrigan instead).
bool ABSGame::BaseTap(float X, float Y)
{
	if (Mode != EBSMode::Lane) return false;
	auto Near = [X, Y](const FVector2D& P, float R) { return Dist(P.X, P.Y, X, Y) < R; };
	if (Sw.Parts.Contains(TEXT("chrysalis")) && Near(BSSwarm::Chrysalis, 1.8f)) { SwarmBuy(TEXT("hero"), FString()); return true; }
	for (int32 i = 0; i < BSSwarm::ChamberMax; i++) if (Sw.ChamberAt[i] && Near(BSSwarm::ChamberSpots[i], 2)) { ChamberPull(i); return true; }
	const TArray<FHatch> Hs = HatchSpots();
	for (int32 i = 0; i < Hs.Num(); i++) if (Near(Hs[i].P, i ? 2.4f : 3.2f)) { Inject(i); return true; }
	return false;
}

// ---- The Terran commander --------------------------------------------------------------------------------------
// The commander reads the swarm: every half second it looks at what is coming (and what is massing at the hive, and which
// strains the hive has bred) and answers it: firebats and siege tanks against massed or light broods, marauders against
// armour, marines, missile turrets, vikings and wraiths against air; its shop picks follow the same reading. It also
// repairs, stims, calls strikes and high tech, and raids the hive. (Outside swarm mode the hive is empty: the "balanced"
// bot.)
ABSGame::FTerranRead ABSGame::TerranRead() const
{
	FTerranRead R;
	for (const TSharedPtr<FBSEnemy>& E : Enemies)
	{
		const FBSZergDef* D = E->Def;
		if (!E->Alive() || D->bUncounted || D->bAmbient || D->bTumor || !D->Boss.IsEmpty() || D->bBaseBuilding) continue;
		R.Total++;
		if (E->bHeld) R.Massed++;
		if (D->bFlying) R.Air++; else if (D->bArmored) R.Armored++; else R.Light++;
	}
	// What the hive has bred counts too (the commander scouts its tech).
	for (const TCHAR* K : { TEXT("mutalisk"), TEXT("broodlord"), TEXT("guardian"), TEXT("devourer"), TEXT("scourge"), TEXT("viper"), TEXT("broodqueen") }) R.bAirTech |= Sw.Strains.Contains(K);
	for (const TCHAR* K : { TEXT("roach"), TEXT("lurker"), TEXT("ultralisk") }) R.bArmorTech |= Sw.Strains.Contains(K);
	R.bSplash = R.Massed + R.Light >= BSSwarm::TerranMass;
	return R;
}

// The trooper the swarm calls for now.
FString ABSGame::TerranTroop(const FTerranRead& R) const
{
	if (R.Air >= FMath::Max(4.f, R.Total * 0.3f)) return TEXT("marine");
	if (R.Armored > R.Light * 0.5f || (R.bArmorTech && R.Armored >= 3)) return TEXT("marauder");
	if (R.Light >= 10 || R.Massed >= 10) return TEXT("firebat");
	return TEXT("marine");
}

void ABSGame::TerranThink()
{
	const FTerranRead R = TerranRead();
	const int32 N0 = R.Total;
	const bool bAir = R.Air > 0 || R.bAirTech;
	if (Bunker.Hp < Bunker.MaxHp * 0.45f) EmergencyRepair();
	if (Up.bStim && Ab.StimCd <= 0 && N0 - R.Massed > 6) UseStim();
	if (N0 - R.Massed > 14 && Ab.StrikeCd <= 0 && Minerals > 260) CallAirStrike();
	if ((N0 > 18 || HeroAlive()) && Ab.YamatoCd <= 0 && Bc.IsValid() && Minerals > BSDefs::YamatoCost + 100) UseYamato();
	if (TechUnlocked.Contains(TEXT("nuke")) && NukeCd <= 0 && N0 > 40 && Minerals > 600) BuyTech(TEXT("nuke"));
	if (TechUnlocked.Contains(TEXT("vikings")) && VikingCd <= 0 && R.Air > 0 && Minerals > 500) BuyTech(TEXT("vikings"));
	if (TechUnlocked.Contains(TEXT("wraiths")) && WraithCd <= 0 && R.Air > 2 && Minerals > 500) BuyTech(TEXT("wraiths"));
	if (PendingInfantry()) return;        // the last trooper reaches the bunker first
	const int32 N = Bunker.Units.Num();
	// A full bunker first, of what the swarm calls for (a firebat before anything else).
	if (!Bunker.Units.ContainsByPredicate([](const FBSInfantryUnit& U) { return U.Def->Key == TEXT("firebat"); })) { if (Minerals >= 100) AddInfantry(TEXT("firebat")); return; }
	if (N < 3 && Minerals >= 50) { AddInfantry(TEXT("marine")); return; }
	if (N < Bunker.Slots) { if (Minerals >= 100) AddInfantry(TerranTroop(R)); return; }
	// Splash before anything when the broods mass or come thick and light.
	if (R.bSplash && Tanks.Num() < TankMax) { if (Minerals >= TankCost()) DropTank(); return; }
	if (Tanks.Num() < 1) { if (Minerals >= TankCost()) DropTank(); return; }
	if (bAir && SamCount() < (R.Air > 6 ? BSDefs::SamMax : 2)) { if (Minerals >= BSDefs::SamCost) BuildStructure(true); return; }
	if (Scvs.Num() < 2 && Minerals >= 50) { AddScv(); return; }
	if (Tanks.Num() < FMath::Min(TankMax, 2) && Minerals >= TankCost()) { DropTank(); return; }
	if (Tanks.Num() < TankMax && Minerals >= TankCost()) { DropTank(); return; }
	// Late game: several purchases a think, each falling through when it is not possible right now.
	for (const TCHAR* K : { TEXT("goliath"), TEXT("thor"), TEXT("odin") })
	{
		const FBSMechDef* Md = BSDefs::FindMech(K);
		if (TechUnlocked.Contains(K) && Md && TechCount(K) < Md->Max && Minerals >= Md->Cost) BuyTech(K);
	}
	if (TechUnlocked.Contains(TEXT("bc")) && !Bc.IsValid() && Minerals >= BSDefs::BcCost) BuyTech(TEXT("bc"));
	if (Scvs.Num() < 4 && Minerals >= 50) AddScv();
	// Troops outside: early when the swarm is massing or thick, otherwise once the research is done or the bank is big.
	bool bTreesDone = true;
	for (const TCHAR* Id : { TEXT("weapons"), TEXT("vehicle"), TEXT("plating"), TEXT("rails") }) bTreesDone &= !UpgradeAvailable(Id);
	const bool bQuiet = bSwarm && Up.bStim && Time > BSSwarm::RaidFrom && R.Total - R.Massed <= BSSwarm::RaidMaxField;
	const float Floor = R.bSplash || R.Total >= 30 || bQuiet ? 150.f : bTreesDone ? 150.f : 800.f;
	for (int32 i = 0; i < 6 && Squad.Num() + PendingInfantry() < BSDefs::SquadMax && Minerals >= Floor; i++)
		if (!AddInfantry(TerranTroop(R))) break;
	if (Minerals > 2000 && Bunker.Hp < Bunker.MaxHp * 0.8f) EmergencyRepair();
	TerranRaid(R);
}

// Its shop between rounds: the cards that answer what it reads first, then research, then high tech, rerolling while it
// can afford to.
void ABSGame::TerranShop()
{
	static const TCHAR* Pri0[] = { TEXT("weapons"), TEXT("neosteel"), TEXT("stim"), TEXT("medivac"), TEXT("plating"), TEXT("hisec"), TEXT("vehicle"), TEXT("rails"),
		TEXT("refinery"), TEXT("shield"), TEXT("firesup"), TEXT("spoils"), TEXT("incinerator"), TEXT("bounty"), TEXT("mule"), TEXT("scvrepair"), TEXT("medpacks"),
		TEXT("concussive"), TEXT("rotors"), TEXT("recruit"), TEXT("siegerange"), TEXT("goliath"), TEXT("thor"), TEXT("nuke"), TEXT("bc"), TEXT("odin"), TEXT("vikings") };
	const FTerranRead R = TerranRead();
	TArray<FString> Pri;
	auto First = [&Pri](std::initializer_list<const TCHAR*> L) { for (const TCHAR* Id : L) Pri.AddUnique(Id); };
	if (R.bSplash || R.Light > R.Armored) First({ TEXT("incinerator"), TEXT("siegerange"), TEXT("firesup"), TEXT("shield") });
	if (R.Armored > R.Light * 0.5f || R.bArmorTech) First({ TEXT("concussive"), TEXT("vehicle"), TEXT("goliath") });
	if (R.Air > 0 || R.bAirTech) First({ TEXT("vikings"), TEXT("wraiths"), TEXT("goliath"), TEXT("ship") });
	if (HeroAlive()) First({ TEXT("bc"), TEXT("nuke") });
	for (const TCHAR* Id : Pri0) Pri.AddUnique(Id);
	for (int32 Pass = 0; Pass < 4 && Shop.bOpen; Pass++)
	{
		TArray<int32> Idx;
		for (int32 i = 0; i < Shop.Offers.Num(); i++) if (!Shop.Bought[i] && Pri.Contains(Shop.Offers[i])) Idx.Add(i);
		Idx.Sort([&](int32 A, int32 B) { return Pri.IndexOfByKey(Shop.Offers[A]) < Pri.IndexOfByKey(Shop.Offers[B]); });
		bool bAny = false;
		for (const int32 i : Idx)
		{
			const FBSCardDef* C = BSDefs::FindCard(Shop.Offers[i]);
			if (C && (Pri.IndexOfByKey(C->Id) < 11 || Minerals > 500) && Minerals >= CardCost(*C) && BuyCard(i)) bAny = true;
		}
		if (!bAny && Minerals > RerollCost() + 150) { Reroll(); continue; }
		break;
	}
	CloseShop();
}

// ---- The zerg bot (-BSAutoPlay in swarm mode) -------------------------------------------------------------------
// A sensible zerg player working down a build order (-BSZergOrder=..., as the headless zbot.js's ORDER): drones and
// extractors up to a count, hatcheries and Spawn Larva, the tiers as soon as it can, every strain and evolution as they
// open, Kerrigan; she advances with the swarm and casts whatever is ready when there are Terrans about her. Base micro as
// zbot.js's: injects, and (with -BSZergHold) holding and releasing the broods.
void ABSGame::ZergBot()
{
	// (The headless zbot.js's order, with the evolution chambers the chamber evolutions now need.)
	static TArray<FString> Order = []()
	{
		FString List = TEXT("drone:8,extractor:1,strain:baneling,hatch,extractor:2,drone:14,queen,strain:roach,evo:boost,chamber,tier,extractor:4,strain:hydralisk,evo:melee,evo:missile,hatch,queen,chamber,strain:mutalisk,strain:lurker,evo:carapace,drone:20,strain:infested,strain:wingling,evo:glial,evo:spines,evo:hooks,tier,hero,strain:ultralisk,hatch,queen,strain:broodlord,evo:melee,evo:missile,evo:carapace,strain:defiler,strain:viper,evo:adrenal,evo:chitin,hatch,queen,drone:30,queen,queen,evo:melee,evo:missile,evo:carapace,strain:guardian,strain:broodqueen,strain:scourge,strain:devourer,strain:overlordDrop,strain:nydus,strain:overlordCreep,strain:queen,hatch,hero,hero,hero,hero");
		FParse::Value(FCommandLine::Get(), TEXT("BSZergOrder="), List, false);
		TArray<FString> Out;
		List.ParseIntoArray(Out, TEXT(","));
		return Out;
	}();
	for (int32 n = 0; n < 4 && ZergStep < Order.Num(); n++)
	{
		FString Kind, Arg;
		if (!Order[ZergStep].Split(TEXT(":"), &Kind, &Arg)) Kind = Order[ZergStep];
		const bool bCount = Kind == TEXT("drone") || Kind == TEXT("extractor");
		BSSwarm::FCost C;
		if (bCount)
		{
			const int32 Want = FCString::Atoi(*Arg), Have = Kind == TEXT("drone") ? Sw.Drones : Sw.Extractors;
			if (Have >= Want || !SwarmCost(Kind, FString(), C)) { ZergStep++; continue; }
		}
		const FString Id = bCount ? FString() : Arg;
		const FString St = SwarmState(Kind, Id);
		if (St == TEXT("max") || St == TEXT("need") || St == TEXT("nochamber")) { ZergStep++; continue; }
		if (!St.IsEmpty()) break;                     // locked, wait, busy or full: later
		SwarmCost(Kind, Id, C);
		if (Sw.Minerals < C.M || Sw.Gas < C.G) break;
		SwarmBuy(Kind, Id);
		if (!bCount) ZergStep++;
	}
	// Base micro: inject every ready hatchery; with -BSZergHold mass the broods and release them at -BSZergHoldN zerg (40)
	// or after -BSZergHoldT s (45).
	for (int32 i = 0; i < Sw.Hatcheries; i++) if (InjectReady(i)) Inject(i);
	static const bool bHoldBot = FParse::Param(FCommandLine::Get(), TEXT("BSZergHold"));
	static float HoldAt = 0;
	if (bHoldBot)
	{
		static int32 HoldN = 40; static float HoldT = 45; static bool bRead = false;
		if (!bRead) { bRead = true; FParse::Value(FCommandLine::Get(), TEXT("BSZergHoldN="), HoldN); FParse::Value(FCommandLine::Get(), TEXT("BSZergHoldT="), HoldT); }
		if (!Sw.bHold && Time - HoldAt > 6) { BroodHold(1); HoldAt = Time; }
		else if (Sw.bHold && (HeldCount() >= HoldN || Time - HoldAt > HoldT)) { BroodHold(0); HoldAt = Time; }
	}
	// Kerrigan: whatever is ready, while there are Terrans about her.
	if (FBSEnemy* E = HeroAlive())
	{
		if (!E->Queued.IsEmpty() || !Targets.ContainsByPredicate([E](const FBSTarget& T) { return Dist(T.X, T.Y, E->X, E->Y) < 9; })) return;
		for (const BSSwarm::FAbility& A : BSSwarm::Abilities()) if (HeroCd(A.Id) <= 0) { E->Queued = A.Id; break; }
	}
}

// The bot in the hive's shop: by a priority list (-BSZergPri=..., default zbot.js's: Brood Mothers first, then the
// economy), keeping 100 minerals in hand.
void ABSGame::ZergShopBot()
{
	static TArray<FString> Pri = []()
	{
		FString List = TEXT("autoinject,larva,pulse,drones,gas,haste,bounty,frenzy,favor,psionic,hide,tithe");
		FParse::Value(FCommandLine::Get(), TEXT("BSZergPri="), List, false);
		TArray<FString> Out;
		List.ParseIntoArray(Out, TEXT(","));
		return Out;
	}();
	TArray<int32> Idx;
	for (int32 i = 0; i < Shop.Offers.Num(); i++) if (Pri.Contains(Shop.Offers[i])) Idx.Add(i);
	Idx.Sort([&](int32 A, int32 B) { return Pri.IndexOfByKey(Shop.Offers[A]) < Pri.IndexOfByKey(Shop.Offers[B]); });
	for (const int32 i : Idx)
	{
		const BSSwarm::FCost C = ZCardCost(*BSSwarm::FindCard(Shop.Offers[i]));
		if (Sw.Minerals >= C.M + 100 && Sw.Gas >= C.G) BuyCard(i);
	}
}

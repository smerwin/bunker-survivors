// Swarm mode: the tables turned. The player is the zerg: minerals and vespene feed hatcheries, a lair and a hive that
// hatch ever-evolving broods on their own; the goal is a hive and the economy to summon Kerrigan (with drop pods, as in
// the Terran runs' finale), who then answers to the player directly. The Terrans play themselves (the commander below,
// the same one the balance bots use) and win by holding out until the Hyperion takes them off. Unlocked by escaping
// Kerrigan once (BSMeta::SwarmOpen). Data in BSSwarm (BSTypes). Port of client/js/swarm.js; keep the two in step.
#include "BSGame.h"
#include "BSTerrain.h"
#include "BSModelActor.h"
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
	if ((Sw.BroodT -= Dt) <= 0) { Sw.BroodT = BSSwarm::Brood; HatchBrood(); }
	// Kerrigan down: she can be summoned again once the hive has recovered (BSSwarm::HeroRespawn).
	if (Sw.Hero.IsValid() && Sw.Hero->Hp <= 0) { Sw.Hero.Reset(); Sw.HeroAt = Time + BSSwarm::HeroRespawn; Toast(TEXT("Kerrigan has fallen: summon her again when the hive recovers")); }
	// The Terran commander.
	if ((Sw.Think -= Dt) <= 0) { Sw.Think = 0.5f; TerranThink(); }
	if ((Sw.BaseT -= Dt) <= 0) { Sw.BaseT = 0.5f; SyncBase(); }
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
		Sw.Larva -= StrainBudget(*Sw.Next);
		SpawnEnemy(Sw.Next->Id, Ang);
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
	if (Kind == TEXT("tier")) { if (Sw.Tier + 1 >= BSSwarm::Tiers().Num()) return false; Out = BSSwarm::Tiers()[Sw.Tier + 1].Cost; return true; }
	if (Kind == TEXT("hero")) { Out = BSSwarm::HeroCost; return !HeroAlive(); }
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

FString ABSGame::SwarmState(const FString& Kind, const FString& Id) const
{
	BSSwarm::FCost C;
	if (!SwarmCost(Kind, Id, C)) return TEXT("max");
	if (SwarmNeeds(Kind, Id) > Sw.Tier) return TEXT("locked");
	if (Kind == TEXT("hero") && Time < Sw.HeroAt) return TEXT("wait");
	if (Kind == TEXT("evo")) { const BSSwarm::FEvo* V = BSSwarm::FindEvo(Id); if (V && V->Need && !Sw.Strains.Contains(V->Need)) return TEXT("need"); }
	return FString();
}

bool ABSGame::SwarmBuy(const FString& Kind, const FString& Id)
{
	const FString St = SwarmState(Kind, Id);
	BSSwarm::FCost C;
	SwarmCost(Kind, Id, C);
	if (St == TEXT("max") || St == TEXT("wait")) return false;
	if (St == TEXT("locked")) { Toast(FString(TEXT("Needs a ")) + BSSwarm::Tiers()[SwarmNeeds(Kind, Id)].Name); return false; }
	if (St == TEXT("need")) { Toast(FString(TEXT("Breed ")) + BSDefs::FindZerg(BSSwarm::FindEvo(Id)->Need)->Name + TEXT("s first")); return false; }
	if (Sw.Minerals < C.M) { Toast(TEXT("Not enough minerals")); return false; }
	if (Sw.Gas < C.G) { Toast(TEXT("Not enough vespene")); return false; }
	Sw.Minerals -= C.M; Sw.Gas -= C.G;
	if (Kind == TEXT("drone")) Sw.Drones++;
	else if (Kind == TEXT("extractor")) Sw.Extractors++;
	else if (Kind == TEXT("hatch")) { Sw.Hatcheries++; Toast(FString::Printf(TEXT("Hatchery %d is up"), Sw.Hatcheries)); }
	else if (Kind == TEXT("queen")) { Sw.Queens++; Toast(FString::Printf(TEXT("Spawn Larva level %d"), Sw.Queens)); }
	else if (Kind == TEXT("tier"))
	{
		Sw.Tier++;
		WaveBanner = FString(TEXT("THE ")) + FString(BSSwarm::Tiers()[Sw.Tier].Name).ToUpper() + TEXT(" IS COMPLETE"); WaveBannerT = Time; WaveSerial++;
		Toast(Sw.Tier == BSSwarm::HeroTier ? TEXT("The hive can summon Kerrigan") : TEXT("New strains and evolutions open"));
	}
	else if (Kind == TEXT("strain")) { Sw.Strains.Add(Id); Toast(FString(BSSwarm::FindStrain(Id)->Name) + TEXT(" join the broods")); }
	else if (Kind == TEXT("evo"))
	{
		const int32 Lv = ++Sw.Evo.FindOrAdd(Id);
		const BSSwarm::FEvo* V = BSSwarm::FindEvo(Id);
		Toast(FString(TEXT("Evolved: ")) + V->Name + (V->Levels() ? FString::Printf(TEXT(" level %d"), Lv) : FString()));
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
	BasePart(TEXT("main"), Main[FMath::Clamp(Sw.Tier, 0, 2)], At.X, At.Y, BSSwarm::BaseMain);
	TArray<FVector> Hatch = { FVector(At.X, At.Y, BSSwarm::BaseMain) };
	for (int32 i = 0; i < Sw.Hatcheries - 1 && i < UE_ARRAY_COUNT(BSSwarm::HatchSpots); i++)
	{
		const FVector2D P = BSSwarm::HatchSpots[i];
		BasePart(FString::Printf(TEXT("hatch%d"), i), TEXT("hatchery"), P.X, P.Y, BSSwarm::HatchScale);
		Hatch.Add(FVector(P.X, P.Y, BSSwarm::HatchScale));
	}
	for (int32 i = 0; i < Sw.Extractors && i < UE_ARRAY_COUNT(BSSwarm::ExtractorSpots); i++)
		BasePart(FString::Printf(TEXT("extractor%d"), i), TEXT("extractor"), BSSwarm::ExtractorSpots[i].X, BSSwarm::ExtractorSpots[i].Y, BSSwarm::ExtractorScale);
	// Tech: each building once anything it serves is bred (or, for the evolution chamber, evolved), in the next free spot.
	bool bEvolved = false;
	for (const auto& V : Sw.Evo) bEvolved |= V.Value > 0;
	for (const BSSwarm::FTech& T : BSSwarm::Tech())
	{
		bool bOn = false;
		for (const TCHAR* S : T.Serves) bOn |= FCString::Strcmp(S, TEXT("evo")) == 0 ? bEvolved : Sw.Strains.Contains(S);
		if (!bOn) continue;
		if (!Sw.TechSpot.Contains(T.Model)) Sw.TechSpot.Add(T.Model, Sw.TechSpot.Num());
		const FVector2D P = BSSwarm::TechSpots[Sw.TechSpot[T.Model] % UE_ARRAY_COUNT(BSSwarm::TechSpots)];
		BasePart(FString(TEXT("tech:")) + T.Model, T.Model, P.X, P.Y, BSSwarm::TechScale);
	}
	// Larva wriggling at every hatchery's foot, the queens round the main one.
	for (int32 i = 0; i < Hatch.Num(); i++)
		for (int32 k = 0; k < BSSwarm::BaseLarva; k++)
		{
			const float A = -PI / 2 + (k - (BSSwarm::BaseLarva - 1) / 2.f) * 0.5f, R = 3.1f * Hatch[i].Z / BSSwarm::BaseMain + 0.4f;
			BasePart(FString::Printf(TEXT("larva%d:%d"), i, k), TEXT("larva"), Hatch[i].X + FMath::Cos(A) * R, Hatch[i].Y + FMath::Sin(A) * R, 0.9f, Rand01() * 2 * PI);
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
	// Drones: up to BaseDrones of them shuttling between the hatcheries and the extractors.
	const int32 Want = FMath::Min(Sw.Drones, BSSwarm::BaseDrones);
	while (Sw.Drones2.Num() < Want)
	{
		const FVector H = Hatch[Sw.Drones2.Num() % Hatch.Num()];
		FSwarmState::FDrone Dr;
		Dr.X = Dr.Tx = H.X; Dr.Y = Dr.Ty = H.Y - 2; Dr.Wait = Rand01() * 2;
		Dr.Actor = AcquireModel(TEXT("drone"));
		if (Dr.Actor) { Dr.Actor->SetTeamColor(ZergColor); Dr.Actor->SetScale(0.8f); Dr.Actor->SetSim(Dr.X, Dr.Y, BSTerrain::Height(Dr.X, Dr.Y), 0); Dr.Actor->Play(TEXT("stand"), true); }
		Sw.Drones2.Add(Dr);
	}
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
	TArray<FVector2D> Stops = { BSSwarm::BaseAt };
	for (int32 i = 0; i < Sw.Hatcheries - 1 && i < UE_ARRAY_COUNT(BSSwarm::HatchSpots); i++) Stops.Add(BSSwarm::HatchSpots[i]);
	for (int32 i = 0; i < Sw.Extractors && i < UE_ARRAY_COUNT(BSSwarm::ExtractorSpots); i++) Stops.Add(BSSwarm::ExtractorSpots[i]);
	for (FSwarmState::FDrone& D : Sw.Drones2)
	{
		if (D.bMoving)
		{
			const float Dx = D.Tx - D.X, Dy = D.Ty - D.Y, L = FMath::Sqrt(Dx * Dx + Dy * Dy), S = BSSwarm::DroneSpeed * Dt;
			if (L <= S) { D.X = D.Tx; D.Y = D.Ty; D.bMoving = false; D.Wait = 1 + Rand01() * 2.5f; if (D.Actor) D.Actor->Play({ TEXT("stand work"), TEXT("stand") }, true); }
			else { D.X += Dx / L * S; D.Y += Dy / L * S; D.Yaw = FMath::Atan2(Dy, Dx); }
			if (D.Actor) D.Actor->SetSim(D.X, D.Y, BSTerrain::Height(D.X, D.Y), D.Yaw);
		}
		else if ((D.Wait -= Dt) <= 0)
		{
			const FVector2D S = Stops[FMath::RandRange(0, Stops.Num() - 1)];
			const float A = Rand01() * 2 * PI;
			D.Tx = S.X + FMath::Cos(A) * 2.2f; D.Ty = S.Y + FMath::Sin(A) * 1.6f; D.bMoving = true;
			if (D.Actor) D.Actor->Play(TEXT("walk"), true);
		}
	}
}

// Where a brood member comes out: at the foot of one of the hatcheries (the east or west ones for the arms).
FVector2D ABSGame::BroodSpawn(float Ang) const
{
	TArray<FVector2D> List = { BSSwarm::BaseAt };
	for (int32 i = 0; i < Sw.Hatcheries - 1 && i < UE_ARRAY_COUNT(BSSwarm::HatchSpots); i++) List.Add(BSSwarm::HatchSpots[i]);
	const int32 Side = FMath::RoundToInt(Ang);         // 1 north-east arm, 2 north-west
	TArray<FVector2D> Pool = List.FilterByPredicate([Side](const FVector2D& P) { return Side == 1 ? P.X > 0.5f : Side == 2 ? P.X < -0.5f : true; });
	if (!Pool.Num()) Pool = List;
	const FVector2D P = Pool[FMath::RandRange(0, Pool.Num() - 1)];
	return FVector2D(P.X + FMath::FRandRange(-1.6f, 1.6f), P.Y - FMath::FRandRange(2.2f, 3.4f));
}

// ---- The Terran commander --------------------------------------------------------------------------------------
// The balance bots' "balanced" commander, extended with the high-tech cards: every half second it repairs, stims, calls
// strikes, buys troops, tanks, SCVs, turrets and tech. (Swarm mode's Terrans, and -BSAutoPlay's default bot.)
void ABSGame::TerranThink()
{
	const int32 N0 = Enemies.Num();
	auto Air = [this]() { return Enemies.ContainsByPredicate([](const TSharedPtr<FBSEnemy>& E) { return E->Alive() && E->Def->bFlying; }); };
	if (Bunker.Hp < Bunker.MaxHp * 0.45f) EmergencyRepair();
	if (Up.bStim && Ab.StimCd <= 0 && N0 > 6) UseStim();
	if (N0 > 14 && Ab.StrikeCd <= 0 && Minerals > 260) CallAirStrike();
	if (N0 > 18 && Ab.YamatoCd <= 0 && Bc.IsValid() && Minerals > BSDefs::YamatoCost + 100) UseYamato();
	if (TechUnlocked.Contains(TEXT("nuke")) && NukeCd <= 0 && N0 > 40 && Minerals > 600) BuyTech(TEXT("nuke"));
	if (TechUnlocked.Contains(TEXT("vikings")) && VikingCd <= 0 && Air() && Minerals > 500) BuyTech(TEXT("vikings"));
	const int32 N = Bunker.Units.Num() + PendingInfantry();
	int32 Fb = 0, Nm = 0;
	for (const auto& U : Bunker.Units) { Fb += U.Def->Key == TEXT("firebat"); Nm += U.Def->Key == TEXT("marauder"); }
	if (PendingInfantry()) return;        // the last trooper reaches the bunker first
	if (Fb < 1) { if (Minerals >= 100) AddInfantry(TEXT("firebat")); return; }
	if (Bunker.Slots > 4 && Fb < 2) { if (Minerals >= 100) AddInfantry(TEXT("firebat")); return; }
	if (Bunker.Slots > 4 && Nm < 1) { if (Minerals >= 100) AddInfantry(TEXT("marauder")); return; }
	if (N < 3 && Minerals >= 50) { AddInfantry(TEXT("marine")); return; }
	if (N >= 4 && Tanks.Num() < 1) { if (Minerals >= TankCost()) DropTank(); return; }
	if (N < Bunker.Slots && Minerals >= 100) { AddInfantry(Fb < 1 ? TEXT("firebat") : (N % 2 ? TEXT("marauder") : TEXT("marine"))); return; }
	if (Scvs.Num() < 2 && Minerals >= 50) { AddScv(); return; }
	if (Tanks.Num() < FMath::Min(TankMax, 2) && Minerals >= TankCost()) { DropTank(); return; }
	if (Tanks.Num() < TankMax && Minerals >= TankCost()) { DropTank(); return; }
	if (Air() && SamCount() < 2 && Minerals >= 100) { BuildStructure(true); return; }
	// Late game: several purchases a think, each falling through when it is not possible right now.
	for (const TCHAR* K : { TEXT("goliath"), TEXT("thor"), TEXT("odin") })
	{
		const FBSMechDef* Md = BSDefs::FindMech(K);
		if (TechUnlocked.Contains(K) && Md && TechCount(K) < Md->Max && Minerals >= Md->Cost) BuyTech(K);
	}
	if (TechUnlocked.Contains(TEXT("bc")) && !Bc.IsValid() && Minerals >= BSDefs::BcCost) BuyTech(TEXT("bc"));
	if (Scvs.Num() < 4 && Minerals >= 50) AddScv();
	bool bTreesDone = true;
	for (const TCHAR* Id : { TEXT("weapons"), TEXT("vehicle"), TEXT("plating"), TEXT("rails") }) bTreesDone &= !UpgradeAvailable(Id);
	static const TCHAR* Mix[] = { TEXT("marine"), TEXT("marauder"), TEXT("firebat") };
	for (int32 i = 0; i < 6 && Squad.Num() + PendingInfantry() < BSDefs::SquadMax && Minerals >= (bTreesDone ? 150 : 800); i++)
		if (!AddInfantry(Mix[(Squad.Num() + i) % 3])) break;
	if (Minerals > 2000 && Bunker.Hp < Bunker.MaxHp * 0.8f) EmergencyRepair();
}

// Its shop between rounds: research first, then high tech, rerolling while it can afford to (AutoShop), then on with the
// next round.
void ABSGame::TerranShop()
{
	AutoShop();
	CloseShop();
}

// ---- The zerg bot (-BSAutoPlay in swarm mode) -------------------------------------------------------------------
// A sensible zerg player working down a build order (-BSZergOrder=..., as the headless zbot.js's ORDER): drones and
// extractors up to a count, hatcheries and Spawn Larva, the tiers as soon as it can, every strain and evolution as they
// open, Kerrigan; she advances with the swarm and casts whatever is ready when there are Terrans about her.
void ABSGame::ZergBot()
{
	static TArray<FString> Order = []()
	{
		FString List = TEXT("drone:8,extractor:1,strain:baneling,hatch,extractor:2,drone:14,queen,strain:roach,evo:boost,tier,extractor:4,strain:hydralisk,evo:melee,evo:missile,hatch,queen,strain:mutalisk,strain:lurker,evo:carapace,drone:20,strain:infested,strain:wingling,evo:glial,evo:spines,evo:hooks,tier,hero,strain:ultralisk,hatch,queen,strain:broodlord,evo:melee,evo:missile,evo:carapace,strain:defiler,strain:viper,evo:adrenal,evo:chitin,hatch,queen,drone:30,queen,queen,evo:melee,evo:missile,evo:carapace,strain:guardian,strain:broodqueen,strain:scourge,strain:devourer,strain:overlordDrop,strain:nydus,strain:overlordCreep,strain:queen,hatch,hero,hero,hero,hero");
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
		if (St == TEXT("max") || St == TEXT("need")) { ZergStep++; continue; }
		if (St == TEXT("locked") || St == TEXT("wait")) break;
		SwarmCost(Kind, Id, C);
		if (Sw.Minerals < C.M || Sw.Gas < C.G) break;
		SwarmBuy(Kind, Id);
		if (!bCount) ZergStep++;
	}
	// Kerrigan: whatever is ready, while there are Terrans about her.
	if (FBSEnemy* E = HeroAlive())
	{
		if (!E->Queued.IsEmpty() || !Targets.ContainsByPredicate([E](const FBSTarget& T) { return Dist(T.X, T.Y, E->X, E->Y) < 9; })) return;
		for (const BSSwarm::FAbility& A : BSSwarm::Abilities()) if (HeroCd(A.Id) <= 0) { E->Queued = A.Id; break; }
	}
}

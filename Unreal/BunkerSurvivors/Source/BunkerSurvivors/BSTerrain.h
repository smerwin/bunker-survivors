// Lane-mode terrain: a Y-shaped chokepoint (port of client/js/terrain.js; keep the two in step). The low ground is a
// stem running north from the bunker that splits into north-west and north-east arms; a mesa sits between the arms
// and plateaus flank the stem. Ramps lead from each arm up onto its side plateau, where ranged zerg can take the high
// ground over the bunker. Used by the simulation (heights, walkability, routes) and the world (the ground mesh).
// Sim metres, Z up.
#pragma once

#include "CoreMinimal.h"

namespace BSTerrain
{
	constexpr float H = 2.0f;                  // plateau height

	void SetMode(bool bLane);
	bool Active();
	float LowSdf(float X, float Y);            // signed distance to the low ground (negative inside)
	float Height(float X, float Y);
	float Slope(float X, float Y);
	bool Walkable(float X, float Y);           // ground units cannot climb cliff faces (the ramps are gentle enough)
	bool OnHigh(float X, float Y);
	// Waypoints for a ground unit spawning at (X, Y): down an arm into the stem, or up a ramp to a perch.
	TArray<FVector2D> Route(float X, float Y, bool bHigh);
	// A point down the north-east (Side 1) or north-west (-1) arm: T along it (0 fork .. 1 far end), Off across it.
	FVector2D ArmPoint(int32 Side, float T, float Off);
}

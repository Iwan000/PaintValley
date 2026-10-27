// Project Rain - Rain Shield constants shared by the game (runtime) and the asset builder (editor).
#pragma once

#include "CoreMinimal.h"

namespace RainShield
{
	/** Boxes the game rain tests on every step (unrolled in Niagara/HLSL/GameRainUpdate.hlsl). The level's
	 *  protected shape and the placed blockers are merged into at most this many boxes. */
	constexpr int32 MaxBoxes = 32;

	/** Box kinds (User.RainBox<i>Kind). */
	constexpr float BoxNone = 0.0f;
	constexpr float BoxBlocker = 1.0f;
	constexpr float BoxProtected = 2.0f;

	/** Drop diameters of the game rain (mm, uniform): the spawn module and the expected-hit estimate. */
	constexpr float DropMinMm = 1.5f;
	constexpr float DropMaxMm = 4.5f;

	/** Hits read back per frame at most (Export Particle Data's fixed GPU buffer). A frame that fills it may
	 *  have lost hits, so the game flags it. */
	constexpr int32 ExportCapacity = 4096;

	/** Rain drops in the game system by default (rain.game.Count). */
	constexpr int32 DefaultRainCount = 200000;

	/** System 2, hail: stone radii (cm), terminal speed (cm/s), stones by default (rain.game.HailCount). */
	constexpr float HailRadiusMin = 1.0f;
	constexpr float HailRadiusMax = 2.0f;
	constexpr float HailSpeed = 900.0f;
	constexpr int32 DefaultHailCount = 2000;
}

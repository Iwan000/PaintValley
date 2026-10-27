// =====================================================================================
// Project Rain - Rain Shield (the game): spawn (custom HLSL module, Map in / MapOut; RainField.hlsl comes first).
//
// The game's rain fills a box aligned with its fall direction D (User.RainGameDir, any direction the player
// picks) around the play grid: a square face of half-size User.RainGameHalf across D and User.RainGameLength
// along it, centred on the grid. Each drop is one slot of a periodic stream: a spot across the face and a
// phase along D. Drops are born parked upwind of the face (hidden); when the round starts they stream in, and
// after one crossing the box is uniformly full (GameRainUpdate.hlsl).
// $(...) placeholders are replaced by the C++ builder.
// =====================================================================================
uint PrSeed = uint(ExecIndex()) * 2654435761u + $(SEED)u;
PR_NEXT(PrSeed) float PrR0 = PR_U01(PrSeed);
PR_NEXT(PrSeed) float PrR1 = PR_U01(PrSeed);
PR_NEXT(PrSeed) float PrR2 = PR_U01(PrSeed);
PR_NEXT(PrSeed) float PrR3 = PR_U01(PrSeed);
PR_NEXT(PrSeed) float PrR4 = PR_U01(PrSeed);
PR_NEXT(PrSeed) float PrR5 = PR_U01(PrSeed);
PR_NEXT(PrSeed) float PrR6 = PR_U01(PrSeed);
PR_NEXT(PrSeed) float PrR7 = PR_U01(PrSeed);

float3 GrD = User.RainGameDir;
float3 GrU = normalize(cross(GrD, abs(GrD.z) < 0.99 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0)));
float3 GrV = cross(GrD, GrU);
float GrH = User.RainGameHalf;
float GrL = User.RainGameLength;

// Drop size: uniform between $(GAME_D_MIN_MM) and $(GAME_D_MAX_MM) mm (film-rain drops, big enough to read).
float PrDmm = $(GAME_D_MIN_MM) + PrR3 * ($(GAME_D_MAX_MM) - $(GAME_D_MIN_MM));
// Terminal velocity, Atlas, Srivastava & Sekhon (1973): v = 9.65 - 10.3 exp(-0.6 D) m/s.
float PrVt = max(0.0, 9.65 - 10.3 * exp(-0.6 * PrDmm)) * 100.0 * (0.94 + 0.12 * PrR4);

Particles.Position = User.RainGameCenter + GrU * ((2.0 * PrR0 - 1.0) * GrH) + GrV * ((2.0 * PrR1 - 1.0) * GrH)
	+ GrD * (-0.5 * GrL - PrR2 * GrL);
Particles.Velocity = GrD * PrVt;
Particles.RainDiameter = PrDmm * 0.1;
Particles.RainRand = float4(PrR5, PrR6, PrR7, PrR4);
Particles.RainFrozen = 0.0;
Particles.RainState = 1.0;
Particles.RainTimer = 0.0;
Particles.RainStage = 0.0;
Particles.RainDir = GrD;
Particles.RainGameHit = false;
Particles.RainGameHitPos = float3(0.0, 0.0, 0.0);
Particles.SpriteAlignment = GrD;
Particles.SpriteSize = float2(0.0, 0.0);
Particles.Color = float4(1.0, 1.0, 1.0, 0.0);
Particles.DynamicMaterialParameter = float4(1.0, 1.0, 0.0, PrR5);
Particles.DynamicMaterialParameter1 = float4(1.0, 0.0, 0.0, 0.0);

#undef PR_NEXT
#undef PR_U01
#undef PR_CELL
#undef PR_FOLD
#undef PR_NEAREST
#undef PR_FAR_FADE
#undef PR_STAGE_XY
#undef PR_LAMP
#undef PR_LAMPS
#undef PR_ACROSS

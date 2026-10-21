// =====================================================================================
// Project Rain - particle spawn (custom HLSL module, Map in / MapOut; RainField.hlsl comes first).
// Every drop is spawned once (single burst) and lives forever; the update module recycles it.
// Units: Unreal cm / s. Drop diameter stored in cm.
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

// ---- Field drops: uniform over the rain field's window. Stage drops (the first $(STAGE_COUNT)): around the
// stage centre (RainField.hlsl). Both from the ground (system Z) up to the cloud base.
bool PrStage = ExecIndex() < $(STAGE_COUNT);
float3 PrPos = float3(PrStage ? PR_STAGE_XY(PrR0, PrR1) : PrWinMin + PR_CELL * float2(PrR0, PrR1), Engine.Owner.Position.z + PrR2 * $(HEIGHT));

// ---- Drop size: truncated exponential (Marshall-Palmer 1948), N(D) ~ exp(-Lambda D), D in mm.
// Lambda = 4.1 R^-0.21 (R = rain rate mm/h), scaled by an artistic factor (<1 favours big film-rain drops).
// Rain-machine drops (stage) are bigger again: a larger minimum and a flatter tail.
float PrLambda = 4.1 * pow($(RAIN_RATE), -0.21) * (PrStage ? $(STAGE_LAMBDA_SCALE) : $(LAMBDA_SCALE));
float PrDmin = PrStage ? $(STAGE_D_MIN_MM) : $(D_MIN_MM);
float PrDmax = $(D_MAX_MM);
float PrDmm = PrDmin - log(1.0 - PrR3 * (1.0 - exp(-PrLambda * (PrDmax - PrDmin)))) / PrLambda;

// ---- Terminal velocity, Atlas, Srivastava & Sekhon (1973): v = 9.65 - 10.3 exp(-0.6 D) m/s.
float PrVt = max(0.0, 9.65 - 10.3 * exp(-0.6 * PrDmm)) * 100.0 * (0.94 + 0.12 * PrR4);

Particles.Position = PrPos;
Particles.Velocity = float3(0.0, 0.0, -PrVt) + User.RainWind;
Particles.RainDiameter = PrDmm * 0.1;
Particles.RainRand = float4(PrR5, PrR6, PrR7, PrR4);
Particles.RainFrozen = 0.0;
Particles.RainState = 0.0;
Particles.RainTimer = 0.0;
Particles.RainStage = PrStage ? 1.0 : 0.0;
Particles.RainDir = float3(0.0, 0.0, -1.0);
Particles.SpriteAlignment = float3(0.0, 0.0, -1.0);
Particles.SpriteSize = float2(PrDmm * 0.1, PrDmm * 0.1);
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

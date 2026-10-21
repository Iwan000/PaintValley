// =====================================================================================
// Project Rain - splash droplet spawn (RainField.hlsl comes first). Particles are grouped into
// clusters of $(PER_SPLASH); each cluster is one impact that re-fires on its own period (recycled
// pool, no spawn/kill).
// =====================================================================================

uint PsIndex = uint(ExecIndex());
uint PsCluster = PsIndex / $(PER_SPLASH)u;
uint PsC = PsCluster * 2654435761u + $(SEED)u;
PR_NEXT(PsC) float PsClusterRand = PR_U01(PsC);
uint PsD = PsIndex * 2246822519u + $(SEED)u + 0x68E31DA4u;
PR_NEXT(PsD) float PsR0 = PR_U01(PsD);
PR_NEXT(PsD) float PsR1 = PR_U01(PsD);
PR_NEXT(PsD) float PsR2 = PR_U01(PsD);

// x: cluster id (as float, exact below 2^24), y: droplet random, z: droplet random, w: cluster random
Particles.RainRand = float4(float(PsCluster), PsR0, PsR1, PsClusterRand);
Particles.RainDiameter = lerp(0.035, 0.09, PsR2);
Particles.Position = Engine.Owner.Position + float3(0.0, 0.0, -1000.0);
Particles.Velocity = float3(0.0, 0.0, 0.0);
Particles.RainFrozen = 0.0;
Particles.RainState = 0.0;
Particles.RainTimer = 0.0;
Particles.RainDir = float3(0.0, 0.0, 1.0);
Particles.SpriteAlignment = float3(0.0, 0.0, 1.0);
Particles.SpriteSize = float2(0.05, 0.05);
Particles.Color = float4(1.0, 1.0, 1.0, 0.0);
Particles.DynamicMaterialParameter = float4(1.0, 1.0, 0.0, PsR0);
Particles.DynamicMaterialParameter1 = float4(1.0, 0.0, 0.0, 0.0);
Particles.RainStage = 0.0;

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

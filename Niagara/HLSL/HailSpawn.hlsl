// =====================================================================================
// Project Rain - Rain Shield, System 2 (hail): spawn (custom HLSL module, Map in / MapOut). Runs on the CPU
// (VectorVM) or the GPU: float-only hashing, no bit operations, no loops.
// Hailstones stream like the game's rain (GameRainSpawn.hlsl): a spot across the face of the box aligned with
// the fall direction and a phase along it, born parked upwind. They are meshes (an Engine sphere, squashed a
// little differently for each stone).
// $(...) placeholders are replaced by the C++ builder.
// =====================================================================================
float HsI = float(ExecIndex()) + $(SEED_F);
float4 HsR = frac(sin(float4(HsI * 12.9898 + 1.0, HsI * 78.233 + 2.0, HsI * 37.719 + 3.0, HsI * 93.989 + 4.0)) * 43758.5453);
float4 HsQ = frac(sin(float4(HsI * 26.651 + 5.0, HsI * 53.117 + 6.0, HsI * 11.131 + 7.0, HsI * 67.411 + 8.0)) * 24634.6345);

float3 HsD = User.RainGameDir;
float HsUpZ = step(abs(HsD.z), 0.99);
float3 HsU = normalize(cross(HsD, float3(1.0 - HsUpZ, 0.0, HsUpZ)));
float3 HsV = cross(HsD, HsU);
float HsH = User.RainGameHalf;
float HsL = User.RainGameLength;
float HsRad = $(HAIL_R_MIN) + HsQ.x * ($(HAIL_R_MAX) - $(HAIL_R_MIN));

Particles.Position = User.RainGameCenter + HsU * ((2.0 * HsR.x - 1.0) * HsH) + HsV * ((2.0 * HsR.y - 1.0) * HsH)
	+ HsD * (-0.5 * HsL - HsR.z * HsL);
Particles.Velocity = HsD * $(HAIL_SPEED);
Particles.HailRadius = HsRad;
Particles.HailState = 0.0;
Particles.HailTimer = 0.0;
Particles.HailCycle = 0.0;
Particles.HailAngle = 6.2831853 * HsQ.w;
Particles.RainRand = float4(HsR.w, HsQ.y, HsQ.z, HsQ.w);
Particles.Scale = float3(0.0, 0.0, 0.0);
Particles.MeshOrientation = float4(0.0, 0.0, 0.0, 1.0);
Particles.Color = float4(0.80, 0.88, 1.0, 1.0) * (0.85 + 0.3 * HsQ.y);
Particles.RainGameHit = false;
Particles.RainGameHitPos = float3(0.0, 0.0, 0.0);
Particles.HailImpact = float3(0.0, 0.0, 0.0);

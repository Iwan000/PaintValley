// =====================================================================================
// Project Rain - Paint Valley, System 2 (paint balls): spawn (custom HLSL module, CPU VectorVM, Map in / MapOut).
// Float-only hashing, no bit operations, no loops. The pool is spawned once: every ball starts in the cloud (hidden),
// waiting a random share of User.RainBallWait. See PaintBallUpdate.hlsl for the cycle.
// $(...) placeholders are replaced by the C++ builder.
// =====================================================================================
// The VectorVM's frac is x - trunc(x) (negative below 0); x - floor(x) is in [0, 1) on both targets.
#define PB_FRAC(X) ((X) - floor(X))
float PbsI = float(ExecIndex()) + $(SEED_F);
float4 PbsR = PB_FRAC(sin(float4(PbsI * 12.9898 + 1.0, PbsI * 78.233 + 2.0, PbsI * 37.719 + 3.0, PbsI * 93.989 + 4.0)) * 43758.5453);
float4 PbsQ = PB_FRAC(sin(float4(PbsI * 26.651 + 5.0, PbsI * 53.117 + 6.0, PbsI * 11.131 + 7.0, PbsI * 67.411 + 8.0)) * 24634.6345);

Particles.Position = User.RainGameCenter - User.RainGameDir * User.RainGameLength;
Particles.Velocity = float3(0.0, 0.0, 0.0);
// x, y: launch seeds; z: size; w: spare.
Particles.RainRand = float4(PbsR.x, PbsR.y, PbsQ.x, PbsQ.y);
Particles.BallState = 0.0;
Particles.BallTimer = PbsR.z * User.RainBallWait;
Particles.BallBounces = 0.0;
Particles.BallSquash = 0.0;
Particles.BallCycle = 0.0;
Particles.BallEpoch = User.RainBallEpoch;
Particles.BallNear = float2(-1.0, -1.0);
Particles.Scale = float3(0.0, 0.0, 0.0);
Particles.MeshOrientation = float4(0.0, 0.0, 0.0, 1.0);
Particles.Color = float4(User.RainPaintInk.rgb, 1.0);
Particles.PaintKind = 0.0;
Particles.RainGameHit = false;
Particles.RainGameHitPos = float3(0.0, 0.0, 0.0);
#undef PB_FRAC

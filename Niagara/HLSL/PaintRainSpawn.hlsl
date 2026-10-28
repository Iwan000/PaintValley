// =====================================================================================
// Project Rain - Paint Valley, System 1 (paint rain): spawn (custom HLSL module, GPU, Map in / MapOut).
//
// The rain is a stream along the player's direction D (User.RainGameDir) through a cylinder around the level: a
// disc of radius User.RainGameHalf across D, User.RainGameLength along it, centred on User.RainGameCenter. The
// drops start spread through it (the stream is full at once). A few of them, User.RainPaintCarrierFrac, carry
// paint (the big drops, whose hits make puddles); the rest are small drops that make puddles spread.
// $(...) placeholders are replaced by the C++ builder.
// =====================================================================================
#define PN_NEXT(S) S = S * 747796405u + 2891336453u;
#define PN_U01(S) (float(((((S) >> (((S) >> 28u) + 4u)) ^ (S)) * 277803737u) >> 8u) * (1.0 / 16777216.0))
uint PnS = uint(ExecIndex()) * 2654435761u + $(SEED)u;
PN_NEXT(PnS) float PnR0 = PN_U01(PnS);
PN_NEXT(PnS) float PnR1 = PN_U01(PnS);
PN_NEXT(PnS) float PnR2 = PN_U01(PnS);
PN_NEXT(PnS) float PnR3 = PN_U01(PnS);
PN_NEXT(PnS) float PnR4 = PN_U01(PnS);
PN_NEXT(PnS) float PnR5 = PN_U01(PnS);
PN_NEXT(PnS) float PnR6 = PN_U01(PnS);

float3 PnD = normalize(User.RainGameDir);
float3 PnU = normalize(cross(PnD, abs(PnD.z) < 0.99 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0)));
float3 PnV = cross(PnD, PnU);
float PnRad = User.RainGameHalf * sqrt(PnR0);
float PnAng = 6.2831853 * PnR1;

Particles.Position = User.RainGameCenter + (PnU * cos(PnAng) + PnV * sin(PnAng)) * PnRad + PnD * ((PnR2 - 0.5) * User.RainGameLength);
Particles.Velocity = PnD * User.RainPaintSpeed * (0.9 + 0.2 * PnR3);
Particles.PaintCarrier = PnR4 < User.RainPaintCarrierFrac ? 1.0 : 0.0;
Particles.RainRand = float4(PnR3, PnR5, PnR6, PnR4);
Particles.RainTimer = 0.0;
Particles.PaintAge = 1.0;
Particles.PaintKind = 0.0;
Particles.RainGameHit = false;
Particles.RainGameHitPos = float3(0.0, 0.0, 0.0);
Particles.PaintRipple = 0.0;
Particles.PaintNormal = float3(0.0, 0.0, 1.0);
Particles.SpriteAlignment = PnD;
Particles.SpriteFacing = User.RainViewFacing;
Particles.SpriteSize = float2(0.0, 0.0);
Particles.Color = float4(0.0, 0.0, 0.0, 0.0);
Particles.DynamicMaterialParameter = float4(0.0, 1.0, 0.0, 0.0);
#undef PN_NEXT
#undef PN_U01

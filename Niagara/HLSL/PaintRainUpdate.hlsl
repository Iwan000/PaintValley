// =====================================================================================
// Project Rain - Paint Valley, System 1 (paint rain): update (custom HLSL module, GPU, Map in / MapOut).
//
// Two kinds of drops, drawn alike (a thin streak with a bead at its head, the user's reference): the big ones
// (Particles.PaintCarrier, User.RainPaintCarrierFrac of them) paint a puddle where they land; the small ones make
// the puddle they land on spread. Rules per drop:
//   1. Steer: the velocity relaxes towards the current direction D at the drop's speed (time constant
//      $(STEER_TAU) s), so when the player turns the rain, the stream in the air bends like a hose's.
//   2. Collide: the step is tested against the level's boxes (slab test, no tunnelling; the face entered gives
//      the normal). While the round is scored a hit raises Particles.RainGameHit for that frame, and Export
//      Particle Data reads back the hit point, the kind (Particles.PaintKind: the box kind, + 10 for a small drop)
//      and the velocity. Every big drop's hit is read back; a small drop's with probability
//      User.RainPaintExportProb (the game weights it by the inverse), so any count stays within the readback budget.
//   3. Ripple: the drop stays where it landed for $(RIPPLE_LIFE) s as a ring spreading on the surface.
//   4. Recycle: after its ripple, or when it leaves the stream's bounding sphere moving outwards, the drop starts
//      again on the disc upwind of the level for the current D (a fresh spot). The stream's flux never changes.
// Between rounds (User.RainGameActive 0) nothing moves or shows.
// $(...) placeholders are replaced by the C++ builder.
// =====================================================================================
#define PN_NEXT(S) S = S * 747796405u + 2891336453u;
#define PN_U01(S) (float(((((S) >> (((S) >> 28u) + 4u)) ^ (S)) * 277803737u) >> 8u) * (1.0 / 16777216.0))
float PnDt = clamp(Engine.DeltaTime, 1e-5, 0.05);
float3 PnP = Particles.Position;
float3 PnVel = Particles.Velocity;
float4 PnRnd = Particles.RainRand;
float PnTimer = Particles.RainTimer;
float PnAge = Particles.PaintAge;
float PnRipple = Particles.PaintRipple;
float3 PnN = Particles.PaintNormal;
bool PnBig = Particles.PaintCarrier > 0.5;

float3 PnD = normalize(User.RainGameDir);
float3 PnU = normalize(cross(PnD, abs(PnD.z) < 0.99 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0)));
float3 PnV = cross(PnD, PnU);
float3 PnC = User.RainGameCenter;
float PnH = User.RainGameHalf;
float PnL = User.RainGameLength;
bool PnOn = User.RainGameActive > 0.5;
float PnSpeed = User.RainPaintSpeed * (0.9 + 0.2 * PnRnd.x);
float PnExit = sqrt(0.25 * PnL * PnL + PnH * PnH);

// Fresh randoms: the drop's own seed and how often it has landed or restarted.
uint PnS = uint(PnRnd.y * 16777215.0) * 2654435761u + uint(PnTimer) * 747796405u + $(SEED)u;
PN_NEXT(PnS) float PnA0 = PN_U01(PnS);
PN_NEXT(PnS) float PnA1 = PN_U01(PnS);
PN_NEXT(PnS) float PnA2 = PN_U01(PnS);
float PnRad = PnH * sqrt(PnA0);
float PnAng = 6.2831853 * PnA1;
float3 PnRestart = PnC + (PnU * cos(PnAng) + PnV * sin(PnAng)) * PnRad - PnD * (0.5 * PnL);

bool PnHit = false;
float3 PnHitPos = Particles.RainGameHitPos;
float PnKind = 0.0;
if (PnOn)
{
	if (PnRipple > 0.0)
	{
		// ---- rule 3, then 4: the ring fades, then the drop starts again upwind.
		PnRipple -= PnDt;
		if (PnRipple <= 0.0)
		{
			PnRipple = 0.0;
			PnP = PnRestart;
			PnVel = PnD * PnSpeed;
			PnAge = 0.0;
		}
	}
	else
	{
		// ---- rule 1: steer towards D.
		PnVel += (PnD * PnSpeed - PnVel) * (1.0 - exp(-PnDt / $(STEER_TAU)));
		float3 PnP0 = PnP;
		float3 PnSeg = PnVel * PnDt;
		PnP = PnP0 + PnSeg;

		// ---- rule 2: the first box this step crosses, and the face it enters.
		float PnHitT = 2.0;
		float PnHitK = 0.0;
		float3 PnHitN = float3(0.0, 0.0, 1.0);
		float3 PnInv = 1.0 / (PnSeg + 1e-4 * (1.0 - abs(sign(PnSeg))));
#define GR_BOX(Mn, Mx, K) if (K > 0.5) { float3 PnT1 = (Mn - PnP0) * PnInv; float3 PnT2 = (Mx - PnP0) * PnInv; \
	float3 PnLo = min(PnT1, PnT2); float3 PnHi = max(PnT1, PnT2); \
	float PnIn = max(max(PnLo.x, PnLo.y), PnLo.z); float PnOut = min(min(PnHi.x, PnHi.y), PnHi.z); \
	if (PnOut >= max(PnIn, 0.0) && PnIn <= 1.0 && PnIn < PnHitT) { PnHitT = max(PnIn, 0.0); PnHitK = K; \
		PnHitN = PnLo.x >= PnLo.y && PnLo.x >= PnLo.z ? float3(-sign(PnSeg.x), 0.0, 0.0) \
			: (PnLo.y >= PnLo.z ? float3(0.0, -sign(PnSeg.y), 0.0) : float3(0.0, 0.0, -sign(PnSeg.z))); } }
$(GAME_BOX_TESTS)
#undef GR_BOX

		bool PnLeft = length(PnP - PnC) > PnExit && dot(PnVel, PnP - PnC) > 0.0;
		if (PnHitK > 0.5)
		{
			float3 PnAt = PnP0 + PnSeg * PnHitT;
			if (User.RainGameScoring > 0.5 && (PnBig || PnA2 < User.RainPaintExportProb))
			{
				PnHit = true;
				PnHitPos = PnAt;
				PnKind = PnHitK + (PnBig ? 0.0 : 10.0);
			}
			// The ring: just off the face it hit.
			PnP = PnAt + PnHitN * 0.4;
			PnN = PnHitN;
			PnRipple = $(RIPPLE_LIFE);
			PnTimer += 1.0;
		}
		else if (PnLeft)
		{
			PnP = PnRestart;
			PnVel = PnD * PnSpeed;
			PnTimer += 1.0;
			PnAge = 0.0;
		}
	}
	PnAge += PnDt;
}
Particles.Position = PnP;
Particles.Velocity = PnRipple > 0.0 ? float3(0.0, 0.0, 0.0) : PnVel;
Particles.RainTimer = PnTimer;
Particles.PaintAge = PnAge;
Particles.PaintRipple = PnRipple;
Particles.PaintNormal = PnN;
Particles.RainGameHit = PnHit;
Particles.RainGameHitPos = PnHitPos;
Particles.PaintKind = PnKind;

// ---- the look. In flight: a streak along the velocity with a bead at its head, facing the camera
// (User.RainViewFacing). Landed: a ring lying on the face, spreading and fading.
float PnSp = length(PnVel);
float3 PnRel = PnP - PnC;
float PnFadeOut = dot(PnVel, PnRel) > 0.0 ? saturate((PnExit - length(PnRel)) / 60.0) : 1.0;
float PnAcross = length(PnRel - PnD * dot(PnRel, PnD));
float PnSide = saturate((PnH - PnAcross) / (0.3 * PnH));
float PnW = PnBig ? $(BIG_WIDTH) * (0.85 + 0.3 * PnRnd.z) : $(SMALL_WIDTH) * (0.8 + 0.4 * PnRnd.z);
if (PnRipple > 0.0)
{
	float PnRa = 1.0 - PnRipple / $(RIPPLE_LIFE);
	float PnRs = (PnBig ? $(RIPPLE_SIZE_BIG) : $(RIPPLE_SIZE)) * (0.25 + 0.75 * sqrt(PnRa));
	Particles.SpriteSize = PnOn ? float2(PnRs, PnRs) : float2(0.0, 0.0);
	Particles.SpriteFacing = PnN;
	Particles.SpriteAlignment = normalize(cross(PnN, abs(PnN.z) < 0.9 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0)));
	Particles.Color = float4(User.RainPaintInk.rgb, $(RIPPLE_ALPHA) * (1.0 - PnRa) * (PnOn ? 1.0 : 0.0));
	Particles.DynamicMaterialParameter = float4(2.0, 1.0, PnRa, 0.0);
}
else
{
	float PnVis = PnOn ? saturate(PnAge / 0.12) * PnFadeOut * PnSide : 0.0;
	float PnLen = PnW + PnSp * (PnBig ? $(BIG_STREAK) : $(SMALL_STREAK));
	Particles.SpriteSize = float2(PnW, PnLen) * (PnVis > 0.001 ? 1.0 : 0.0);
	Particles.SpriteFacing = User.RainViewFacing;
	Particles.SpriteAlignment = PnSp > 1.0 ? PnVel / PnSp : PnD;
	Particles.Color = float4(User.RainPaintInk.rgb, (PnBig ? 1.0 : $(SMALL_ALPHA)) * PnVis);
	Particles.DynamicMaterialParameter = float4(PnBig ? 1.0 : 0.0, PnW / PnLen, 0.0, 0.0);
}
#undef PN_NEXT
#undef PN_U01

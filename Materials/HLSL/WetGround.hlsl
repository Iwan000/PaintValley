// =====================================================================================
// Project Rain - wet stone pavement with puddles and rain ripples (material Custom node).
// Returns the tangent-space normal (float3); additional outputs BaseCol (float3), RoughOut (float), EmisOut
// (float3) and SpecOut (float).
//
// Wet surface model after Lagarde ("Water drop 3b"): porous darkening, glossier wet film,
// puddles flatten the normal and become mirror-like. Ripples: 4 layers of procedural rings
// (Lagarde "Water drop 2b": per-layer time multipliers 1, .85, .93, 1.13, offsets 0,.2,.45,.7).
// Ripple strength follows MPC RainIntensity, so puddles go glassy-still when the rain is frozen.
// The camera's lens (MPC Lens: blur disc at infinity in radians, focus distance, pixels per radian) blurs the
// pavement like the drops and the backdrop: out of focus, its fine detail averages out over the blur disc,
// and at the horizon, where it meets the backdrop, the two blur into each other.
//
// Inputs: Time(float) Rain(float 0..1, MPC RainIntensity) Wet(float 0..1, MPC Wetness) Lens(float4, MPC Lens)
//         Env(TextureCube: the stage's environment cube, with the backdrop in it)
// =====================================================================================
struct FPrGround
{
	float Hash12(float2 P)
	{
		float3 P3 = frac(float3(P.xyx) * 0.1031);
		P3 += dot(P3, P3.yzx + 33.33);
		return frac((P3.x + P3.y) * P3.z);
	}
	float2 Hash22(float2 P)
	{
		float3 P3 = frac(float3(P.xyx) * float3(0.1031, 0.1030, 0.0973));
		P3 += dot(P3, P3.yzx + 33.33);
		return frac((P3.xx + P3.yz) * P3.zy);
	}
	float Noise(float2 P)
	{
		float2 I = floor(P);
		float2 F = frac(P);
		float2 U = F * F * (3.0 - 2.0 * F);
		return lerp(lerp(Hash12(I), Hash12(I + float2(1, 0)), U.x), lerp(Hash12(I + float2(0, 1)), Hash12(I + float2(1, 1)), U.x), U.y);
	}
	float Fbm(float2 P)
	{
		float A = 0.5;
		float S = 0.0;
		for (int i = 0; i < 4; i++)
		{
			S += A * Noise(P);
			P = P * 2.03 + 17.1;
			A *= 0.5;
		}
		return S;
	}
	// Returns the ripple normal perturbation (xy) at P (cm).
	float2 Ripples(float2 P, float Time, float Rain)
	{
		const float TimeMul[4] = { 1.0, 0.85, 0.93, 1.13 };
		const float TimeAdd[4] = { 0.0, 0.2, 0.45, 0.7 };
		const float Cell = 22.0;
		float2 N = float2(0.0, 0.0);
		[unroll]
		for (int L = 0; L < 4; L++)
		{
			float2 Q = P / Cell + float2(L * 0.37, L * 0.61);
			float2 Ci = floor(Q);
			float2 Fp = frac(Q);
			float2 Ctr = 0.2 + 0.6 * Hash22(Ci + L * 13.0);
			float Phase = Hash12(Ci * 1.7 + L * 5.0);
			float T = frac(Time * 1.35 * TimeMul[L] + TimeAdd[L] + Phase);
			// Only some cells get a drop each cycle; more cells when it rains harder.
			float Active = step(Hash12(Ci + floor(Time * 1.35 * TimeMul[L] + TimeAdd[L] + Phase) * 3.1), Rain * 0.85);
			float2 D = (Fp - Ctr) * Cell;
			float R = length(D);
			float Radius = T * 9.0;
			float X = (R - Radius) / 0.85;
			float Ring = sin(clamp(X * 3.0 + 3.0, 0.0, 9.42)) * exp(-X * X * 0.5);
			float Fade = (1.0 - T) * (1.0 - T);
			N += Active * Fade * Ring * D / max(R, 1e-3);
		}
		return N * 0.35;
	}
};

FPrGround G;
float3 WP = WSDemote(GetWorldPosition(Parameters));
float2 P = WP.xy;

// Paver grid (60 x 40 cm, staggered rows) with recessed joints that hold water.
float2 Pv = P / float2(60.0, 40.0);
Pv.x += 0.5 * floor(Pv.y);
float2 Fp = frac(Pv);
float2 Jd = min(Fp, 1.0 - Fp) * float2(60.0, 40.0);
float Joint = 1.0 - smoothstep(0.4, 1.4, min(Jd.x, Jd.y));
float StoneVar = G.Hash12(floor(Pv));

// Puddles: large-scale noise, more water where the joints are.
float Pud = G.Fbm(P / 420.0);
float Puddle = smoothstep(0.52, 0.58, Pud + Joint * 0.05) * Wet;

// Micro relief of the stone (dry normal).
float H0 = G.Fbm(P / 6.0);
float Hx = G.Fbm((P + float2(1.5, 0.0)) / 6.0);
float Hy = G.Fbm((P + float2(0.0, 1.5)) / 6.0);
float2 StoneN = float2(H0 - Hx, H0 - Hy) * 1.6 * (1.0 - Puddle);

float2 RippleN = G.Ripples(P, Time, Rain) * lerp(0.35, 1.0, Puddle);
float2 Nxy = StoneN + RippleN;

// Lens blur here (thin lens: a blur disc of Lens.x |1 - focus / distance| radians), in pixels. Out of focus
// the relief and ripples flatten and the tones go to the pavement's mean (the wet film still mirrors the lights).
float CamD = length(GetTranslatedWorldPosition(Parameters) - ResolvedView.TranslatedWorldCameraOrigin);
float BlurPx = Lens.x * abs(1.0 - Lens.y / max(CamD, 1.0)) * Lens.z;
float Sharp = 1.0 - smoothstep(1.0, 6.0, BlurPx);
Nxy *= Sharp;

// Roughness: dry stone 0.65, wet film 0.18, puddle 0.03.
float Rough = lerp(0.65, 0.18, Wet);
Rough = lerp(Rough, 0.03, Puddle);
Rough = lerp(Rough, 0.12, Joint * Wet);
Rough += StoneVar * 0.04;

// Stone albedo, darkened by water (porosity ~0.6 -> x0.52), puddles and joints darker still.
float3 Stone = lerp(float3(0.13, 0.125, 0.115), float3(0.21, 0.195, 0.175), StoneVar) * (0.85 + 0.3 * G.Noise(P / 35.0));
Stone *= lerp(1.0, 0.52, Wet);
Stone *= lerp(1.0, 0.55, Puddle);
Stone *= lerp(1.0, 0.6, Joint);
// The mean of the pavement's tones (wet stone, ~30% puddles, joints).
float3 MeanStone = float3(0.074, 0.069, 0.063) * lerp(1.0, 0.52, Wet) / 0.52;
BaseCol = lerp(MeanStone, Stone, Sharp);
RoughOut = saturate(Rough);

// The horizon: a pixel whose blur disc (radius: half the lens blur, at least a pixel) reaches above the line
// where the pavement meets the backdrop shows that share of the backdrop (read blurred, just above the line).
// The wet film's grazing mirror image of the lit horizon is a line a few pixels thin that the blur would
// spread out: within the blur radius it gives way to the blurred horizon as well.
float3 ViewDir = (GetTranslatedWorldPosition(Parameters) - ResolvedView.TranslatedWorldCameraOrigin) / max(CamD, 1.0);
float BlurRad = 0.5 * Lens.x * abs(1.0 - Lens.y / max(CamD, 1.0)) + 1.0 / max(Lens.z, 1.0);
float Below = max(-ViewDir.z, 0.0) / BlurRad;
float Mix = saturate(0.5 * (1.0 - Below));
float3 Horizon = Env.SampleLevel(EnvSampler, normalize(float3(ViewDir.xy, BlurRad)), log2(max(BlurRad / (1.5707963 / 512.0), 1.0))).rgb;
EmisOut = Horizon * Mix;
BaseCol *= 1.0 - Mix;
SpecOut = 0.5 * saturate(Below);
return normalize(float3(Nxy, 1.0));

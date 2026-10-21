// =====================================================================================
// Project Rain - raindrop sprite shading (material Custom node, returns float4 rgb + alpha).
//
// One shader covers the whole continuum from a falling streak to a hanging bead:
//  * The sprite is a capsule of length L = v*T + D and width D (Niagara writes s = L/D in Dyn.x).
//  * Analytic motion blur: a pixel is covered while the moving disc passes over it. Coverage is
//    the fraction of the exposure the disc overlaps the pixel (= D/L for long streaks, energy
//    conserving, Garg & Nayar 2006), and colour is the drop's shading averaged over that path.
//  * Drop shading treats the drop as a water ball lens (IOR 1.333, F0 0.02): front reflection +
//    double refraction through the sphere (inverted, ~165 deg field of view) sampled from an HDR
//    environment cube, plus analytic glints from up to six stage lights (reflected and transmitted).
//  * Dyn.y > 0.5: drop too small on screen to resolve, lit per particle (PCol.rgb); Dyn.y <= 0: resolved
//    drop, ball-lens shading plus -Dyn.y of the per-particle light (smooth LOD transition). The
//    per-particle light is a lamp seen through the ball lens: a bright core in a faint drop, not an evenly
//    lit disc (which reads as snow). It is drawn as a core (same energy over the disc) at least ~1 px
//    wide, widened by the lens blur.
//  * The ball-lens image is filtered by what one pixel of the drop covers and by the lens blur: both spread
//    the directions a pixel sees (sigma, in normal space), so the environment is read from a blurrier mip
//    and the glints widen with their energy kept. A small drop shows a soft average of its surroundings
//    instead of crawling sub-pixel arcs, and a defocused drop is the same drop smeared over its blur disc:
//    never brighter than in focus.
//  * Dyn.z: the soft (out-of-focus) fraction of the disc radius, from the particle-space depth of field.
//  * SkyGlow: optional extra sky radiance above the horizon, for an environment without a lit sky.
//
// Inputs: UV(float2) DynX..DynW(Niagara dynamic parameter: s, path, soft, rand) PCol(float4: tint, visibility)
//         CamVec(float3) Env(TextureCube) CamW(float3) Look(float4: env gain, glint gain, streak gain, exposure)
//         SkyGlow(float4: rgb radiance of the sky above the horizon)
//         L0P..L5P (float4 world pos xyz, soft radius w)  L0C..L5C (float4 rgb = colour * intensity)
// =====================================================================================
struct FPrDrop
{
	float Fresnel(float CosT)
	{
		float M = 1.0 - saturate(CosT);
		float M2 = M * M;
		return 0.02 + 0.98 * M2 * M2 * M;
	}

	float3 Sky(float3 D, float3 Glow)
	{
		return Glow * smoothstep(-0.1, 0.5, D.z);
	}

	// SigN: angular spread of the normals one pixel sees (pixel footprint and lens blur, radians).
	float3 Shade(float3 N, float3 V, float3 Pw, TextureCube EnvT, SamplerState EnvS, float4 LP[6], float4 LC[6], float4 Look, float3 Glow, float Rnd, float SigN)
	{
		// The reflected ray turns twice as fast as the normal, the ray refracted through the sphere ~1.5x.
		float SigR = 2.0 * SigN;
		float SigT = 1.5 * SigN;
		// Environment cube: 512 texels per 90 deg face.
		const float EnvTexel = 1.5707963 / 512.0;
		float MipR = log2(max(SigR / EnvTexel, 1.0));
		float MipT = log2(max(SigT / EnvTexel, 1.0));
		// Glint lobes pow(cos, n) ~ exp(-n theta^2 / 2) widened by the spread (1/n' = 1/n + sigma^2), scaled
		// by n'/n so a glint keeps its energy as it widens.
		float NR = 1.0 / (1.0 / 900.0 + SigR * SigR);
		float NT = 1.0 / (1.0 / 140.0 + SigT * SigT);
		float F = Fresnel(dot(N, V));
		float3 R = reflect(-V, N);
		// Entry refraction, chord through the unit sphere, exit refraction (never TIR for a sphere).
		float3 T1 = refract(-V, N, 1.0 / 1.333);
		float3 N2 = normalize(N - 2.0 * dot(N, T1) * T1);
		float3 T2 = refract(T1, -N2, 1.333);
		if (dot(T2, T2) < 1e-4)
		{
			T2 = reflect(T1, -N2);
		}
		float Ft = (1.0 - F) * (1.0 - F);

		float3 C = (F * (EnvT.SampleLevel(EnvS, R, MipR).rgb + Sky(R, Glow)) + Ft * 0.985 * (EnvT.SampleLevel(EnvS, T2, MipT).rgb + Sky(T2, Glow))) * Look.x;

		[unroll]
		for (int i = 0; i < 6; i++)
		{
			float3 Lv = LP[i].xyz - Pw;
			float D2 = dot(Lv, Lv);
			float3 L = Lv * rsqrt(max(D2, 1.0));
			// Irradiance relative to 10 m: LC is "brightness of the glint at 10 m".
			float3 E = LC[i].rgb * (1.0e6 / max(D2, LP[i].w * LP[i].w + 100.0));
			float GR = pow(saturate(dot(R, L)), NR) * (NR / 900.0) * F * 40.0;
			float GT = pow(saturate(dot(T2, L)), NT) * (NT / 140.0) * Ft * 6.0;
			C += E * (GR + GT) * Look.y;
		}
		return C * (0.85 + 0.3 * Rnd);
	}
};

FPrDrop PrDrop;
float4 Dyn = float4(DynX, DynY, DynZ, DynW);
float4 PrLP[6] = { L0P, L1P, L2P, L3P, L4P, L5P };
float4 PrLC[6] = { L0C, L1C, L2C, L3C, L4C, L5C };

// Debug view: Look.w (DropExposure) > 99 paints every sprite opaque magenta (geometry/visibility check).
if (Look.w > 99.0)
{
	return float4(20.0, 0.0, 20.0, PCol.a > 0.0 ? 1.0 : 0.15);
}

float PrVis = PCol.a;
float PrS = max(Dyn.x, 1.0);
float2 PrQ = UV * 2.0 - 1.0;
float PrX = PrQ.x;
float PrY = PrQ.y * PrS;          // along the sprite axis, in drop-radius units
float PrHs = PrS - 1.0;           // half length of the swept segment

// Derivatives must be taken before any divergent branch / early return (undefined on Metal otherwise).
float PrRound = length(float2(PrX, PrY));
float PrRoundFw = max(fwidth(PrRound) * 1.2, 1e-3);
// Film-rain boost for long falling streaks (Look.z): thin streaks are physically too faint to read, so they
// are lit up; a streak several pixels wide (a drop close to the lens) already reads and keeps its true
// brightness. As a streak shortens the boost falls smoothly to the bead's (no flash at the stop).
float PrPixAcross = 1.0 / max(fwidth(UV.x), 1e-4);
float PrBoost = max(1.0, Look.z * PrS / max(PrS, 60.0));
PrBoost = lerp(PrBoost, 1.0, smoothstep(2.0, 10.0, PrPixAcross));

// The per-particle light's core: a Gaussian of sigma 0.35 drop radii (in the sprite's units, the drop is
// sqrt(1 - soft^2) of the blurred disc), widened by the lens blur and to at least one pixel, normalized so
// its average over the unit disc is 1. Streaks keep it spread along their length (the core sweeps a line).
float PrSoftQ = saturate(Dyn.z);
float PrCoreS2 = 0.1225 * (1.0 - PrSoftQ * PrSoftQ) + 0.25 * PrSoftQ * PrSoftQ + 4.0 / (PrPixAcross * PrPixAcross);
float PrCoreR = length(float2(PrX, max(abs(PrY) - PrHs, 0.0)));
float PrCore = exp(-PrCoreR * PrCoreR / (2.0 * PrCoreS2)) / (2.0 * PrCoreS2 * (1.0 - exp(-0.5 / PrCoreS2)));
PrCore = lerp(PrCore, 1.0, saturate(PrHs * 2.0));

float PrHx = 1.0 - PrX * PrX;
if (PrHx <= 0.0 || PrVis <= 0.0)
{
	return float4(0.0, 0.0, 0.0, 0.0);
}

// Cheap path for unresolved drops (Dyn.y = 1, decided per particle in Niagara): the drop is lit
// per particle (PCol.rgb); only the motion-blurred capsule coverage is evaluated here.
if (Dyn.y > 0.5)
{
	float PrSoftC = saturate(Dyn.z);
	float PrHwC = sqrt(PrHx);
	float PrSpanC = min(PrHs, PrY + PrHwC) - max(-PrHs, PrY - PrHwC);
	float PrCoverC = PrHs > 1e-3 ? saturate(PrSpanC / (2.0 * PrHs)) : saturate((1.0 - PrRound) / PrRoundFw);
	// Defocused drops: a soft disc (the drop's light spread evenly over its blur disc).
	float PrEdgeD = length(float2(PrX, max(abs(PrY) - PrHs, 0.0)));
	float PrBokeh = saturate((1.0 - PrEdgeD) / max(PrSoftC * 0.6, PrRoundFw));
	PrCoverC = lerp(PrCoverC, PrBokeh * (PrHs > 1e-3 ? 1.0 / (1.0 + PrHs) : 1.0), saturate(PrSoftC * 3.0));
	return float4(PCol.rgb * PrCore * PrBoost * Look.w, saturate(PrCoverC * PrVis));
}
float PrHw = sqrt(PrHx);
float PrC0 = max(-PrHs, PrY - PrHw);
float PrC1 = min(PrHs, PrY + PrHw);
float PrSpan = PrC1 - PrC0;
if (PrSpan <= 0.0)
{
	return float4(0.0, 0.0, 0.0, 0.0);
}
float PrCover = PrHs > 1e-3 ? saturate(PrSpan / (2.0 * PrHs)) : 1.0;

// Shading frame: view-facing sphere built from the sprite axes.
float3 PrV = normalize(CamVec);
float3 PrUp = Parameters.TangentToWorld[1];
PrUp = normalize(PrUp - PrV * dot(PrUp, PrV) + 1e-6);
float3 PrRt = normalize(cross(PrUp, PrV));
if (dot(PrRt, Parameters.TangentToWorld[0]) < 0.0)
{
	PrRt = -PrRt;
}
float3 PrPw = (GetTranslatedWorldPosition(Parameters) - ResolvedView.TranslatedWorldCameraOrigin) + CamW.xyz;

// Normal spread per pixel: the unit disc spans PrPixAcross pixels (2 / PrPixAcross per pixel); lens blur
// smears the image over the blur disc, radius Dyn.z of the sprite's (a uniform disc of radius r: sigma r/2).
float PrSigN = sqrt(4.0 / (PrPixAcross * PrPixAcross) + 0.25 * Dyn.z * Dyn.z);

float PrJit = frac(52.9829189 * frac(dot(Parameters.SvPosition.xy, float2(0.06711056, 0.00583715))));
const int PrK = PrHs > 0.25 ? 4 : 1;
float3 PrAcc = float3(0.0, 0.0, 0.0);
[unroll]
for (int k = 0; k < 4; k++)
{
	if (k < PrK)
	{
		float PrC = PrK == 1 ? 0.0 : lerp(PrC0, PrC1, (k + PrJit) / PrK);
		float2 PrL = float2(PrX, PrY - PrC);
		float PrZ = sqrt(saturate(1.0 - dot(PrL, PrL)));
		float3 PrN = normalize(PrL.x * PrRt + PrL.y * PrUp + PrZ * PrV);
		PrAcc += PrDrop.Shade(PrN, PrV, PrPw, Env, EnvSampler, PrLP, PrLC, Look, SkyGlow.rgb, Dyn.w, PrSigN);
	}
}
PrAcc /= PrK;
// Barely resolved drops also carry the per-particle scattered light of the cheap path (Dyn.y = -weight).
PrAcc += PCol.rgb * PrCore * saturate(-Dyn.y);

// Anti-aliased rim for hanging beads, softened as they leave the focal plane.
float PrEdge = 1.0;
if (PrHs <= 0.25)
{
	PrEdge = saturate((1.0 - PrRound) / max(PrRoundFw, Dyn.z * 0.6));
}

// Falling rain is physically very faint; film rain is lit to read ("milk", Tatarchuk 2006).
float3 PrRgb = PrAcc * PrBoost * Look.w;
return float4(PrRgb, saturate(PrCover * PrEdge * PrVis));

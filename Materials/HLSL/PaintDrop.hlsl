// =====================================================================================
// Project Rain - Paint Valley drops (material Custom node, unlit translucent, Niagara sprites). Returns float4:
// colour, opacity. Two looks for the drops in the air (DynW), the same ripple once landed:
//   - paint: a short rounded capsule of paint, fading from its head to its tail; held in the air (the freeze) it
//     draws in to a round, even, see-through drop;
//   - film: a drop of water, as in the film version: a ball lens showing what is behind it upside down (Scene, looked
//     up by PaintDropLens.hlsl), tinted a little by the paint, darker at its rim where the light bends away, with a
//     glint; falling, the same image smeared along its streak with a bright line down its core.
// Landed: a ring spreading on the surface.
//
// Inputs: UV (float2), PColor (float4), DynX (0 small drop, 1 big drop, 2 ripple), DynY (width / length),
//         DynZ (a ripple's age, 0..1; a drop's hold, 0 falling .. 1 held), DynW (1: the film look), Scene (float3)
// =====================================================================================
float2 PdP = UV - 0.5;
float3 PdC = PColor.rgb;
float PdA = 0.0;
if (DynX > 1.5)
{
	// A thin ring near the sprite's edge, a little lighter than the paint.
	float PdR = length(PdP) * 2.0;
	float PdD = abs(PdR - 0.8) - lerp(0.07, 0.03, DynZ);
	PdA = saturate(-PdD / max(fwidth(PdD), 1e-3) + 0.5);
	PdC = lerp(PdC, float3(1.0, 1.0, 1.0), 0.45);
}
else
{
	// A capsule along the velocity (round at both ends, no point), as wide as the sprite: its head is -y (the end it
	// moves towards), +y its tail (up, for falling rain). Held, it is round (width = length).
	float PdAsp = 1.0 / clamp(DynY, 0.01, 1.0);
	float2 PdQ = float2(PdP.x, PdP.y * PdAsp);
	float PdHalf = 0.5 * PdAsp - 0.5;
	float PdD = length(float2(PdQ.x, max(abs(PdQ.y) - PdHalf, 0.0))) - 0.5;
	float PdShape = saturate(-PdD / max(fwidth(PdD), 1e-3) + 0.5);
	float PdHold = saturate(DynZ);
	// From head (0) to tail (1), and across (1 on the axis).
	float PdT = saturate((PdQ.y + PdHalf + 0.5) / max(PdAsp, 1.0));
	float PdCore = saturate(1.0 - abs(PdQ.x) * 2.5);
	if (DynW < 0.5)
	{
		// Paint: falling, densest at the head and a little lighter down the middle; held, even all over.
		float3 PdStreakC = lerp(PdC, float3(1.0, 1.0, 1.0), 0.22 * PdCore * (1.0 - PdT));
		PdC = lerp(PdStreakC, PdC, PdHold);
		PdA = PdShape * lerp(lerp(1.0, 0.3, PdT), 1.0, PdHold);
	}
	else
	{
		// Film: the lens image, a little brighter than what it shows and tinted by the paint.
		float PdR = saturate(length(float2(PdQ.x, max(abs(PdQ.y) - PdHalf, 0.0))) * 2.0);
		float3 PdLens = lerp(Scene.rgb * 1.06 + 0.04, PdC, 0.22);
		// Held: darker towards the rim (light leaves a sphere's edge at a grazing angle), a glint at the upper left.
		float PdRim = smoothstep(0.55, 1.0, PdR);
		float2 PdHi = PdQ - float2(0.15, 0.18);
		float PdGlintD = length(PdHi) - 0.09;
		float PdGlint = saturate(-PdGlintD / max(fwidth(PdGlintD), 1e-3) + 0.5);
		float3 PdBead = lerp(PdLens, PdLens * 0.45 + PdC * 0.15, PdRim);
		PdBead = lerp(PdBead, float3(1.0, 1.0, 1.0), 0.9 * PdGlint);
		// Falling: the smeared image with a bright line down its core, fading towards the tail.
		float3 PdStreak = PdLens + 0.35 * PdCore * (1.0 - 0.6 * PdT);
		PdC = lerp(PdStreak, PdBead, PdHold);
		PdA = PdShape * lerp(lerp(0.95, 0.35, PdT), 1.0, PdHold);
	}
}
return float4(PdC, PdA * PColor.a);

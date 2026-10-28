// =====================================================================================
// Project Rain - Paint Valley drops (material Custom node, unlit translucent, Niagara sprites). Returns float4:
// colour, opacity. After the user's reference: in flight a drop is a thin streak with a bead at its head; landed,
// a ring spreading on the surface.
//
// Inputs: UV (float2), PColor (float4), DynX (0 small drop, 1 big drop, 2 ripple), DynY (width / length),
//         DynZ (the ripple's age, 0..1)
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
	// In widths along the streak: the bead (radius 0.5) at the head end, -y; a tapering, fading tail behind it.
	float PdAsp = 1.0 / clamp(DynY, 0.01, 1.0);
	float2 PdQ = float2(PdP.x, PdP.y * PdAsp);
	float PdHead = -0.5 * PdAsp + 0.5;
	float PdBead = length(PdQ - float2(0.0, PdHead)) - 0.5;
	float PdT = saturate((PdQ.y - PdHead) / max(PdAsp - 1.0, 0.01));
	float PdTail = max(abs(PdQ.x) - lerp(0.2, 0.05, PdT), max(PdHead - PdQ.y, PdQ.y - 0.5 * PdAsp));
	float PdAA = max(fwidth(PdBead), 1e-3);
	float PdInBead = saturate(-PdBead / PdAA + 0.5);
	float PdInTail = saturate(-PdTail / max(fwidth(PdTail), 1e-3) + 0.5) * (1.0 - PdT) * 0.85;
	PdA = max(PdInBead, PdInTail);
	// A glint on the bead.
	float PdG = length(PdQ - float2(-0.16, PdHead - 0.12)) - 0.15;
	PdC = lerp(PdC, float3(1.0, 1.0, 1.0), saturate(-PdG / PdAA + 0.5) * 0.75 * PdInBead);
}
return float4(PdC, PdA * PColor.a);

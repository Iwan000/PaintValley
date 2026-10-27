// Drops brushing the lens become huge sorted quads (overdraw) and distract; fade them out.
float3 PrToCam = PrCam - PrP;
PrCamD = max(length(PrToCam), 1.0);
PrVis *= saturate((PrCamD - 30.0) / 30.0);
// Outside the view (a cone through the frame's corners for any aspect >= 1, plus a margin; drops within
// a metre are big enough to reach in from outside it): invisible.
float PrViewCos = cos(atan(User.RainViewTan * 1.42) + 0.05);
if (PrCamD > 100.0 && dot(PrToCam, User.RainCameraFwd) > -PrViewCos * PrCamD)
{
	PrVis = 0.0;
}
PrToCam /= PrCamD;

Particles.Position = PrP;
Particles.Velocity = PrV;
Particles.RainFrozen = PrFrz;
Particles.RainState = PrState;
Particles.RainTimer = PrTimer;
Particles.RainDir = PrDir;

// ---------------------------------------------------------------- shape and streak
// The drop's shape: an oblate spheroid flattened along its fall (Beard & Chuang 1987: axis ratio 0.93 at
// 2 mm, 0.78 at 4 mm, 0.64 at 6 mm), each tilted a little its own way. A held drop is liquid held in the
// air: it rocks and breathes slowly (big drops more), so no two beads catch the light the same way.
float PrWob = PrFrz * saturate(PrDmm / 4.0);
float PrPh = PrTime * (1.2 + 1.3 * PrRnd.z) + 6.2832 * PrRnd.w;
float PrRatio = 1.0048 + PrDmm * (5.7e-4 + PrDmm * (-2.628e-2 + PrDmm * (3.682e-3 - 1.677e-4 * PrDmm)));
PrRatio *= 1.0 + 0.12 * PrWob * sin(PrPh);
float3 PrAxis = normalize(float3(0.35 * (PrRnd.x - 0.5) + 0.25 * PrWob * sin(0.8 * PrPh + 1.3),
	0.35 * (PrRnd.y - 0.5) + 0.25 * PrWob * cos(0.7 * PrPh), 1.0));
// Seen from the camera, a spheroid is an ellipse: its axis on screen, its ratio from how far the axis
// leans out of the view (round when seen along the axis).
float PrCosA = dot(PrAxis, PrToCam);
float PrBeadR = sqrt(PrRatio * PrRatio * (1.0 - PrCosA * PrCosA) + PrCosA * PrCosA);
float3 PrAxS = PR_ACROSS(PrAxis, PrToCam);
// The streak: the motion across the line of sight during the exposure (RainField.hlsl). A bead while it
// is under half the drop's width, a streak beyond: the sprite turns from the drop's axis to its motion.
float3 PrVS = PR_ACROSS(PrV, PrToCam);
float PrStreak = length(PrVS) * User.RainShutter;
float3 PrMv = PrVS / max(length(PrVS), 1e-6);
float PrRound = 1.0 - saturate(2.0 * PrStreak / max(PrW, 1e-4));
PrAxS *= dot(PrAxS, PrMv) < 0.0 ? -1.0 : 1.0;
float3 PrAlign = lerp(PrMv, PrAxS, PrRound);
if (dot(PrAlign, PrAlign) < 1e-8)
{
	PrAlign = cross(PrToCam, abs(PrToCam.z) < 0.9 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0));
}
Particles.SpriteAlignment = normalize(PrAlign);
// The bead's semi-axis along the sprite, in half-widths (1 for a streak: its cross-section is the width).
float PrR = lerp(1.0, PrBeadR, PrRound);
float PrL = PrStreak + PrW * PrR;

// ---------------------------------------------------------------- screen-size LOD
// Drops narrower than a few pixels can't show their ball-lens image: light them once per particle
// here (stage lamps, RainField.hlsl) and let the material take a cheap path. Only resolved drops
// near the camera get the full per-pixel ball-lens shading.
float PrPixW = PrW * User.RainPixelScale / PrCamD;
float PrSimple = PrPixW < User.RainLodPixels ? 1.0 : 0.0;

// Particle-space depth of field: the camera's own lens (thin lens, object-space blur diameter
// b = A |d - s| / s, A = focal length / f-number; User.RainDofScale = 1 is the real lens). The drops render
// after the engine's DOF, so each drop is blurred here: drawn over its blur disc with its light spread
// over the disc (alpha / blur^2), so a defocused drop is always dimmer than in focus, never a bright
// bokeh ball. Resolved drops keep their ball-lens shading, smeared by the blur in the material (Dyn.z).
// No drop is drawn smaller than 1.5 px either: a sub-pixel sprite crawls and flickers under temporal
// AA, so its light is spread over a 1.5 px footprint instead (same energy, like the blur).
float PrCoC = User.RainAperture * abs(PrCamD - User.RainFocusDist) / max(User.RainFocusDist, 1.0) * User.RainDofScale;
float PrBlurW = max(sqrt(PrW * PrW + PrCoC * PrCoC), 1.5 * PrCamD / max(User.RainPixelScale, 1.0));
float PrBlur = PrBlurW / PrW;
float PrSoft = sqrt(max(PrBlurW * PrBlurW - PrW * PrW, 0.0)) / PrBlurW;
PrVis *= 1.0 / (PrBlur * PrBlur);
float PrGrow = PrBlurW - PrW;
float3 PrLum = User.RainAmbient;
if (PrVis > 0.002)
{
	PR_LAMPS(PrLum, PrP, PrToCam)
}

// Invisible drops (out of view, parked, faded out, too defocused, at the lens) collapse to degenerate
// quads: they cost nothing in rasterization and tile binning. (Not culled with Particles.VisibilityTag:
// in UE 5.7 the GPU cull + depth sort then loses the nearest drops, the end of the sorted list.)
Particles.SpriteSize = PrVis > 0.002 ? float2(PrW + PrGrow, PrL + PrGrow) : float2(0.0, 0.0);
// How much of a lamp's light a drop sends to the lens depends on where its shape and tilt put the lamp's
// image: it differs a lot from drop to drop (x0.45 .. x1.55, the same factor scales the glints).
Particles.Color = float4(PrLum * (0.45 + 1.1 * PrRnd.y), PrVis);
// x: sprite length / width. y: shading path, 1 = cheap (per-particle light); -1..0 = full ball-lens
// shading, with -y = how much of the per-particle scattered light to add (1 at the switch to the cheap
// path, 0 from 4x the size threshold), so drops don't change brightness when they switch (the environment
// refracted by a resolved drop is darker than the forward-scattering estimate). z: soft (defocused)
// fraction of the disc radius. w: random (brightness, as above).
float PrGlow = saturate(1.0 - (PrPixW - User.RainLodPixels) / (3.0 * max(User.RainLodPixels, 0.1)));
Particles.DynamicMaterialParameter = float4((PrL + PrGrow) / (PrW + PrGrow), PrSimple > 0.5 ? 1.0 : -PrGlow, PrSoft, PrRnd.y);
// x: the bead's semi-axis along the sprite, in half-widths of the sprite (the blur rounds it).
Particles.DynamicMaterialParameter1 = float4((PrW * PrR + PrGrow) / (PrW + PrGrow), 0.0, 0.0, 0.0);

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

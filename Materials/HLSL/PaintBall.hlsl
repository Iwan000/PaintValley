// =====================================================================================
// Project Rain - Paint Valley trackball (material Custom node, unlit translucent, the engine's sphere). Returns
// float4: colour, opacity. A see-through cream globe: clear in the middle, denser towards its rim, lit from above,
// with a small shine; parallels and meridians every 30 degrees drawn in the ball's own space, so they turn with it
// and show the roll under the cursor. Its poles are the rain's axis (the arrow inside runs through them).
//
// Inputs: LocalPos (float3, the mesh's space: radius 50), NormalWS (float3), ToCamera (float3, unit),
//         Body (float3), Line (float3), Opacity (float3: centre, rim, lines), Shine (float3: towards the light, unit;
//         the game sets it to the upper left of the view, towards the camera)
// =====================================================================================
float3 PbL = normalize(LocalPos);
float3 PbN = normalize(NormalWS);
// Angle on the sphere per pixel (radians), for lines about a pixel and a half wide at any size.
float PbPix = max(length(ddx(PbL)) + length(ddy(PbL)), 1e-4);
const float PbStep = 0.5235988;
float PbLat = asin(clamp(PbL.z, -1.0, 1.0));
float PbLon = atan2(PbL.y, PbL.x);
float PbDLat = abs(frac(PbLat / PbStep + 0.5) - 0.5) * PbStep;
float PbDLon = abs(frac(PbLon / PbStep + 0.5) - 0.5) * PbStep * cos(PbLat);
// Meridians stop short of the poles (they would crowd into a blot round the arrow).
float PbMeridian = (1.0 - smoothstep(0.5 * PbPix, 1.2 * PbPix, PbDLon)) * (1.0 - smoothstep(1.2, 1.35, abs(PbLat)));
float PbParallel = 1.0 - smoothstep(0.5 * PbPix, 1.2 * PbPix, PbDLat);
float PbEquator = 1.0 - smoothstep(0.9 * PbPix, 1.8 * PbPix, abs(PbLat));
float PbLines = max(max(PbMeridian, PbParallel), PbEquator);

float PbFacing = saturate(dot(PbN, ToCamera));
float PbRim = pow(1.0 - PbFacing, 2.0);
float PbLight = saturate(dot(PbN, Shine));
float PbShine = pow(PbLight, 90.0);
float3 PbC = Body * (0.82 + 0.18 * PbLight);
PbC = lerp(PbC, Line, PbLines);
PbC = lerp(PbC, float3(1.0, 1.0, 1.0), PbShine);
float PbA = lerp(Opacity.x, Opacity.y, PbRim);
PbA = max(PbA, Opacity.z * PbLines);
PbA = max(PbA, 0.9 * PbShine);
return float4(PbC, PbA);

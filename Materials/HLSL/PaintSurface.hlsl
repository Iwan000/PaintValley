// =====================================================================================
// Project Rain - Paint Valley surfaces (material Custom node, unlit): Monument Valley's flat tones.
// Returns the emissive colour (float3).
//
// No lights: every face gets the colour of its direction, like Monument Valley's hand-set face colours. Tops
// are lightest; the side faces go from the dark tone to the light one with the face's angle to the key
// direction (MPC Key.xy: the camera's right, so the face on the right of the view is always the light one and
// the look survives turning the view). Colours are hue-shifted per palette, not just darkened. On top:
//   - a height gradient (Grad: z0, z1, amount) towards GradColor, and faint floor tiles (Tile),
//   - baked ambient occlusion tinted with AOColor (atlas G),
//   - openings (doors, windows, portholes; atlas B, a signed distance: rim, ring and core bands, sharp to the pixel),
//   - a thin light line along convex top edges (atlas A, EdgeColor.a = amount),
//   - paint (atlas R, InkColor): puddles of one even colour, a metaball field sharpened the same way,
//   - a fade towards FadeColor below z1 (Fade: z0, z1, amount): the structure dissolves into the backdrop.
// The facet normal comes from the screen-space derivatives of the position, so cones and domes are flat-shaded
// whatever their vertex normals.
//
// Inputs: Atlas (Texture2D), UV (float2), UseAtlas, Top, LightC, DarkC, Bottom, AOColor, GradColor, OpeningColor,
//         InkColor (float3), Grad, Fade, EdgeColor, OpeningRim, OpeningBarrel, AtlasTexel, Tile, VColor, Key (float4),
//         FadeColor (float3)
// =====================================================================================
float3 PsP = WSDemote(GetWorldPosition(Parameters));
float3 PsN = normalize(cross(ddx(PsP), ddy(PsP)));
PsN *= dot(PsN, Parameters.CameraVector) < 0.0 ? -1.0 : 1.0;

// Face tone: a 6-direction "ambient cube" whose four sides are blended by the key direction.
float2 PsK = normalize(Key.xy + float2(1e-5, 0.0));
float2 PsNs = PsN.xy / max(length(PsN.xy), 1e-5);
float PsSideT = smoothstep(0.0, 1.0, 0.5 + 0.5 * dot(PsNs, PsK));
float3 PsSide = lerp(DarkC, LightC, PsSideT);
float PsZ2 = PsN.z * PsN.z;
float3 PsC = PsN.z >= 0.0 ? lerp(PsSide, Top, PsZ2) : lerp(PsSide, Bottom, PsZ2);
// Per-vertex shade (alternate spire facets).
PsC *= VColor.rgb;

// Height gradient: towards GradColor (a warm light) over z0..z1, strongest on the lit sides, half on tops.
float PsG = saturate((PsP.z - Grad.x) / max(Grad.y - Grad.x, 1.0)) * Grad.z * lerp(0.55, 1.0, PsSideT) * (PsN.z > 0.5 ? 0.5 : 1.0);
PsC = lerp(PsC, GradColor, PsG);

// Floor tiles: faint lines every Tile.x cm (Tile.y wide, Tile.z darker) on tops below z = Tile.w.
float2 PsTq = abs(frac(PsP.xy / max(Tile.x, 1.0) + 0.5) - 0.5) * Tile.x;
float PsTd = min(PsTq.x, PsTq.y);
float PsTl = 1.0 - smoothstep(0.5 * Tile.y - fwidth(PsTd), 0.5 * Tile.y + fwidth(PsTd), PsTd);
PsC *= 1.0 - Tile.z * PsTl * step(0.9, PsN.z) * step(PsP.z, Tile.w) * step(0.5, Tile.x);

// The atlas: R paint, G ambient occlusion, B openings, A edge light.
float4 PsA = UseAtlas > 0.5 ? Atlas.SampleLevel(AtlasSampler, UV, 0.0) : float4(0.0, 1.0, 0.0, 0.0);
float3 PsAO = lerp(AOColor, float3(1.0, 1.0, 1.0), PsA.g);
PsC *= PsAO;

// Openings: depth inside the outline (cm; B is a signed distance over +-16 cm). A rim band (OpeningRim.a cm),
// then the hole: its far end is the core moved OpeningBarrel.a cm up and towards the screen's left
// (OpeningColor); the crescent it uncovers is the hole's side, a porthole's barrel or a window's reveal
// (OpeningBarrel.rgb). The face's U axis in the world is +Y on X-facing faces and +X on the others; the atlas
// is AtlasTexel.xy per texel, AtlasTexel.z cm.
float PsDin = (PsA.b - 0.5) * 32.0;
float PsAA = max(fwidth(PsDin), 1e-3);
float PsO = saturate(PsDin / PsAA + 0.5);
float PsCoreM = saturate((PsDin - OpeningRim.a) / PsAA + 0.5);
float2 PsUw = abs(PsN.x) > 0.5 ? float2(0.0, 1.0) : float2(1.0, 0.0);
// OpeningBarrel.a > 0: moved up and sideways (a porthole's barrel); < 0: sideways only (a window's side reveal).
float2 PsShift = float2(dot(PsUw, PsK) > 0.0 ? -1.0 : 1.0, OpeningBarrel.a > 0.0 ? 1.0 : 0.0) * (abs(OpeningBarrel.a) / max(AtlasTexel.z, 0.01)) * AtlasTexel.xy;
float PsDin2 = UseAtlas > 0.5 ? (Atlas.SampleLevel(AtlasSampler, UV - PsShift, 0.0).b - 0.5) * 32.0 : -16.0;
float PsFar = saturate((PsDin2 - OpeningRim.a) / PsAA + 0.5);
float3 PsOpen = lerp(OpeningRim.rgb, lerp(OpeningBarrel.rgb, OpeningColor, PsFar), PsCoreM);
PsC = lerp(PsC, PsOpen * lerp(0.9, 1.0, PsSideT), PsO);

float PsE = saturate((PsA.a - 0.667) / max(fwidth(PsA.a), 1e-4) + 0.5) * EdgeColor.a * (1.0 - PsO);
PsC = lerp(PsC, EdgeColor.rgb, PsE);

// Paint: puddles of one even colour, whatever their depth (the user's choice, Docs/ArtDirection.md). R is the
// puddles' metaball field over four times the paint level: the paint shows from 0.25. Only
//   - an edge that wanders a little (fine noise on the field, near the edge only), so no puddle is a stamped circle;
//   - a faint broad drift of value (low-frequency noise in world space, +-3 %), so a large area is not dead flat.
// Toned like the face under it and a little see-through (the floor's lines show).
struct FPsNoise
{
	float Hash(float3 P)
	{
		P = frac(P * 0.1031);
		P += dot(P, P.zyx + 31.32);
		return frac((P.x + P.y) * P.z);
	}
	float Value(float3 P)
	{
		float3 I = floor(P);
		float3 F = frac(P);
		F = F * F * (3.0 - 2.0 * F);
		return lerp(lerp(lerp(Hash(I), Hash(I + float3(1, 0, 0)), F.x), lerp(Hash(I + float3(0, 1, 0)), Hash(I + float3(1, 1, 0)), F.x), F.y),
			lerp(lerp(Hash(I + float3(0, 0, 1)), Hash(I + float3(1, 0, 1)), F.x), lerp(Hash(I + float3(0, 1, 1)), Hash(I + float3(1, 1, 1)), F.x), F.y), F.z);
	}
};
FPsNoise PsNz;
float PsBroad = PsNz.Value(PsP / 70.0) * 0.5 + PsNz.Value(PsP / 23.0) * 0.3 + PsNz.Value(PsP / 9.0) * 0.2;
float PsFine = PsNz.Value(PsP / 3.5);
// The fine noise only wobbles the edge: it fades out a little way inside, so the interior stays smooth.
float PsR = PsA.r + (PsFine - 0.5) * 0.06 * (1.0 - saturate((PsA.r - 0.25) / 0.08));
float PsW = max(fwidth(PsR), 1e-4);
float PsI = saturate((PsR - 0.25) / (0.7 * PsW) + 0.5);
float3 PsWash = InkColor * lerp(0.97, 1.03, PsBroad);
// The face's tone: tops as they are, sides from dark to light, undersides darker. Continuous in N.z: on a wall
// the screen-space normal's z is 0 give or take rounding, and a branch on its sign would flicker pixel by pixel.
float3 PsTone = lerp(float3(0.74, 0.8, 0.9), float3(0.94, 0.96, 1.0), PsSideT);
PsTone = lerp(PsTone, float3(1.0, 1.0, 1.0), saturate(PsN.z) * saturate(PsN.z)) * lerp(1.0, 0.8, saturate(-PsN.z));
PsC = lerp(PsC, PsWash * PsTone * PsAO, PsI * 0.9);

float PsF = saturate((Fade.y - PsP.z) / max(Fade.y - Fade.x, 1.0)) * Fade.z;
PsC = lerp(PsC, FadeColor, PsF);
return PsC;

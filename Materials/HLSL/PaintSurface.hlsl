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
//   - paint (atlas R, InkColor): puddles, a metaball field sharpened the same way,
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

// Paint: puddles (the user's reference, Docs/ArtDirection.md). R is the puddles' metaball field (0.5 on the
// edge): merged circles with a scalloped outline, sharpened to the pixel. A flat, matte colour in the face's tones
// (tops lighter, dark sides darker), a little see-through (the floor's lines show), a lighter rim along the edge.
float PsI = saturate((PsA.r - 0.5) / max(0.7 * fwidth(PsA.r), 1e-4) + 0.5);
float PsRim = 1.0 - saturate((PsA.r - 0.5) / max(1.6 * fwidth(PsA.r), 1e-4));
float3 PsInkSide = lerp(InkColor * float3(0.72, 0.78, 0.88), InkColor, PsSideT);
float3 PsInk = (PsN.z >= 0.0 ? lerp(PsInkSide, lerp(InkColor, float3(1.0, 1.0, 1.0), 0.18), PsZ2) : PsInkSide * 0.8) * PsAO;
PsInk = lerp(PsInk, lerp(PsInk, float3(1.0, 1.0, 1.0), 0.28), PsRim);
PsC = lerp(PsC, PsInk, PsI * 0.92);

float PsF = saturate((Fade.y - PsP.z) / max(Fade.y - Fade.x, 1.0)) * Fade.z;
PsC = lerp(PsC, FadeColor, PsF);
return PsC;

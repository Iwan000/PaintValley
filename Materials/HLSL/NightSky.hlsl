// =====================================================================================
// Project Rain - night backdrop (material Custom node on the sky dome, returns float3 emissive).
//
// An HDR panorama of a city square at night, at infinity: looked up by direction only, so it never shows
// parallax however the camera moves. The direction is taken from the ground below the camera (not from the
// camera): the panorama's horizon then sits exactly where the stage's ground plane meets the dome, so the
// buildings stand on our pavement and the panorama's own ground never shows. (The dome is far enough that
// this changes nothing above the horizon.) Besides being the background, it is what the drops see: the
// environment cube they refract and reflect is captured from the stage with this dome in it, so every
// bead carries a small inverted image of the lit buildings, the lamps and the glowing overcast sky.
// Exposure scales the panorama into the stage's light levels; Yaw turns it about the vertical. The camera's
// lens blurs it like everything at infinity: a blur disc of Lens.x radians (aperture / focus distance, MPC
// Lens from the controller), read from the panorama's mips (a texel is 90 deg / $(FACE_SIZE)).
//
// Inputs: Sky(TextureCube) Exposure(float) YawDeg(float) Lens(float4) CamW(float4, MPC CamPos: camera world
//         position) GroundZ(float, the stage ground's height)
// =====================================================================================
float3 PrD = GetTranslatedWorldPosition(Parameters) - ResolvedView.TranslatedWorldCameraOrigin;
PrD.z += CamW.z - GroundZ;
PrD = normalize(PrD);
float PrS;
float PrC;
sincos(radians(YawDeg), PrS, PrC);
PrD.xy = float2(PrC * PrD.x - PrS * PrD.y, PrS * PrD.x + PrC * PrD.y);
float PrMip = log2(max(0.5 * Lens.x / (1.5707963 / $(FACE_SIZE)), 1.0));
return Sky.SampleLevel(SkySampler, PrD, PrMip).rgb * Exposure;

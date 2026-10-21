// =====================================================================================
// Project Rain - night backdrop (material Custom node on the sky dome, returns float3 emissive).
//
// An HDR panorama of a city square at night, at infinity: looked up by view direction only, so it never
// shows parallax however the camera moves. Besides being the background, it is what the drops see: the
// environment cube they refract and reflect is captured from the stage with this dome in it, so every
// bead carries a small inverted image of the lit buildings, the lamps and the glowing overcast sky.
// Exposure scales the panorama into the stage's light levels; Yaw turns it about the vertical.
//
// Inputs: CamVec(float3, camera vector WS) Sky(TextureCube) Exposure(float) YawDeg(float)
// =====================================================================================
float3 PrD = -normalize(CamVec);
float PrS;
float PrC;
sincos(radians(YawDeg), PrS, PrC);
PrD.xy = float2(PrC * PrD.x - PrS * PrD.y, PrS * PrD.x + PrC * PrD.y);
return Sky.SampleLevel(SkySampler, PrD, 0).rgb * Exposure;

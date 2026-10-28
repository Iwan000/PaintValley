// =====================================================================================
// Project Rain - Paint Valley paint balls (material Custom node, unlit, Niagara meshes). Returns float3. A ball of
// the level's paint (the particle colour) in Monument Valley's flat light: lighter towards the top and the camera,
// a soft sheen round the rim and a small glint.
//
// Inputs: NormalWS (float3), ToCamera (float3, unit), PColor (float4)
// =====================================================================================
float3 PbN = normalize(NormalWS);
float PbLight = saturate(dot(PbN, normalize(float3(0.0, 0.0, 0.8) + 0.6 * ToCamera)));
float PbRim = pow(1.0 - saturate(dot(PbN, ToCamera)), 3.0);
float PbGlint = pow(saturate(dot(PbN, normalize(ToCamera + float3(0.0, 0.0, 1.1)))), 48.0);
float3 PbC = PColor.rgb * (0.6 + 0.4 * PbLight);
PbC = lerp(PbC, float3(1.0, 1.0, 1.0), 0.22 * PbRim + 0.7 * PbGlint);
return PbC;

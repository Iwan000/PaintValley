// =====================================================================================
// Project Rain - Paint Valley backdrop (material Custom node, unlit, on a plane kept behind everything in the
// orthographic view): Monument Valley's soft, low-contrast gradient. Returns the emissive colour (float3).
//
// A radial gradient from Center (middle of the screen) to Edge over Spread (in screen heights), plus one soft
// glow (GlowPos: screen x, y, radius in screen heights, amount), e.g. the warm corner of mv_2_09. Dithered
// against banding.
//
// Inputs: Center, Edge, Glow (float3), GlowPos, Shadow, ShadowSize, ShadowColor (float4), Spread (float)
// =====================================================================================
float2 BdUV = (Parameters.SvPosition.xy - ResolvedView.ViewRectMin.xy) * ResolvedView.ViewSizeAndInvSize.zw;
float BdAspect = ResolvedView.ViewSizeAndInvSize.x * ResolvedView.ViewSizeAndInvSize.w;
float2 BdD = (BdUV - 0.5) * float2(BdAspect, 1.0);
float3 BdC = lerp(Center, Edge, smoothstep(0.0, 1.0, length(BdD) / max(Spread, 0.01)));
float2 BdG = (BdUV - GlowPos.xy) * float2(BdAspect, 1.0);
BdC = lerp(BdC, Glow, exp(-dot(BdG, BdG) / max(GlowPos.z * GlowPos.z, 1e-4)) * GlowPos.w);
// The soft shadow under a floating island: Shadow.xyz is its centre relative to the camera (world, cm), Shadow.w
// unused, ShadowSize.xy its radii across the screen (cm); projected with the view itself, so it sits right at any
// zoom and aspect ratio. ShadowColor.a = amount.
float3 BdV = mul(Shadow.xyz, (float3x3)ResolvedView.TranslatedWorldToView);
float4 BdClip = mul(float4(BdV, 1.0), ResolvedView.ViewToClip);
float2 BdSC = BdClip.xy / BdClip.w * float2(0.5, -0.5) + 0.5;
float2 BdR = max(ShadowSize.xy * float2(ResolvedView.ViewToClip[0][0], ResolvedView.ViewToClip[1][1]) * 0.5, float2(1e-4, 1e-4));
float2 BdS = (BdUV - BdSC) / BdR;
BdC = lerp(BdC, ShadowColor.rgb, exp(-2.5 * dot(BdS, BdS)) * ShadowColor.a);
float BdN = frac(52.9829189 * frac(dot(Parameters.SvPosition.xy, float2(0.06711056, 0.00583715))));
return BdC * (1.0 + (BdN - 0.5) * 0.006);

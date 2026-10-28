// =====================================================================================
// Project Rain - Paint Valley drops, film look: where a drop looks (material Custom node, feeds Scene Color's
// offset). Returns float2: the offset, as a fraction of the screen, from this pixel to the point of the scene it
// shows. A drop of water is a ball lens: it shows what lies behind it upside down and shrunk, so a pixel at offset p
// from the drop's centre shows the scene at -LensSpread p from that centre. A falling drop is a capsule: the offset is
// from the nearest point of its axis (the image smeared along the motion, as on film).
//
// Inputs: UV (float2), DynY (width / length)
// =====================================================================================
float2 PlP = UV - 0.5;
float PlAsp = 1.0 / clamp(DynY, 0.01, 1.0);
float PlHalf = 0.5 * PlAsp - 0.5;
// From the axis, in UV units.
float PlY = PlP.y * PlAsp;
float2 PlOff = float2(PlP.x, (PlY - clamp(PlY, -PlHalf, PlHalf)) / PlAsp);
// UV per pixel along x and y: dUV = M dPixel, M = [ddx(UV) ddy(UV)]; the offset in pixels is M^-1 dUV.
float2 PlDx = ddx(UV);
float2 PlDy = ddy(UV);
float PlDet = PlDx.x * PlDy.y - PlDx.y * PlDy.x;
PlDet = abs(PlDet) > 1e-12 ? PlDet : 1e-12;
float2 PlPix = float2(PlDy.y * PlOff.x - PlDy.x * PlOff.y, -PlDx.y * PlOff.x + PlDx.x * PlOff.y) / PlDet;
const float LensSpread = 5.0;
return -(1.0 + LensSpread) * PlPix * View.ViewSizeAndInvSize.zw;

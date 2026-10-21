// =====================================================================================
// Project Rain - the rain field. The C++ builder puts this in front of every particle module
// (rain and splash); each module #undefs the PR_* macros at its end.
//
// The rain is an infinite, statistically uniform field, periodic in x and y with period PR_CELL.
// Only one period is simulated: a PR_CELL x PR_CELL window of rain over the ground (system Z) that
// always sits in front of the camera, with the camera PR_BACK inside its back edge. A drop that
// leaves the window re-enters through the opposite side, and in a periodic field that is the same
// drop, so the rain is seamless from any viewpoint and across camera cuts. The camera is always
// inside the rain: perspective alone gives the few big drops at the lens and the many small ones
// further away.
//
// The wrap stays invisible because every force on the drops is periodic too (fields centred on the
// performer use the nearest periodic image and vanish within half a period), and drops fade out
// with horizontal distance from the camera before they can reach the window's far or side edges.
// =====================================================================================
#define PR_NEXT(h) h = h * 747796405u + 2891336453u; h = ((h >> ((h >> 28u) + 4u)) ^ h) * 277803737u; h = (h >> 22u) ^ h;
#define PR_U01(h) (float(h & 0x00FFFFFFu) * (1.0 / 16777216.0))
#define PR_CELL $(CELL)
// Folds a world xy position into the window (any distance: a camera cut moves it many periods).
#define PR_FOLD(xy) (PrWinMin + PR_CELL * frac(((xy) - PrWinMin) / PR_CELL))
// The nearest periodic image of an xy offset, for forces centred on a point.
#define PR_NEAREST(d) ((d) - PR_CELL * round((d) / PR_CELL))
// Horizontal fade-out towards the window edges (1 near the camera, 0 at PrFarEnd).
#define PR_FAR_FADE(p) (1.0 - smoothstep(PrFarEnd - $(FAR_FADE), PrFarEnd, length((p).xy - PrCam.xy)))

// Window placement: centred across the view direction, the camera $(BACK) inside the edge behind it.
float3 PrCam = User.RainCameraPos;
float2 PrLook = User.RainCameraFwd.xy;
PrLook = dot(PrLook, PrLook) > 1e-4 ? normalize(PrLook) : float2(1.0, 0.0);
float2 PrWinMin = PrCam.xy - 0.5 * PR_CELL + PrLook * ((0.5 * PR_CELL - $(BACK)) / max(abs(PrLook.x), abs(PrLook.y)));
// Visible range: up to the far edge, and for wide lenses only as far as the frustum stays clear of the
// side edges (half a period away from the camera's axis).
float PrSinHalfFov = User.RainViewTan * rsqrt(1.0 + User.RainViewTan * User.RainViewTan);
float PrFarEnd = min(PR_CELL - $(BACK), 0.5 * PR_CELL / max(PrSinHalfFov, 0.1)) - 50.0;

// Project Rain - Paint Valley: the paintable surfaces of a level. A level is a set of axis-aligned boxes (the
// Monument Valley architecture: towers, walls, steps, merlons, a floating base). Every face a box shows gets its
// own rectangle of texels in one atlas texture, so the whole level is one procedural mesh and one texture:
//   R  paint (the puddles' metaball field over four times the paint level: 0.25 on the edge, written at run time)
//   G  ambient occlusion (baked when the level is built: rays against the boxes)
//   B  openings (doors, windows, portholes stamped onto faces: signed-distance mask, sharpened in the material)
//   A  edge light (thin highlight along the convex top edges)
// The material (Materials/HLSL/PaintSurface.hlsl) shades each face by its direction (Monument Valley's flat tones)
// and reads the atlas for AO, openings, edges and paint.
#pragma once

#include "CoreMinimal.h"
#include "PaintSurface.generated.h"

class UProceduralMeshComponent;
class UTexture2D;

UENUM(BlueprintType)
enum class EPaintBoxKind : uint8
{
	/** Floor, base and props: paint shows, but does not count. */
	Ground,
	/** The object to paint: its coverage is the score. */
	Target,
	/** Pre-placed obstacles the rain has to get around. */
	Blocker,
};

USTRUCT(BlueprintType)
struct FPaintBox
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") FVector Min = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") FVector Max = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") EPaintBoxKind Kind = EPaintBoxKind::Ground;
	/** Index into the level's palettes (one mesh section and material each). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") int32 Palette = 0;
	/** In the particles' collision set (a coarse subset: fine detail such as merlons is render-and-paint only). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") bool bCollide = true;
	/** Solid for AO, burial and collision, but draws no faces of its own (the solid parts of an arch). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") bool bProxy = false;
};

/** A box with a round or pointed arch through it: a gate, a vaulted passage. The front and back faces have the
 *  arch cut out, the soffit is a ring of flat strips, all painted like box faces. */
USTRUCT(BlueprintType)
struct FPaintArch
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") FVector Min = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") FVector Max = FVector::ZeroVector;
	/** The axis the passage runs along (0 = X, 1 = Y). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") int32 Axis = 1;
	/** Centre of the opening across the passage, from Min (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") float Center = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") float Width = 30.0f;
	/** Height of the straight jambs below the arch. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") float Spring = 30.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") bool bPointed = false;
	/** Flat strips of the soffit (even). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") int32 Segments = 12;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") EPaintBoxKind Kind = EPaintBoxKind::Ground;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") int32 Palette = 0;
};

/** A wavy, tapering pennant (decorative: flat facets, not painted, not collided). */
USTRUCT(BlueprintType)
struct FPaintRibbon
{
	GENERATED_BODY()

	/** Where it is tied to the pole (middle of its first edge); it streams along +Direction. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") FVector Start = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") FVector Direction = FVector(1.0, 0.0, 0.0);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") float Length = 60.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") float Height = 12.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") float Waves = 1.5f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") float Amplitude = 5.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") int32 Palette = 0;
};

/** A door, window or porthole stamped onto the face of a box (atlas B). */
USTRUCT(BlueprintType)
struct FPaintOpening
{
	GENERATED_BODY()

	/** Bottom centre of the opening, on the face. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") FVector At = FVector::ZeroVector;
	/** The face's outward normal (an axis). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") FVector Normal = FVector(-1.0, 0.0, 0.0);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") float Width = 12.0f;
	/** Height of the straight part below the arch (Round: ignored, a circle of diameter Width). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") float Height = 18.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") bool bRound = false;
	/** A pointed (equilateral, gothic) arch instead of a round one. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") bool bPointed = false;
};

/** A decorative cone (a spire) or a dome: rendered with flat facets, not painted, not collided. */
USTRUCT(BlueprintType)
struct FPaintSpire
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") FVector Base = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") float Radius = 15.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") float Height = 40.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") int32 Sides = 8;
	/** 0: a cone; > 0: an onion dome bulging out to Radius * (1 + Bulge) a third of the way up. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") float Bulge = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") int32 Palette = 0;
};

/** The texel grid, the atlas and the mesh of a level's boxes. */
class PROJECTRAIN_API FPaintSurface
{
public:
	/** Texel size on the surfaces (cm). */
	float TexelSize = 2.0f;
	/** Ambient occlusion: a broad, faint term (Monument Valley's soft gradients) and a short contact term (the
	 *  darkening where volumes meet): ray lengths (cm) and strengths. */
	float AORadius = 70.0f;
	float AOStrength = 0.35f;
	float ContactRadius = 14.0f;
	float ContactStrength = 0.8f;
	/** Width of the edge light along convex top edges (cm), only on faces at least EdgeMinFace wide both ways. */
	float EdgeWidth = 1.4f;
	float EdgeMinFace = 20.0f;
	/** Openings are stored as a signed distance over +-OpeningRange cm (atlas B = 0.5 - d / (2 OpeningRange)): the
	 *  material draws rims and rings at set depths inside the outline. */
	static constexpr float OpeningRange = 16.0f;

	/** Lays out the faces, bakes AO, openings and edges, and creates the atlas texture (owned by Outer, which keeps
	 *  it referenced). Boxes and openings in the mesh component's space. Returns the number of texels. */
	int32 Build(const TArray<FPaintBox>& Boxes, const TArray<FPaintArch>& Arches, const TArray<FPaintOpening>& Openings, UObject* Outer);
	/** Section p: the faces of palette p's boxes and arches. Section NumPalettes + p: the spires and ribbons of
	 *  palette p (no atlas). */
	void BuildMesh(UProceduralMeshComponent* Mesh, int32 NumPalettes, const TArray<FPaintSpire>& Spires, const TArray<FPaintRibbon>& Ribbons) const;

	/** A drop lands (mesh-component space): a puddle that spreads from At to Radius (cm) over about 3 * SpreadTime, on
	 *  the faces around it that are not turned away from the incoming direction Dir. Puddles are metaballs: each adds
	 *  a smooth bump, (1 - (d / R)^2)^3, to the paint field, and the paint shows where the field reaches PaintLevel.
	 *  One alone is a circle of Radius; two close ones flow together through a neck before they touch. */
	void AddPuddle(const FVector& At, float Radius, const FVector& Dir);
	/** A small drop's worth of paint (Area cm^2) lands at At. On paint (the paint shows at the nearest texel) it feeds
	 *  the body of paint it landed in and returns true; on bare surface it does nothing and returns false (the drop is
	 *  not used up). A body is all the paint joined together on one face (a body never crosses an edge): fed, its
	 *  whole outline moves out by the same distance, Area over its perimeter, round holes and far gaps alike. */
	bool Feed(const FVector& At, float Area);
	/** Applies the fed growth (every FeedPeriod s), spreads the puddles still growing and moves the fed outlines. */
	void Tick(float DeltaSeconds);
	/** Clears the paint (keeps the bake). */
	void ClearPaint();
	int32 NumGrowing() const { return Growing.Num(); }
	/** Nothing fed or spreading is still to show (the coverage will not change by itself). */
	bool IsSettled() const;
	int32 NumPuddles() const { return Puddles.Num(); }

	/** Seconds for a puddle to cover 63 % of the way to its size. */
	float SpreadTime = 0.35f;
	/** Fed growth is applied in batches every FeedPeriod s. Behind a body's moving outline the paint deepens over
	 *  GrowRamp cm. */
	float FeedPeriod = 0.15f;
	float GrowRamp = 2.0f;
	/** A face counts as full when this share of its scored texels is painted (corners in shadow may never be). */
	float FullShare = 0.975f;

	/** Indices of the boxes in the particles' collision set (as CollisionBoxes lists them). */
	TArray<int32> CollisionBoxIndices() const;
	/** One bit per face of a box that takes no more paint: +X 1, -X 2, +Y 4, -Y 8, +Z 16, -Z 32. A face is full when
	 *  FullShare of its scored texels are painted, or when it has none (buried, facing down, or not the target's). */
	int32 FullFaces(int32 Box) const;
	/** For the demo's player: the target's face (not full, not in Skip) with the most bare scored texels, or INDEX_NONE;
	 *  a face turned away from the view (View: the camera's forward vector) counts HiddenWeight times its bare texels.
	 *  Samples: up to MaxSamples of its bare texels, spread over it; Normal: its outward normal (mesh-component space). */
	int32 BarestFace(const TSet<int32>& Skip, const FVector& View, float HiddenWeight, TArray<FVector>& Samples, FVector& Normal, int32 MaxSamples = 9) const;
	/** Painted scored texels of a face (BarestFace's index). */
	int32 FacePainted(int32 FaceIndex) const { return Faces.IsValidIndex(FaceIndex) ? Faces[FaceIndex].Painted : 0; }
	/** Whether a ray from At along Dir (a unit vector) leaves the level without meeting a box of the collision set. */
	bool IsOpen(const FVector& At, const FVector& Dir) const;
	/** Painted share of the target's visible surface (area-weighted, 0..1). */
	float TargetCoverage() const { return TargetArea > 0.0 ? float(TargetPainted / TargetArea) : 0.0f; }
	float TargetAreaCm2() const { return float(TargetArea); }
	/** Uploads the tiles that changed since the last call (one texture region per tile). */
	void Flush();

	/** The boxes in the particles' collision set (the level's and the arches' solid parts), mesh-component space. */
	TArray<FPaintBox> CollisionBoxes() const { return Boxes.FilterByPredicate([](const FPaintBox& B) { return B.bCollide; }); }
	/** Bounds of every box. */
	FBox Bounds() const;

	UTexture2D* GetAtlas() const { return Atlas; }
	int32 GetAtlasWidth() const { return AtlasW; }
	int32 GetAtlasHeight() const { return AtlasH; }
	int32 NumFaces() const { return Faces.Num(); }
	int32 NumTexels() const { return Texels.Num(); }
	int32 NumVisibleTexels() const { return VisibleTexels; }

private:
	/** An arch cut out of a face, in the face's own coordinates (cm along U and V). */
	struct FHole
	{
		bool bValid = false;
		double Cu = 0.0;
		double R = 0.0;
		double Spring = 0.0;
		bool bPointed = false;
		bool Contains(double U, double V) const;
	};
	/** Paint joined together on one face. Its outline is GrowNow cm out from where it was when the face was last
	 *  regrouped, on its way to GrowTo; Perimeter (cm) as of then; Rep one of its texels (to find it again). Fed is the
	 *  area it was given, Gained what its outline has painted so far: an outline held back (by another body's ground,
	 *  the face's edge) leaves the rest owed, given back at the next regrouping. */
	struct FBody
	{
		float GrowNow = 0.0f;
		float GrowTo = 0.0f;
		float Perimeter = 0.0f;
		float Fed = 0.0f;
		float Gained = 0.0f;
		int32 Rep = INDEX_NONE;
	};
	struct FFace
	{
		/** The box that gives the face its palette and kind; Skip: the box left out of its burial and AO tests. */
		int32 Box = 0;
		int32 Skip = INDEX_NONE;
		/** Corner, the two axes along the face and the outward normal. */
		FVector Origin = FVector::ZeroVector;
		FVector U = FVector::ZeroVector;
		FVector V = FVector::ZeroVector;
		FVector N = FVector::ZeroVector;
		float W = 0.0f;
		float H = 0.0f;
		int32 NU = 0;
		int32 NV = 0;
		/** Interior texel (0, 0) in the atlas. */
		int32 AX = 0;
		int32 AY = 0;
		/** First texel in Texels. */
		int32 First = 0;
		int32 Visible = 0;
		FHole Hole;
		/** Visible scored texels, and how many of them are painted. */
		int32 Scored = 0;
		int32 Painted = 0;
		/** Triangles in the face's own coordinates (cm), when it is not the whole rectangle (a face with an arch cut out). */
		TArray<FVector2D> Poly;
		/** Its bodies of paint as of the last regrouping, the drops fed to them since, and whether an outline moves. */
		TArray<FBody> Bodies;
		TArray<TPair<int32, float>> Feeds;
		bool bGrowing = false;
		/** An outline stopped short of what it was fed: regroup to give the rest. */
		bool bOwed = false;
	};
	struct FTexel
	{
		FVector3f P = FVector3f::ZeroVector;
		int32 Face = 0;
		bool bVisible = false;
		/** Takes paint (the target's faces: other boxes repel it). */
		bool bPaintable = false;
		/** Counts towards the coverage (a paintable texel that can be seen: not facing down). */
		bool bTarget = false;
		float Area = 0.0f;
	};

	bool Inside(const FVector& P, int32 Skip = INDEX_NONE) const;
	/** Adds a face and its texels; drops it again if no texel shows. */
	void AddFace(FFace F);
	/** First pass (bFaces false): the arch's solid parts as proxy boxes, Owner = the first. Second pass: its faces. */
	void AddArch(const FPaintArch& A, bool bFaces, int32& Owner);
	static TArray<FVector2D> ArcPoints(double Cu, double R, double Spring, bool bPointed, int32 Segments);
	float RayHit(const FVector& From, const FVector& Dir, float MaxT, const TArray<int32>& Candidates) const;
	void BakeFace(int32 FaceIndex, const TArray<FPaintOpening>& Openings);
	void WriteTile(int32 FaceIndex);

	FIntVector HashCell(const FVector& P) const;
	/** The paintable visible texel nearest At within 1.5 texels, or INDEX_NONE. */
	int32 TexelAt(const FVector& At) const;
	/** Shows texel t's paint (the larger of its field and its body's growth): counts it in when it reaches the paint
	 *  level, writes the atlas value. Returns whether it has just started to show paint. */
	bool Show(int32 t);
	/** Bakes face f's growth into the field, finds its bodies (joined paint), measures their perimeters and how far
	 *  every bare texel lies from each one's outline (with the outline placed between texels), then gives each body
	 *  its unapplied growth and the drops fed to it. */
	void Regroup(int32 FaceIndex);
	/** Moves face f's outlines towards their growth and shows the texels they pass. */
	void GrowFace(int32 FaceIndex, float Step);
	/** Adds the bump of a puddle grown from radius From to To (kernel radii), times Height, to the field around At. */
	void Spread(const FVector& At, const FVector& In, float From, float To, float Height = 1.0f);

	struct FPuddle
	{
		FVector At = FVector::ZeroVector;
		FVector In = FVector::ZeroVector;
		/** Kernel radius it is growing to, the radius on its way there, and the radius stamped into the field. */
		float Full = 0.0f;
		float Shown = 0.0f;
		float Now = 0.0f;
	};
	TArray<FPuddle> Puddles;
	/** Puddles still spreading (indices). */
	TArray<int32> Growing;
	float FeedClock = 0.0f;
	/** The paint field per texel (sum of the puddles' bumps, and the bodies' growth once baked); atlas R = paint /
	 *  (4 PaintLevel). */
	TArray<float> Field;
	/** Per texel: the growth of the body it belongs to or lies outside of (paint units), its distance (cm) from that
	 *  body's outline as of the last regrouping (bare texels), that body (its index in the face's Bodies), whether it
	 *  shows paint. */
	TArray<float> Grow;
	TArray<float> Reach;
	TArray<int32> BodyOf;
	TArray<bool> Painted;
	static constexpr float PaintLevel = 0.125f;
	/** Paint inside a body is at least this many times the paint level (well clear of the edge's noise). */
	static constexpr float SolidLevel = 2.0f;
	/** A lone puddle's edge lies at this share of its kernel radius: sqrt(1 - PaintLevel^(1/3)). */
	static constexpr float EdgeShare = 0.7071f;

	TArray<FPaintBox> Boxes;
	/** Visible texels by hash cell (HashSize cm). */
	TMap<FIntVector, TArray<int32>> Hash;
	static constexpr float HashSize = 8.0f;
	TSet<int32> DirtyFaces;
	double TargetArea = 0.0;
	double TargetPainted = 0.0;
	TArray<FFace> Faces;
	TArray<FTexel> Texels;
	/** Per texel: paint, AO, opening, edge (0..255). */
	TArray<FColor> Values;
	/** The atlas pixels (BGRA8): interior texels plus a gutter around every tile. */
	TArray<FColor> Pixels;
	int32 AtlasW = 0;
	int32 AtlasH = 0;
	int32 VisibleTexels = 0;
	static constexpr int32 Gutter = 2;
	UTexture2D* Atlas = nullptr;
};

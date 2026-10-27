// Project Rain - Paint Valley: the paintable surfaces of a level. A level is a set of axis-aligned boxes (the
// Monument Valley architecture: towers, walls, steps, merlons, a floating base). Every face a box shows gets its
// own rectangle of texels in one atlas texture, so the whole level is one procedural mesh and one texture:
//   R  paint (0..1, written by splats at run time)
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

	/** Paints a splat (mesh-component space): every visible texel within Radius of At (a blobby outline, its own
	 *  shape per Seed), on faces not turned away from the incoming direction Dir. Returns the texels newly painted. */
	int32 Splat(const FVector& At, float Radius, const FVector& Dir, uint32 Seed);
	/** Clears the paint (keeps the bake). */
	void ClearPaint();
	/** Painted share of the target's visible surface (area-weighted, 0..1). */
	float TargetCoverage() const { return TargetArea > 0.0 ? float(TargetPainted / TargetArea) : 0.0f; }
	float TargetAreaCm2() const { return float(TargetArea); }
	/** Uploads the tiles that changed since the last call (one texture region per tile). */
	void Flush();

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
		/** Triangles in the face's own coordinates (cm), when it is not the whole rectangle (a face with an arch cut out). */
		TArray<FVector2D> Poly;
	};
	struct FTexel
	{
		FVector3f P = FVector3f::ZeroVector;
		int32 Face = 0;
		bool bVisible = false;
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

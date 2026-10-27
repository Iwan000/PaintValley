// Project Rain - Paint Valley: the rain paints a Monument Valley structure. A floating island, the object to
// paint and pre-placed blockers are axis-aligned boxes (FPaintSurface: one procedural mesh, one paint atlas),
// seen through an orthographic isometric camera in front of a soft gradient backdrop. Step 1 of the rework:
// the look (the paint, the rain and the round come next).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PaintSurface.h"
#include "RainPaintGame.generated.h"

class ACameraActor;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UMaterialParameterCollection;
class UProceduralMeshComponent;
class UStaticMeshComponent;
class UTexture2D;

/** Monument Valley tones of one material (sRGB colours, as sampled from the references). */
USTRUCT(BlueprintType)
struct FPaintPalette
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") FString Name;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") FColor Top = FColor::White;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") FColor Light = FColor::White;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") FColor Dark = FColor::White;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") FColor Bottom = FColor::White;
	/** The colour (sRGB) the faces blend towards at the top of the level's gradient range, by GradAmount. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") FColor Grad = FColor::White;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") float GradAmount = 0.0f;
	/** Faint floor tiles on tops below TileMaxZ (actor space): size and line width (cm), darkening (0 = none). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") float TileSize = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") float TileLine = 1.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") float TileDarken = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") float TileMaxZ = 5.0f;
	/** Tint of the occluded corners (sRGB). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") FColor AO = FColor(128, 110, 120);
	/** Openings: a rim band inside the outline (width in cm), then the hole: its far end (Opening) and the side it
	 *  shows where the far end is moved OpeningShift cm up and to the left (a porthole's barrel, a window's reveal). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") FColor Opening = FColor(0x60, 0x48, 0x38);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") FColor OpeningRim = FColor(0x60, 0x48, 0x38);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") float OpeningRimWidth = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") FColor OpeningBarrel = FColor(0x60, 0x48, 0x38);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") float OpeningShift = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") FColor Edge = FColor::White;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") float EdgeAmount = 0.0f;
};

USTRUCT(BlueprintType)
struct FPaintLevel
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") FString Name;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") TArray<FPaintPalette> Palettes;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") TArray<FPaintBox> Boxes;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") TArray<FPaintOpening> Openings;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") TArray<FPaintArch> Arches;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") TArray<FPaintSpire> Spires;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") TArray<FPaintRibbon> Ribbons;
	/** Height range of the palettes' gradient (cm, actor space). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") FVector2D GradRange = FVector2D(0.0, 250.0);
	/** Below Fade.Y the structure fades towards the backdrop (fully at Fade.X, times FadeAmount). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") FVector2D FadeRange = FVector2D(-120.0, -40.0);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") float FadeAmount = 0.0f;
	/** Backdrop: centre and edge of the radial gradient, a soft glow (screen x, y, radius, amount). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") FColor BackCenter = FColor(0xff, 0xe9, 0xdc);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") FColor BackEdge = FColor(0xfd, 0xe2, 0xd2);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") FColor BackGlow = FColor(0xf6, 0xba, 0x95);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") FVector4 BackGlowPos = FVector4(0.12, 1.0, 0.45, 0.9);
	/** The soft shadow under the floating island on the backdrop: it hangs under the island's bottom corner nearest
	 *  the camera (ShadowAt: the bottom's centre, actor space; ShadowHalf: its half-size), ShadowSize its radii (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") FVector ShadowAt = FVector(0.0, 0.0, -110.0);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") FVector2D ShadowHalf = FVector2D(0.0, 0.0);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") FVector2D ShadowSize = FVector2D(200.0, 45.0);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") FColor ShadowColor = FColor(0xf4, 0xb0, 0x8c);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") float ShadowAmount = 0.0f;
	/** Point the camera looks at (actor space) and the view's height (cm; the width follows the window). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") FVector ViewTarget = FVector(0.0, 0.0, 70.0);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") float ViewHeight = 700.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") FColor Ink = FColor(0x6a, 0x3c, 0xff);
};

UCLASS()
class PROJECTRAIN_API ARainPaintGame : public AActor
{
	GENERATED_BODY()

public:
	ARainPaintGame();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY(VisibleAnywhere, Category = "Paint") TObjectPtr<UProceduralMeshComponent> Surfaces;
	UPROPERTY(EditAnywhere, Category = "Paint") TObjectPtr<UMaterialInterface> SurfaceMaterial;
	UPROPERTY(EditAnywhere, Category = "Paint") TObjectPtr<UMaterialInterface> BackdropMaterial;
	UPROPERTY(EditAnywhere, Category = "Paint") TObjectPtr<UMaterialParameterCollection> Collection;
	UPROPERTY(EditAnywhere, Category = "Paint") TArray<FPaintLevel> Levels;

	/** Isometric view: the camera's pitch (true isometric: -35.26) and yaw (45 + 90 k, turned with Q / E). */
	UPROPERTY(EditAnywhere, Category = "Paint") float ViewPitch = -35.264f;
	UPROPERTY(EditAnywhere, Category = "Paint") float ViewYaw = 45.0f;

	UFUNCTION(BlueprintCallable, Category = "Paint") void StartLevel(int32 Index);
	/** Sets the view at once (Python, screenshots): yaw in degrees, height in cm (0 keeps the level's). */
	UFUNCTION(BlueprintCallable, Category = "Paint") void SetView(float Yaw, float Height = 0.0f);
	UFUNCTION(BlueprintPure, Category = "Paint") FString Describe() const;

private:
	void BuildLevel();
	void UpdateCamera();
	void HandleInput(float DeltaSeconds);

	// -PaintMeasure: settle 4 s, then average stat unit's raw times over 5 s, log them and quit.
	void TickMeasure(float DeltaSeconds);
	float MeasureClock = -1.0f;
	int32 MeasureFrames = 0;
	double MeasureFrameMs = 0.0, MeasureGameMs = 0.0, MeasureRenderMs = 0.0, MeasureGpuMs = 0.0;

	/** r.AntiAliasingMethod before the game set FXAA (restored at EndPlay: the film scene keeps its own). */
	int32 PreviousAntiAliasing = -1;

	int32 LevelIndex = 0;
	FPaintSurface Surface;
	float TargetYaw = 45.0f;
	float ViewHeight = 700.0f;
	float ViewAspect = 0.0f;
	double BuildSeconds = 0.0;

	UPROPERTY(Transient) TObjectPtr<ACameraActor> Camera;
	UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> Backdrop;
	UPROPERTY(Transient) TObjectPtr<UTexture2D> Atlas;
	UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInstanceDynamic>> Materials;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> BackdropMID;
};

// Project Rain - editor entry points (callable from Python: unreal.RainBuilderLibrary.*)
#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "RainBuilderLibrary.generated.h"

UCLASS()
class PROJECTRAINEDITOR_API URainBuilderLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	/** Minimal end-to-end check: custom HLSL module -> GPU emitter -> compiled system. Returns the compile report. */
	UFUNCTION(BlueprintCallable, Category = "Project Rain")
	static FString BuildTestSystem();

	/** Builds MPC_Rain, RT_RainEnv, M_RainDrop, the HLSL modules and NS_RainMagic: one rain emitter of RainCount
	 *  drops, StageCount of them falling around the stage (rain towers: big drops, densest at the centre), the
	 *  rest filling a 16 m x 16 m x 6 m window of an infinite periodic rain field that follows the camera
	 *  (170k drops = 110 drops/m^3), plus splashes. Returns the compile report. */
	UFUNCTION(BlueprintCallable, Category = "Project Rain")
	static FString BuildRainAssets(int32 RainCount = 230000, int32 SplashCount = 15000, int32 StageCount = 60000);

	/** Builds NS_RainGame, the Rain Shield game's rain (System 1: GPU sprites): RainCount drops by default
	 *  (User.RainGameCount, changed at runtime by rain.game.Count), falling in the player's direction through
	 *  the play grid's boxes, hits read back with Export Particle Data. Needs BuildRainAssets first (M_RainDrop). */
	UFUNCTION(BlueprintCallable, Category = "Project Rain")
	static FString BuildRainGameAssets(int32 RainCount = 200000);

	/** Builds M_RainBasic (parameter material for stage props), M_WetGround (puddles + ripples) and
	 *  M_NightSky (the night backdrop; needs Scripts/import_backdrop.py first). */
	UFUNCTION(BlueprintCallable, Category = "Project Rain")
	static FString BuildStageMaterials();

	/** Diagnostics: lists every script of a system with its usage, sim target and rapid-iteration parameters. */
	UFUNCTION(BlueprintCallable, Category = "Project Rain")
	static FString DescribeNiagaraSystem(const FString& ObjectPath);

	/** Writes the generated HLSL (CPU + GPU) of every emitter script to Saved/NiagaraHlsl/<System>_<Emitter>_<usage>.hlsl. */
	UFUNCTION(BlueprintCallable, Category = "Project Rain")
	static FString DumpGeneratedHlsl(const FString& ObjectPath);

	/** Builds a single-emitter diagnostic system /Game/RainMagic/Diag/NS_<Name> from the given pieces (bisecting tool). */
	UFUNCTION(BlueprintCallable, Category = "Project Rain")
	static FString BuildDiagSystem(const FString& Name, const FString& SpawnHlsl, const FString& UpdateHlsl,
		bool bDefaultModules, bool bUseBurst, int32 Count, bool bGpu, bool bDeclareRainAttributes, bool bUseRainMaterial);
};

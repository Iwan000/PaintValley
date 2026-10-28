// Project Rain - builds every generated asset: material parameter collection, environment cube
// render target, the raindrop material, Niagara HLSL modules and the NS_RainMagic system.
#include "RainBuilderLibrary.h"
#include "RainNiagaraAuthoring.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/TextureCube.h"
#include "Engine/TextureRenderTargetCube.h"
#include "MaterialEditingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionCameraVectorWS.h"
#include "Materials/MaterialExpressionCollectionParameter.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionDynamicParameter.h"
#include "Materials/MaterialExpressionParticleColor.h"
#include "Materials/MaterialExpressionTextureBase.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionTextureObjectParameter.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionSceneColor.h"
#include "Materials/MaterialExpressionTime.h"
#include "Materials/MaterialExpressionVertexColor.h"
#include "Materials/MaterialExpressionLocalPosition.h"
#include "Materials/MaterialExpressionVertexNormalWS.h"
#include "Materials/MaterialParameterCollection.h"
#include "NiagaraEditorUtilities.h"
#include "NiagaraEmitter.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraScript.h"
#include "NiagaraSpriteRendererProperties.h"
#include "NiagaraMeshRendererProperties.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "NiagaraSystem.h"
#include "NiagaraActor.h"
#include "NiagaraDataInterfaceExport.h"
#include "NiagaraGraph.h"
#include "NiagaraScriptSource.h"
#include "RainShieldTypes.h"
#include "NiagaraComponent.h"
#include "EngineUtils.h"
#include "Editor.h"
#include "ShaderCompiler.h"
#include "MaterialShared.h"

using namespace RainAuthoring;

namespace
{
	const FString RainRoot = TEXT("/Game/RainMagic");

	constexpr int32 ParticleModuleMask =
		(1 << int32(ENiagaraScriptUsage::ParticleSpawnScript)) |
		(1 << int32(ENiagaraScriptUsage::ParticleSpawnScriptInterpolated)) |
		(1 << int32(ENiagaraScriptUsage::ParticleUpdateScript)) |
		(1 << int32(ENiagaraScriptUsage::ParticleSimulationStageScript));

	constexpr int32 NumStageLights = 6;

	// The rain field (Niagara/HLSL/RainField.hlsl): one 16 m x 16 m period of an infinite periodic rain,
	// 6 m deep, kept in front of the camera with the camera 1.5 m inside its back edge. Drops are visible
	// out to ~14 m and fade out over the last 6 m. Lifted drops are parked at 24 m. The stage rain (rain
	// towers over the performance area) thins out as a Gaussian of sigma 2.5 m around the stage centre,
	// cut at 6 m (inside half a period).
	struct FRainField
	{
		float Cell = 1600.0f;
		float Back = 150.0f;
		float Height = 600.0f;
		float FarFade = 600.0f;
		float ParkHeight = 2400.0f;
		float StageSigma = 250.0f;
		float StageRadius = 600.0f;
	};
	const FRainField RainField;

	// A particle module's HLSL with the rain-field prelude in front of it and the field's constants filled in.
	// Tail: a file appended as is (RainLook.hlsl: the drops' shape, streak, LOD, lens and lamps, shared by the
	// film rain and the game rain).
	FString LoadParticleModule(const TCHAR* File, const TCHAR* Tail = nullptr)
	{
		FString Text = LoadTextFile(TEXT("Niagara/HLSL/RainField.hlsl")) + TEXT("\n") + LoadTextFile(File);
		if (Tail)
		{
			Text += LoadTextFile(Tail);
		}
		auto F = [](float V) { return FString::Printf(TEXT("%.4f"), V); };
		Text.ReplaceInline(TEXT("$(CELL)"), *F(RainField.Cell));
		Text.ReplaceInline(TEXT("$(BACK)"), *F(RainField.Back));
		Text.ReplaceInline(TEXT("$(HEIGHT)"), *F(RainField.Height));
		Text.ReplaceInline(TEXT("$(FAR_FADE)"), *F(RainField.FarFade));
		Text.ReplaceInline(TEXT("$(PARK_HEIGHT)"), *F(RainField.ParkHeight));
		Text.ReplaceInline(TEXT("$(STAGE_SIGMA)"), *F(RainField.StageSigma));
		Text.ReplaceInline(TEXT("$(STAGE_RADIUS)"), *F(RainField.StageRadius));
		return Text;
	}

	// Rebuilds a material in place (keeps the UObject so instances and renderers stay valid).
	UMaterial* GetOrCreateMaterial(const FString& Path, const FString& Name)
	{
		UMaterial* M = LoadObject<UMaterial>(nullptr, *(Path / Name + TEXT(".") + Name), nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (M)
		{
			UMaterialEditingLibrary::DeleteAllMaterialExpressions(M);
			M->GetEditorOnlyData()->EmissiveColor.Expression = nullptr;
			M->GetEditorOnlyData()->Opacity.Expression = nullptr;
			M->GetEditorOnlyData()->BaseColor.Expression = nullptr;
			M->GetEditorOnlyData()->Normal.Expression = nullptr;
			M->GetEditorOnlyData()->Roughness.Expression = nullptr;
			M->GetEditorOnlyData()->Metallic.Expression = nullptr;
			return M;
		}
		UPackage* Package = CreatePackage(*(Path / Name));
		M = NewObject<UMaterial>(Package, *Name, RF_Public | RF_Standalone | RF_Transactional);
		FAssetRegistryModule::AssetCreated(M);
		return M;
	}

	bool CompileMaterialAndReport(UMaterial* M, FString& Report)
	{
		M->PreEditChange(nullptr);
		M->PostEditChange();
		UMaterialEditingLibrary::RecompileMaterial(M);
		if (GShaderCompilingManager)
		{
			GShaderCompilingManager->FinishAllCompilation();
		}
		bool bOk = true;
		if (const FMaterialResource* Res = M->GetMaterialResource(GMaxRHIShaderPlatform))
		{
			for (const FString& Err : Res->GetCompileErrors())
			{
				Report += FString::Printf(TEXT("  MATERIAL ERROR %s: %s\n"), *M->GetName(), *Err);
				bOk = false;
			}
		}
		Report += FString::Printf(TEXT("Material %s %s\n"), *M->GetPathName(), bOk ? TEXT("compiled") : TEXT("HAS ERRORS"));
		SaveAsset(M);
		return bOk;
	}

	// ------------------------------------------------------------------ material parameter collection
	UMaterialParameterCollection* BuildCollection()
	{
		// Created once; later builds only add parameters that are missing (instances keep their GUIDs).
		UMaterialParameterCollection* MPC = LoadObject<UMaterialParameterCollection>(nullptr, *(RainRoot / TEXT("Materials/MPC_Rain.MPC_Rain")), nullptr, LOAD_NoWarn | LOAD_Quiet);
		const bool bCreated = MPC == nullptr;
		if (bCreated)
		{
			UPackage* Package = CreatePackage(*(RainRoot / TEXT("Materials/MPC_Rain")));
			MPC = NewObject<UMaterialParameterCollection>(Package, TEXT("MPC_Rain"), RF_Public | RF_Standalone | RF_Transactional);
		}
		bool bChanged = bCreated;
		auto AddVector = [MPC, &bChanged](const TCHAR* Name, const FLinearColor& Default)
		{
			if (MPC->GetVectorParameterByName(Name))
			{
				return;
			}
			FCollectionVectorParameter P;
			P.ParameterName = Name;
			P.DefaultValue = Default;
			MPC->VectorParameters.Add(P);
			bChanged = true;
		};
		auto AddScalar = [MPC, &bChanged](const TCHAR* Name, float Default)
		{
			if (MPC->GetScalarParameterByName(Name))
			{
				return;
			}
			FCollectionScalarParameter P;
			P.ParameterName = Name;
			P.DefaultValue = Default;
			MPC->ScalarParameters.Add(P);
			bChanged = true;
		};
		AddVector(TEXT("CamPos"), FLinearColor(0, -800, 170, 0));
		// x env gain, y glint gain, z falling-streak gain, w exposure
		AddVector(TEXT("Look"), FLinearColor(1.0f, 1.0f, 4.0f, 1.0f));
		for (int32 i = 0; i < NumStageLights; ++i)
		{
			AddVector(*FString::Printf(TEXT("L%dP"), i), FLinearColor(0, 0, -100000, 50));
			// rgb: colour * intensity, a: cos of the spot's inner cone (-1 for a point lamp)
			AddVector(*FString::Printf(TEXT("L%dC"), i), FLinearColor(0, 0, 0, -1));
			// rgb: spot axis, a: cos of the outer cone (-2 for a point lamp: no cone)
			AddVector(*FString::Printf(TEXT("L%dD"), i), FLinearColor(0, 0, 0, -2));
		}
		// The camera's lens for the opaque stage (the drops are blurred in Niagara): x blur disc of things at
		// infinity (radians, aperture / focus distance), y focus distance (cm), z pixels per radian.
		AddVector(TEXT("Lens"), FLinearColor(0.0f, 650.0f, 2700.0f, 0.0f));
		// Radiance of the night sky's glow above the horizon, seen in the drops' ball lenses.
		AddVector(TEXT("SkyGlow"), FLinearColor(0.08f, 0.088f, 0.104f, 0.0f));
		AddScalar(TEXT("Wetness"), 1.0f);
		AddScalar(TEXT("RainIntensity"), 1.0f);
		if (bCreated)
		{
			FAssetRegistryModule::AssetCreated(MPC);
		}
		if (bChanged)
		{
			MPC->PostEditChange();
			SaveAsset(MPC);
		}
		return MPC;
	}

	// The drops' environment: a cube captured once from the stage (night backdrop, lamps, ground). With mips,
	// so small or defocused drops can read a blurred environment (Materials/HLSL/RainDrop.hlsl).
	UTextureRenderTargetCube* BuildEnvCube()
	{
		UTextureRenderTargetCube* RT = LoadObject<UTextureRenderTargetCube>(nullptr, *(RainRoot / TEXT("Materials/RT_RainEnv.RT_RainEnv")), nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (!RT)
		{
			UPackage* Package = CreatePackage(*(RainRoot / TEXT("Materials/RT_RainEnv")));
			RT = NewObject<UTextureRenderTargetCube>(Package, TEXT("RT_RainEnv"), RF_Public | RF_Standalone | RF_Transactional);
			FAssetRegistryModule::AssetCreated(RT);
		}
		RT->bHDR = true;
		RT->bAutoGenerateMips = true;
		RT->MipsSamplerFilter = TF_Bilinear;
		RT->ClearColor = FLinearColor(0.01f, 0.012f, 0.015f, 1.0f);
		RT->Init(512, PF_FloatRGBA);
		RT->UpdateResourceImmediate(true);
		SaveAsset(RT);
		return RT;
	}

	template<typename T>
	T* AddExpr(UMaterial* M, int32 X, int32 Y)
	{
		return Cast<T>(UMaterialEditingLibrary::CreateMaterialExpression(M, T::StaticClass(), X, Y));
	}

	UMaterial* BuildDropMaterial(UMaterialParameterCollection* MPC, UTextureRenderTargetCube* EnvCube, FString& Report)
	{
		UMaterial* M = GetOrCreateMaterial(RainRoot / TEXT("Materials"), TEXT("M_RainDrop"));
		M->MaterialDomain = MD_Surface;
		M->BlendMode = BLEND_Translucent;
		M->SetShadingModel(MSM_Unlit);
		M->TwoSided = true;
		M->bUsedWithNiagaraSprites = true;
		M->TranslucencyPass = MTP_AfterDOF;
		M->bEnableResponsiveAA = true;

		UMaterialExpressionCustom* Custom = AddExpr<UMaterialExpressionCustom>(M, -300, 0);
		Custom->Description = TEXT("RainDrop (Materials/HLSL/RainDrop.hlsl)");
		Custom->Code = LoadTextFile(TEXT("Materials/HLSL/RainDrop.hlsl"));
		Custom->OutputType = CMOT_Float4;
		Custom->Inputs.Reset();

		auto AddInput = [Custom](const TCHAR* Name, UMaterialExpression* From, int32 OutputIndex)
		{
			FCustomInput& In = Custom->Inputs.AddDefaulted_GetRef();
			In.InputName = Name;
			In.Input.Connect(OutputIndex, From);
		};

		int32 Y = -900;
		UMaterialExpressionTextureCoordinate* UV = AddExpr<UMaterialExpressionTextureCoordinate>(M, -900, Y += 80);
		AddInput(TEXT("UV"), UV, 0);

		UMaterialExpressionDynamicParameter* Dyn = AddExpr<UMaterialExpressionDynamicParameter>(M, -900, Y += 80);
		Dyn->ParamNames = { TEXT("Stretch"), TEXT("Path"), TEXT("Soft"), TEXT("Random") };
		AddInput(TEXT("DynX"), Dyn, 0);
		AddInput(TEXT("DynY"), Dyn, 1);
		AddInput(TEXT("DynZ"), Dyn, 2);
		AddInput(TEXT("DynW"), Dyn, 3);

		UMaterialExpressionDynamicParameter* DynOne = AddExpr<UMaterialExpressionDynamicParameter>(M, -900, Y += 160);
		DynOne->ParameterIndex = 1;
		DynOne->ParamNames = { TEXT("BeadRatio"), TEXT("Unused1"), TEXT("Unused2"), TEXT("Unused3") };
		AddInput(TEXT("DynOneX"), DynOne, 0);

		UMaterialExpressionParticleColor* PColor = AddExpr<UMaterialExpressionParticleColor>(M, -900, Y += 160);
		AddInput(TEXT("PCol"), PColor, 5); // RGBA (outputs: RGB, R, G, B, A, RGBA)

		UMaterialExpressionCameraVectorWS* CamVec = AddExpr<UMaterialExpressionCameraVectorWS>(M, -900, Y += 80);
		AddInput(TEXT("CamVec"), CamVec, 0);

		UMaterialExpressionTextureObjectParameter* Env = AddExpr<UMaterialExpressionTextureObjectParameter>(M, -900, Y += 80);
		Env->ParameterName = TEXT("EnvCube");
		Env->Texture = EnvCube;
		Env->SamplerType = SAMPLERTYPE_Color;
		AddInput(TEXT("Env"), Env, 0);

		auto AddCollectionInput = [&](const TCHAR* Name)
		{
			UMaterialExpressionCollectionParameter* P = AddExpr<UMaterialExpressionCollectionParameter>(M, -900, Y += 60);
			P->Collection = MPC;
			P->ParameterName = Name;
			P->ParameterId = MPC->GetParameterId(Name);
			AddInput(Name, P, 0);
		};
		AddCollectionInput(TEXT("CamPos"));
		Custom->Inputs.Last().InputName = TEXT("CamW");
		AddCollectionInput(TEXT("Look"));
		AddCollectionInput(TEXT("SkyGlow"));
		for (int32 i = 0; i < NumStageLights; ++i)
		{
			AddCollectionInput(*FString::Printf(TEXT("L%dP"), i));
		}
		for (int32 i = 0; i < NumStageLights; ++i)
		{
			AddCollectionInput(*FString::Printf(TEXT("L%dC"), i));
		}
		for (int32 i = 0; i < NumStageLights; ++i)
		{
			AddCollectionInput(*FString::Printf(TEXT("L%dD"), i));
		}

		UMaterialExpressionComponentMask* RGB = AddExpr<UMaterialExpressionComponentMask>(M, 0, -60);
		RGB->R = RGB->G = RGB->B = true;
		RGB->A = false;
		RGB->Input.Connect(0, Custom);
		UMaterialExpressionComponentMask* A = AddExpr<UMaterialExpressionComponentMask>(M, 0, 60);
		A->R = A->G = A->B = false;
		A->A = true;
		A->Input.Connect(0, Custom);

		UMaterialEditingLibrary::ConnectMaterialProperty(RGB, TEXT(""), MP_EmissiveColor);
		UMaterialEditingLibrary::ConnectMaterialProperty(A, TEXT(""), MP_Opacity);

		CompileMaterialAndReport(M, Report);
		return M;
	}

	// ------------------------------------------------------------------ Niagara
	// The drops: one size distribution (Marshall-Palmer, truncated) for the whole field, shifted to the big
	// drops of film rain machines (mean ~2 mm instead of ~1.3 mm for natural heavy rain).
	struct FRainDropConfig
	{
		float RainRate = 30.0f;
		float LambdaScale = 0.5f;
		float DMinMm = 1.0f;
		float DMaxMm = 6.0f;
		int32 Seed = 29;
		// Stage rain (rain machines): how many of the drops, and their bigger sizes (mean ~3.6 mm).
		int32 StageCount = 0;
		float StageLambdaScale = 0.3f;
		float StageDMinMm = 2.0f;
	};

	FString Fill(FString Text, const FRainDropConfig& C)
	{
		auto F = [](float V) { return FString::Printf(TEXT("%.4f"), V); };
		Text.ReplaceInline(TEXT("$(SEED)"), *FString::Printf(TEXT("%u"), uint32(C.Seed) * 0x9E3779B9u));
		Text.ReplaceInline(TEXT("$(RAIN_RATE)"), *F(C.RainRate));
		Text.ReplaceInline(TEXT("$(LAMBDA_SCALE)"), *F(C.LambdaScale));
		Text.ReplaceInline(TEXT("$(D_MIN_MM)"), *F(C.DMinMm));
		Text.ReplaceInline(TEXT("$(D_MAX_MM)"), *F(C.DMaxMm));
		Text.ReplaceInline(TEXT("$(STAGE_COUNT)"), *FString::Printf(TEXT("%d"), C.StageCount));
		Text.ReplaceInline(TEXT("$(STAGE_LAMBDA_SCALE)"), *F(C.StageLambdaScale));
		Text.ReplaceInline(TEXT("$(STAGE_D_MIN_MM)"), *F(C.StageDMinMm));
		return Text;
	}

	// The window follows the camera anywhere in the level: bounds that never cull the system.
	const FBox RainBounds(FVector(-50000.0, -50000.0, -1000.0), FVector(50000.0, 50000.0, 5000.0));

	struct FUserParamDefault
	{
		const TCHAR* Name;
		FNiagaraTypeDefinition Type;
		FVector4f Value;
	};

	TArray<FUserParamDefault> RainUserParameters()
	{
		const FNiagaraTypeDefinition F = FNiagaraTypeDefinition::GetFloatDef();
		const FNiagaraTypeDefinition V = FNiagaraTypeDefinition::GetVec3Def();
		const FNiagaraTypeDefinition V4 = FNiagaraTypeDefinition::GetVec4Def();
		return {
			{ TEXT("RainCameraPos"), V, FVector4f(-650, 45, 160, 0) },
			{ TEXT("RainCameraFwd"), V, FVector4f(1, 0, 0, 0) },
			{ TEXT("RainViewTan"), F, FVector4f(0.36f) },
			{ TEXT("RainStopCenter"), V, FVector4f(0, 0, 150, 0) },
			{ TEXT("RainStopRadius"), F, FVector4f(0) },
			{ TEXT("RainStopSoftness"), F, FVector4f(250) },
			{ TEXT("RainStopTau"), F, FVector4f(0.07f) },
			{ TEXT("RainHover"), F, FVector4f(1.2f) },
			{ TEXT("RainLiftTime"), F, FVector4f(-1) },
			{ TEXT("RainLiftSpread"), F, FVector4f(2.5f) },
			{ TEXT("RainLiftAccel"), F, FVector4f(60) },
			{ TEXT("RainLiftMaxSpeed"), F, FVector4f(400) },
			{ TEXT("RainChaos"), F, FVector4f(0) },
			{ TEXT("RainChaosSwirl"), F, FVector4f(380) },
			{ TEXT("RainChaosTurb"), F, FVector4f(260) },
			{ TEXT("RainHandPos"), V, FVector4f(0, 0, -100000, 0) },
			{ TEXT("RainHandVel"), V, FVector4f(0) },
			{ TEXT("RainHandRadius"), F, FVector4f(90) },
			{ TEXT("RainHandStrength"), F, FVector4f(0) },
			{ TEXT("RainPush"), V, FVector4f(0) },
			{ TEXT("RainBrushDir"), V, FVector4f(1, 0, 0, 0) },
			{ TEXT("RainBrushSpin"), V, FVector4f(0) },
			{ TEXT("RainBrushStrength"), F, FVector4f(0) },
			{ TEXT("RainBrushCone"), F, FVector4f(0.0025f) },
			{ TEXT("RainWind"), V, FVector4f(0) },
			{ TEXT("RainGust"), F, FVector4f(90.0f) },
			{ TEXT("RainWrap"), F, FVector4f(1) },
			{ TEXT("RainRespawnSpread"), F, FVector4f(2.5f) },
			{ TEXT("RainActiveFraction"), F, FVector4f(1) },
			{ TEXT("RainShutter"), F, FVector4f(1.0f / 48.0f) },
			{ TEXT("RainSizeScale"), F, FVector4f(1.0f) },
			{ TEXT("RainSplash"), F, FVector4f(1.0f) },
			{ TEXT("RainFrozenSizeScale"), F, FVector4f(2.2f) },
			{ TEXT("RainFocusDist"), F, FVector4f(600.0f) },
			{ TEXT("RainAperture"), F, FVector4f(1.25f) },
			{ TEXT("RainDofScale"), F, FVector4f(0.6f) },
			{ TEXT("RainPixelScale"), F, FVector4f(1400.0f) },
			{ TEXT("RainLodPixels"), F, FVector4f(2.5f) },
			{ TEXT("RainAmbient"), V, FVector4f(0.01f, 0.012f, 0.015f, 0) },
			{ TEXT("RainForward"), F, FVector4f(1.0f) },
			{ TEXT("RainLobe"), F, FVector4f(0.15f) },
			{ TEXT("RainSide"), F, FVector4f(0.003f) },
			{ TEXT("RainL0P"), V4, FVector4f(0, 0, -100000, 50) }, { TEXT("RainL0C"), V4, FVector4f(0, 0, 0, -1) }, { TEXT("RainL0D"), V4, FVector4f(0, 0, 0, -2) },
			{ TEXT("RainL1P"), V4, FVector4f(0, 0, -100000, 50) }, { TEXT("RainL1C"), V4, FVector4f(0, 0, 0, -1) }, { TEXT("RainL1D"), V4, FVector4f(0, 0, 0, -2) },
			{ TEXT("RainL2P"), V4, FVector4f(0, 0, -100000, 50) }, { TEXT("RainL2C"), V4, FVector4f(0, 0, 0, -1) }, { TEXT("RainL2D"), V4, FVector4f(0, 0, 0, -2) },
			{ TEXT("RainL3P"), V4, FVector4f(0, 0, -100000, 50) }, { TEXT("RainL3C"), V4, FVector4f(0, 0, 0, -1) }, { TEXT("RainL3D"), V4, FVector4f(0, 0, 0, -2) },
			{ TEXT("RainL4P"), V4, FVector4f(0, 0, -100000, 50) }, { TEXT("RainL4C"), V4, FVector4f(0, 0, 0, -1) }, { TEXT("RainL4D"), V4, FVector4f(0, 0, 0, -2) },
			{ TEXT("RainL5P"), V4, FVector4f(0, 0, -100000, 50) }, { TEXT("RainL5C"), V4, FVector4f(0, 0, 0, -1) }, { TEXT("RainL5D"), V4, FVector4f(0, 0, 0, -2) },
		};
	}


	// Particle attributes used by the HLSL must be declared before custom HLSL can touch them.
	TArray<FNiagaraVariable> RainAttributes()
	{
		const FNiagaraTypeDefinition F = FNiagaraTypeDefinition::GetFloatDef();
		const FNiagaraTypeDefinition V3 = FNiagaraTypeDefinition::GetVec3Def();
		const FNiagaraTypeDefinition V4 = FNiagaraTypeDefinition::GetVec4Def();
		return {
			FNiagaraVariable(FNiagaraTypeDefinition::GetPositionDef(), TEXT("Particles.Position")),
			FNiagaraVariable(V3, TEXT("Particles.Velocity")),
			FNiagaraVariable(FNiagaraTypeDefinition::GetColorDef(), TEXT("Particles.Color")),
			FNiagaraVariable(FNiagaraTypeDefinition::GetVec2Def(), TEXT("Particles.SpriteSize")),
			FNiagaraVariable(V3, TEXT("Particles.SpriteAlignment")),
			FNiagaraVariable(V4, TEXT("Particles.DynamicMaterialParameter")),
			FNiagaraVariable(V4, TEXT("Particles.DynamicMaterialParameter1")),
			FNiagaraVariable(F, TEXT("Particles.RainDiameter")),
			FNiagaraVariable(V4, TEXT("Particles.RainRand")),
			FNiagaraVariable(F, TEXT("Particles.RainFrozen")),
			FNiagaraVariable(F, TEXT("Particles.RainState")),
			FNiagaraVariable(F, TEXT("Particles.RainTimer")),
			FNiagaraVariable(V3, TEXT("Particles.RainDir")),
			FNiagaraVariable(F, TEXT("Particles.RainStage")),
		};
	}

	TArray<FString> RainAttributeDefaults()
	{
		TArray<FString> Defaults;
		Defaults.SetNum(RainAttributes().Num());
		return Defaults;
	}

	// StageShare: the share of the impacts that fall on the stage (its drops are fewer but bigger and faster).
	UNiagaraEmitter* BuildSplashEmitter(int32 Count, float StageShare, UMaterialInterface* Material, FString& Report)
	{
		const FString ModPath = RainRoot / TEXT("Niagara/Modules");
		auto FillSplash = [StageShare](FString Text)
		{
			Text.ReplaceInline(TEXT("$(PER_SPLASH)"), TEXT("12"));
			Text.ReplaceInline(TEXT("$(SEED)"), TEXT("2654435769"));
			Text.ReplaceInline(TEXT("$(DENSITY)"), TEXT("1.0"));
			Text.ReplaceInline(TEXT("$(STAGE_SPLASH_SHARE)"), *FString::Printf(TEXT("%.4f"), StageShare));
			return Text;
		};
		UNiagaraScript* Spawn = CreateHlslModule(ModPath, TEXT("NM_SplashSpawn"), FillSplash(LoadParticleModule(TEXT("Niagara/HLSL/SplashSpawn.hlsl"))),
			TEXT("Project Rain: splash droplet pool"), ParticleModuleMask);
		UNiagaraScript* Update = CreateHlslModule(ModPath, TEXT("NM_SplashUpdate"), FillSplash(LoadParticleModule(TEXT("Niagara/HLSL/SplashUpdate.hlsl"))),
			TEXT("Project Rain: ballistic splash droplets (Garg & Nayar 2007)"), ParticleModuleMask);
		SaveAsset(Spawn);
		SaveAsset(Update);

		UNiagaraEmitter* Emitter = CreateEmptyEmitter(TEXT("Splash"), false);
		FVersionedNiagaraEmitterData* D = Emitter->GetLatestEmitterData();
		D->SimTarget = ENiagaraSimTarget::GPUComputeSim;
		D->bLocalSpace = false;
		D->CalculateBoundsMode = ENiagaraEmitterCalculateBoundMode::Fixed;
		D->FixedBounds = RainBounds;
		UNiagaraNodeFunctionCall* State = AddModuleByPath(*D, ENiagaraScriptUsage::EmitterUpdateScript, TEXT("/Niagara/Modules/Emitter/EmitterState.EmitterState"));
		SetModuleInputFloat(Emitter, *D, ENiagaraScriptUsage::EmitterUpdateScript, State, TEXT("Loop Duration"), 1.0e7f);
		UNiagaraNodeFunctionCall* Burst = AddModuleByPath(*D, ENiagaraScriptUsage::EmitterUpdateScript, TEXT("/Niagara/Modules/Emitter/SpawnBurst_Instantaneous.SpawnBurst_Instantaneous"));
		SetModuleInputInt(Emitter, *D, ENiagaraScriptUsage::EmitterUpdateScript, Burst, TEXT("Spawn Count"), Count);
		LinkModuleInput(Burst, TEXT("Age"), FNiagaraTypeDefinition::GetFloatDef(), FNiagaraVariableBase(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Emitter.LoopedAge")));
		AddAttributeDeclarations(*D, ENiagaraScriptUsage::ParticleSpawnScript, RainAttributes(), RainAttributeDefaults());
		AddModule(*D, ENiagaraScriptUsage::ParticleSpawnScript, Spawn);
		AddModule(*D, ENiagaraScriptUsage::ParticleUpdateScript, Update);

		UNiagaraSpriteRendererProperties* Sprite = NewObject<UNiagaraSpriteRendererProperties>(Emitter, TEXT("SplashSprites"));
		Sprite->Material = Material;
		Sprite->Alignment = ENiagaraSpriteAlignment::CustomAlignment;
		Sprite->FacingMode = ENiagaraSpriteFacingMode::FaceCamera;
		Sprite->SortMode = ENiagaraSortMode::None;
		Sprite->PixelCoverageMode = ENiagaraRendererPixelCoverageMode::Enabled_A;
		Sprite->bCastShadows = false;
		Emitter->AddRenderer(Sprite, D->Version.VersionGuid);
		Report += FString::Printf(TEXT("Emitter Splash: %d droplets in clusters of 12 over the rain field\n"), Count);
		return Emitter;
	}

	UNiagaraEmitter* BuildRainEmitter(int32 Count, const FRainDropConfig& C, UMaterialInterface* Material, FString& Report)
	{
		const FString ModPath = RainRoot / TEXT("Niagara/Modules");
		UNiagaraScript* Spawn = CreateHlslModule(ModPath, TEXT("NM_RainSpawn"),
			Fill(LoadParticleModule(TEXT("Niagara/HLSL/RainSpawn.hlsl")), C), TEXT("Project Rain: spawn drops (Marshall-Palmer sizes, terminal velocity)"), ParticleModuleMask);
		UNiagaraScript* Update = CreateHlslModule(ModPath, TEXT("NM_RainUpdate"),
			Fill(LoadParticleModule(TEXT("Niagara/HLSL/RainUpdate.hlsl"), TEXT("Niagara/HLSL/RainLook.hlsl")), C), TEXT("Project Rain: fall / freeze / control / recycle"), ParticleModuleMask);
		SaveAsset(Spawn);
		SaveAsset(Update);

		UNiagaraEmitter* Emitter = CreateEmptyEmitter(TEXT("Rain"), false);
		FVersionedNiagaraEmitterData* D = Emitter->GetLatestEmitterData();
		D->SimTarget = ENiagaraSimTarget::GPUComputeSim;
		D->bLocalSpace = false;
		D->CalculateBoundsMode = ENiagaraEmitterCalculateBoundMode::Fixed;
		D->FixedBounds = RainBounds;

		// Emitter: one burst at t=0, then never loop again.
		UNiagaraNodeFunctionCall* State = AddModuleByPath(*D, ENiagaraScriptUsage::EmitterUpdateScript, TEXT("/Niagara/Modules/Emitter/EmitterState.EmitterState"));
		SetModuleInputFloat(Emitter, *D, ENiagaraScriptUsage::EmitterUpdateScript, State, TEXT("Loop Duration"), 1.0e7f);
		UNiagaraNodeFunctionCall* Burst = AddModuleByPath(*D, ENiagaraScriptUsage::EmitterUpdateScript, TEXT("/Niagara/Modules/Emitter/SpawnBurst_Instantaneous.SpawnBurst_Instantaneous"));
		SetModuleInputInt(Emitter, *D, ENiagaraScriptUsage::EmitterUpdateScript, Burst, TEXT("Spawn Count"), Count);
		LinkModuleInput(Burst, TEXT("Age"), FNiagaraTypeDefinition::GetFloatDef(), FNiagaraVariableBase(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Emitter.LoopedAge")));

		AddAttributeDeclarations(*D, ENiagaraScriptUsage::ParticleSpawnScript, RainAttributes(), RainAttributeDefaults());
		AddModule(*D, ENiagaraScriptUsage::ParticleSpawnScript, Spawn);
		AddModule(*D, ENiagaraScriptUsage::ParticleUpdateScript, Update);

		UNiagaraSpriteRendererProperties* Sprite = NewObject<UNiagaraSpriteRendererProperties>(Emitter, TEXT("DropSprites"));
		Sprite->Material = Material;
		Sprite->Alignment = ENiagaraSpriteAlignment::CustomAlignment;
		Sprite->FacingMode = ENiagaraSpriteFacingMode::FaceCamera;
		Sprite->SortMode = ENiagaraSortMode::ViewDepth;
		Sprite->bSortOnlyWhenTranslucent = true;
		Sprite->PixelCoverageMode = ENiagaraRendererPixelCoverageMode::Enabled_A;
		Sprite->PixelCoverageBlend = 1.0f;
		Sprite->bCastShadows = false;
		Emitter->AddRenderer(Sprite, D->Version.VersionGuid);

		const float Volume = RainField.Cell * RainField.Cell * RainField.Height * 1e-6f;
		// Peak of the stage's Gaussian column (per m^3), truncated at StageRadius.
		const float S2 = FMath::Square(RainField.StageSigma * 0.01f);
		const float StagePeak = C.StageCount / (2.0f * PI * S2 * (1.0f - FMath::Exp(-0.5f * FMath::Square(RainField.StageRadius * 0.01f) / S2)) * RainField.Height * 0.01f);
		Report += FString::Printf(TEXT("Emitter Rain: %d drops: %d in a %.0f x %.0f x %.0f cm periodic window (%.0f drops/m^3) + %d stage drops (%.0f drops/m^3 at the centre)\n"),
			Count, Count - C.StageCount, RainField.Cell, RainField.Cell, RainField.Height, (Count - C.StageCount) / Volume, C.StageCount, StagePeak);
		return Emitter;
	}
}


namespace
{
	UMaterial* BuildBasicMaterial(FString& Report)
	{
		UMaterial* M = GetOrCreateMaterial(RainRoot / TEXT("Materials"), TEXT("M_RainBasic"));
		auto Vec = [M](const TCHAR* Name, FLinearColor Default, int32 Y)
		{
			UMaterialExpressionVectorParameter* P = AddExpr<UMaterialExpressionVectorParameter>(M, -400, Y);
			P->ParameterName = Name;
			P->DefaultValue = Default;
			return P;
		};
		auto Scalar = [M](const TCHAR* Name, float Default, int32 Y)
		{
			UMaterialExpressionScalarParameter* P = AddExpr<UMaterialExpressionScalarParameter>(M, -400, Y);
			P->ParameterName = Name;
			P->DefaultValue = Default;
			return P;
		};
		UMaterialEditingLibrary::ConnectMaterialProperty(Vec(TEXT("BaseColor"), FLinearColor(0.2f, 0.2f, 0.2f), 0), TEXT(""), MP_BaseColor);
		UMaterialEditingLibrary::ConnectMaterialProperty(Scalar(TEXT("Metallic"), 0.0f, 150), TEXT(""), MP_Metallic);
		UMaterialEditingLibrary::ConnectMaterialProperty(Scalar(TEXT("Roughness"), 0.5f, 250), TEXT(""), MP_Roughness);
		UMaterialEditingLibrary::ConnectMaterialProperty(Vec(TEXT("Emissive"), FLinearColor(0, 0, 0), 350), TEXT(""), MP_EmissiveColor);
		CompileMaterialAndReport(M, Report);
		return M;
	}

	UMaterial* BuildGroundMaterial(UMaterialParameterCollection* MPC, FString& Report)
	{
		UMaterial* M = GetOrCreateMaterial(RainRoot / TEXT("Materials"), TEXT("M_WetGround"));
		UMaterialExpressionCustom* Custom = AddExpr<UMaterialExpressionCustom>(M, -300, 0);
		Custom->Description = TEXT("WetGround (Materials/HLSL/WetGround.hlsl)");
		Custom->Code = LoadTextFile(TEXT("Materials/HLSL/WetGround.hlsl"));
		Custom->OutputType = CMOT_Float3;
		Custom->Inputs.Reset();
		Custom->AdditionalOutputs.Reset();
		FCustomOutput& OutCol = Custom->AdditionalOutputs.AddDefaulted_GetRef();
		OutCol.OutputName = TEXT("BaseCol");
		OutCol.OutputType = CMOT_Float3;
		FCustomOutput& OutRough = Custom->AdditionalOutputs.AddDefaulted_GetRef();
		OutRough.OutputName = TEXT("RoughOut");
		OutRough.OutputType = CMOT_Float1;
		FCustomOutput& OutEmis = Custom->AdditionalOutputs.AddDefaulted_GetRef();
		OutEmis.OutputName = TEXT("EmisOut");
		OutEmis.OutputType = CMOT_Float3;
		FCustomOutput& OutSpec = Custom->AdditionalOutputs.AddDefaulted_GetRef();
		OutSpec.OutputName = TEXT("SpecOut");
		OutSpec.OutputType = CMOT_Float1;

		UMaterialExpressionTime* Time = AddExpr<UMaterialExpressionTime>(M, -700, -100);
		FCustomInput& InTime = Custom->Inputs.AddDefaulted_GetRef();
		InTime.InputName = TEXT("Time");
		InTime.Input.Connect(0, Time);
		auto AddCollection = [&](const TCHAR* InputName, const TCHAR* Param, int32 Y)
		{
			UMaterialExpressionCollectionParameter* P = AddExpr<UMaterialExpressionCollectionParameter>(M, -700, Y);
			P->Collection = MPC;
			P->ParameterName = Param;
			P->ParameterId = MPC->GetParameterId(Param);
			FCustomInput& In = Custom->Inputs.AddDefaulted_GetRef();
			In.InputName = InputName;
			In.Input.Connect(0, P);
		};
		AddCollection(TEXT("Rain"), TEXT("RainIntensity"), 0);
		AddCollection(TEXT("Wet"), TEXT("Wetness"), 100);
		AddCollection(TEXT("Lens"), TEXT("Lens"), 200);
		// The stage's environment cube (RT_RainEnv, it holds the backdrop): what the pavement blends into at the horizon.
		UMaterialExpressionTextureObjectParameter* Env = AddExpr<UMaterialExpressionTextureObjectParameter>(M, -700, 300);
		Env->ParameterName = TEXT("EnvCube");
		Env->Texture = LoadObject<UTextureRenderTargetCube>(nullptr, *(RainRoot / TEXT("Materials/RT_RainEnv.RT_RainEnv")));
		Env->SamplerType = SAMPLERTYPE_Color;
		FCustomInput& InEnv = Custom->Inputs.AddDefaulted_GetRef();
		InEnv.InputName = TEXT("Env");
		InEnv.Input.Connect(0, Env);
		Custom->RebuildOutputs();

		M->GetEditorOnlyData()->Normal.Connect(0, Custom);
		M->GetEditorOnlyData()->BaseColor.Connect(1, Custom);
		M->GetEditorOnlyData()->Roughness.Connect(2, Custom);
		M->GetEditorOnlyData()->EmissiveColor.Connect(3, Custom);
		M->GetEditorOnlyData()->Specular.Connect(4, Custom);
		CompileMaterialAndReport(M, Report);
		return M;
	}

	// The night backdrop on the sky dome (Materials/HLSL/NightSky.hlsl): the imported HDR panorama
	// (Scripts/import_backdrop.py) at infinity, with Exposure and Yaw parameters for the stage, blurred by
	// the camera's lens (MPC Lens).
	UMaterial* BuildNightSkyMaterial(UMaterialParameterCollection* MPC, FString& Report)
	{
		UTextureCube* Panorama = LoadObject<UTextureCube>(nullptr, *(RainRoot / TEXT("Environment/T_NightSky.T_NightSky")), nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (!Panorama)
		{
			Report += TEXT("M_NightSky skipped: import the backdrop first (Scripts/import_backdrop.py)\n");
			return nullptr;
		}
		UMaterial* M = GetOrCreateMaterial(RainRoot / TEXT("Materials"), TEXT("M_NightSky"));
		M->MaterialDomain = MD_Surface;
		M->BlendMode = BLEND_Opaque;
		M->SetShadingModel(MSM_Unlit);
		M->TwoSided = true;
		M->bIsSky = true;

		UMaterialExpressionCustom* Custom = AddExpr<UMaterialExpressionCustom>(M, -300, 0);
		Custom->Description = TEXT("NightSky (Materials/HLSL/NightSky.hlsl)");
		Custom->Code = LoadTextFile(TEXT("Materials/HLSL/NightSky.hlsl")).Replace(TEXT("$(FACE_SIZE)"), *FString::Printf(TEXT("%d.0"), Panorama->GetSizeX()));
		Custom->OutputType = CMOT_Float3;
		Custom->Inputs.Reset();
		auto AddInput = [Custom](const TCHAR* Name, UMaterialExpression* From)
		{
			FCustomInput& In = Custom->Inputs.AddDefaulted_GetRef();
			In.InputName = Name;
			In.Input.Connect(0, From);
		};
		UMaterialExpressionTextureObjectParameter* Sky = AddExpr<UMaterialExpressionTextureObjectParameter>(M, -700, -100);
		Sky->ParameterName = TEXT("Sky");
		Sky->Texture = Panorama;
		Sky->SamplerType = UMaterialExpressionTextureBase::GetSamplerTypeForTexture(Panorama);
		AddInput(TEXT("Sky"), Sky);
		UMaterialExpressionScalarParameter* Exposure = AddExpr<UMaterialExpressionScalarParameter>(M, -700, 0);
		Exposure->ParameterName = TEXT("Exposure");
		Exposure->DefaultValue = 1.0f;
		AddInput(TEXT("Exposure"), Exposure);
		UMaterialExpressionScalarParameter* Yaw = AddExpr<UMaterialExpressionScalarParameter>(M, -700, 100);
		Yaw->ParameterName = TEXT("Yaw");
		Yaw->DefaultValue = 0.0f;
		AddInput(TEXT("YawDeg"), Yaw);
		UMaterialExpressionCollectionParameter* Lens = AddExpr<UMaterialExpressionCollectionParameter>(M, -700, 200);
		Lens->Collection = MPC;
		Lens->ParameterName = TEXT("Lens");
		Lens->ParameterId = MPC->GetParameterId(TEXT("Lens"));
		AddInput(TEXT("Lens"), Lens);
		UMaterialExpressionCollectionParameter* CamPos = AddExpr<UMaterialExpressionCollectionParameter>(M, -700, 300);
		CamPos->Collection = MPC;
		CamPos->ParameterName = TEXT("CamPos");
		CamPos->ParameterId = MPC->GetParameterId(TEXT("CamPos"));
		AddInput(TEXT("CamW"), CamPos);
		UMaterialExpressionScalarParameter* GroundZ = AddExpr<UMaterialExpressionScalarParameter>(M, -700, 400);
		GroundZ->ParameterName = TEXT("GroundZ");
		GroundZ->DefaultValue = 0.0f;
		AddInput(TEXT("GroundZ"), GroundZ);

		M->GetEditorOnlyData()->EmissiveColor.Connect(0, Custom);
		CompileMaterialAndReport(M, Report);
		return M;
	}
}

FString URainBuilderLibrary::BuildStageMaterials()
{
	FString Report;
	UMaterialParameterCollection* MPC = LoadObject<UMaterialParameterCollection>(nullptr, *(RainRoot / TEXT("Materials/MPC_Rain.MPC_Rain")));
	if (!MPC)
	{
		return TEXT("FAILED: run BuildRainAssets first (MPC_Rain missing)");
	}
	BuildBasicMaterial(Report);
	BuildGroundMaterial(MPC, Report);
	BuildNightSkyMaterial(MPC, Report);
	return Report;
}

FString URainBuilderLibrary::BuildRainAssets(int32 RainCount, int32 SplashCount, int32 StageCount)
{
	FString Report;
	UMaterialParameterCollection* MPC = BuildCollection();
	UTextureRenderTargetCube* Env = BuildEnvCube();
	UMaterial* DropMaterial = BuildDropMaterial(MPC, Env, Report);

	UNiagaraSystem* System = CreateSystemAsset(RainRoot / TEXT("Niagara"), TEXT("NS_RainMagic"));
	AddRainUserParameters(System);

	// One rain: a single emitter. The field has one density and one drop-size distribution everywhere (near
	// and far, big and small on screen, come from perspective alone); on top of it the stage rain, the rain
	// towers' big drops around the performance area.
	FRainDropConfig Config;
	Config.StageCount = FMath::Clamp(StageCount, 0, RainCount);
	if (RainCount > 0)
	{
		UNiagaraEmitter* Emitter = BuildRainEmitter(RainCount, Config, DropMaterial, Report);
		FNiagaraEditorUtilities::AddEmitterToSystem(*System, *Emitter, Emitter->GetExposedVersion().VersionGuid, true);
	}
	if (SplashCount > 0)
	{
		// Impacts are proportional to drops x fall speed (~8.3 m/s for the stage's drops, ~6.5 m/s in the field).
		const float StageRate = Config.StageCount * 8.3f;
		const float StageShare = RainCount > 0 ? StageRate / (StageRate + (RainCount - Config.StageCount) * 6.5f) : 0.0f;
		UNiagaraEmitter* Splash = BuildSplashEmitter(SplashCount, StageShare, DropMaterial, Report);
		FNiagaraEditorUtilities::AddEmitterToSystem(*System, *Splash, Splash->GetExposedVersion().VersionGuid, true);
	}

	const bool bOk = CompileAndReport(System, Report);
	SaveAsset(System);

	// Point any NS_RainMagic actors in the open level at the freshly built system.
	if (GEditor)
	{
		if (UWorld* World = GEditor->GetEditorWorldContext().World())
		{
			for (TActorIterator<ANiagaraActor> It(World); It; ++It)
			{
				UNiagaraComponent* NC = It->GetNiagaraComponent();
				if (NC && NC->GetAsset() && NC->GetAsset()->GetName().StartsWith(TEXT("NS_RainMagic")))
				{
					NC->SetAsset(System);
					NC->ResetSystem();
					Report += FString::Printf(TEXT("Rebound %s\n"), *It->GetActorLabel());
				}
			}
		}
	}
	return FString::Printf(TEXT("%s\n%s"), bOk ? TEXT("SUCCESS") : TEXT("FAILED"), *Report);
}

void RainAuthoring::AddRainUserParameters(UNiagaraSystem* System)
{
	for (const FUserParamDefault& P : RainUserParameters())
	{
		FNiagaraVariable Var(P.Type, *(FString(TEXT("User.")) + P.Name));
		Var.AllocateData();
		if (P.Type == FNiagaraTypeDefinition::GetFloatDef())
		{
			Var.SetValue(P.Value.X);
		}
		else if (P.Type == FNiagaraTypeDefinition::GetVec4Def())
		{
			Var.SetValue(P.Value);
		}
		else
		{
			Var.SetValue(FVector3f(P.Value.X, P.Value.Y, P.Value.Z));
		}
		AddUserParameter(System, Var);
	}
}

// ---------------------------------------------------------------------------------------------- Rain Shield
// The game (Source/ProjectRain/Private/RainShieldGame.cpp): NS_RainGame, one GPU rain emitter that falls in
// the direction the player picks, is absorbed by blockers and reads its hits on the protected shape back
// to gameplay through Export Particle Data. Its drops look like the film rain's (the same RainLook.hlsl tail,
// material and per-frame User.Rain* look parameters, pushed by ARainMagicController).
namespace
{
	struct FGameRainConfig
	{
		float GustWavelength = 260.0f;
		float FaceFade = 40.0f;
		int32 Seed = 71;
		float HailRestitution = 0.45f;
		float HailFriction = 0.15f;
		float HailRestSpeed = 40.0f;
		float HailEventSpeed = 150.0f;
		float HailMeltDelay = 2.0f;
		float HailMeltRate = 0.5f;
	};

	FString FillGame(FString Text, const FGameRainConfig& C)
	{
		auto F = [](float V) { return FString::Printf(TEXT("%.4f"), V); };
		Text.ReplaceInline(TEXT("$(SEED)"), *FString::Printf(TEXT("%u"), uint32(C.Seed) * 0x9E3779B9u));
		Text.ReplaceInline(TEXT("$(GAME_D_MIN_MM)"), *F(RainShield::DropMinMm));
		Text.ReplaceInline(TEXT("$(GAME_D_MAX_MM)"), *F(RainShield::DropMaxMm));
		Text.ReplaceInline(TEXT("$(GAME_GUST_WAVELENGTH)"), *F(C.GustWavelength));
		Text.ReplaceInline(TEXT("$(GAME_FACE_FADE)"), *F(C.FaceFade));
		FString Boxes;
		for (int32 i = 0; i < RainShield::MaxBoxes; ++i)
		{
			Boxes += FString::Printf(TEXT("\t\tGR_BOX(User.RainBox%dMin, User.RainBox%dMax, User.RainBox%dKind)\n"), i, i, i);
		}
		Text.ReplaceInline(TEXT("$(GAME_BOX_TESTS)"), *Boxes);
		Text.ReplaceInline(TEXT("$(GAME_BOX_TESTS_HAIL)"), *Boxes.Replace(TEXT("GR_BOX"), TEXT("HB_BOX")));
		// System 2 (hail): CPU-friendly (float seed), RainShieldTypes.h sizes and speed, bounce and melt.
		Text.ReplaceInline(TEXT("$(SEED_F)"), *F(float(C.Seed) * 0.6180339f));
		Text.ReplaceInline(TEXT("$(HAIL_R_MIN)"), *F(RainShield::HailRadiusMin));
		Text.ReplaceInline(TEXT("$(HAIL_R_MAX)"), *F(RainShield::HailRadiusMax));
		Text.ReplaceInline(TEXT("$(HAIL_SPEED)"), *F(RainShield::HailSpeed));
		Text.ReplaceInline(TEXT("$(HAIL_RESTITUTION)"), *F(C.HailRestitution));
		Text.ReplaceInline(TEXT("$(HAIL_FRICTION)"), *F(C.HailFriction));
		Text.ReplaceInline(TEXT("$(HAIL_REST_SPEED)"), *F(C.HailRestSpeed));
		Text.ReplaceInline(TEXT("$(HAIL_EVENT_SPEED)"), *F(C.HailEventSpeed));
		Text.ReplaceInline(TEXT("$(HAIL_MELT_DELAY)"), *F(C.HailMeltDelay));
		Text.ReplaceInline(TEXT("$(HAIL_MELT_RATE)"), *F(C.HailMeltRate));
		return Text;
	}

	const TCHAR* ExportModulePath = TEXT("/Niagara/Modules/ExportParticleData/ExportParticleDataToBlueprint.ExportParticleDataToBlueprint");
	const FNiagaraVariableBase GameExportParam(FNiagaraTypeDefinition(UNiagaraDataInterfaceExport::StaticClass()), TEXT("User.RainGameExport"));
	const FNiagaraVariableBase GameHitParam(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Particles.RainGameHit"));

	void AddGameUserParameters(UNiagaraSystem* System, int32 RainCount)
	{
		auto Add = [System](FNiagaraVariable Var) { Var.SetName(*(TEXT("User.") + Var.GetName().ToString())); AddUserParameter(System, Var); };
		auto Float = [&Add](const TCHAR* Name, float V) { FNiagaraVariable Var(FNiagaraTypeDefinition::GetFloatDef(), Name); Var.SetValue(V); Add(Var); };
		auto Vec3 = [&Add](const TCHAR* Name, const FVector3f& V) { FNiagaraVariable Var(FNiagaraTypeDefinition::GetVec3Def(), Name); Var.SetValue(V); Add(Var); };
		FNiagaraVariable Count(FNiagaraTypeDefinition::GetIntDef(), TEXT("RainGameCount"));
		Count.SetValue(RainCount);
		Add(Count);
		Vec3(TEXT("RainGameDir"), FVector3f(0.0f, 0.0f, -1.0f));
		Vec3(TEXT("RainGameCenter"), FVector3f(0.0f, 0.0f, 250.0f));
		Float(TEXT("RainGameHalf"), 290.0f);
		Float(TEXT("RainGameLength"), 700.0f);
		Float(TEXT("RainGameActive"), 0.0f);
		Float(TEXT("RainGameScoring"), 0.0f);
		Float(TEXT("RainGameGust"), 60.0f);
		// Boxes as Vec3 min, Vec3 max and a float kind (the same for both systems; the CPU hail reads them all).
		for (int32 i = 0; i < RainShield::MaxBoxes; ++i)
		{
			Vec3(*FString::Printf(TEXT("RainBox%dMin"), i), FVector3f(1e6f));
			Vec3(*FString::Printf(TEXT("RainBox%dMax"), i), FVector3f(-1e6f));
			Float(*FString::Printf(TEXT("RainBox%dKind"), i), RainShield::BoxNone);
		}

		// Hits go back to gameplay through Export Particle Data: the DI calls the object in User.RainHitHandler
		// (the game actor sets itself there) with the particles that raised RainGameHit this frame.
		const FNiagaraVariable Handler(FNiagaraTypeDefinition::GetUObjectDef(), TEXT("User.RainHitHandler"));
		System->GetExposedParameters().AddParameter(Handler, true, true);
		UNiagaraDataInterfaceExport* Export = NewObject<UNiagaraDataInterfaceExport>(System, NAME_None, RF_Transactional | RF_Public);
		Export->CallbackHandlerParameter.Parameter = Handler;
		Export->GPUAllocationMode = ENDIExport_GPUAllocationMode::FixedSize;
		Export->GPUAllocationFixedSize = RainShield::ExportCapacity;
		const FNiagaraVariable ExportVar(GameExportParam);
		System->GetExposedParameters().AddParameter(ExportVar, true, true);
		System->GetExposedParameters().SetDataInterface(Export, ExportVar);
	}


	// The stock Export Particle Data module's inputs, found by type (their names are the engine's, not ours).
	struct FGameExportInputs
	{
		UNiagaraScript* Module = nullptr;
		FName Export, Store, Position, Size, Velocity, Delay;
		FNiagaraTypeDefinition PositionType = FNiagaraTypeDefinition::GetVec3Def();
		FNiagaraTypeDefinition VelocityType = FNiagaraTypeDefinition::GetVec3Def();
	};

	FGameExportInputs FindExportInputs(FString& Report)
	{
		FGameExportInputs In;
		In.Module = LoadObject<UNiagaraScript>(nullptr, ExportModulePath);
		UNiagaraScriptSource* Source = In.Module ? Cast<UNiagaraScriptSource>(In.Module->GetLatestSource()) : nullptr;
		if (!Source || !Source->NodeGraph)
		{
			Report += FString::Printf(TEXT("  MISSING export module %s\n"), ExportModulePath);
			In.Module = nullptr;
			return In;
		}
		for (const auto& Pair : Source->NodeGraph->GetAllMetaData())
		{
			const FString Name = Pair.Key.GetName().ToString();
			if (!Name.StartsWith(TEXT("Module.")))
			{
				continue;
			}
			const FNiagaraTypeDefinition& Type = Pair.Key.GetType();
			const FName Input(*Name.RightChop(7));
			if (Type.GetClass() && Type.GetClass()->IsChildOf(UNiagaraDataInterfaceExport::StaticClass()))
			{
				In.Export = Input;
			}
			else if (Type == FNiagaraTypeDefinition::GetBoolDef())
			{
				In.Store = Input;
			}
			else if (Name.Contains(TEXT("Position")))
			{
				In.Position = Input;
				In.PositionType = Type;
			}
			else if (Name.Contains(TEXT("Velocity")))
			{
				In.Velocity = Input;
				In.VelocityType = Type;
			}
			else if (Name.Contains(TEXT("Size")))
			{
				In.Size = Input;
			}
			else if (Name.Contains(TEXT("Delay")))
			{
				In.Delay = Input;
			}
		}
		return In;
	}

	// Adds Export Particle Data after the update: exports the particles that raise Particles.RainGameHit this frame
	// (position Particles.RainGameHitPos, size and velocity from the given attributes) to User.RainGameExport.
	bool AddExportModule(UNiagaraEmitter* Emitter, FVersionedNiagaraEmitterData& D, const FGameExportInputs& In,
		const FNiagaraVariableBase& SizeAttribute, const FNiagaraVariableBase* VelocityAttribute)
	{
		if (!In.Module)
		{
			return false;
		}
		UNiagaraNodeFunctionCall* Export = AddModule(D, ENiagaraScriptUsage::ParticleUpdateScript, In.Module);
		const bool bLinked = !In.Export.IsNone() && !In.Store.IsNone()
			&& LinkModuleInput(Export, In.Export, GameExportParam.GetType(), GameExportParam)
			&& LinkModuleInput(Export, In.Store, FNiagaraTypeDefinition::GetBoolDef(), GameHitParam);
		if (!In.Delay.IsNone())
		{
			// Its default (0.1 s between exports) would drop most hits: export every frame.
			SetModuleInputFloat(Emitter, D, ENiagaraScriptUsage::ParticleUpdateScript, Export, In.Delay, 0.0f);
		}
		if (!In.Position.IsNone())
		{
			LinkModuleInput(Export, In.Position, In.PositionType, FNiagaraVariableBase(In.PositionType, TEXT("Particles.RainGameHitPos")));
		}
		if (!In.Size.IsNone())
		{
			LinkModuleInput(Export, In.Size, FNiagaraTypeDefinition::GetFloatDef(), SizeAttribute);
		}
		if (VelocityAttribute && !In.Velocity.IsNone())
		{
			LinkModuleInput(Export, In.Velocity, In.VelocityType, *VelocityAttribute);
		}
		return bLinked;
	}

	UNiagaraEmitter* BuildGameRainEmitter(const FGameRainConfig& C, UMaterialInterface* Material, FString& Report)
	{
		const FString ModPath = RainRoot / TEXT("Niagara/Modules");
		UNiagaraScript* Spawn = CreateHlslModule(ModPath, TEXT("NM_GameRainSpawn"),
			FillGame(LoadParticleModule(TEXT("Niagara/HLSL/GameRainSpawn.hlsl")), C), TEXT("Rain Shield: drops parked upwind of the stream"), ParticleModuleMask);
		UNiagaraScript* Update = CreateHlslModule(ModPath, TEXT("NM_GameRainUpdate"),
			FillGame(LoadParticleModule(TEXT("Niagara/HLSL/GameRainUpdate.hlsl"), TEXT("Niagara/HLSL/RainLook.hlsl")), C), TEXT("Rain Shield: fall, gusts, grid boxes, hits"), ParticleModuleMask);
		SaveAsset(Spawn);
		SaveAsset(Update);

		const FGameExportInputs Inputs = FindExportInputs(Report);

		UNiagaraEmitter* Emitter = CreateEmptyEmitter(TEXT("GameRain"), false);
		FVersionedNiagaraEmitterData* D = Emitter->GetLatestEmitterData();
		D->SimTarget = ENiagaraSimTarget::GPUComputeSim;
		D->bLocalSpace = false;
		D->CalculateBoundsMode = ENiagaraEmitterCalculateBoundMode::Fixed;
		D->FixedBounds = FBox(FVector(-5000.0), FVector(5000.0));

		// One burst of User.RainGameCount drops at t = 0 (rain.game.Count changes it and reinitialises the system).
		UNiagaraNodeFunctionCall* State = AddModuleByPath(*D, ENiagaraScriptUsage::EmitterUpdateScript, TEXT("/Niagara/Modules/Emitter/EmitterState.EmitterState"));
		SetModuleInputFloat(Emitter, *D, ENiagaraScriptUsage::EmitterUpdateScript, State, TEXT("Loop Duration"), 1.0e7f);
		UNiagaraNodeFunctionCall* Burst = AddModuleByPath(*D, ENiagaraScriptUsage::EmitterUpdateScript, TEXT("/Niagara/Modules/Emitter/SpawnBurst_Instantaneous.SpawnBurst_Instantaneous"));
		const bool bCountLinked = LinkModuleInput(Burst, TEXT("Spawn Count"), FNiagaraTypeDefinition::GetIntDef(), FNiagaraVariableBase(FNiagaraTypeDefinition::GetIntDef(), TEXT("User.RainGameCount")));
		LinkModuleInput(Burst, TEXT("Age"), FNiagaraTypeDefinition::GetFloatDef(), FNiagaraVariableBase(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Emitter.LoopedAge")));

		TArray<FNiagaraVariable> Attributes = RainAttributes();
		Attributes.Add(FNiagaraVariable(GameHitParam));
		Attributes.Add(FNiagaraVariable(Inputs.PositionType, TEXT("Particles.RainGameHitPos")));
		TArray<FString> Defaults;
		Defaults.SetNum(Attributes.Num());
		AddAttributeDeclarations(*D, ENiagaraScriptUsage::ParticleSpawnScript, Attributes, Defaults);
		AddModule(*D, ENiagaraScriptUsage::ParticleSpawnScript, Spawn);
		AddModule(*D, ENiagaraScriptUsage::ParticleUpdateScript, Update);

		// After the update: exports the drops that hit the protected shape this frame.
		const bool bExportLinked = AddExportModule(Emitter, *D, Inputs, FNiagaraVariableBase(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Particles.RainDiameter")), nullptr);

		UNiagaraSpriteRendererProperties* Sprite = NewObject<UNiagaraSpriteRendererProperties>(Emitter, TEXT("DropSprites"));
		Sprite->Material = Material;
		Sprite->Alignment = ENiagaraSpriteAlignment::CustomAlignment;
		Sprite->FacingMode = ENiagaraSpriteFacingMode::FaceCamera;
		Sprite->SortMode = ENiagaraSortMode::ViewDepth;
		Sprite->bSortOnlyWhenTranslucent = true;
		Sprite->PixelCoverageMode = ENiagaraRendererPixelCoverageMode::Enabled_A;
		Sprite->PixelCoverageBlend = 1.0f;
		Sprite->bCastShadows = false;
		Emitter->AddRenderer(Sprite, D->Version.VersionGuid);

		Report += FString::Printf(TEXT("Emitter GameRain: User.RainGameCount drops (%s), %d grid boxes, hit export %s\n"),
			bCountLinked ? TEXT("linked") : TEXT("COUNT NOT LINKED"), RainShield::MaxBoxes, bExportLinked ? TEXT("linked") : TEXT("NOT LINKED"));
		return Emitter;
	}
}

FString URainBuilderLibrary::BuildRainGameAssets(int32 RainCount)
{
	FString Report;
	UMaterial* DropMaterial = LoadObject<UMaterial>(nullptr, *(RainRoot / TEXT("Materials/M_RainDrop.M_RainDrop")), nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (!DropMaterial)
	{
		return TEXT("FAILED: run BuildRainAssets first (M_RainDrop missing)");
	}
	UNiagaraSystem* System = CreateSystemAsset(RainRoot / TEXT("Niagara"), TEXT("NS_RainGame"));
	AddRainUserParameters(System);
	AddGameUserParameters(System, RainCount);
	FGameRainConfig Config;
	UNiagaraEmitter* Emitter = BuildGameRainEmitter(Config, DropMaterial, Report);
	FNiagaraEditorUtilities::AddEmitterToSystem(*System, *Emitter, Emitter->GetExposedVersion().VersionGuid, true);
	const bool bOk = CompileAndReport(System, Report);
	SaveAsset(System);
	return FString::Printf(TEXT("%s\n%s"), bOk ? TEXT("SUCCESS") : TEXT("FAILED"), *Report);
}

namespace
{
	// Hailstones: lit, a touch glossy and self-lit enough to read at night; colour per stone from the particle.
	UMaterial* BuildHailMaterial(FString& Report)
	{
		UMaterial* M = GetOrCreateMaterial(RainRoot / TEXT("Materials"), TEXT("M_Hail"));
		M->MaterialDomain = MD_Surface;
		M->BlendMode = BLEND_Opaque;
		M->SetShadingModel(MSM_DefaultLit);
		M->bUsedWithNiagaraMeshParticles = true;
		M->bUsedWithNiagaraSprites = true;
		UMaterialExpressionParticleColor* PColor = AddExpr<UMaterialExpressionParticleColor>(M, -600, 0);
		auto Scalar = [M](const TCHAR* Name, float Default, int32 Y)
		{
			UMaterialExpressionScalarParameter* P = AddExpr<UMaterialExpressionScalarParameter>(M, -400, Y);
			P->ParameterName = Name;
			P->DefaultValue = Default;
			return P;
		};
		UMaterialExpressionMultiply* Glow = AddExpr<UMaterialExpressionMultiply>(M, -200, 300);
		Glow->A.Connect(0, PColor);
		Glow->B.Connect(0, Scalar(TEXT("Glow"), 0.03f, 400));
		UMaterialEditingLibrary::ConnectMaterialProperty(PColor, TEXT("RGB"), MP_BaseColor);
		UMaterialEditingLibrary::ConnectMaterialProperty(Scalar(TEXT("Roughness"), 0.22f, 100), TEXT(""), MP_Roughness);
		UMaterialEditingLibrary::ConnectMaterialProperty(Scalar(TEXT("Specular"), 0.9f, 200), TEXT(""), MP_Specular);
		UMaterialEditingLibrary::ConnectMaterialProperty(Glow, TEXT(""), MP_EmissiveColor);
		CompileMaterialAndReport(M, Report);
		return M;
	}

	UNiagaraEmitter* BuildHailEmitter(const FGameRainConfig& C, bool bGpu, UMaterialInterface* Material, FString& Report)
	{
		// The same HLSL for both targets (the CPU -> GPU port is the measured optimisation).
		const FString ModPath = RainRoot / TEXT("Niagara/Modules");
		const TCHAR* Suffix = bGpu ? TEXT("GPU") : TEXT("");
		UNiagaraScript* Spawn = CreateHlslModule(ModPath, FString(TEXT("NM_HailSpawn")) + Suffix,
			FillGame(LoadTextFile(TEXT("Niagara/HLSL/HailSpawn.hlsl")), C), TEXT("Rain Shield hail: stones parked upwind"), ParticleModuleMask);
		UNiagaraScript* Update = CreateHlslModule(ModPath, FString(TEXT("NM_HailUpdate")) + Suffix,
			FillGame(LoadTextFile(TEXT("Niagara/HLSL/HailUpdate.hlsl")), C), TEXT("Rain Shield hail: fall, bounce, rest, melt, impacts"), ParticleModuleMask);
		SaveAsset(Spawn);
		SaveAsset(Update);
		const FGameExportInputs Inputs = FindExportInputs(Report);

		UNiagaraEmitter* Emitter = CreateEmptyEmitter(TEXT("Hail"), false);
		FVersionedNiagaraEmitterData* D = Emitter->GetLatestEmitterData();
		D->SimTarget = bGpu ? ENiagaraSimTarget::GPUComputeSim : ENiagaraSimTarget::CPUSim;
		D->bLocalSpace = false;
		D->CalculateBoundsMode = ENiagaraEmitterCalculateBoundMode::Fixed;
		D->FixedBounds = FBox(FVector(-5000.0), FVector(5000.0));

		UNiagaraNodeFunctionCall* State = AddModuleByPath(*D, ENiagaraScriptUsage::EmitterUpdateScript, TEXT("/Niagara/Modules/Emitter/EmitterState.EmitterState"));
		SetModuleInputFloat(Emitter, *D, ENiagaraScriptUsage::EmitterUpdateScript, State, TEXT("Loop Duration"), 1.0e7f);
		UNiagaraNodeFunctionCall* Burst = AddModuleByPath(*D, ENiagaraScriptUsage::EmitterUpdateScript, TEXT("/Niagara/Modules/Emitter/SpawnBurst_Instantaneous.SpawnBurst_Instantaneous"));
		const bool bCountLinked = LinkModuleInput(Burst, TEXT("Spawn Count"), FNiagaraTypeDefinition::GetIntDef(), FNiagaraVariableBase(FNiagaraTypeDefinition::GetIntDef(), TEXT("User.RainGameCount")));
		LinkModuleInput(Burst, TEXT("Age"), FNiagaraTypeDefinition::GetFloatDef(), FNiagaraVariableBase(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Emitter.LoopedAge")));

		const FNiagaraTypeDefinition F = FNiagaraTypeDefinition::GetFloatDef();
		const FNiagaraTypeDefinition V3 = FNiagaraTypeDefinition::GetVec3Def();
		TArray<FNiagaraVariable> Attributes = {
			FNiagaraVariable(FNiagaraTypeDefinition::GetPositionDef(), TEXT("Particles.Position")),
			FNiagaraVariable(V3, TEXT("Particles.Velocity")),
			FNiagaraVariable(FNiagaraTypeDefinition::GetColorDef(), TEXT("Particles.Color")),
			FNiagaraVariable(V3, TEXT("Particles.Scale")),
			FNiagaraVariable(FNiagaraTypeDefinition::GetQuatDef(), TEXT("Particles.MeshOrientation")),
			FNiagaraVariable(FNiagaraTypeDefinition::GetVec4Def(), TEXT("Particles.RainRand")),
			FNiagaraVariable(F, TEXT("Particles.HailRadius")),
			FNiagaraVariable(F, TEXT("Particles.HailState")),
			FNiagaraVariable(F, TEXT("Particles.HailTimer")),
			FNiagaraVariable(F, TEXT("Particles.HailCycle")),
			FNiagaraVariable(F, TEXT("Particles.HailAngle")),
			FNiagaraVariable(GameHitParam),
			FNiagaraVariable(Inputs.PositionType, TEXT("Particles.RainGameHitPos")),
			FNiagaraVariable(Inputs.VelocityType, TEXT("Particles.HailImpact")),
		};
		TArray<FString> Defaults;
		Defaults.SetNum(Attributes.Num());
		AddAttributeDeclarations(*D, ENiagaraScriptUsage::ParticleSpawnScript, Attributes, Defaults);
		AddModule(*D, ENiagaraScriptUsage::ParticleSpawnScript, Spawn);
		AddModule(*D, ENiagaraScriptUsage::ParticleUpdateScript, Update);
		const FNiagaraVariableBase Impact(Inputs.VelocityType, TEXT("Particles.HailImpact"));
		const bool bExportLinked = AddExportModule(Emitter, *D, Inputs, FNiagaraVariableBase(F, TEXT("Particles.HailRadius")), &Impact);

		UNiagaraMeshRendererProperties* Meshes = NewObject<UNiagaraMeshRendererProperties>(Emitter, TEXT("HailMeshes"));
		Meshes->Meshes.SetNum(1);
		Meshes->Meshes[0].Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
		Meshes->bOverrideMaterials = true;
		FNiagaraMeshMaterialOverride& Override = Meshes->OverrideMaterials.AddDefaulted_GetRef();
		Override.ExplicitMat = Material;
		Emitter->AddRenderer(Meshes, D->Version.VersionGuid);

		Report += FString::Printf(TEXT("Emitter Hail (%s): User.RainGameCount stones (%s), mesh renderer, impact export %s\n"),
			bGpu ? TEXT("GPU") : TEXT("CPU"), bCountLinked ? TEXT("linked") : TEXT("COUNT NOT LINKED"), bExportLinked ? TEXT("linked") : TEXT("NOT LINKED"));
		return Emitter;
	}
}

FString URainBuilderLibrary::BuildRainHailAssets(int32 HailCount)
{
	FString Report;
	UMaterial* HailMaterial = BuildHailMaterial(Report);
	bool bOk = true;
	for (const bool bGpu : { false, true })
	{
		UNiagaraSystem* System = CreateSystemAsset(RainRoot / TEXT("Niagara"), bGpu ? TEXT("NS_RainHailGPU") : TEXT("NS_RainHail"));
		AddGameUserParameters(System, HailCount);
		FGameRainConfig Config;
		UNiagaraEmitter* Emitter = BuildHailEmitter(Config, bGpu, HailMaterial, Report);
		FNiagaraEditorUtilities::AddEmitterToSystem(*System, *Emitter, Emitter->GetExposedVersion().VersionGuid, true);
		bOk &= CompileAndReport(System, Report);
		SaveAsset(System);
	}
	return FString::Printf(TEXT("%s\n%s"), bOk ? TEXT("SUCCESS") : TEXT("FAILED"), *Report);
}

// ---------------------------------------------------------------------------------------------- Paint Valley
// The Monument Valley look of the paint game: one unlit material for every surface (face tones by direction,
// the atlas's AO, openings, edges and paint) and one for the backdrop. MPC_Paint carries the key direction.

namespace
{
	UMaterialParameterCollection* BuildPaintCollection()
	{
		const FString Path = RainRoot / TEXT("Materials/MPC_Paint");
		UMaterialParameterCollection* MPC = LoadObject<UMaterialParameterCollection>(nullptr, *(Path + TEXT(".MPC_Paint")), nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (!MPC)
		{
			UPackage* Package = CreatePackage(*Path);
			MPC = NewObject<UMaterialParameterCollection>(Package, TEXT("MPC_Paint"), RF_Public | RF_Standalone | RF_Transactional);
			FAssetRegistryModule::AssetCreated(MPC);
		}
		if (!MPC->GetVectorParameterByName(TEXT("Key")))
		{
			FCollectionVectorParameter P;
			P.ParameterName = TEXT("Key");
			P.DefaultValue = FLinearColor(-0.7071f, 0.7071f, 0.0f, 0.0f);
			MPC->VectorParameters.Add(P);
			MPC->PostEditChange();
		}
		SaveAsset(MPC);
		return MPC;
	}

	// A Custom node fed by parameters: Inputs are (input name, expression) in order.
	UMaterialExpressionCustom* AddCustom(UMaterial* M, const TCHAR* File, ECustomMaterialOutputType Output)
	{
		UMaterialExpressionCustom* Custom = AddExpr<UMaterialExpressionCustom>(M, -300, 0);
		Custom->Description = FString::Printf(TEXT("%s"), File);
		Custom->Code = LoadTextFile(File);
		Custom->OutputType = Output;
		Custom->Inputs.Reset();
		return Custom;
	}

	// Output: 0 for a vector parameter's RGB, 5 for its RGBA.
	void AddCustomInput(UMaterialExpressionCustom* Custom, const TCHAR* Name, UMaterialExpression* From, int32 Output = 0)
	{
		FCustomInput& In = Custom->Inputs.AddDefaulted_GetRef();
		In.InputName = Name;
		In.Input.Connect(Output, From);
	}

	UMaterial* BuildPaintSurfaceMaterial(UMaterialParameterCollection* MPC, FString& Report)
	{
		UMaterial* M = GetOrCreateMaterial(RainRoot / TEXT("Materials"), TEXT("M_PaintSurface"));
		M->MaterialDomain = MD_Surface;
		M->BlendMode = BLEND_Opaque;
		M->SetShadingModel(MSM_Unlit);
		M->TwoSided = true;
		UMaterialExpressionCustom* Custom = AddCustom(M, TEXT("Materials/HLSL/PaintSurface.hlsl"), CMOT_Float3);
		int32 Y = -600;
		auto Vec = [M, &Y](const TCHAR* Name, const FLinearColor& Default)
		{
			UMaterialExpressionVectorParameter* P = AddExpr<UMaterialExpressionVectorParameter>(M, -900, Y += 90);
			P->ParameterName = Name;
			P->DefaultValue = Default;
			return P;
		};
		UMaterialExpressionTextureObjectParameter* Atlas = AddExpr<UMaterialExpressionTextureObjectParameter>(M, -900, Y += 90);
		Atlas->ParameterName = TEXT("Atlas");
		Atlas->Texture = LoadObject<UTexture>(nullptr, TEXT("/Engine/EngineResources/Black.Black"));
		Atlas->SamplerType = SAMPLERTYPE_LinearColor;
		AddCustomInput(Custom, TEXT("Atlas"), Atlas);
		UMaterialExpressionTextureCoordinate* UV = AddExpr<UMaterialExpressionTextureCoordinate>(M, -900, Y += 90);
		AddCustomInput(Custom, TEXT("UV"), UV);
		UMaterialExpressionScalarParameter* UseAtlas = AddExpr<UMaterialExpressionScalarParameter>(M, -900, Y += 90);
		UseAtlas->ParameterName = TEXT("UseAtlas");
		UseAtlas->DefaultValue = 1.0f;
		AddCustomInput(Custom, TEXT("UseAtlas"), UseAtlas);
		AddCustomInput(Custom, TEXT("Top"), Vec(TEXT("Top"), FLinearColor(0.8f, 0.8f, 0.8f)));
		AddCustomInput(Custom, TEXT("LightC"), Vec(TEXT("Light"), FLinearColor(0.6f, 0.6f, 0.6f)));
		AddCustomInput(Custom, TEXT("DarkC"), Vec(TEXT("Dark"), FLinearColor(0.4f, 0.4f, 0.4f)));
		AddCustomInput(Custom, TEXT("Bottom"), Vec(TEXT("Bottom"), FLinearColor(0.3f, 0.3f, 0.3f)));
		AddCustomInput(Custom, TEXT("AOColor"), Vec(TEXT("AOColor"), FLinearColor(0.5f, 0.45f, 0.5f)));
		AddCustomInput(Custom, TEXT("GradColor"), Vec(TEXT("GradColor"), FLinearColor(0.78f, 0.75f, 0.35f)));
		AddCustomInput(Custom, TEXT("OpeningColor"), Vec(TEXT("OpeningColor"), FLinearColor(0.2f, 0.12f, 0.08f)));
		AddCustomInput(Custom, TEXT("InkColor"), Vec(TEXT("InkColor"), FLinearColor(0.25f, 0.05f, 0.9f)));
		AddCustomInput(Custom, TEXT("Grad"), Vec(TEXT("Grad"), FLinearColor(0.0f, 100.0f, 0.0f, 0.0f)), 5);
		AddCustomInput(Custom, TEXT("Fade"), Vec(TEXT("Fade"), FLinearColor(-100.0f, 0.0f, 0.0f, 0.0f)), 5);
		AddCustomInput(Custom, TEXT("EdgeColor"), Vec(TEXT("EdgeColor"), FLinearColor(1.0f, 1.0f, 1.0f, 0.0f)), 5);
		AddCustomInput(Custom, TEXT("OpeningRim"), Vec(TEXT("OpeningRim"), FLinearColor(0.6f, 0.5f, 0.4f, 0.0f)), 5);
		AddCustomInput(Custom, TEXT("OpeningBarrel"), Vec(TEXT("OpeningBarrel"), FLinearColor(0.4f, 0.3f, 0.2f, 0.0f)), 5);
		AddCustomInput(Custom, TEXT("AtlasTexel"), Vec(TEXT("AtlasTexel"), FLinearColor(1.0f / 1024.0f, 1.0f / 256.0f, 2.0f, 0.0f)), 5);
		AddCustomInput(Custom, TEXT("Tile"), Vec(TEXT("Tile"), FLinearColor(0.0f, 1.0f, 0.0f, 0.0f)), 5);
		UMaterialExpressionVertexColor* VColor = AddExpr<UMaterialExpressionVertexColor>(M, -900, Y += 90);
		AddCustomInput(Custom, TEXT("VColor"), VColor, 0);
		UMaterialExpressionCollectionParameter* Key = AddExpr<UMaterialExpressionCollectionParameter>(M, -900, Y += 90);
		Key->Collection = MPC;
		Key->ParameterName = TEXT("Key");
		Key->ParameterId = MPC->GetParameterId(TEXT("Key"));
		AddCustomInput(Custom, TEXT("Key"), Key);
		AddCustomInput(Custom, TEXT("FadeColor"), Vec(TEXT("FadeColor"), FLinearColor(1.0f, 0.8f, 0.7f)));
		M->GetEditorOnlyData()->EmissiveColor.Connect(0, Custom);
		CompileMaterialAndReport(M, Report);
		return M;
	}

	UMaterial* BuildPaintBackdropMaterial(FString& Report)
	{
		UMaterial* M = GetOrCreateMaterial(RainRoot / TEXT("Materials"), TEXT("M_PaintBackdrop"));
		M->MaterialDomain = MD_Surface;
		M->BlendMode = BLEND_Opaque;
		M->SetShadingModel(MSM_Unlit);
		M->TwoSided = true;
		UMaterialExpressionCustom* Custom = AddCustom(M, TEXT("Materials/HLSL/PaintBackdrop.hlsl"), CMOT_Float3);
		int32 Y = -300;
		auto Vec = [M, &Y](const TCHAR* Name, const FLinearColor& Default)
		{
			UMaterialExpressionVectorParameter* P = AddExpr<UMaterialExpressionVectorParameter>(M, -900, Y += 90);
			P->ParameterName = Name;
			P->DefaultValue = Default;
			return P;
		};
		AddCustomInput(Custom, TEXT("Center"), Vec(TEXT("Center"), FLinearColor(1.0f, 0.82f, 0.72f)));
		AddCustomInput(Custom, TEXT("Edge"), Vec(TEXT("Edge"), FLinearColor(1.0f, 0.75f, 0.62f)));
		AddCustomInput(Custom, TEXT("Glow"), Vec(TEXT("Glow"), FLinearColor(0.92f, 0.5f, 0.3f)));
		AddCustomInput(Custom, TEXT("GlowPos"), Vec(TEXT("GlowPos"), FLinearColor(0.15f, 0.95f, 0.35f, 0.6f)), 5);
		AddCustomInput(Custom, TEXT("Shadow"), Vec(TEXT("Shadow"), FLinearColor(0.0f, 0.0f, -5000.0f, 0.0f)), 5);
		AddCustomInput(Custom, TEXT("ShadowSize"), Vec(TEXT("ShadowSize"), FLinearColor(100.0f, 30.0f, 0.0f, 0.0f)), 5);
		AddCustomInput(Custom, TEXT("ShadowColor"), Vec(TEXT("ShadowColor"), FLinearColor(0.9f, 0.6f, 0.45f, 0.0f)), 5);
		UMaterialExpressionScalarParameter* Spread = AddExpr<UMaterialExpressionScalarParameter>(M, -900, Y += 90);
		Spread->ParameterName = TEXT("Spread");
		Spread->DefaultValue = 0.9f;
		AddCustomInput(Custom, TEXT("Spread"), Spread);
		M->GetEditorOnlyData()->EmissiveColor.Connect(0, Custom);
		CompileMaterialAndReport(M, Report);
		return M;
	}

	// The trackball: a see-through globe (Materials/HLSL/PaintBall.hlsl), lined in its own space so the lines turn
	// with it; the arrow inside stays opaque.
	UMaterial* BuildPaintBallMaterial(FString& Report)
	{
		UMaterial* M = GetOrCreateMaterial(RainRoot / TEXT("Materials"), TEXT("M_PaintBall"));
		M->MaterialDomain = MD_Surface;
		M->BlendMode = BLEND_Translucent;
		M->SetShadingModel(MSM_Unlit);
		M->TwoSided = false;
		UMaterialExpressionCustom* Custom = AddCustom(M, TEXT("Materials/HLSL/PaintBall.hlsl"), CMOT_Float4);
		int32 Y = -300;
		auto Vec = [M, &Y](const TCHAR* Name, const FLinearColor& Default)
		{
			UMaterialExpressionVectorParameter* P = AddExpr<UMaterialExpressionVectorParameter>(M, -900, Y += 90);
			P->ParameterName = Name;
			P->DefaultValue = Default;
			return P;
		};
		UMaterialExpressionLocalPosition* Local = AddExpr<UMaterialExpressionLocalPosition>(M, -900, Y += 90);
		AddCustomInput(Custom, TEXT("LocalPos"), Local);
		UMaterialExpressionVertexNormalWS* Normal = AddExpr<UMaterialExpressionVertexNormalWS>(M, -900, Y += 90);
		AddCustomInput(Custom, TEXT("NormalWS"), Normal);
		UMaterialExpressionCameraVectorWS* View = AddExpr<UMaterialExpressionCameraVectorWS>(M, -900, Y += 90);
		AddCustomInput(Custom, TEXT("ToCamera"), View);
		AddCustomInput(Custom, TEXT("Body"), Vec(TEXT("Body"), FLinearColor::FromSRGBColor(FColor(0xf6, 0xf1, 0xe4))));
		AddCustomInput(Custom, TEXT("Line"), Vec(TEXT("Line"), FLinearColor::FromSRGBColor(FColor(0x8a, 0x80, 0x6c))));
		// x: opacity at the centre, y: at the rim, z: of the lines.
		AddCustomInput(Custom, TEXT("Opacity"), Vec(TEXT("Opacity"), FLinearColor(0.12f, 0.62f, 0.5f, 0.0f)));
		AddCustomInput(Custom, TEXT("Shine"), Vec(TEXT("Shine"), FLinearColor(-0.3f, -0.6f, 0.75f, 0.0f)));
		UMaterialExpressionComponentMask* RGB = AddExpr<UMaterialExpressionComponentMask>(M, 0, -60);
		RGB->R = RGB->G = RGB->B = true;
		RGB->A = false;
		RGB->Input.Connect(0, Custom);
		UMaterialExpressionComponentMask* A = AddExpr<UMaterialExpressionComponentMask>(M, 0, 60);
		A->R = A->G = A->B = false;
		A->A = true;
		A->Input.Connect(0, Custom);
		UMaterialEditingLibrary::ConnectMaterialProperty(RGB, TEXT(""), MP_EmissiveColor);
		UMaterialEditingLibrary::ConnectMaterialProperty(A, TEXT(""), MP_Opacity);
		CompileMaterialAndReport(M, Report);
		return M;
	}
}

FString URainBuilderLibrary::BuildPaintAssets()
{
	FString Report;
	UMaterialParameterCollection* MPC = BuildPaintCollection();
	bool bOk = BuildPaintSurfaceMaterial(MPC, Report) != nullptr;
	bOk &= BuildPaintBackdropMaterial(Report) != nullptr;
	bOk &= BuildPaintBallMaterial(Report) != nullptr;
	return FString::Printf(TEXT("%s\n%s"), Report.Contains(TEXT("ERROR")) ? TEXT("FAILED") : TEXT("SUCCESS"), *Report);
}

// ---------------------------------------------------------------------------------------------- Paint Valley rain
// System 1 of the paint game: GPU ink rain steered by the player, the paint carriers' hits read back.

namespace
{
	struct FPaintRainConfig
	{
		int32 Seed = 97;
		/** Seconds for a drop's velocity to turn towards a new direction. */
		float SteerTau = 0.25f;
		/** Big and small drops: bead width (cm), streak length per cm/s of speed (s), the small ones' opacity. */
		float BigWidth = 4.2f;
		float BigStreak = 0.018f;
		float SmallWidth = 2.6f;
		float SmallStreak = 0.02f;
		float SmallAlpha = 0.38f;
		/** Held in the air (the freeze): a drop rounds up this many times its width, see-through (small and big drops'
		 *  opacity, the paint look). */
		float BeadScale = 1.4f;
		float BeadAlpha = 0.5f;
		float BeadAlphaBig = 0.7f;
		/** Ripples: seconds, diameter (cm) of a small and a big drop's ring, opacity. */
		float RippleLife = 0.5f;
		float RippleSize = 9.0f;
		float RippleSizeBig = 16.0f;
		float RippleAlpha = 0.6f;
	};

	UMaterial* BuildPaintDropMaterial(FString& Report)
	{
		UMaterial* M = GetOrCreateMaterial(RainRoot / TEXT("Materials"), TEXT("M_PaintDrop"));
		M->MaterialDomain = MD_Surface;
		M->BlendMode = BLEND_Translucent;
		M->SetShadingModel(MSM_Unlit);
		M->TwoSided = true;
		M->bUsedWithNiagaraSprites = true;
		UMaterialExpressionCustom* Custom = AddCustom(M, TEXT("Materials/HLSL/PaintDrop.hlsl"), CMOT_Float4);
		UMaterialExpressionTextureCoordinate* UV = AddExpr<UMaterialExpressionTextureCoordinate>(M, -900, -200);
		AddCustomInput(Custom, TEXT("UV"), UV);
		UMaterialExpressionParticleColor* PColor = AddExpr<UMaterialExpressionParticleColor>(M, -900, -100);
		AddCustomInput(Custom, TEXT("PColor"), PColor, 5);
		UMaterialExpressionDynamicParameter* Dyn = AddExpr<UMaterialExpressionDynamicParameter>(M, -900, 0);
		Dyn->ParamNames = { TEXT("Kind"), TEXT("WidthOverLength"), TEXT("RippleAgeOrHold"), TEXT("Film") };
		AddCustomInput(Custom, TEXT("DynX"), Dyn, 0);
		AddCustomInput(Custom, TEXT("DynY"), Dyn, 1);
		AddCustomInput(Custom, TEXT("DynZ"), Dyn, 2);
		AddCustomInput(Custom, TEXT("DynW"), Dyn, 3);
		// The film look's lens image: the scene behind, looked up at the offset PaintDropLens.hlsl gives. Scene Color's
		// class is not exported from the engine: made by name, its fields set through its header.
		UMaterialExpressionCustom* Lens = AddExpr<UMaterialExpressionCustom>(M, -900, 150);
		Lens->Description = TEXT("Materials/HLSL/PaintDropLens.hlsl");
		Lens->Code = LoadTextFile(TEXT("Materials/HLSL/PaintDropLens.hlsl"));
		Lens->OutputType = CMOT_Float2;
		Lens->Inputs.Reset();
		AddCustomInput(Lens, TEXT("UV"), UV);
		AddCustomInput(Lens, TEXT("DynY"), Dyn, 1);
		UClass* SceneColorClass = FindObject<UClass>(nullptr, TEXT("/Script/Engine.MaterialExpressionSceneColor"));
		UMaterialExpressionSceneColor* SceneColor = SceneColorClass
			? static_cast<UMaterialExpressionSceneColor*>(UMaterialEditingLibrary::CreateMaterialExpression(M, SceneColorClass, -600, 150)) : nullptr;
		if (SceneColor)
		{
			SceneColor->InputMode = EMaterialSceneAttributeInputMode::OffsetFraction;
			SceneColor->Input.Connect(0, Lens);
			AddCustomInput(Custom, TEXT("Scene"), SceneColor, 0);
		}
		else
		{
			Report += TEXT("  ERROR: no Scene Color expression class\n");
		}
		UMaterialExpressionComponentMask* RGB = AddExpr<UMaterialExpressionComponentMask>(M, 0, -60);
		RGB->R = RGB->G = RGB->B = true;
		RGB->A = false;
		RGB->Input.Connect(0, Custom);
		UMaterialExpressionComponentMask* A = AddExpr<UMaterialExpressionComponentMask>(M, 0, 60);
		A->R = A->G = A->B = false;
		A->A = true;
		A->Input.Connect(0, Custom);
		UMaterialEditingLibrary::ConnectMaterialProperty(RGB, TEXT(""), MP_EmissiveColor);
		UMaterialEditingLibrary::ConnectMaterialProperty(A, TEXT(""), MP_Opacity);
		CompileMaterialAndReport(M, Report);
		return M;
	}

	FString FillPaintRain(FString Text, const FPaintRainConfig& P)
	{
		FGameRainConfig G;
		G.Seed = P.Seed;
		Text = FillGame(Text, G);
		auto F = [](float V) { return FString::Printf(TEXT("%.4f"), V); };
		Text.ReplaceInline(TEXT("$(STEER_TAU)"), *F(P.SteerTau));
		Text.ReplaceInline(TEXT("$(BIG_WIDTH)"), *F(P.BigWidth));
		Text.ReplaceInline(TEXT("$(BIG_STREAK)"), *F(P.BigStreak));
		Text.ReplaceInline(TEXT("$(SMALL_WIDTH)"), *F(P.SmallWidth));
		Text.ReplaceInline(TEXT("$(SMALL_STREAK)"), *F(P.SmallStreak));
		Text.ReplaceInline(TEXT("$(SMALL_ALPHA)"), *F(P.SmallAlpha));
		Text.ReplaceInline(TEXT("$(BEAD_SCALE)"), *F(P.BeadScale));
		Text.ReplaceInline(TEXT("$(BEAD_ALPHA_BIG)"), *F(P.BeadAlphaBig));
		Text.ReplaceInline(TEXT("$(BEAD_ALPHA)"), *F(P.BeadAlpha));
		Text.ReplaceInline(TEXT("$(RIPPLE_LIFE)"), *F(P.RippleLife));
		Text.ReplaceInline(TEXT("$(RIPPLE_SIZE)"), *F(P.RippleSize));
		Text.ReplaceInline(TEXT("$(RIPPLE_SIZE_BIG)"), *F(P.RippleSizeBig));
		Text.ReplaceInline(TEXT("$(RIPPLE_ALPHA)"), *F(P.RippleAlpha));
		Text.ReplaceInline(TEXT("$(KIND_TARGET)"), *F(RainShield::BoxProtected));
		FString Boxes;
		for (int32 i = 0; i < RainShield::MaxBoxes; ++i)
		{
			Boxes += FString::Printf(TEXT("\t\tGR_BOX(User.RainBox%dMin, User.RainBox%dMax, User.RainBox%dKind, User.RainBox%dFull)\n"), i, i, i, i);
		}
		Text.ReplaceInline(TEXT("$(PAINT_BOX_TESTS)"), *Boxes);
		return Text;
	}
}

FString URainBuilderLibrary::BuildPaintRainAssets(int32 RainCount)
{
	FString Report;
	UMaterial* Material = BuildPaintDropMaterial(Report);
	const FPaintRainConfig Config;
	const FString ModPath = RainRoot / TEXT("Niagara/Modules");
	UNiagaraScript* Spawn = CreateHlslModule(ModPath, TEXT("NM_PaintRainSpawn"), FillPaintRain(LoadTextFile(TEXT("Niagara/HLSL/PaintRainSpawn.hlsl")), Config),
		TEXT("Paint Valley: ink drops spread through the stream"), ParticleModuleMask);
	UNiagaraScript* Update = CreateHlslModule(ModPath, TEXT("NM_PaintRainUpdate"), FillPaintRain(LoadTextFile(TEXT("Niagara/HLSL/PaintRainUpdate.hlsl")), Config),
		TEXT("Paint Valley: steer, boxes, paint hits, recycle"), ParticleModuleMask);
	SaveAsset(Spawn);
	SaveAsset(Update);
	const FGameExportInputs Inputs = FindExportInputs(Report);

	UNiagaraSystem* System = CreateSystemAsset(RainRoot / TEXT("Niagara"), TEXT("NS_PaintRain"));
	AddGameUserParameters(System, RainCount);
	{
		auto Add = [System](FNiagaraVariable Var) { Var.SetName(*(TEXT("User.") + Var.GetName().ToString())); AddUserParameter(System, Var); };
		FNiagaraVariable Speed(FNiagaraTypeDefinition::GetFloatDef(), TEXT("RainPaintSpeed"));
		Speed.SetValue(700.0f);
		Add(Speed);
		auto Float = [&Add](const TCHAR* Name, float V) { FNiagaraVariable Var(FNiagaraTypeDefinition::GetFloatDef(), Name); Var.SetValue(V); Add(Var); };
		Float(TEXT("RainPaintBigFrac"), 0.03f);
		Float(TEXT("RainPaintLeft"), 1.0f);
		Float(TEXT("RainPaintFilm"), 0.0f);
		Float(TEXT("RainPaintOpacity"), 1.0f);
		Float(TEXT("RainPaintCloudWait"), 4.0f);
		Float(TEXT("RainPaintEpoch"), 0.0f);
		Float(TEXT("RainFreeze"), 0.0f);
		for (int32 i = 0; i < RainShield::MaxBoxes; ++i)
		{
			Float(*FString::Printf(TEXT("RainBox%dFull"), i), 0.0f);
		}
		FNiagaraVariable Ink(FNiagaraTypeDefinition::GetColorDef(), TEXT("RainPaintInk"));
		Ink.SetValue(FLinearColor(0.15f, 0.45f, 0.85f, 1.0f));
		Add(Ink);
		FNiagaraVariable Facing(FNiagaraTypeDefinition::GetVec3Def(), TEXT("RainViewFacing"));
		Facing.SetValue(FVector3f(-0.577f, -0.577f, 0.577f));
		Add(Facing);
	}

	UNiagaraEmitter* Emitter = CreateEmptyEmitter(TEXT("PaintRain"), false);
	FVersionedNiagaraEmitterData* D = Emitter->GetLatestEmitterData();
	D->SimTarget = ENiagaraSimTarget::GPUComputeSim;
	D->bLocalSpace = false;
	D->CalculateBoundsMode = ENiagaraEmitterCalculateBoundMode::Fixed;
	D->FixedBounds = FBox(FVector(-5000.0), FVector(5000.0));
	UNiagaraNodeFunctionCall* State = AddModuleByPath(*D, ENiagaraScriptUsage::EmitterUpdateScript, TEXT("/Niagara/Modules/Emitter/EmitterState.EmitterState"));
	SetModuleInputFloat(Emitter, *D, ENiagaraScriptUsage::EmitterUpdateScript, State, TEXT("Loop Duration"), 1.0e7f);
	UNiagaraNodeFunctionCall* Burst = AddModuleByPath(*D, ENiagaraScriptUsage::EmitterUpdateScript, TEXT("/Niagara/Modules/Emitter/SpawnBurst_Instantaneous.SpawnBurst_Instantaneous"));
	const bool bCountLinked = LinkModuleInput(Burst, TEXT("Spawn Count"), FNiagaraTypeDefinition::GetIntDef(), FNiagaraVariableBase(FNiagaraTypeDefinition::GetIntDef(), TEXT("User.RainGameCount")));
	LinkModuleInput(Burst, TEXT("Age"), FNiagaraTypeDefinition::GetFloatDef(), FNiagaraVariableBase(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Emitter.LoopedAge")));

	const FNiagaraTypeDefinition F = FNiagaraTypeDefinition::GetFloatDef();
	const FNiagaraTypeDefinition V3 = FNiagaraTypeDefinition::GetVec3Def();
	const FNiagaraTypeDefinition V4 = FNiagaraTypeDefinition::GetVec4Def();
	TArray<FNiagaraVariable> Attributes = {
		FNiagaraVariable(FNiagaraTypeDefinition::GetPositionDef(), TEXT("Particles.Position")),
		FNiagaraVariable(V3, TEXT("Particles.Velocity")),
		FNiagaraVariable(FNiagaraTypeDefinition::GetColorDef(), TEXT("Particles.Color")),
		FNiagaraVariable(FNiagaraTypeDefinition::GetVec2Def(), TEXT("Particles.SpriteSize")),
		FNiagaraVariable(V3, TEXT("Particles.SpriteAlignment")),
		FNiagaraVariable(V4, TEXT("Particles.DynamicMaterialParameter")),
		FNiagaraVariable(V4, TEXT("Particles.RainRand")),
		FNiagaraVariable(F, TEXT("Particles.RainTimer")),
		FNiagaraVariable(F, TEXT("Particles.PaintAge")),
		FNiagaraVariable(F, TEXT("Particles.PaintKind")),
		FNiagaraVariable(F, TEXT("Particles.PaintCarrier")),
		FNiagaraVariable(F, TEXT("Particles.PaintRipple")),
		FNiagaraVariable(F, TEXT("Particles.PaintState")),
		FNiagaraVariable(F, TEXT("Particles.PaintEpoch")),
		FNiagaraVariable(V3, TEXT("Particles.PaintNormal")),
		FNiagaraVariable(V3, TEXT("Particles.SpriteFacing")),
		FNiagaraVariable(GameHitParam),
		FNiagaraVariable(Inputs.PositionType, TEXT("Particles.RainGameHitPos")),
	};
	TArray<FString> Defaults;
	Defaults.SetNum(Attributes.Num());
	AddAttributeDeclarations(*D, ENiagaraScriptUsage::ParticleSpawnScript, Attributes, Defaults);
	AddModule(*D, ENiagaraScriptUsage::ParticleSpawnScript, Spawn);
	AddModule(*D, ENiagaraScriptUsage::ParticleUpdateScript, Update);
	// After the update: the carriers that hit a box this frame (hit point, box kind as size, velocity).
	const FNiagaraVariableBase Velocity(Inputs.VelocityType, TEXT("Particles.Velocity"));
	const bool bExportLinked = AddExportModule(Emitter, *D, Inputs, FNiagaraVariableBase(F, TEXT("Particles.PaintKind")), &Velocity);

	// Drops face the camera (User.RainViewFacing), rings lie on the face they landed on: a custom facing for both.
	UNiagaraSpriteRendererProperties* Sprite = NewObject<UNiagaraSpriteRendererProperties>(Emitter, TEXT("InkSprites"));
	Sprite->Material = Material;
	Sprite->Alignment = ENiagaraSpriteAlignment::CustomAlignment;
	Sprite->FacingMode = ENiagaraSpriteFacingMode::CustomFacingVector;
	Sprite->SortMode = ENiagaraSortMode::ViewDepth;
	Sprite->bSortOnlyWhenTranslucent = true;
	Sprite->bCastShadows = false;
	Emitter->AddRenderer(Sprite, D->Version.VersionGuid);

	FNiagaraEditorUtilities::AddEmitterToSystem(*System, *Emitter, Emitter->GetExposedVersion().VersionGuid, true);
	const bool bOk = CompileAndReport(System, Report);
	SaveAsset(System);
	Report += FString::Printf(TEXT("Emitter PaintRain: User.RainGameCount drops (%s), %d boxes, carrier hit export %s\n"),
		bCountLinked ? TEXT("linked") : TEXT("COUNT NOT LINKED"), RainShield::MaxBoxes, bExportLinked ? TEXT("linked") : TEXT("NOT LINKED"));
	return FString::Printf(TEXT("%s\n%s"), bOk && !Report.Contains(TEXT("ERROR")) ? TEXT("SUCCESS") : TEXT("FAILED"), *Report);
}

// ---------------------------------------------------------------------------------------------- Paint Valley balls
// System 2 of the paint game: CPU paint balls (meshes) that bounce off the level and burst on the target.

namespace
{
	struct FPaintBallConfig
	{
		int32 Seed = 41;
		/** Radius (cm), launch speed, pull along the direction (cm/s^2) and top speed (cm/s). */
		float Radius = 5.0f;
		float LaunchSpeed = 600.0f;
		float Pull = 981.0f;
		float TopSpeed = 1100.0f;
		/** Bounces: restitution, friction; below RestSpeed a ball rests, after RestTime s (or MaxBounces) it goes back. */
		float Restitution = 0.72f;
		float Friction = 0.08f;
		float RestSpeed = 40.0f;
		float RestTime = 0.5f;
		float MaxBounces = 10.0f;
		/** Seconds a burst ball takes to flatten on the face. */
		float BurstTime = 0.12f;
	};

	FString FillPaintBall(FString Text, const FPaintBallConfig& C)
	{
		auto F = [](float V) { return FString::Printf(TEXT("%.4f"), V); };
		Text.ReplaceInline(TEXT("$(SEED_F)"), *F(float(C.Seed) * 0.6180339f));
		Text.ReplaceInline(TEXT("$(BALL_RADIUS)"), *F(C.Radius));
		Text.ReplaceInline(TEXT("$(BALL_LAUNCH_SPEED)"), *F(C.LaunchSpeed));
		Text.ReplaceInline(TEXT("$(BALL_PULL)"), *F(C.Pull));
		Text.ReplaceInline(TEXT("$(BALL_TOP_SPEED)"), *F(C.TopSpeed));
		Text.ReplaceInline(TEXT("$(BALL_RESTITUTION)"), *F(C.Restitution));
		Text.ReplaceInline(TEXT("$(BALL_FRICTION)"), *F(C.Friction));
		Text.ReplaceInline(TEXT("$(BALL_REST_SPEED)"), *F(C.RestSpeed));
		Text.ReplaceInline(TEXT("$(BALL_REST_TIME)"), *F(C.RestTime));
		Text.ReplaceInline(TEXT("$(BALL_MAX_BOUNCES)"), *F(C.MaxBounces));
		Text.ReplaceInline(TEXT("$(BALL_BURST_TIME)"), *F(C.BurstTime));
		Text.ReplaceInline(TEXT("$(KIND_TARGET)"), *F(RainShield::BoxProtected));
		FString Boxes;
		for (int32 i = 0; i < RainShield::MaxBoxes; ++i)
		{
			Boxes += FString::Printf(TEXT("\tBB_BOX(User.RainBox%dMin, User.RainBox%dMax, User.RainBox%dKind, User.RainBox%dFull)\n"), i, i, i, i);
		}
		Text.ReplaceInline(TEXT("$(BALL_BOX_TESTS)"), *Boxes);
		return Text;
	}

	UMaterial* BuildPaintBlobMaterial(FString& Report)
	{
		UMaterial* M = GetOrCreateMaterial(RainRoot / TEXT("Materials"), TEXT("M_PaintBlob"));
		M->MaterialDomain = MD_Surface;
		M->BlendMode = BLEND_Opaque;
		M->SetShadingModel(MSM_Unlit);
		M->bUsedWithNiagaraMeshParticles = true;
		UMaterialExpressionCustom* Custom = AddCustom(M, TEXT("Materials/HLSL/PaintBlob.hlsl"), CMOT_Float3);
		UMaterialExpressionVertexNormalWS* Normal = AddExpr<UMaterialExpressionVertexNormalWS>(M, -900, -100);
		AddCustomInput(Custom, TEXT("NormalWS"), Normal);
		UMaterialExpressionCameraVectorWS* View = AddExpr<UMaterialExpressionCameraVectorWS>(M, -900, 0);
		AddCustomInput(Custom, TEXT("ToCamera"), View);
		UMaterialExpressionParticleColor* PColor = AddExpr<UMaterialExpressionParticleColor>(M, -900, 100);
		AddCustomInput(Custom, TEXT("PColor"), PColor, 5);
		M->GetEditorOnlyData()->EmissiveColor.Connect(0, Custom);
		CompileMaterialAndReport(M, Report);
		return M;
	}
}

FString URainBuilderLibrary::BuildPaintBallAssets(int32 BallCount)
{
	FString Report;
	UMaterial* Material = BuildPaintBlobMaterial(Report);
	const FPaintBallConfig Config;
	const FString ModPath = RainRoot / TEXT("Niagara/Modules");
	const FGameExportInputs Inputs = FindExportInputs(Report);
	bool bOk = true;
	// The same HLSL for both targets: NS_PaintBalls (CPU) is System 2; NS_PaintBallsGPU is the CPU -> GPU port
	// measured in the report (rain.paint.BallsGPU 1).
	for (const bool bGpu : { false, true })
	{
		const TCHAR* Suffix = bGpu ? TEXT("GPU") : TEXT("");
		UNiagaraScript* Spawn = CreateHlslModule(ModPath, FString(TEXT("NM_PaintBallSpawn")) + Suffix, FillPaintBall(LoadTextFile(TEXT("Niagara/HLSL/PaintBallSpawn.hlsl")), Config),
			TEXT("Paint Valley: paint balls waiting in the cloud"), ParticleModuleMask);
		UNiagaraScript* Update = CreateHlslModule(ModPath, FString(TEXT("NM_PaintBallUpdate")) + Suffix, FillPaintBall(LoadTextFile(TEXT("Niagara/HLSL/PaintBallUpdate.hlsl")), Config),
			TEXT("Paint Valley: paint balls fly, bounce and burst on the target"), ParticleModuleMask);
		UNiagaraScript* Neighbors = CreateReaderModule(ModPath, FString(TEXT("NM_PaintBallNeighbors")) + Suffix, FillPaintBall(LoadTextFile(TEXT("Niagara/HLSL/PaintBallNeighbors.hlsl")), Config),
			TEXT("Paint Valley: paint balls find their nearest balls and collide with them"), ParticleModuleMask, Report);
		SaveAsset(Spawn);
		SaveAsset(Update);
		if (Neighbors)
		{
			SaveAsset(Neighbors);
		}

		UNiagaraSystem* System = CreateSystemAsset(RainRoot / TEXT("Niagara"), bGpu ? TEXT("NS_PaintBallsGPU") : TEXT("NS_PaintBalls"));
		AddGameUserParameters(System, BallCount);
		{
			auto Add = [System](FNiagaraVariable Var) { Var.SetName(*(TEXT("User.") + Var.GetName().ToString())); AddUserParameter(System, Var); };
			auto Float = [&Add](const TCHAR* Name, float V) { FNiagaraVariable Var(FNiagaraTypeDefinition::GetFloatDef(), Name); Var.SetValue(V); Add(Var); };
			Float(TEXT("RainBallLeft"), 1.0f);
			Float(TEXT("RainBallWait"), 6.0f);
			Float(TEXT("RainBallEpoch"), 0.0f);
			Float(TEXT("RainFreeze"), 0.0f);
			for (int32 i = 0; i < RainShield::MaxBoxes; ++i)
			{
				Float(*FString::Printf(TEXT("RainBox%dFull"), i), 0.0f);
			}
			FNiagaraVariable Ink(FNiagaraTypeDefinition::GetColorDef(), TEXT("RainPaintInk"));
			Ink.SetValue(FLinearColor(0.15f, 0.45f, 0.85f, 1.0f));
			Add(Ink);
		}

		UNiagaraEmitter* Emitter = CreateEmptyEmitter(TEXT("PaintBalls"), false);
		FVersionedNiagaraEmitterData* D = Emitter->GetLatestEmitterData();
		D->SimTarget = bGpu ? ENiagaraSimTarget::GPUComputeSim : ENiagaraSimTarget::CPUSim;
		D->bLocalSpace = false;
		D->CalculateBoundsMode = ENiagaraEmitterCalculateBoundMode::Fixed;
		D->FixedBounds = FBox(FVector(-5000.0), FVector(5000.0));
		UNiagaraNodeFunctionCall* State = AddModuleByPath(*D, ENiagaraScriptUsage::EmitterUpdateScript, TEXT("/Niagara/Modules/Emitter/EmitterState.EmitterState"));
		SetModuleInputFloat(Emitter, *D, ENiagaraScriptUsage::EmitterUpdateScript, State, TEXT("Loop Duration"), 1.0e7f);
		UNiagaraNodeFunctionCall* Burst = AddModuleByPath(*D, ENiagaraScriptUsage::EmitterUpdateScript, TEXT("/Niagara/Modules/Emitter/SpawnBurst_Instantaneous.SpawnBurst_Instantaneous"));
		const bool bCountLinked = LinkModuleInput(Burst, TEXT("Spawn Count"), FNiagaraTypeDefinition::GetIntDef(), FNiagaraVariableBase(FNiagaraTypeDefinition::GetIntDef(), TEXT("User.RainGameCount")));
		LinkModuleInput(Burst, TEXT("Age"), FNiagaraTypeDefinition::GetFloatDef(), FNiagaraVariableBase(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Emitter.LoopedAge")));

		const FNiagaraTypeDefinition F = FNiagaraTypeDefinition::GetFloatDef();
		const FNiagaraTypeDefinition V3 = FNiagaraTypeDefinition::GetVec3Def();
		TArray<FNiagaraVariable> Attributes = {
			FNiagaraVariable(FNiagaraTypeDefinition::GetPositionDef(), TEXT("Particles.Position")),
			FNiagaraVariable(V3, TEXT("Particles.Velocity")),
			FNiagaraVariable(FNiagaraTypeDefinition::GetColorDef(), TEXT("Particles.Color")),
			FNiagaraVariable(V3, TEXT("Particles.Scale")),
			FNiagaraVariable(FNiagaraTypeDefinition::GetQuatDef(), TEXT("Particles.MeshOrientation")),
			FNiagaraVariable(FNiagaraTypeDefinition::GetVec4Def(), TEXT("Particles.RainRand")),
			FNiagaraVariable(F, TEXT("Particles.BallState")),
			FNiagaraVariable(F, TEXT("Particles.BallTimer")),
			FNiagaraVariable(F, TEXT("Particles.BallBounces")),
			FNiagaraVariable(F, TEXT("Particles.BallSquash")),
			FNiagaraVariable(F, TEXT("Particles.BallCycle")),
			FNiagaraVariable(F, TEXT("Particles.BallEpoch")),
			FNiagaraVariable(V3, TEXT("Particles.BallNormal")),
			FNiagaraVariable(FNiagaraTypeDefinition::GetVec2Def(), TEXT("Particles.BallNear")),
			FNiagaraVariable(F, TEXT("Particles.PaintKind")),
			FNiagaraVariable(GameHitParam),
			FNiagaraVariable(Inputs.PositionType, TEXT("Particles.RainGameHitPos")),
		};
		TArray<FString> Defaults;
		Defaults.SetNum(Attributes.Num());
		AddAttributeDeclarations(*D, ENiagaraScriptUsage::ParticleSpawnScript, Attributes, Defaults);
		AddModule(*D, ENiagaraScriptUsage::ParticleSpawnScript, Spawn);
		AddModule(*D, ENiagaraScriptUsage::ParticleUpdateScript, Update);
		if (Neighbors)
		{
			AddModule(*D, ENiagaraScriptUsage::ParticleUpdateScript, Neighbors);
		}
		// After the update: the balls that burst on the target this frame (contact point, box kind as size, velocity).
		const FNiagaraVariableBase Velocity(Inputs.VelocityType, TEXT("Particles.Velocity"));
		const bool bExportLinked = AddExportModule(Emitter, *D, Inputs, FNiagaraVariableBase(F, TEXT("Particles.PaintKind")), &Velocity);

		UNiagaraMeshRendererProperties* Meshes = NewObject<UNiagaraMeshRendererProperties>(Emitter, TEXT("BallMeshes"));
		Meshes->Meshes.SetNum(1);
		Meshes->Meshes[0].Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
		Meshes->bOverrideMaterials = true;
		FNiagaraMeshMaterialOverride& Override = Meshes->OverrideMaterials.AddDefaulted_GetRef();
		Override.ExplicitMat = Material;
		Emitter->AddRenderer(Meshes, D->Version.VersionGuid);

		FNiagaraEditorUtilities::AddEmitterToSystem(*System, *Emitter, Emitter->GetExposedVersion().VersionGuid, true);
		bOk &= CompileAndReport(System, Report);
		SaveAsset(System);
		Report += FString::Printf(TEXT("Emitter PaintBalls (%s): User.RainGameCount balls (%s), %d boxes, burst export %s\n"), bGpu ? TEXT("GPU") : TEXT("CPU"),
			bCountLinked ? TEXT("linked") : TEXT("COUNT NOT LINKED"), RainShield::MaxBoxes, bExportLinked ? TEXT("linked") : TEXT("NOT LINKED"));
	}
	return FString::Printf(TEXT("%s\n%s"), bOk && !Report.Contains(TEXT("ERROR")) ? TEXT("SUCCESS") : TEXT("FAILED"), *Report);
}

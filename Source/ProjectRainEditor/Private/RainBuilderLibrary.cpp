#include "RainBuilderLibrary.h"
#include "RainNiagaraAuthoring.h"

#include "NiagaraEmitter.h"
#include "NiagaraSystem.h"
#include "NiagaraScript.h"
#include "NiagaraScriptSource.h"
#include "NiagaraGraph.h"
#include "NiagaraNodeCustomHlsl.h"
#include "EdGraphSchema_Niagara.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraEditorUtilities.h"
#include "NiagaraSpriteRendererProperties.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Materials/MaterialInterface.h"

using namespace RainAuthoring;

static constexpr int32 ParticleModuleMask =
	(1 << int32(ENiagaraScriptUsage::ParticleSpawnScript)) |
	(1 << int32(ENiagaraScriptUsage::ParticleSpawnScriptInterpolated)) |
	(1 << int32(ENiagaraScriptUsage::ParticleUpdateScript)) |
	(1 << int32(ENiagaraScriptUsage::ParticleSimulationStageScript));

FString URainBuilderLibrary::BuildTestSystem()
{
	const FString Root = TEXT("/Game/RainMagic/Test");
	UNiagaraScript* Red = CreateHlslModule(Root, TEXT("NM_TestRed"),
		TEXT("Particles.Color = float4(1.0, 0.05, 0.02, 1.0);\nParticles.SpriteSize = float2(40.0, 40.0);\n"),
		TEXT("Test module"), ParticleModuleMask);

	UNiagaraSystem* System = CreateSystemAsset(Root, TEXT("NS_Test"));
	UNiagaraEmitter* Emitter = CreateEmptyEmitter(TEXT("TestEmitter"), true);
	{
		FVersionedNiagaraEmitterData* Data = Emitter->GetLatestEmitterData();
		Data->SimTarget = ENiagaraSimTarget::GPUComputeSim;
		Data->CalculateBoundsMode = ENiagaraEmitterCalculateBoundMode::Fixed;
		Data->FixedBounds = FBox(FVector(-500), FVector(500));
		AddModule(*Data, ENiagaraScriptUsage::ParticleUpdateScript, Red);
	}
	FNiagaraEditorUtilities::AddEmitterToSystem(*System, *Emitter, Emitter->GetExposedVersion().VersionGuid, true);

	FString Report;
	const bool bOk = CompileAndReport(System, Report);
	SaveAsset(Red);
	SaveAsset(System);
	return FString::Printf(TEXT("%s\n%s"), bOk ? TEXT("SUCCESS") : TEXT("FAILED"), *Report);
}

FString URainBuilderLibrary::DescribeNiagaraSystem(const FString& ObjectPath)
{
	UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, *ObjectPath);
	if (!System)
	{
		return TEXT("not found");
	}
	FString Out;
	auto DumpScript = [&Out](UNiagaraScript* Script, const FString& Owner)
	{
		if (!Script)
		{
			return;
		}
		Out += FString::Printf(TEXT("[%s] usage=%d status=%d\n"), *Owner, int32(Script->GetUsage()), int32(Script->GetLastCompileStatus()));
		TArray<FNiagaraVariable> Params;
		Script->RapidIterationParameters.GetParameters(Params);
		for (const FNiagaraVariable& P : Params)
		{
			const int32* Offset = Script->RapidIterationParameters.FindParameterOffset(P);
			FString Value;
			if (Offset)
			{
				const uint8* Data = Script->RapidIterationParameters.GetParameterData(*Offset);
				if (P.GetType() == FNiagaraTypeDefinition::GetFloatDef()) Value = FString::SanitizeFloat(*(const float*)Data);
				else if (P.GetType() == FNiagaraTypeDefinition::GetIntDef()) Value = FString::FromInt(*(const int32*)Data);
				else if (P.GetType().IsEnum()) Value = FString::FromInt(*(const int32*)Data);
				else if (P.GetType() == FNiagaraTypeDefinition::GetBoolDef()) Value = (*(const int32*)Data) ? TEXT("true") : TEXT("false");
			}
			Out += FString::Printf(TEXT("    %s (%s) = %s\n"), *P.GetName().ToString(), *P.GetType().GetName(), *Value);
		}
	};
	DumpScript(System->GetSystemSpawnScript(), TEXT("SystemSpawn"));
	DumpScript(System->GetSystemUpdateScript(), TEXT("SystemUpdate"));
	for (const FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
	{
		FVersionedNiagaraEmitterData* D = Handle.GetEmitterData();
		Out += FString::Printf(TEXT("== Emitter %s enabled=%d simtarget=%d\n"), *Handle.GetName().ToString(), Handle.GetIsEnabled() ? 1 : 0, D ? int32(D->SimTarget) : -1);
		if (D)
		{
			TArray<UNiagaraScript*> Scripts;
			D->GetScripts(Scripts, false, true);
			for (UNiagaraScript* S : Scripts)
			{
				DumpScript(S, Handle.GetName().ToString());
			}
		}
	}
	return Out;
}

FString URainBuilderLibrary::DumpGeneratedHlsl(const FString& ObjectPath)
{
	UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, *ObjectPath);
	if (!System)
	{
		return TEXT("not found");
	}
	const FString Dir = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("NiagaraHlsl"));
	FString Out;
	for (const FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
	{
		FVersionedNiagaraEmitterData* D = Handle.GetEmitterData();
		if (!D)
		{
			continue;
		}
		TArray<UNiagaraScript*> Scripts;
		D->GetScripts(Scripts, false, true);
		for (UNiagaraScript* S : Scripts)
		{
			const FNiagaraVMExecutableData& Exe = S->GetVMExecutableData();
			for (const FString* Src : { &Exe.LastHlslTranslation, &Exe.LastHlslTranslationGPU })
			{
				if (Src->IsEmpty())
				{
					continue;
				}
				const FString File = Dir / FString::Printf(TEXT("%s_%s_u%d%s.hlsl"), *System->GetName(), *Handle.GetName().ToString(), int32(S->GetUsage()), Src == &Exe.LastHlslTranslationGPU ? TEXT("_gpu") : TEXT(""));
				FFileHelper::SaveStringToFile(*Src, *File);
				Out += File + TEXT("\n");
			}
		}
	}
	return Out.IsEmpty() ? TEXT("no HLSL cached (compile came from DDC?)") : Out;
}

FString URainBuilderLibrary::BuildDiagSystem(const FString& Name, const FString& SpawnHlsl, const FString& UpdateHlsl,
	bool bDefaultModules, bool bUseBurst, int32 Count, bool bGpu, bool bDeclareRainAttributes, bool bUseRainMaterial)
{
	const FString Root = TEXT("/Game/RainMagic/Diag");
	constexpr int32 Mask = (1 << int32(ENiagaraScriptUsage::ParticleSpawnScript)) | (1 << int32(ENiagaraScriptUsage::ParticleSpawnScriptInterpolated)) |
		(1 << int32(ENiagaraScriptUsage::ParticleUpdateScript)) | (1 << int32(ENiagaraScriptUsage::ParticleSimulationStageScript));

	UNiagaraSystem* System = CreateSystemAsset(Root, TEXT("NS_") + Name);
	AddRainUserParameters(System);
	UNiagaraEmitter* Emitter = CreateEmptyEmitter(Name, bDefaultModules);
	FVersionedNiagaraEmitterData* D = Emitter->GetLatestEmitterData();
	D->SimTarget = bGpu ? ENiagaraSimTarget::GPUComputeSim : ENiagaraSimTarget::CPUSim;
	D->CalculateBoundsMode = ENiagaraEmitterCalculateBoundMode::Fixed;
	D->FixedBounds = FBox(FVector(-3000), FVector(3000));
	FString Report;
	if (!bDefaultModules)
	{
		UNiagaraNodeFunctionCall* State = AddModuleByPath(*D, ENiagaraScriptUsage::EmitterUpdateScript, TEXT("/Niagara/Modules/Emitter/EmitterState.EmitterState"));
		SetModuleInputFloat(Emitter, *D, ENiagaraScriptUsage::EmitterUpdateScript, State, TEXT("Loop Duration"), 1.0e7f);
		if (bUseBurst)
		{
			UNiagaraNodeFunctionCall* Burst = AddModuleByPath(*D, ENiagaraScriptUsage::EmitterUpdateScript, TEXT("/Niagara/Modules/Emitter/SpawnBurst_Instantaneous.SpawnBurst_Instantaneous"));
			SetModuleInputInt(Emitter, *D, ENiagaraScriptUsage::EmitterUpdateScript, Burst, TEXT("Spawn Count"), Count);
			const bool bLinked = LinkModuleInput(Burst, TEXT("Age"), FNiagaraTypeDefinition::GetFloatDef(), FNiagaraVariableBase(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Emitter.LoopedAge")));
			Report += FString::Printf(TEXT("burst age linked=%d\n"), bLinked ? 1 : 0);
		}
		else
		{
			UNiagaraNodeFunctionCall* Rate = AddModuleByPath(*D, ENiagaraScriptUsage::EmitterUpdateScript, TEXT("/Niagara/Modules/Emitter/SpawnRate.SpawnRate"));
			SetModuleInputFloat(Emitter, *D, ENiagaraScriptUsage::EmitterUpdateScript, Rate, TEXT("SpawnRate"), float(Count));
		}
	}
	if (bDeclareRainAttributes)
	{
		const FNiagaraTypeDefinition F = FNiagaraTypeDefinition::GetFloatDef();
		const FNiagaraTypeDefinition V3 = FNiagaraTypeDefinition::GetVec3Def();
		const FNiagaraTypeDefinition V4 = FNiagaraTypeDefinition::GetVec4Def();
		TArray<FNiagaraVariable> Attrs = {
			FNiagaraVariable(FNiagaraTypeDefinition::GetPositionDef(), TEXT("Particles.Position")),
			FNiagaraVariable(V3, TEXT("Particles.Velocity")),
			FNiagaraVariable(FNiagaraTypeDefinition::GetColorDef(), TEXT("Particles.Color")),
			FNiagaraVariable(FNiagaraTypeDefinition::GetVec2Def(), TEXT("Particles.SpriteSize")),
			FNiagaraVariable(V3, TEXT("Particles.SpriteAlignment")),
			FNiagaraVariable(V4, TEXT("Particles.DynamicMaterialParameter")),
			FNiagaraVariable(F, TEXT("Particles.RainDiameter")),
			FNiagaraVariable(V4, TEXT("Particles.RainRand")),
			FNiagaraVariable(F, TEXT("Particles.RainFrozen")),
			FNiagaraVariable(F, TEXT("Particles.RainState")),
			FNiagaraVariable(F, TEXT("Particles.RainTimer")),
			FNiagaraVariable(V3, TEXT("Particles.RainDir")),
		};
		TArray<FString> Defaults;
		Defaults.SetNum(Attrs.Num());
		AddAttributeDeclarations(*D, ENiagaraScriptUsage::ParticleSpawnScript, Attrs, Defaults);
	}
	if (!SpawnHlsl.IsEmpty())
	{
		AddModule(*D, ENiagaraScriptUsage::ParticleSpawnScript, CreateHlslModule(Root, TEXT("NM_") + Name + TEXT("_Spawn"), SpawnHlsl, TEXT("diag"), Mask));
	}
	if (!UpdateHlsl.IsEmpty())
	{
		AddModule(*D, ENiagaraScriptUsage::ParticleUpdateScript, CreateHlslModule(Root, TEXT("NM_") + Name + TEXT("_Update"), UpdateHlsl, TEXT("diag"), Mask));
	}
	if (!bDefaultModules)
	{
		UNiagaraSpriteRendererProperties* Sprite = NewObject<UNiagaraSpriteRendererProperties>(Emitter, TEXT("Sprites"));
		if (bUseRainMaterial)
		{
			Sprite->Material = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/RainMagic/Materials/M_RainDrop.M_RainDrop"));
			Sprite->Alignment = ENiagaraSpriteAlignment::CustomAlignment;
			Sprite->FacingMode = ENiagaraSpriteFacingMode::FaceCamera;
			Sprite->PixelCoverageMode = ENiagaraRendererPixelCoverageMode::Enabled_A;
		}
		Emitter->AddRenderer(Sprite, D->Version.VersionGuid);
	}
	FNiagaraEditorUtilities::AddEmitterToSystem(*System, *Emitter, Emitter->GetExposedVersion().VersionGuid, true);
	const bool bOk = CompileAndReport(System, Report);
	SaveAsset(System);
	return FString::Printf(TEXT("%s %s\n%s"), bOk ? TEXT("SUCCESS") : TEXT("FAILED"), *System->GetPathName(), *Report);
}

FString URainBuilderLibrary::DumpNiagaraScriptGraph(const FString& ObjectPath)
{
	UNiagaraScript* Script = LoadObject<UNiagaraScript>(nullptr, *ObjectPath);
	UNiagaraScriptSource* Source = Script ? Cast<UNiagaraScriptSource>(Script->GetLatestSource()) : nullptr;
	if (!Source || !Source->NodeGraph)
	{
		return TEXT("no graph");
	}
	FString Out;
	for (UEdGraphNode* Node : Source->NodeGraph->Nodes)
	{
		Out += FString::Printf(TEXT("NODE %s %s\n"), *Node->GetClass()->GetName(), *Node->GetNodeTitle(ENodeTitleType::ListView).ToString());
		for (UEdGraphPin* Pin : Node->Pins)
		{
			FString Links;
			for (UEdGraphPin* L : Pin->LinkedTo)
			{
				Links += FString::Printf(TEXT(" -> %s.%s"), *L->GetOwningNode()->GetClass()->GetName(), *L->PinName.ToString());
			}
			const FNiagaraTypeDefinition Type = UEdGraphSchema_Niagara::PinToTypeDefinition(Pin);
			Out += FString::Printf(TEXT("  %s %s [%s]%s%s\n"), Pin->Direction == EGPD_Input ? TEXT("in ") : TEXT("out"), *Pin->PinName.ToString(),
				*Type.GetName(), *Links, Pin->bHidden ? TEXT(" hidden") : TEXT(""));
		}
		if (UNiagaraNodeCustomHlsl* Custom = Cast<UNiagaraNodeCustomHlsl>(Node))
		{
			if (FStrProperty* Prop = FindFProperty<FStrProperty>(UNiagaraNodeCustomHlsl::StaticClass(), TEXT("CustomHlsl")))
			{
				Out += TEXT("  HLSL:\n") + Prop->GetPropertyValue_InContainer(Custom) + TEXT("\n");
			}
		}
	}
	return Out;
}

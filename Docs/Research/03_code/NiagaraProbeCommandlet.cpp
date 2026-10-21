#include "NiagaraProbeCommandlet.h"

#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"
#include "UObject/Package.h"
#include "Misc/PackageName.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Materials/MaterialInterface.h"
#include "Engine/StaticMesh.h"
#include "EdGraphUtilities.h"
#include "UObject/StructOnScope.h"

#include "NiagaraSystem.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraScript.h"
#include "NiagaraScriptSource.h"
#include "NiagaraGraph.h"
#include "NiagaraNodeOutput.h"
#include "NiagaraNodeInput.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraNodeCustomHlsl.h"
#include "NiagaraNodeAssignment.h"
#include "NiagaraScriptVariable.h"
#include "NiagaraShared.h"
#include "EdGraphSchema_Niagara.h"
#include "NiagaraEditorUtilities.h"
#include "NiagaraSystemFactoryNew.h"
#include "NiagaraEmitterFactoryNew.h"
#include "NiagaraSpriteRendererProperties.h"
#include "NiagaraMeshRendererProperties.h"
#include "NiagaraCommon.h"
#include "ViewModels/Stack/NiagaraStackGraphUtilities.h"
#include "ViewModels/Stack/NiagaraParameterHandle.h"
#include "ViewModels/NiagaraSystemViewModel.h"
#include "ViewModels/NiagaraEmitterHandleViewModel.h"
#include "ViewModels/Stack/NiagaraStackViewModel.h"
#include "ViewModels/Stack/NiagaraStackEntry.h"
#include "ViewModels/Stack/NiagaraStackModuleItem.h"
#include "ViewModels/Stack/NiagaraStackFunctionInput.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(NiagaraProbeCommandlet)

// Exported (NIAGARAEDITOR_API) helpers from Public/Widgets/Wizard/SNiagaraModuleWizard.h (UE 5.5+).
// Re-declared instead of including the header: that header has an inline NIAGARAEDITOR_API template (SetDefaultValue)
// which MSVC rejects (C2491) when compiled in a dllimport context. The node classes live in NiagaraEditor/Private.
class UNiagaraNodeParameterMapGet;
class UNiagaraNodeParameterMapSet;
namespace UE::Niagara::Wizard::Utilities
{
	NIAGARAEDITOR_API UEdGraphPin* AddReadParameterPin(const FNiagaraTypeDefinition& Type, const FName& Name, UNiagaraNodeParameterMapGet* MapGetNode);
	NIAGARAEDITOR_API UEdGraphPin* AddWriteParameterPin(const FNiagaraTypeDefinition& Type, const FName& Name, UNiagaraNodeParameterMapSet* MapSetNode);
}

DEFINE_LOG_CATEGORY_STATIC(LogNiagaraProbe, Log, All);
#define PROBE(Fmt, ...) UE_LOG(LogNiagaraProbe, Display, Fmt, ##__VA_ARGS__)

namespace NiagaraProbe
{
	static const FName AddPinSubCategoryName(TEXT("DynamicAddPin")); // UNiagaraNodeWithDynamicPins::AddPinSubCategory (not exported)

	UNiagaraScript* LoadModule(const TCHAR* ObjectPath)
	{
		UNiagaraScript* S = LoadObject<UNiagaraScript>(nullptr, ObjectPath);
		if (!S) { PROBE(TEXT("!! failed to load %s"), ObjectPath); }
		return S;
	}

	UNiagaraNodeOutput* GetOutputNode(UNiagaraScript* Script)
	{
		return Script ? FNiagaraEditorUtilities::GetScriptOutputNode(*Script) : nullptr;
	}

	UNiagaraNodeFunctionCall* AddModule(UNiagaraNodeOutput* Out, UNiagaraScript* Module, int32 Index = INDEX_NONE)
	{
		if (!Module || !Out) { return nullptr; }
		UNiagaraNodeFunctionCall* N = FNiagaraStackGraphUtilities::AddScriptModuleToStack(Module, *Out, Index);
		PROBE(TEXT("AddModule %s -> node '%s'"), *Module->GetPathName(), N ? *N->GetFunctionName() : TEXT("NULL"));
		return N;
	}

	UNiagaraNodeFunctionCall* AddModule(UNiagaraNodeOutput* Out, const TCHAR* Path, int32 Index = INDEX_NONE)
	{
		return AddModule(Out, LoadModule(Path), Index);
	}

	// Mirrors FNiagaraStackGraphUtilities::FindAffectedScripts (not exported).
	void GetAffectedScripts(UNiagaraSystem& System, const FVersionedNiagaraEmitter& VE, ENiagaraScriptUsage Usage, TArray<UNiagaraScript*>& Out)
	{
		TArray<UNiagaraScript*> Candidates;
		Candidates.Add(System.GetSystemSpawnScript());
		Candidates.Add(System.GetSystemUpdateScript());
		if (FVersionedNiagaraEmitterData* D = VE.GetEmitterData())
		{
			TArray<UNiagaraScript*> EmitterScripts;
			D->GetScripts(EmitterScripts, false);
			Candidates.Append(EmitterScripts);
		}
		for (UNiagaraScript* S : Candidates)
		{
			if (S && S->ContainsUsage(Usage)) { Out.AddUnique(S); }
		}
	}

	// Replacement for the non-exported FNiagaraStackGraphUtilities::CreateRapidIterationParameter + stack SetLocalValue.
	template<typename T>
	void SetRapidIterationInput(UNiagaraSystem& System, const FVersionedNiagaraEmitter& VE, UNiagaraNodeFunctionCall& Module, FName InputName, const FNiagaraTypeDefinition& Type, const T& Value)
	{
		const ENiagaraScriptUsage Usage = FNiagaraStackGraphUtilities::GetOutputNodeUsage(Module);
		const FNiagaraParameterHandle Aliased = FNiagaraParameterHandle::CreateAliasedModuleParameterHandle(
			FNiagaraParameterHandle::CreateModuleParameterHandle(InputName), &Module);
		const bool bSystemScript = Usage == ENiagaraScriptUsage::SystemSpawnScript || Usage == ENiagaraScriptUsage::SystemUpdateScript;
		const FString EmitterName = VE.Emitter ? VE.Emitter->GetUniqueEmitterName() : FString();
		FNiagaraVariable RIVar = FNiagaraUtilities::ConvertVariableToRapidIterationConstantName(
			FNiagaraVariable(Type, Aliased.GetParameterHandleString()), bSystemScript ? nullptr : *EmitterName, Usage);
		RIVar.SetValue(Value);
		TArray<UNiagaraScript*> Scripts;
		GetAffectedScripts(System, VE, Usage, Scripts);
		for (UNiagaraScript* S : Scripts)
		{
			S->Modify();
			S->RapidIterationParameters.SetParameterData(RIVar.GetData(), RIVar, true);
		}
		PROBE(TEXT("RI '%s' written to %d scripts"), *RIVar.GetName().ToString(), Scripts.Num());
	}

	FNiagaraVariable MakeRIVar(const FVersionedNiagaraEmitter& VE, UNiagaraNodeFunctionCall& Module, FName InputName, const FNiagaraTypeDefinition& Type)
	{
		const ENiagaraScriptUsage Usage = FNiagaraStackGraphUtilities::GetOutputNodeUsage(Module);
		const FNiagaraParameterHandle Aliased = FNiagaraParameterHandle::CreateAliasedModuleParameterHandle(
			FNiagaraParameterHandle::CreateModuleParameterHandle(InputName), &Module);
		const bool bSystemScript = Usage == ENiagaraScriptUsage::SystemSpawnScript || Usage == ENiagaraScriptUsage::SystemUpdateScript;
		const FString EmitterName = VE.Emitter ? VE.Emitter->GetUniqueEmitterName() : FString();
		return FNiagaraUtilities::ConvertVariableToRapidIterationConstantName(
			FNiagaraVariable(Type, Aliased.GetParameterHandleString()), bSystemScript ? nullptr : *EmitterName, Usage);
	}

	void ReadBackRI(UNiagaraScript* S, const TCHAR* ScriptLabel, const FNiagaraVariable& Var)
	{
		const int32* Off = S->RapidIterationParameters.FindParameterOffset(Var);
		if (!Off) { PROBE(TEXT("READBACK [%s] %s : MISSING"), ScriptLabel, *Var.GetName().ToString()); return; }
		const uint8* Data = S->RapidIterationParameters.GetParameterData(*Off);
		if (Var.GetType() == FNiagaraTypeDefinition::GetFloatDef())
		{
			PROBE(TEXT("READBACK [%s] %s = %f"), ScriptLabel, *Var.GetName().ToString(), *reinterpret_cast<const float*>(Data));
		}
		else
		{
			const FVector3f V = *reinterpret_cast<const FVector3f*>(Data);
			PROBE(TEXT("READBACK [%s] %s = (%f, %f, %f)"), ScriptLabel, *Var.GetName().ToString(), V.X, V.Y, V.Z);
		}
	}

	template<typename T>
	FNiagaraVariable AddUserParameter(UNiagaraSystem& System, const FNiagaraTypeDefinition& Type, const TCHAR* Name, const T& DefaultValue)
	{
		FNiagaraVariable Var(Type, FName(Name));     // must start with "User."
		Var.SetValue(DefaultValue);
		FNiagaraParameterStore& Store = System.GetExposedParameters(); // base-class ref => virtual dispatch to non-exported override
		Store.AddParameter(Var, true, true);
		Store.SetParameterData(Var.GetData(), Var, false);
		FNiagaraEditorUtilities::UserParameters::GetScriptVariableForUserParameter(Var, System); // editor-side metadata
		return Var;
	}

	bool LinkInputToParameter(UNiagaraNodeFunctionCall& Module, FName InputName, const FNiagaraTypeDefinition& Type, const FNiagaraVariableBase& Param)
	{
		const FNiagaraParameterHandle Aliased = FNiagaraParameterHandle::CreateAliasedModuleParameterHandle(
			FNiagaraParameterHandle::CreateModuleParameterHandle(InputName), &Module);
		FGuid VarId;
		if (UNiagaraGraph* Called = Module.GetCalledGraph())
		{
			if (UNiagaraScriptVariable* SV = Called->GetScriptVariable(FName(*(TEXT("Module.") + InputName.ToString()))))
			{
				VarId = SV->Metadata.GetVariableGuid();
			}
		}
		UEdGraphPin& OverridePin = FNiagaraStackGraphUtilities::GetOrCreateStackFunctionInputOverridePin(Module, Aliased, Type, VarId, FGuid());
		if (OverridePin.LinkedTo.Num() != 0) { return false; }
		TSet<FNiagaraVariableBase> Known;
		Known.Add(Param);
		FNiagaraStackGraphUtilities::SetLinkedParameterValueForFunctionInput(OverridePin, Param, Known);
		PROBE(TEXT("Linked %s.%s -> %s (scriptvar guid valid=%d)"), *Module.GetFunctionName(), *InputName.ToString(), *Param.GetName().ToString(), VarId.IsValid());
		return true;
	}

	void SetCustomHlslText(UNiagaraNodeCustomHlsl* Node, const FString& Hlsl)
	{
		// UNiagaraNodeCustomHlsl::SetCustomHlsl is not exported -> write the private UPROPERTY via reflection, then fire the virtual PostEditChangeProperty.
		FStrProperty* Prop = FindFProperty<FStrProperty>(UNiagaraNodeCustomHlsl::StaticClass(), TEXT("CustomHlsl"));
		check(Prop);
		Node->Modify();
		Prop->SetPropertyValue_InContainer(Node, Hlsl);
		FPropertyChangedEvent Evt(Prop, EPropertyChangeType::ValueSet);
		static_cast<UObject*>(Node)->PostEditChangeProperty(Evt);
	}

	UNiagaraNodeCustomHlsl* CreateCustomHlslNode(UNiagaraGraph& Graph, FName FunctionName, const TArray<FNiagaraVariable>& Inputs, const TArray<FNiagaraVariable>& Outputs, const FString& Hlsl)
	{
		FGraphNodeCreator<UNiagaraNodeCustomHlsl> Creator(Graph);
		UNiagaraNodeCustomHlsl* Node = Creator.CreateNode(false);
		Node->Signature.Name = FunctionName;
		Node->Signature.Inputs = Inputs;          // public UPROPERTY on UNiagaraNodeFunctionCall
		Node->Signature.Outputs.Reset();
		for (const FNiagaraVariable& O : Outputs) { Node->Signature.Outputs.Add(O); }
		Creator.Finalize();                       // AllocateDefaultPins(): pins from Signature + add pins
		SetCustomHlslText(Node, Hlsl);
		return Node;
	}

	UEdGraphPin* FindAddPin(UEdGraphNode* Node, EEdGraphPinDirection Dir)
	{
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin->Direction == Dir && Pin->PinType.PinSubCategory == AddPinSubCategoryName) { return Pin; }
		}
		return nullptr;
	}

	UEdGraphPin* FindPinByName(UEdGraphNode* Node, EEdGraphPinDirection Dir, FName Name)
	{
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin->Direction == Dir && Pin->PinName == Name) { return Pin; }
		}
		return nullptr;
	}

	void DumpPins(UEdGraphNode* Node, const TCHAR* Label)
	{
		for (UEdGraphPin* Pin : Node->Pins)
		{
			PROBE(TEXT("   [%s] %s pin '%s' cat=%s sub=%s default='%s' links=%d"), Label,
				Pin->Direction == EGPD_Input ? TEXT("in ") : TEXT("out"), *Pin->PinName.ToString(),
				*Pin->PinType.PinCategory.ToString(), *Pin->PinType.PinSubCategory.ToString(), *Pin->DefaultValue, Pin->LinkedTo.Num());
		}
	}

	UNiagaraScript* CreateModuleScriptAsset(const FString& PackagePath, const FString& Name, TFunctionRef<void(UNiagaraGraph&, UNiagaraNodeInput&, UNiagaraNodeOutput&)> Build)
	{
		UPackage* Pkg = CreatePackage(*(PackagePath / Name));
		Pkg->FullyLoad();
		UNiagaraScript* Script = NewObject<UNiagaraScript>(Pkg, *Name, RF_Public | RF_Standalone | RF_Transactional);
		Script->SetUsage(ENiagaraScriptUsage::Module);
		UNiagaraScriptSource* Source = NewObject<UNiagaraScriptSource>(Script, NAME_None, RF_Transactional);
		UNiagaraGraph* Graph = NewObject<UNiagaraGraph>(Source, NAME_None, RF_Transactional);
		Source->NodeGraph = Graph;

		FGraphNodeCreator<UNiagaraNodeOutput> OutCreator(*Graph);
		UNiagaraNodeOutput* Out = OutCreator.CreateNode(false);
		Out->SetUsage(ENiagaraScriptUsage::Module);
		Out->Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetParameterMapDef(), TEXT("Output")));
		OutCreator.Finalize();

		FGraphNodeCreator<UNiagaraNodeInput> InCreator(*Graph);
		UNiagaraNodeInput* In = InCreator.CreateNode(false);
		In->Input = FNiagaraVariable(FNiagaraTypeDefinition::GetParameterMapDef(), TEXT("MapIn"));
		In->Usage = ENiagaraInputNodeUsage::Parameter;
		InCreator.Finalize();

		Build(*Graph, *In, *Out);

		Script->SetLatestSource(Source);
		if (FVersionedNiagaraScriptData* Data = Script->GetLatestScriptData())
		{
			Data->LibraryVisibility = ENiagaraScriptLibraryVisibility::Library;
		}
		Script->RequestCompile(FGuid());
		FAssetRegistryModule::AssetCreated(Script);
		Pkg->MarkPackageDirty();
		return Script;
	}

	bool SavePackageOf(UObject* Asset)
	{
		UPackage* Pkg = Asset->GetOutermost();
		const FString File = FPackageName::LongPackageNameToFilename(Pkg->GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		Args.SaveFlags = SAVE_NoError;
		const bool bOk = UPackage::SavePackage(Pkg, Asset, *File, Args);
		PROBE(TEXT("Save %s -> %s : %s"), *Pkg->GetName(), *File, bOk ? TEXT("OK") : TEXT("FAILED"));
		return bOk;
	}

	void ReportScript(UNiagaraScript* S, const FString& Label)
	{
		if (!S) { return; }
		const FNiagaraVMExecutableData& Exe = S->GetVMExecutableData();
		PROBE(TEXT("[%s] usage=%d status=%d err='%s' events=%d"), *Label, (int32)S->GetUsage(), (int32)S->GetLastCompileStatus(), *Exe.ErrorMsg, Exe.LastCompileEvents.Num());
		for (const FNiagaraVariable& P : Exe.Parameters.Parameters)
		{
			if (P.GetName().ToString().StartsWith(TEXT("User."))) { PROBE(TEXT("    compiled param: %s"), *P.GetName().ToString()); }
		}
		for (const FNiagaraCompileEvent& Ev : Exe.LastCompileEvents)
		{
			PROBE(TEXT("    event sev=%d : %s"), (int32)Ev.Severity, *Ev.Message);
		}
		for (const FString* Src : { &Exe.LastHlslTranslation, &Exe.LastHlslTranslationGPU })
		{
			TArray<FString> Lines;
			Src->ParseIntoArrayLines(Lines);
			int32 Shown = 0;
			for (const FString& L : Lines)
			{
				if (Shown < 60 && (L.Contains(TEXT("RainWind")) || L.Contains(TEXT("FallSpeed")) || L.Contains(TEXT("Damping")) || L.Contains(TEXT("RainPhase")) || L.Contains(TEXT("RainRate")) || L.Contains(TEXT("= V;")) || L.Contains(TEXT("Velocity001")) || L.Contains(TEXT("Drag_Drag")) || L.Contains(TEXT("CustomHLSLOutput = "))))
				{
					PROBE(TEXT("    hlsl%s: %s"), Src == &Exe.LastHlslTranslationGPU ? TEXT("(gpu)") : TEXT("(vm)"), *L.TrimStartAndEnd());
					++Shown;
				}
			}
			if (Src->Len() > 0) { PROBE(TEXT("    %s translation length=%d matched=%d"), Src == &Exe.LastHlslTranslationGPU ? TEXT("gpu") : TEXT("vm"), Src->Len(), Shown); }
		}
		if (S->GetUsage() == ENiagaraScriptUsage::ParticleGPUComputeScript)
		{
			PROBE(TEXT("    IsReadyToRun(GPU)=%d"), S->IsReadyToRun(ENiagaraSimTarget::GPUComputeSim));
			if (const FNiagaraShaderScript* SS = S->GetRenderThreadScript())
			{
				PROBE(TEXT("    gpu shader errors=%d"), SS->GetCompileErrors().Num());
				for (const FString& E : SS->GetCompileErrors()) { PROBE(TEXT("    gpu error: %s"), *E); }
			}
		}
	}

	UNiagaraStackFunctionInput* FindStackInput(FNiagaraSystemViewModel& SVM, UNiagaraNodeFunctionCall* ModuleNode, const FString& InputNameSuffix)
	{
		UNiagaraStackViewModel* StackVM = SVM.GetEmitterHandleViewModels()[0]->GetEmitterStackViewModel();
		UNiagaraStackEntry* Root = StackVM->GetRootEntry();
		Root->RefreshChildren();
		TArray<UNiagaraStackEntry*> ToCheck;
		Root->GetUnfilteredChildren(ToCheck);
		UNiagaraStackFunctionInput* Found = nullptr;
		while (ToCheck.Num() > 0)
		{
			UNiagaraStackEntry* E = ToCheck.Pop();
			if (UNiagaraStackModuleItem* M = Cast<UNiagaraStackModuleItem>(E))
			{
				if (&M->GetModuleNode() == ModuleNode)
				{
					M->RefreshChildren();
					TArray<UNiagaraStackFunctionInput*> Inputs;
					M->GetParameterInputs(Inputs);
					for (UNiagaraStackFunctionInput* In : Inputs)
					{
						const FString N = In->GetInputParameterHandle().GetName().ToString();
						PROBE(TEXT("   stack input of '%s': '%s' (%s) mode=%d"), *ModuleNode->GetFunctionName(), *N, *In->GetInputType().GetName(), (int32)In->GetValueMode());
						if (!Found && N.EndsWith(InputNameSuffix)) { Found = In; }
					}
					return Found;
				}
			}
			E->GetUnfilteredChildren(ToCheck);
		}
		return Found;
	}

	bool Build(const FString& PackagePath, const FString& AssetName)
	{
		PROBE(TEXT("==== NiagaraProbe Build %s/%s ===="), *PackagePath, *AssetName);
		const UEdGraphSchema_Niagara* Schema = GetDefault<UEdGraphSchema_Niagara>();

		// ---------- 1. System asset through the (non-MinimalAPI) factory; CreateAsset does NOT call ConfigureProperties()
		IAssetTools& AssetTools = FAssetToolsModule::GetModule().Get();
		UNiagaraSystemFactoryNew* SystemFactory = NewObject<UNiagaraSystemFactoryNew>();
		UNiagaraSystem* System = Cast<UNiagaraSystem>(AssetTools.CreateAsset(AssetName, PackagePath, UNiagaraSystem::StaticClass(), SystemFactory));
		if (!System) { PROBE(TEXT("!! CreateAsset failed")); return false; }
		PROBE(TEXT("System created: %s  effectType=%s"), *System->GetPathName(), *GetNameSafe(System->GetEffectType()));

		// ---------- 2. Standalone emitter (no parent) -> add to system (system makes its own copy)
		UNiagaraEmitter* SrcEmitter = NewObject<UNiagaraEmitter>(GetTransientPackage(), TEXT("RainDrops"), RF_Transactional);
		UNiagaraEmitterFactoryNew::InitializeEmitter(SrcEmitter, false);
		SrcEmitter->SetUniqueEmitterName(TEXT("RainDrops"));
		SrcEmitter->bIsInheritable = false;
		const FGuid HandleId = FNiagaraEditorUtilities::AddEmitterToSystem(*System, *SrcEmitter, SrcEmitter->GetExposedVersion().VersionGuid);

		FNiagaraEmitterHandle* Handle = nullptr;
		for (FNiagaraEmitterHandle& H : System->GetEmitterHandles()) { if (H.GetId() == HandleId) { Handle = &H; } }
		check(Handle);
		const FVersionedNiagaraEmitter VE = Handle->GetInstance();
		FVersionedNiagaraEmitterData* D = VE.GetEmitterData();
		PROBE(TEXT("Emitter instance: %s unique='%s' handleName='%s'"), *VE.Emitter->GetPathName(), *VE.Emitter->GetUniqueEmitterName(), *Handle->GetName().ToString());

		// ---------- 3. Emitter properties (InitializeEmitter forces CPUSim, so set after)
		D->SimTarget = ENiagaraSimTarget::GPUComputeSim;
		D->CalculateBoundsMode = ENiagaraEmitterCalculateBoundMode::Fixed;
		D->FixedBounds = FBox(FVector(-2000, -2000, -100), FVector(2000, 2000, 3000));
		D->bLocalSpace = false;
		D->GraphSource->MarkNotSynchronized(TEXT("Probe: sim target / bounds changed"));

		UNiagaraNodeOutput* EmitterSpawnOut = GetOutputNode(D->EmitterSpawnScriptProps.Script);
		UNiagaraNodeOutput* EmitterUpdateOut = GetOutputNode(D->EmitterUpdateScriptProps.Script);
		UNiagaraNodeOutput* ParticleSpawnOut = GetOutputNode(D->SpawnScriptProps.Script);
		UNiagaraNodeOutput* ParticleUpdateOut = GetOutputNode(D->UpdateScriptProps.Script);
		PROBE(TEXT("Output nodes: ES=%d EU=%d PS=%d PU=%d"), EmitterSpawnOut != nullptr, EmitterUpdateOut != nullptr, ParticleSpawnOut != nullptr, ParticleUpdateOut != nullptr);

		// ---------- 4. User parameters
		const FNiagaraVariable UserRainRate = AddUserParameter(*System, FNiagaraTypeDefinition::GetFloatDef(), TEXT("User.RainRate"), 3000.0f);
		const FNiagaraVariable UserRainWind = AddUserParameter(*System, FNiagaraTypeDefinition::GetFloatDef(), TEXT("User.RainWind"), 50.0f);
		PROBE(TEXT("Exposed params now: %d"), System->GetExposedParameters().Num());

		// ---------- 5. Engine modules
		AddModule(EmitterUpdateOut, TEXT("/Niagara/Modules/Emitter/EmitterState.EmitterState"));
		UNiagaraNodeFunctionCall* SpawnRate = AddModule(EmitterUpdateOut, TEXT("/Niagara/Modules/Emitter/SpawnRate.SpawnRate"));
		UNiagaraNodeFunctionCall* InitParticle = AddModule(ParticleSpawnOut, TEXT("/Niagara/Modules/Spawn/Initialization/V2/InitializeParticle.InitializeParticle"));
		UNiagaraNodeFunctionCall* BoxLoc = AddModule(ParticleSpawnOut, TEXT("/Niagara/Modules/Spawn/Location/BoxLocation.BoxLocation"));
		UNiagaraNodeFunctionCall* AddVel = AddModule(ParticleSpawnOut, TEXT("/Niagara/Modules/Spawn/Velocity/AddVelocity.AddVelocity"));

		// Declare a custom attribute with the exported "Set Parameters" (assignment) helper
		TArray<FNiagaraVariable> AssignVars = { FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Particles.RainPhase")) };
		TArray<FString> AssignDefaults = { TEXT("0.0") };
		UNiagaraNodeAssignment* Assign = FNiagaraStackGraphUtilities::AddParameterModuleToStack(AssignVars, *ParticleSpawnOut, INDEX_NONE, AssignDefaults);
		PROBE(TEXT("AddParameterModuleToStack -> %s"), Assign ? *Assign->GetFunctionName() : TEXT("NULL"));

		UNiagaraNodeFunctionCall* Gravity = AddModule(ParticleUpdateOut, TEXT("/Niagara/Modules/Update/Forces/GravityForce.GravityForce"));
		UNiagaraNodeFunctionCall* Drag = AddModule(ParticleUpdateOut, TEXT("/Niagara/Modules/Update/Forces/Drag.Drag"));

		// ---------- 6. Custom HLSL module A: ParameterMap in/out only (no Map Get/Set nodes)
		const FString ModulePath = PackagePath / TEXT("Modules");
		UNiagaraNodeCustomHlsl* CustomA = nullptr;
		UNiagaraScript* ModuleA = CreateModuleScriptAsset(ModulePath, AssetName + TEXT("_WindParamMap"),
			[&](UNiagaraGraph& Graph, UNiagaraNodeInput& In, UNiagaraNodeOutput& Out)
			{
				const FString Hlsl =
					TEXT("float3 V = Particles.Velocity;\n")
					TEXT("V.x = V.x + User.RainWind * Engine.DeltaTime;\n")
					TEXT("Particles.Velocity = V;\n")
					TEXT("Particles.RainPhase = frac(Particles.RainPhase + Engine.DeltaTime);\n");
				CustomA = CreateCustomHlslNode(Graph, TEXT("RainWindParamMap"),
					{ FNiagaraVariable(FNiagaraTypeDefinition::GetParameterMapDef(), TEXT("MapIn")) },
					{ FNiagaraVariable(FNiagaraTypeDefinition::GetParameterMapDef(), TEXT("MapOut")) }, Hlsl);
				const bool b1 = Schema->TryCreateConnection(In.GetOutputPin(0), CustomA->GetInputPin(0));
				const bool b2 = Schema->TryCreateConnection(CustomA->GetOutputPin(0), Out.GetInputPin(0));
				PROBE(TEXT("ModuleA wiring: %d %d"), b1, b2);
				DumpPins(CustomA, TEXT("CustomA"));
			});

		// ---------- 7a. Custom HLSL module W: classic Map Get -> Custom HLSL (typed pins) -> Map Set, pins via exported wizard utilities
		UNiagaraScript* ModuleW = CreateModuleScriptAsset(ModulePath, AssetName + TEXT("_WizardPins"),
			[&](UNiagaraGraph& Graph, UNiagaraNodeInput& In, UNiagaraNodeOutput& Out)
			{
				UClass* MapGetClass = FindObject<UClass>(nullptr, TEXT("/Script/NiagaraEditor.NiagaraNodeParameterMapGet"));
				UClass* MapSetClass = FindObject<UClass>(nullptr, TEXT("/Script/NiagaraEditor.NiagaraNodeParameterMapSet"));
				FGraphNodeCreator<UNiagaraNode> GetCreator(Graph);
				UNiagaraNode* MapGet = GetCreator.CreateNode(false, MapGetClass);
				GetCreator.Finalize();
				FGraphNodeCreator<UNiagaraNode> SetCreator(Graph);
				UNiagaraNode* MapSet = SetCreator.CreateNode(false, MapSetClass);
				SetCreator.Finalize();

				UEdGraphPin* ReadDrag = UE::Niagara::Wizard::Utilities::AddReadParameterPin(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Module.Damping"), reinterpret_cast<UNiagaraNodeParameterMapGet*>(MapGet));
				UEdGraphPin* ReadVel = UE::Niagara::Wizard::Utilities::AddReadParameterPin(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Particles.Velocity"), reinterpret_cast<UNiagaraNodeParameterMapGet*>(MapGet));
				UEdGraphPin* ReadDt = UE::Niagara::Wizard::Utilities::AddReadParameterPin(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Engine.DeltaTime"), reinterpret_cast<UNiagaraNodeParameterMapGet*>(MapGet));
				// AddRead/WriteParameterPin always create a *new, uniquely named* graph parameter (UNiagaraGraph::MakeUniqueParameterName),
				// so writing back to an attribute that is also read would yield "Particles.Velocity001".
				// For the write side of an already-registered parameter, create the pin directly (UEdGraphNode::CreatePin is ENGINE_API, public).
				UEdGraphPin* SetAddPin = FindAddPin(MapSet, EGPD_Input);
				FEdGraphPinType VelPinType = UEdGraphSchema_Niagara::TypeDefinitionToPinType(FNiagaraTypeDefinition::GetVec3Def());
				VelPinType.PinSubCategory = TEXT("ParameterPin"); // UNiagaraNodeParameterMapBase::ParameterPinSubCategory
				UEdGraphPin* WriteVel = MapSet->CreatePin(EGPD_Input, VelPinType, TEXT("Particles.Velocity"), MapSet->Pins.IndexOfByKey(SetAddPin));
				WriteVel->PinFriendlyName = FText::FromString(TEXT("Particles.Velocity"));
				WriteVel->PersistentGuid = FGuid::NewGuid();

				UNiagaraNodeCustomHlsl* Custom = CreateCustomHlslNode(Graph, TEXT("RainDamping"),
					{ FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Damping")),
					  FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Velocity")),
					  FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("DeltaTime")) },
					{ FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("OutVelocity")) },
					TEXT("OutVelocity = Velocity * exp(-Damping * DeltaTime);"));

				bool bOk = true;
				bOk &= Schema->TryCreateConnection(In.GetOutputPin(0), MapGet->GetInputPin(0));
				bOk &= Schema->TryCreateConnection(In.GetOutputPin(0), MapSet->GetInputPin(0));
				bOk &= Schema->TryCreateConnection(ReadDrag, FindPinByName(Custom, EGPD_Input, TEXT("Damping")));
				bOk &= Schema->TryCreateConnection(ReadVel, FindPinByName(Custom, EGPD_Input, TEXT("Velocity")));
				bOk &= Schema->TryCreateConnection(ReadDt, FindPinByName(Custom, EGPD_Input, TEXT("DeltaTime")));
				bOk &= Schema->TryCreateConnection(FindPinByName(Custom, EGPD_Output, TEXT("OutVelocity")), WriteVel);
				bOk &= Schema->TryCreateConnection(MapSet->GetOutputPin(0), Out.GetInputPin(0));
				PROBE(TEXT("ModuleW wiring ok=%d"), bOk);
				if (UEdGraphPin* DefaultPin = FindPinByName(MapGet, EGPD_Input, TEXT("Module.Damping")))
				{
					Schema->TrySetDefaultValue(*DefaultPin, TEXT("0.5"));
				}
				DumpPins(MapGet, TEXT("W.MapGet"));
				DumpPins(MapSet, TEXT("W.MapSet"));
				DumpPins(Custom, TEXT("W.Custom"));
			});

		// ---------- 7b. Custom HLSL module B: Map Get (Module.FallSpeed via add-pin trick) + ParameterMap pass-through
		UNiagaraNodeCustomHlsl* CustomB = nullptr;
		UNiagaraScript* ModuleB = CreateModuleScriptAsset(ModulePath, AssetName + TEXT("_FallSpeed"),
			[&](UNiagaraGraph& Graph, UNiagaraNodeInput& In, UNiagaraNodeOutput& Out)
			{
				UClass* MapGetClass = FindObject<UClass>(nullptr, TEXT("/Script/NiagaraEditor.NiagaraNodeParameterMapGet"));
				FGraphNodeCreator<UNiagaraNode> GetCreator(Graph);
				UNiagaraNode* MapGet = GetCreator.CreateNode(false, MapGetClass);
				GetCreator.Finalize();
				Schema->TryCreateConnection(In.GetOutputPin(0), MapGet->GetInputPin(0));

				const FString Hlsl =
					TEXT("float3 V = Particles.Velocity;\n")
					TEXT("V.z = min(V.z, -FallSpeed);\n")
					TEXT("Particles.Velocity = V;\n");
				CustomB = CreateCustomHlslNode(Graph, TEXT("RainFallSpeed"),
					{ FNiagaraVariable(FNiagaraTypeDefinition::GetParameterMapDef(), TEXT("MapIn")), FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("FallSpeed")) },
					{ FNiagaraVariable(FNiagaraTypeDefinition::GetParameterMapDef(), TEXT("MapOut")) }, Hlsl);
				Schema->TryCreateConnection(In.GetOutputPin(0), CustomB->GetInputPin(0));

				// add-pin trick: link MapGet's "+" output pin to the typed custom input -> MapGet grows "Module.FallSpeed"
				UEdGraphPin* GetAdd = FindAddPin(MapGet, EGPD_Output);
				const bool bAdd = GetAdd && Schema->TryCreateConnection(GetAdd, FindPinByName(CustomB, EGPD_Input, TEXT("FallSpeed")));
				Schema->TryCreateConnection(CustomB->GetOutputPin(0), Out.GetInputPin(0));
				PROBE(TEXT("ModuleB add-pin trick: %d"), bAdd);
				// default value of the module input lives on MapGet's paired default (input) pin
				if (UEdGraphPin* DefaultPin = FindPinByName(MapGet, EGPD_Input, TEXT("Module.FallSpeed")))
				{
					Schema->TrySetDefaultValue(*DefaultPin, TEXT("800.0"));
				}
				DumpPins(MapGet, TEXT("MapGet"));
				DumpPins(CustomB, TEXT("CustomB"));

				// T3D export of the nodes (input for the ImportNodesFromText route)
				TSet<UObject*> ToExport = { MapGet, CustomB };
				FString Text;
				FEdGraphUtilities::ExportNodesToText(ToExport, Text);
				FFileHelper::SaveStringToFile(Text, *(FPaths::ProjectSavedDir() / TEXT("NiagaraProbe_ModuleB_Nodes.t3d")));
				PROBE(TEXT("Exported T3D length=%d canImport=%d"), Text.Len(), FEdGraphUtilities::CanImportNodesFromText(&Graph, Text));
			});

		UNiagaraNodeFunctionCall* ModuleANode = AddModule(ParticleUpdateOut, ModuleA);
		UNiagaraNodeFunctionCall* ModuleBNode = AddModule(ParticleUpdateOut, ModuleB);
		UNiagaraNodeFunctionCall* ModuleWNode = AddModule(ParticleUpdateOut, ModuleW);
		AddModule(ParticleUpdateOut, TEXT("/Niagara/Modules/Solvers/SolveForcesAndVelocity.SolveForcesAndVelocity"));
		if (ModuleWNode) { SetRapidIterationInput(*System, VE, *ModuleWNode, TEXT("Damping"), FNiagaraTypeDefinition::GetFloatDef(), 0.25f); }

		// ---------- 8. Inputs: RI literal, linked user parameter
		if (SpawnRate) { LinkInputToParameter(*SpawnRate, TEXT("SpawnRate"), FNiagaraTypeDefinition::GetFloatDef(), UserRainRate); }
		if (SpawnRate) { SetRapidIterationInput(*System, VE, *SpawnRate, TEXT("Spawn Probability"), FNiagaraTypeDefinition::GetFloatDef(), 0.5f); }
		if (ModuleBNode) { SetRapidIterationInput(*System, VE, *ModuleBNode, TEXT("FallSpeed"), FNiagaraTypeDefinition::GetFloatDef(), 900.0f); }
		if (Gravity) { SetRapidIterationInput(*System, VE, *Gravity, TEXT("Gravity"), FNiagaraTypeDefinition::GetVec3Def(), FVector3f(0, 0, -1200)); }

		// ---------- 9. Renderers
		UNiagaraSpriteRendererProperties* Sprite = NewObject<UNiagaraSpriteRendererProperties>(VE.Emitter, NAME_None, RF_Transactional);
		Sprite->Alignment = ENiagaraSpriteAlignment::VelocityAligned;
		Sprite->FacingMode = ENiagaraSpriteFacingMode::FaceCameraPosition;
		Sprite->Material = LoadObject<UMaterialInterface>(nullptr, TEXT("/Niagara/DefaultAssets/DefaultSpriteMaterial.DefaultSpriteMaterial"));
		VE.Emitter->AddRenderer(Sprite, VE.Version);
		UNiagaraMeshRendererProperties* MeshR = NewObject<UNiagaraMeshRendererProperties>(VE.Emitter, NAME_None, RF_Transactional);
		if (MeshR->Meshes.Num() == 0) { MeshR->Meshes.AddDefaulted(); }
		MeshR->Meshes[0].Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
		MeshR->OnMeshChanged();
		VE.Emitter->AddRenderer(MeshR, VE.Version);
		PROBE(TEXT("Renderers: %d"), D->GetRenderers().Num());

		// ---------- 10. Headless view-model path (what the UI does)
		{
			TSharedRef<FNiagaraSystemViewModel> SVM = MakeShared<FNiagaraSystemViewModel>();
			FNiagaraSystemViewModelOptions Opt;
			Opt.bCanModifyEmittersFromTimeline = false;
			Opt.bCanSimulate = false;
			Opt.bCanAutoCompile = false;
			Opt.bIsForDataProcessingOnly = true;
			Opt.MessageLogGuid = System->GetAssetGuid();
			SVM->Initialize(*System, Opt);
			PROBE(TEXT("VM emitter handles: %d"), SVM->GetEmitterHandleViewModels().Num());

			FindStackInput(*SVM, InitParticle, TEXT("__dump__"));
			if (UNiagaraStackFunctionInput* DragIn = FindStackInput(*SVM, Drag, TEXT("Drag")))
			{
				TSharedRef<FStructOnScope> V = MakeShared<FStructOnScope>(DragIn->GetInputType().GetStruct());
				*reinterpret_cast<float*>(V->GetStructMemory()) = 2.0f;
				DragIn->SetLocalValue(V);
				PROBE(TEXT("VM SetLocalValue Drag.Drag done, mode=%d"), (int32)DragIn->GetValueMode());
			}
			if (UNiagaraStackFunctionInput* BoxSize = FindStackInput(*SVM, BoxLoc, TEXT("Box Size")))
			{
				TSharedRef<FStructOnScope> V = MakeShared<FStructOnScope>(BoxSize->GetInputType().GetStruct());
				*reinterpret_cast<FVector3f*>(V->GetStructMemory()) = FVector3f(3000.f, 3000.f, 10.f);
				BoxSize->SetLocalValue(V);
				PROBE(TEXT("VM SetLocalValue BoxLocation.BoxSize done, mode=%d"), (int32)BoxSize->GetValueMode());
			}
			if (UNiagaraStackFunctionInput* CoordSpace = FindStackInput(*SVM, Gravity, TEXT("Coordinate Space")))
			{
				TSharedRef<FStructOnScope> V = MakeShared<FStructOnScope>(CoordSpace->GetInputType().GetStruct());
				*reinterpret_cast<int32*>(V->GetStructMemory()) = 1; // enum value as int32 (non rapid-iteration type -> override pin default)
				CoordSpace->SetLocalValue(V);
				PROBE(TEXT("VM SetLocalValue Gravity.CoordinateSpace(enum) done, mode=%d"), (int32)CoordSpace->GetValueMode());
			}
			if (UNiagaraStackFunctionInput* Phase = FindStackInput(*SVM, Assign, TEXT("RainPhase")))
			{
				Phase->SetCustomExpression(TEXT("frac(Engine.Time * 0.37)"));
				PROBE(TEXT("VM SetCustomExpression RainPhase done, mode=%d"), (int32)Phase->GetValueMode());
			}
			FindStackInput(*SVM, BoxLoc, TEXT("__dump__"));
			FindStackInput(*SVM, AddVel, TEXT("__dump__"));
			FindStackInput(*SVM, Gravity, TEXT("__dump__"));
			FindStackInput(*SVM, Drag, TEXT("__dump__"));
			FindStackInput(*SVM, SpawnRate, TEXT("__dump__"));
			FindStackInput(*SVM, ModuleBNode, TEXT("__dump__"));
			FindStackInput(*SVM, ModuleWNode, TEXT("__dump__"));
		}

		// RI read-back (proves the manual RI path writes where the compiler reads)
		for (UNiagaraScript* S : { D->GetGPUComputeScript() })
		{
			TArray<FNiagaraVariable> Params;
			S->RapidIterationParameters.GetParameters(Params);
			for (const FNiagaraVariable& P : Params) { PROBE(TEXT("    GPU RI param: %s"), *P.GetName().ToString()); }
		}
		// ---------- 11. Compile and report
		System->RequestCompile(true /*force*/);
		System->WaitForCompilationComplete(true /*include GPU shaders*/, false /*progress*/);
		PROBE(TEXT("IsReadyToRun=%d"), System->IsReadyToRun());
		ReportScript(System->GetSystemSpawnScript(), TEXT("SystemSpawn"));
		ReportScript(System->GetSystemUpdateScript(), TEXT("SystemUpdate"));
		TArray<UNiagaraScript*> EmitterScripts;
		D->GetScripts(EmitterScripts, false);
		for (UNiagaraScript* S : EmitterScripts) { ReportScript(S, TEXT("Emitter")); }
		{
			UNiagaraScript* Gpu = D->GetGPUComputeScript();
			UNiagaraScript* SysUpd = System->GetSystemUpdateScript();
			if (ModuleBNode) { ReadBackRI(Gpu, TEXT("GPU"), MakeRIVar(VE, *ModuleBNode, TEXT("FallSpeed"), FNiagaraTypeDefinition::GetFloatDef())); }
			if (ModuleWNode) { ReadBackRI(Gpu, TEXT("GPU"), MakeRIVar(VE, *ModuleWNode, TEXT("Damping"), FNiagaraTypeDefinition::GetFloatDef())); }
			if (Gravity)     { ReadBackRI(Gpu, TEXT("GPU"), MakeRIVar(VE, *Gravity, TEXT("Gravity"), FNiagaraTypeDefinition::GetVec3Def())); }
			if (Drag)        { ReadBackRI(Gpu, TEXT("GPU"), MakeRIVar(VE, *Drag, TEXT("Drag"), FNiagaraTypeDefinition::GetFloatDef())); }
			if (BoxLoc)      { ReadBackRI(Gpu, TEXT("GPU"), MakeRIVar(VE, *BoxLoc, TEXT("Box Size"), FNiagaraTypeDefinition::GetVec3Def())); }
			if (SpawnRate)   { ReadBackRI(SysUpd, TEXT("SystemUpdate"), MakeRIVar(VE, *SpawnRate, TEXT("Spawn Probability"), FNiagaraTypeDefinition::GetFloatDef())); }
			if (Drag)        { ReadBackRI(D->UpdateScriptProps.Script, TEXT("CPU ParticleUpdate"), MakeRIVar(VE, *Drag, TEXT("Drag"), FNiagaraTypeDefinition::GetFloatDef())); }
		}
		ReportScript(ModuleA, TEXT("ModuleA(standalone)"));
		ReportScript(ModuleB, TEXT("ModuleB(standalone)"));
		ReportScript(ModuleW, TEXT("ModuleW(standalone)"));

		// ---------- 12. Save
		SavePackageOf(ModuleA);
		SavePackageOf(ModuleB);
		SavePackageOf(ModuleW);
		SavePackageOf(System);
		return true;
	}
}

static void AuditEngineModules()
{
	IAssetRegistry& AR = FAssetRegistryModule::GetRegistry();
	AR.SearchAllAssets(true);
	TArray<FAssetData> Assets;
	AR.GetAssetsByPath(FName(TEXT("/Niagara/Modules")), Assets, true);
	FString Csv = TEXT("ObjectPath,Usage,Deprecated,Recommendation,UsageBitmask,Visibility\n");
	for (const FAssetData& A : Assets)
	{
		UNiagaraScript* S = Cast<UNiagaraScript>(A.GetAsset());
		if (!S) { continue; }
		const FVersionedNiagaraScriptData* Data = S->GetLatestScriptData();
		Csv += FString::Printf(TEXT("%s,%d,%d,%s,%d,%d\n"), *A.GetObjectPathString(), (int32)S->GetUsage(),
			Data ? (int32)Data->bDeprecated : -1, Data && Data->DeprecationRecommendation ? *Data->DeprecationRecommendation->GetPathName() : TEXT(""),
			Data ? Data->ModuleUsageBitmask : 0, Data ? (int32)Data->LibraryVisibility : -1);
	}
	FFileHelper::SaveStringToFile(Csv, *(FPaths::ProjectSavedDir() / TEXT("NiagaraModuleAudit.csv")));
	PROBE(TEXT("Audit wrote %d assets"), Assets.Num());
}

int32 UNiagaraProbeCommandlet::Main(const FString& Params)
{
	if (FParse::Param(*Params, TEXT("Verify")))
	{
		UNiagaraSystem* Sys = LoadObject<UNiagaraSystem>(nullptr, TEXT("/Game/Probe/NS_Probe.NS_Probe"));
		if (!Sys) { PROBE(TEXT("VERIFY: load failed")); return 1; }
		PROBE(TEXT("VERIFY: loaded, emitters=%d userParams=%d"), Sys->GetEmitterHandles().Num(), Sys->GetExposedParameters().Num());
		for (FNiagaraEmitterHandle& H : Sys->GetEmitterHandles())
		{
			FVersionedNiagaraEmitterData* ED = H.GetEmitterData();
			PROBE(TEXT("VERIFY: emitter %s simTarget=%d renderers=%d"), *H.GetName().ToString(), (int32)ED->SimTarget, ED->GetRenderers().Num());
		}
		if (UNiagaraEmitter* PyEm = LoadObject<UNiagaraEmitter>(nullptr, TEXT("/Game/ProbePy/NE_PyEmitter.NE_PyEmitter")))
		{
			FVersionedNiagaraEmitterData* PD = PyEm->GetLatestEmitterData();
			UNiagaraScriptSource* Src = Cast<UNiagaraScriptSource>(PD->GraphSource);
			TArray<UNiagaraNodeFunctionCall*> Calls;
			if (Src && Src->NodeGraph) { Src->NodeGraph->GetNodesOfClass<UNiagaraNodeFunctionCall>(Calls); }
			PROBE(TEXT("VERIFY: python NE_PyEmitter renderers=%d functionCallNodes=%d"), PD->GetRenderers().Num(), Calls.Num());
			for (UNiagaraNodeFunctionCall* C : Calls) { PROBE(TEXT("VERIFY:    module %s"), *C->GetFunctionName()); }
		}
		Sys->RequestCompile(true);
		Sys->WaitForCompilationComplete(true, false);
		NiagaraProbe::ReportScript(Sys->GetSystemUpdateScript(), TEXT("VERIFY SystemUpdate"));
		NiagaraProbe::ReportScript(Sys->GetEmitterHandles()[0].GetEmitterData()->GetGPUComputeScript(), TEXT("VERIFY GPU"));
		return 0;
	}
	if (FParse::Param(*Params, TEXT("Audit")))
	{
		AuditEngineModules();
		return 0;
	}
	FString Path = TEXT("/Game/Probe");
	FString Name = TEXT("NS_Probe");
	FParse::Value(*Params, TEXT("Path="), Path);
	FParse::Value(*Params, TEXT("Name="), Name);
	return NiagaraProbe::Build(Path, Name) ? 0 : 1;
}

bool UNiagaraProbeLibrary::BuildProbeSystem(const FString& PackagePath, const FString& AssetName)
{
	return NiagaraProbe::Build(PackagePath, AssetName);
}

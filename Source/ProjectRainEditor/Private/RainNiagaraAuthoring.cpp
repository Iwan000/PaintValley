// Project Rain - helpers for authoring Niagara assets from C++.
#include "RainNiagaraAuthoring.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_Niagara.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "NiagaraEditorUtilities.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterFactoryNew.h"
#include "NiagaraGraph.h"
#include "NiagaraNodeCustomHlsl.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraNodeInput.h"
#include "NiagaraNodeOutput.h"
#include "NiagaraScript.h"
#include "NiagaraScriptSource.h"
#include "NiagaraShared.h"
#include "NiagaraSystem.h"
#include "NiagaraSystemFactoryNew.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "ViewModels/Stack/NiagaraStackGraphUtilities.h"
#include "ViewModels/Stack/NiagaraParameterHandle.h"
#include "NiagaraScriptVariable.h"
#include "NiagaraDataInterfaceParticleRead.h"

DEFINE_LOG_CATEGORY_STATIC(LogRainAuthoring, Log, All);

namespace RainAuthoring
{
	static UEdGraphPin* FindPinByDir(UEdGraphNode* Node, EEdGraphPinDirection Dir, FName Name = NAME_None)
	{
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin->Direction == Dir && (Name.IsNone() || Pin->PinName == Name))
			{
				return Pin;
			}
		}
		return nullptr;
	}

	UPackage* PreparePackage(const FString& PackagePath, const FString& AssetName)
	{
		const FString PackageName = PackagePath / AssetName;
		UPackage* Package = CreatePackage(*PackageName);
		Package->FullyLoad();
		if (UObject* Existing = StaticFindObject(UObject::StaticClass(), Package, *AssetName))
		{
			// Move the old asset out of the way so a fresh one can take its name.
			const FName Trash = MakeUniqueObjectName(GetTransientPackage(), Existing->GetClass(), *(AssetName + TEXT("_OLD")));
			Existing->Rename(*Trash.ToString(), GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
			Existing->ClearFlags(RF_Public | RF_Standalone);
			Existing->MarkAsGarbage();
		}
		return Package;
	}

	UNiagaraScript* CreateHlslModule(const FString& PackagePath, const FString& AssetName, const FString& Hlsl,
		const FString& Description, int32 ModuleUsageBitmask)
	{
		UPackage* Package = PreparePackage(PackagePath, AssetName);
		UNiagaraScript* Script = NewObject<UNiagaraScript>(Package, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
		Script->Usage = ENiagaraScriptUsage::Module;
		if (FVersionedNiagaraScriptData* Data = Script->GetLatestScriptData())
		{
			Data->ModuleUsageBitmask = ModuleUsageBitmask;
			Data->Description = FText::FromString(Description);
			Data->Category = FText::FromString(TEXT("Project Rain"));
			Data->LibraryVisibility = ENiagaraScriptLibraryVisibility::Library;
		}

		UNiagaraScriptSource* Source = NewObject<UNiagaraScriptSource>(Script, NAME_None, RF_Transactional);
		UNiagaraGraph* Graph = NewObject<UNiagaraGraph>(Source, NAME_None, RF_Transactional);
		Source->NodeGraph = Graph;
		Script->SetLatestSource(Source);

		const UEdGraphSchema_Niagara* Schema = CastChecked<UEdGraphSchema_Niagara>(Graph->GetSchema());

		FGraphNodeCreator<UNiagaraNodeOutput> OutputCreator(*Graph);
		UNiagaraNodeOutput* OutputNode = OutputCreator.CreateNode();
		OutputNode->SetUsage(ENiagaraScriptUsage::Module);
		OutputNode->Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetParameterMapDef(), TEXT("Output")));
		OutputNode->NodePosX = 900;
		OutputCreator.Finalize();

		FGraphNodeCreator<UNiagaraNodeInput> InputCreator(*Graph);
		UNiagaraNodeInput* InputNode = InputCreator.CreateNode();
		InputNode->Usage = ENiagaraInputNodeUsage::Parameter;
		InputNode->Input = FNiagaraVariable(FNiagaraTypeDefinition::GetParameterMapDef(), TEXT("InputMap"));
		InputNode->NodePosX = -300;
		InputCreator.Finalize();

		FGraphNodeCreator<UNiagaraNodeCustomHlsl> HlslCreator(*Graph);
		UNiagaraNodeCustomHlsl* HlslNode = HlslCreator.CreateNode();
		HlslNode->ScriptUsage = ENiagaraScriptUsage::Module;
		HlslNode->Signature.Name = *AssetName;
		HlslNode->Signature.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetParameterMapDef(), TEXT("Map")));
		HlslNode->Signature.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetParameterMapDef(), TEXT("MapOut")));
		HlslNode->NodePosX = 100;
		// CustomHlsl is a private UPROPERTY and SetCustomHlsl isn't exported: write it via reflection.
		if (FStrProperty* HlslProp = FindFProperty<FStrProperty>(UNiagaraNodeCustomHlsl::StaticClass(), TEXT("CustomHlsl")))
		{
			HlslProp->SetPropertyValue_InContainer(HlslNode, Hlsl);
		}
		else
		{
			UE_LOG(LogRainAuthoring, Error, TEXT("Could not find CustomHlsl property"));
		}
		HlslCreator.Finalize();

		Schema->TryCreateConnection(FindPinByDir(InputNode, EGPD_Output), FindPinByDir(HlslNode, EGPD_Input, TEXT("Map")));
		Schema->TryCreateConnection(FindPinByDir(HlslNode, EGPD_Output, TEXT("MapOut")), FindPinByDir(OutputNode, EGPD_Input));

		if (FStrProperty* HlslProp = FindFProperty<FStrProperty>(UNiagaraNodeCustomHlsl::StaticClass(), TEXT("CustomHlsl")))
		{
			FPropertyChangedEvent Event(HlslProp);
			HlslNode->PostEditChangeProperty(Event);
		}
		Graph->NotifyGraphChanged();

		FAssetRegistryModule::AssetCreated(Script);
		Script->MarkPackageDirty();
		return Script;
	}

	UNiagaraScript* CreateReaderModule(const FString& PackagePath, const FString& AssetName, const FString& Hlsl,
		const FString& Description, int32 ModuleUsageBitmask, FString& Report)
	{
		UNiagaraScript* Stock = LoadObject<UNiagaraScript>(nullptr, TEXT("/Niagara/Modules/Update/Neighbor/CalculateNeighbors.CalculateNeighbors"));
		if (!Stock)
		{
			Report += TEXT("  ERROR: no stock CalculateNeighbors module\n");
			return nullptr;
		}
		UPackage* Package = PreparePackage(PackagePath, AssetName);
		UNiagaraScript* Script = DuplicateObject<UNiagaraScript>(Stock, Package, *AssetName);
		Script->SetFlags(RF_Public | RF_Standalone | RF_Transactional);
		if (FVersionedNiagaraScriptData* Data = Script->GetLatestScriptData())
		{
			Data->ModuleUsageBitmask = ModuleUsageBitmask;
			Data->Description = FText::FromString(Description);
			Data->Category = FText::FromString(TEXT("Project Rain"));
			Data->LibraryVisibility = ENiagaraScriptLibraryVisibility::Library;
		}
		UNiagaraScriptSource* Source = Cast<UNiagaraScriptSource>(Script->GetLatestSource());
		UNiagaraGraph* Graph = Source ? Source->NodeGraph : nullptr;
		if (!Graph)
		{
			Report += TEXT("  ERROR: the duplicated module has no graph\n");
			return nullptr;
		}
		const UEdGraphSchema_Niagara* Schema = CastChecked<UEdGraphSchema_Niagara>(Graph->GetSchema());

		// The pieces kept: the output, the map input, the map-get that reads the module's reader input, and the input
		// node holding the reader's default.
		UNiagaraNodeOutput* OutputNode = nullptr;
		UNiagaraNodeInput* MapNode = nullptr;
		UNiagaraNodeInput* ReaderDefault = nullptr;
		UEdGraphPin* ReaderPin = nullptr;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (UNiagaraNodeOutput* O = Cast<UNiagaraNodeOutput>(Node))
			{
				OutputNode = O;
			}
			else if (UNiagaraNodeInput* I = Cast<UNiagaraNodeInput>(Node))
			{
				if (I->Input.GetType() == FNiagaraTypeDefinition::GetParameterMapDef())
				{
					MapNode = I;
				}
				else if (I->Input.GetType().GetClass() && I->Input.GetType().GetClass()->IsChildOf(UNiagaraDataInterfaceParticleRead::StaticClass()))
				{
					ReaderDefault = I;
				}
			}
			else if (Node->GetClass()->GetName() == TEXT("NiagaraNodeParameterMapGet"))
			{
				for (UEdGraphPin* Pin : Node->Pins)
				{
					const FString Name = Pin->PinName.ToString();
					if (Pin->Direction == EGPD_Output && Name.StartsWith(TEXT("Module.")) && Name.EndsWith(TEXT("AttributeReader")))
					{
						ReaderPin = Pin;
					}
				}
			}
		}
		if (!OutputNode || !MapNode || !ReaderPin)
		{
			Report += FString::Printf(TEXT("  ERROR: CalculateNeighbors' graph changed (output %p, map %p, reader %p)\n"), OutputNode, MapNode, ReaderPin);
			return nullptr;
		}
		UEdGraphPin* MapOut = FindPinByDir(MapNode, EGPD_Output);
		// The reader's map-get reads straight from the input map.
		for (UEdGraphPin* Pin : ReaderPin->GetOwningNode()->Pins)
		{
			if (Pin->Direction == EGPD_Input && UEdGraphSchema_Niagara::PinToTypeDefinition(Pin) == FNiagaraTypeDefinition::GetParameterMapDef())
			{
				Schema->BreakPinLinks(*Pin, false);
				Schema->TryCreateConnection(MapOut, Pin);
			}
		}

		FGraphNodeCreator<UNiagaraNodeCustomHlsl> HlslCreator(*Graph);
		UNiagaraNodeCustomHlsl* HlslNode = HlslCreator.CreateNode();
		HlslNode->ScriptUsage = ENiagaraScriptUsage::Module;
		HlslNode->Signature.Name = *AssetName;
		HlslNode->Signature.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetParameterMapDef(), TEXT("Map")));
		HlslNode->Signature.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(UNiagaraDataInterfaceParticleRead::StaticClass()), TEXT("Reader")));
		HlslNode->Signature.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetParameterMapDef(), TEXT("MapOut")));
		FStrProperty* HlslProp = FindFProperty<FStrProperty>(UNiagaraNodeCustomHlsl::StaticClass(), TEXT("CustomHlsl"));
		if (HlslProp)
		{
			HlslProp->SetPropertyValue_InContainer(HlslNode, Hlsl);
		}
		HlslCreator.Finalize();
		UEdGraphPin* OutIn = FindPinByDir(OutputNode, EGPD_Input);
		Schema->BreakPinLinks(*OutIn, false);
		const bool bLinked = Schema->TryCreateConnection(MapOut, FindPinByDir(HlslNode, EGPD_Input, TEXT("Map")))
			&& Schema->TryCreateConnection(ReaderPin, FindPinByDir(HlslNode, EGPD_Input, TEXT("Reader")))
			&& Schema->TryCreateConnection(FindPinByDir(HlslNode, EGPD_Output, TEXT("MapOut")), OutIn);
		if (HlslProp)
		{
			FPropertyChangedEvent Event(HlslProp);
			HlslNode->PostEditChangeProperty(Event);
		}
		// The reader reads the emitter it runs in.
		// (UNiagaraNodeInput::GetDataInterface is not exported: its property, by reflection.)
		UNiagaraDataInterfaceParticleRead* Reader = nullptr;
		if (FObjectPropertyBase* Prop = ReaderDefault ? FindFProperty<FObjectPropertyBase>(UNiagaraNodeInput::StaticClass(), TEXT("DataInterface")) : nullptr)
		{
			Reader = Cast<UNiagaraDataInterfaceParticleRead>(Prop->GetObjectPropertyValue_InContainer(ReaderDefault));
		}
		if (Reader)
		{
			Reader->EmitterBinding.BindingMode = ENiagaraDataInterfaceEmitterBindingMode::Self;
		}
		Report += FString::Printf(TEXT("  reader module %s: %s, default reader %s\n"), *AssetName, bLinked ? TEXT("linked") : TEXT("ERROR: NOT LINKED"),
			Reader ? TEXT("bound to self") : TEXT("ERROR: missing"));
		Graph->NotifyGraphChanged();
		FAssetRegistryModule::AssetCreated(Script);
		Script->MarkPackageDirty();
		return Script;
	}

	UNiagaraEmitter* CreateEmptyEmitter(const FString& Name, bool bAddDefaultModulesAndRenderers)
	{
		UNiagaraEmitter* Emitter = NewObject<UNiagaraEmitter>(GetTransientPackage(), MakeUniqueObjectName(GetTransientPackage(), UNiagaraEmitter::StaticClass(), *Name), RF_Transactional);
		UNiagaraEmitterFactoryNew::InitializeEmitter(Emitter, bAddDefaultModulesAndRenderers);
		Emitter->SetUniqueEmitterName(Name);
		return Emitter;
	}

	UNiagaraNodeOutput* GetOutputNode(FVersionedNiagaraEmitterData& EmitterData, ENiagaraScriptUsage Usage)
	{
		UNiagaraScriptSource* Source = Cast<UNiagaraScriptSource>(EmitterData.GraphSource);
		if (!Source || !Source->NodeGraph)
		{
			return nullptr;
		}
		UNiagaraScript* Script = EmitterData.GetScript(Usage, FGuid());
		return Source->NodeGraph->FindEquivalentOutputNode(Usage, Script ? Script->GetUsageId() : FGuid());
	}

	UNiagaraNodeFunctionCall* AddModule(FVersionedNiagaraEmitterData& EmitterData, ENiagaraScriptUsage Usage, UNiagaraScript* Module)
	{
		UNiagaraNodeOutput* Out = GetOutputNode(EmitterData, Usage);
		if (!Out || !Module)
		{
			UE_LOG(LogRainAuthoring, Error, TEXT("AddModule failed (output node %p, module %p)"), Out, Module);
			return nullptr;
		}
		return FNiagaraStackGraphUtilities::AddScriptModuleToStack(Module, *Out);
	}

	UNiagaraNodeFunctionCall* AddModuleByPath(FVersionedNiagaraEmitterData& EmitterData, ENiagaraScriptUsage Usage, const FString& ObjectPath)
	{
		UNiagaraScript* Module = LoadObject<UNiagaraScript>(nullptr, *ObjectPath);
		if (!Module)
		{
			UE_LOG(LogRainAuthoring, Error, TEXT("Missing module %s"), *ObjectPath);
			return nullptr;
		}
		return AddModule(EmitterData, Usage, Module);
	}

	void AddAttributeDeclarations(FVersionedNiagaraEmitterData& EmitterData, ENiagaraScriptUsage Usage,
		const TArray<FNiagaraVariable>& Vars, const TArray<FString>& Defaults)
	{
		// Attributes read/written only inside Custom HLSL are invisible to the attribute trimmer, which
		// would strip them from the particle data set (they then read back as 0 every frame).
		for (const FNiagaraVariable& Var : Vars)
		{
			EmitterData.AttributesToPreserve.AddUnique(Var.GetName().ToString());
		}
		if (UNiagaraNodeOutput* Out = GetOutputNode(EmitterData, Usage))
		{
			FNiagaraStackGraphUtilities::AddParameterModuleToStack(Vars, *Out, INDEX_NONE, Defaults);
		}
	}

	template<typename T>
	static void SetRapidIteration(UNiagaraEmitter* Emitter, FVersionedNiagaraEmitterData& EmitterData, ENiagaraScriptUsage Usage,
		UNiagaraNodeFunctionCall* Module, FName InputName, const FNiagaraTypeDefinition& Type, const T& Value)
	{
		if (!Module)
		{
			return;
		}
		// Aliased module input "<FunctionName>.<Input>", converted to "Constants.<Emitter>.<FunctionName>.<Input>".
		const FName Aliased(*(Module->GetFunctionName() + TEXT(".") + InputName.ToString()));
		FNiagaraVariable Var(Type, Aliased);
		const FString EmitterName = Emitter->GetUniqueEmitterName();
		FNiagaraVariable RIVar = FNiagaraUtilities::ConvertVariableToRapidIterationConstantName(Var, *EmitterName, Usage);
		RIVar.SetValue(Value);
		// Write to every script that runs this stage (incl. the combined GPU compute script and interpolated spawn).
		TArray<UNiagaraScript*> Scripts;
		EmitterData.GetScripts(Scripts, false, true);
		for (UNiagaraScript* Script : Scripts)
		{
			if (Script && Script->ContainsUsage(Usage))
			{
				Script->RapidIterationParameters.SetParameterData(RIVar.GetData(), RIVar, true);
			}
		}
	}

	void SetModuleInputFloat(UNiagaraEmitter* Emitter, FVersionedNiagaraEmitterData& EmitterData, ENiagaraScriptUsage Usage,
		UNiagaraNodeFunctionCall* Module, FName InputName, float Value)
	{
		SetRapidIteration(Emitter, EmitterData, Usage, Module, InputName, FNiagaraTypeDefinition::GetFloatDef(), Value);
	}

	void SetModuleInputInt(UNiagaraEmitter* Emitter, FVersionedNiagaraEmitterData& EmitterData, ENiagaraScriptUsage Usage,
		UNiagaraNodeFunctionCall* Module, FName InputName, int32 Value)
	{
		SetRapidIteration(Emitter, EmitterData, Usage, Module, InputName, FNiagaraTypeDefinition::GetIntDef(), Value);
	}

	bool LinkModuleInput(UNiagaraNodeFunctionCall* Module, FName InputName, const FNiagaraTypeDefinition& Type, const FNiagaraVariableBase& Param)
	{
		if (!Module)
		{
			return false;
		}
		const FNiagaraParameterHandle Aliased = FNiagaraParameterHandle::CreateAliasedModuleParameterHandle(
			FNiagaraParameterHandle::CreateModuleParameterHandle(InputName), Module);
		FGuid VarId;
		if (UNiagaraGraph* Called = Module->GetCalledGraph())
		{
			if (UNiagaraScriptVariable* SV = Called->GetScriptVariable(FName(*(TEXT("Module.") + InputName.ToString()))))
			{
				VarId = SV->Metadata.GetVariableGuid();
			}
		}
		UEdGraphPin& OverridePin = FNiagaraStackGraphUtilities::GetOrCreateStackFunctionInputOverridePin(*Module, Aliased, Type, VarId, FGuid());
		if (OverridePin.LinkedTo.Num() != 0)
		{
			return false;
		}
		TSet<FNiagaraVariableBase> Known;
		Known.Add(Param);
		FNiagaraStackGraphUtilities::SetLinkedParameterValueForFunctionInput(OverridePin, Param, Known);
		return true;
	}

	UNiagaraSystem* CreateSystemAsset(const FString& PackagePath, const FString& AssetName)
	{
		UPackage* Package = PreparePackage(PackagePath, AssetName);
		UNiagaraSystem* System = NewObject<UNiagaraSystem>(Package, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
		UNiagaraSystemFactoryNew::InitializeSystem(System, true);
		// The default system state loops every 5 s. The emitters follow the system's life cycle, and each
		// system loop re-fires their one-shot spawn burst while the GPU particles survive the loop: a new
		// full set of drops piled up every 5 s. A single 1e7 s loop fires the burst exactly once.
		FNiagaraVariable LoopDuration(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Constants.SystemState.Loop Duration"));
		LoopDuration.SetValue(1.0e7f);
		for (UNiagaraScript* Script : { System->GetSystemSpawnScript(), System->GetSystemUpdateScript() })
		{
			if (Script)
			{
				Script->RapidIterationParameters.SetParameterData(LoopDuration.GetData(), LoopDuration, true);
			}
		}
		// Custom-HLSL-only attributes must survive compilation (see AddAttributeDeclarations).
		System->SetTrimAttributesOnCook(false);
		FAssetRegistryModule::AssetCreated(System);
		System->MarkPackageDirty();
		return System;
	}

	void AddUserParameter(UNiagaraSystem* System, const FNiagaraVariable& Var)
	{
		FNiagaraVariable UserVar = Var;
		if (!UserVar.GetName().ToString().StartsWith(TEXT("User.")))
		{
			UserVar.SetName(*(TEXT("User.") + Var.GetName().ToString()));
		}
		System->GetExposedParameters().AddParameter(UserVar, true, true);
		if (Var.IsDataAllocated())
		{
			System->GetExposedParameters().SetParameterData(Var.GetData(), UserVar);
		}
	}

	static void ReportScript(UNiagaraScript* Script, const FString& Owner, FString& Report, bool& bOk)
	{
		if (!Script)
		{
			return;
		}
		// Emitter spawn/update scripts are compiled into the system scripts; their own status stays Unknown.
		if (Script->GetUsage() == ENiagaraScriptUsage::EmitterSpawnScript || Script->GetUsage() == ENiagaraScriptUsage::EmitterUpdateScript)
		{
			return;
		}
		const ENiagaraScriptCompileStatus Status = Script->GetLastCompileStatus();
		const bool bScriptOk = Status == ENiagaraScriptCompileStatus::NCS_UpToDate || Status == ENiagaraScriptCompileStatus::NCS_UpToDateWithWarnings || Status == ENiagaraScriptCompileStatus::NCS_ComputeUpToDateWithWarnings;
		Report += FString::Printf(TEXT("  [%s] %s usage=%d status=%d\n"), bScriptOk ? TEXT("OK") : TEXT("!!"), *Owner,
			int32(Script->GetUsage()), int32(Status));
		const FNiagaraVMExecutableData& Exec = Script->GetVMExecutableData();
		for (const FNiagaraCompileEvent& Event : Exec.LastCompileEvents)
		{
			if (Event.Severity >= FNiagaraCompileEventSeverity::Warning)
			{
				Report += FString::Printf(TEXT("      %s: %s\n"), Event.Severity == FNiagaraCompileEventSeverity::Error ? TEXT("ERROR") : TEXT("warn"), *Event.Message);
				if (Event.Severity == FNiagaraCompileEventSeverity::Error)
				{
					bOk = false;
				}
			}
		}
		if (!Exec.ErrorMsg.IsEmpty())
		{
			Report += FString::Printf(TEXT("      ErrorMsg: %s\n"), *Exec.ErrorMsg);
		}
		if (Script->IsGPUScript(Script->GetUsage()) || Script->GetUsage() == ENiagaraScriptUsage::ParticleGPUComputeScript)
		{
			if (const FNiagaraShaderScript* Shader = Script->GetRenderThreadScript())
			{
				for (const FString& Err : Shader->GetCompileErrors())
				{
					Report += FString::Printf(TEXT("      GPU: %s\n"), *Err);
					bOk = false;
				}
			}
		}
		if (!bScriptOk)
		{
			bOk = false;
		}
	}

	bool CompileAndReport(UNiagaraSystem* System, FString& OutReport)
	{
		System->RequestCompile(false);
		System->WaitForCompilationComplete(true, false);

		bool bOk = true;
		OutReport += FString::Printf(TEXT("Compile report for %s\n"), *System->GetPathName());
		ReportScript(System->GetSystemSpawnScript(), TEXT("SystemSpawn"), OutReport, bOk);
		ReportScript(System->GetSystemUpdateScript(), TEXT("SystemUpdate"), OutReport, bOk);
		for (const FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
		{
			FVersionedNiagaraEmitterData* Data = Handle.GetEmitterData();
			if (!Data)
			{
				continue;
			}
			TArray<UNiagaraScript*> Scripts;
			Data->GetScripts(Scripts, false, true);
			for (UNiagaraScript* Script : Scripts)
			{
				ReportScript(Script, Handle.GetName().ToString(), OutReport, bOk);
			}
		}
		UE_LOG(LogRainAuthoring, Display, TEXT("%s"), *OutReport);
		return bOk;
	}

	bool SaveAsset(UObject* Asset)
	{
		UPackage* Package = Asset->GetPackage();
		Package->MarkPackageDirty();
		const FString FileName = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		Args.SaveFlags = SAVE_NoError;
		Args.Error = GError;
		const bool bSaved = UPackage::SavePackage(Package, Asset, *FileName, Args);
		UE_LOG(LogRainAuthoring, Display, TEXT("Saved %s -> %s (%s)"), *Asset->GetPathName(), *FileName, bSaved ? TEXT("ok") : TEXT("FAILED"));
		return bSaved;
	}

	FString LoadTextFile(const FString& ProjectRelativePath)
	{
		FString Text;
		const FString Full = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / ProjectRelativePath);
		if (!FFileHelper::LoadFileToString(Text, *Full))
		{
			UE_LOG(LogRainAuthoring, Error, TEXT("Could not read %s"), *Full);
		}
		return Text;
	}
}

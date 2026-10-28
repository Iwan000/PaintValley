// Project Rain - helpers for authoring Niagara assets from C++ (no UI clicking).
#pragma once

#include "CoreMinimal.h"
#include "NiagaraTypes.h"
#include "NiagaraCommon.h"

class UNiagaraScript;
class UNiagaraEmitter;
class UNiagaraSystem;
class UNiagaraNodeOutput;
class UNiagaraNodeFunctionCall;
struct FVersionedNiagaraEmitterData;

namespace RainAuthoring
{
	/** Creates (or recreates in place) a Niagara module script asset whose graph is
	 *  InputMap -> CustomHlsl(Map in / MapOut out) -> Output. The HLSL reads and writes
	 *  namespaced parameters directly (Particles.*, User.*, Engine.*, Emitter.*). */
	UNiagaraScript* CreateHlslModule(const FString& PackagePath, const FString& AssetName, const FString& Hlsl,
		const FString& Description, int32 ModuleUsageBitmask);

	/** Like CreateHlslModule, with a second input: Reader, a Particle Attribute Reader bound to the emitter the module
	 *  runs in (it reads the previous frame's particles). Module graphs reach a data interface through a map-get node,
	 *  whose class the engine does not export: the module is duplicated from the engine's CalculateNeighbors (which
	 *  has that chain) and its graph rewired to InputMap -> CustomHlsl(Map, Reader / MapOut) -> Output. */
	UNiagaraScript* CreateReaderModule(const FString& PackagePath, const FString& AssetName, const FString& Hlsl,
		const FString& Description, int32 ModuleUsageBitmask, FString& Report);

	/** A transient emitter initialised with empty spawn/update stacks (no default modules or renderers). */
	UNiagaraEmitter* CreateEmptyEmitter(const FString& Name, bool bAddDefaultModulesAndRenderers = false);

	UNiagaraNodeOutput* GetOutputNode(FVersionedNiagaraEmitterData& EmitterData, ENiagaraScriptUsage Usage);

	UNiagaraNodeFunctionCall* AddModule(FVersionedNiagaraEmitterData& EmitterData, ENiagaraScriptUsage Usage, UNiagaraScript* Module);
	UNiagaraNodeFunctionCall* AddModuleByPath(FVersionedNiagaraEmitterData& EmitterData, ENiagaraScriptUsage Usage, const FString& ObjectPath);

	/** Declares (and initialises) attributes in the given stage via a "Set Parameters" assignment module. */
	void AddAttributeDeclarations(FVersionedNiagaraEmitterData& EmitterData, ENiagaraScriptUsage Usage,
		const TArray<FNiagaraVariable>& Vars, const TArray<FString>& Defaults);

	/** Sets a literal module input (rapid iteration parameter). */
	void SetModuleInputFloat(UNiagaraEmitter* Emitter, FVersionedNiagaraEmitterData& EmitterData, ENiagaraScriptUsage Usage,
		UNiagaraNodeFunctionCall* Module, FName InputName, float Value);
	void SetModuleInputInt(UNiagaraEmitter* Emitter, FVersionedNiagaraEmitterData& EmitterData, ENiagaraScriptUsage Usage,
		UNiagaraNodeFunctionCall* Module, FName InputName, int32 Value);

	/** Links a module input to a parameter (e.g. SpawnBurst.Age -> Emitter.LoopedAge), like the stack's "Link Inputs" menu. */
	bool LinkModuleInput(UNiagaraNodeFunctionCall* Module, FName InputName, const FNiagaraTypeDefinition& Type, const FNiagaraVariableBase& Param);

	/** Creates (replacing any existing) a system asset. */
	UNiagaraSystem* CreateSystemAsset(const FString& PackagePath, const FString& AssetName);

	void AddUserParameter(UNiagaraSystem* System, const FNiagaraVariable& Var);

	/** Adds every User.Rain* parameter the rain HLSL reads (defined in RainAssetBuilder.cpp). */
	void AddRainUserParameters(UNiagaraSystem* System);

	/** Compiles synchronously and logs every script's status/errors. Returns true if everything compiled cleanly. */
	bool CompileAndReport(UNiagaraSystem* System, FString& OutReport);

	bool SaveAsset(UObject* Asset);

	FString LoadTextFile(const FString& ProjectRelativePath);

	/** Recreates a fresh object at PackagePath/AssetName, moving any old one out of the way. */
	UPackage* PreparePackage(const FString& PackagePath, const FString& AssetName);
}

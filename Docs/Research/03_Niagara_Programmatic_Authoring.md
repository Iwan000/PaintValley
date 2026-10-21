# 03 用代码创作 Niagara（UE 5.7）：C++ 编辑器模块 / Python / 无头运行

> 适用：UE 5.7 安装版（`/Users/Shared/Epic Games/UE_5.7`），macOS Apple Silicon。
> 目标：不点 UI，由项目内的 C++ 编辑器模块（Python 只做编排）生成完整的雨效 Niagara System：GPU 模拟、自定义 HLSL 逻辑、Sprite/Mesh 渲染器、可由 Blueprint/Sequencer 驱动的 User 参数。
>
> **证据等级标注**
> - 【运行已验证】：在 scratchpad 中复制本项目骨架，编译了一个探针编辑器模块 + Commandlet，用 `UnrealEditor-Cmd` 无头运行；资产成功生成、保存、重新加载、重新编译，GPU HLSL 翻译结果已逐行检查。
> - 【链接已验证】：探针模块在 Mac 上正常链接（真实链接不带 `-undefined dynamic_lookup`），并用 `nm` 把全部未定义符号与 `UnrealEditor-NiagaraEditor.dylib` / `UnrealEditor-Niagara.dylib` 的导出表逐一比对。
> - 【仅读源码】：只读了引擎源码，未实测。
> - 【网络】：来自 GitHub/文档，本人未编译。
>
> **实测代码**：实际编译并运行的探针源码已拷贝到 [`Docs/Research/03_code/`](03_code/)，它就是【运行已验证】这一标签的依据。
> - `NiagaraProbeCommandlet.h/.cpp`：Commandlet + BlueprintFunctionLibrary，以及 `-Audit` / `-Verify` 模式。
> - `ProjectRainEditor.Build.cs.txt`：模块依赖。
> - `probe_py.py`：Python 探针。
> - `NiagaraModuleAudit_UE57.csv`：引擎模块审计结果。
>
> 正文代码片段是从这份文件**节选整理**的，函数签名略有简化。如果片段和文件对不上，以文件为准。文件本身放在 Docs 下，不会参与编译；使用时拷进 `Source/ProjectRainEditor/Private/`。

---

## 0. 结论速览

1. **整条链路在 UE 5.7 原版引擎上能从项目模块跑通，不需要改引擎、不需要私有 include 路径。**【运行已验证】
   流程是：建 System 资产 → 建独立 Emitter 并加入 System → 改为 GPU 模拟并设固定包围盒 → 加引擎模块 → 设字面量输入（Rapid Iteration 参数）→ 新增 User 参数并把模块输入链接过去 → 加 Sprite（速度对齐）和 Mesh 渲染器 → **用代码生成 3 种自定义 HLSL 模块资产** → 用"Set Parameters + 自定义表达式"写自定义属性 → 编译 → 保存 → 重新加载并重编译，全部成功。GPU compute 脚本状态为 `NCS_UpToDate`，生成的 GPU HLSL 里能看到我们写的代码。
2. **UE 5.7 中有一个对我们很有利的行为（从哪个版本开始的没有核对）。** UHT 给**每个** UCLASS 都导出 `Z_Construct_UClass_X_NoRegister()`（`NIAGARAEDITOR_API`），而 `StaticClass()` 正是走这个函数。【链接已验证】
   - 因此，即使类本身没加 API 宏，也能在项目模块里 `NewObject<>` / `Cast<>` / 取 `StaticClass()`。例如 `UNiagaraSystemFactoryNew`，以及位于 Private 目录的 `UNiagaraNodeParameterMapGet/Set`（后者用 `FindObject<UClass>` 拿类即可）。
   - **但这些类的非虚成员函数仍然链接不到**，例如 `SetCustomHlsl`、`RequestNewTypedPin`、`UNiagaraGraph::AddParameter`。虚函数走 vtable，可以正常调用。
3. **自定义逻辑有 4 条路线已验证可用**，详见第 3 节：
   - (A) 只用 ParameterMap 引脚的 Custom HLSL 模块：最省事，HLSL 里直接读写 `Particles.*` / `User.*` / `Engine.*`；
   - (B) 经典的 MapGet → CustomHlsl → MapSet：引脚通过**已导出**的 `UE::Niagara::Wizard::Utilities::AddReadParameterPin/AddWriteParameterPin` 添加；
   - (C) "拖线到 + 号"技巧：用 `UEdGraphSchema_Niagara::TryCreateConnection` 把线连到动态 Add 引脚，节点会自动生成带类型的引脚；
   - (D) 栈上的"New Expression"：`UNiagaraStackFunctionInput::SetCustomExpression`，完全导出。
   - Custom HLSL 的代码文本是私有 UPROPERTY，用反射写入，再调用虚函数 `PostEditChangeProperty`。
4. **设置模块输入有两套办法，都已验证：**
   - 底层：手写 Rapid Iteration 参数名，用已导出的 `FNiagaraUtilities::ConvertVariableToRapidIterationConstantName` 代替未导出的 `CreateRapidIterationParameter`；
   - 高层：无头创建 `FNiagaraSystemViewModel`（设 `bIsForDataProcessingOnly=true`），再调用 `UNiagaraStackFunctionInput::SetLocalValue / SetLinkedParameterValue / SetCustomExpression / SetDynamicInput`。这正是 UI 的做法，会自动处理 GPU 脚本、插值 spawn 脚本、GUID 映射等细节。
5. **Python 没有 Niagara 编辑 API**（没有 `unreal.NiagaraEditorLibrary`）。【运行已验证】
   - Python 能做的：用工厂建空 System / Module / Emitter 资产，读写少量 System 属性。
   - 正确做法：在项目 C++ 编辑器模块里写 `UBlueprintFunctionLibrary`，Python 调 `unreal.XxxLibrary.build_xxx()`。这条路径已实测可用。
6. **本机环境有两个必须先处理的阻断问题（实测发现）：**
   - **Xcode 27 缺少 Metal Toolchain。** 只要引擎需要编译着色器（打开编辑器，或在 commandlet 里加 `-AllowCommandletRendering`），就会弹出 `cannot execute tool 'metal' due to missing Metal Toolchain` 然后退出。需要你手动执行 `xcodebuild -downloadComponent MetalToolchain`。在此之前，只能做"HLSL 翻译级"的验证，GPU 着色器真正编译成 Metal 字节码这一步无法验证。
   - **`ProjectRain.uproject` 里 `ProjectRainEditor` 的 `LoadingPhase` 是 `PostEngineInit`。** 这样放在该模块里的 Commandlet **找不到**，日志报 `NiagaraProbeCommandlet looked like a commandlet, but we could not find the class`。要么改成 `Default`，要么把 Commandlet 放进另一个 `Default` 阶段加载的编辑器模块。

---

## 1. 创建 System / Emitter 资产

### 1.1 关键事实

| 符号 | 头文件:行 | 导出 | 说明 |
|---|---|---|---|
| `UNiagaraSystemFactoryNew` | `NiagaraEditor/Public/NiagaraSystemFactoryNew.h:16` | 类无 API，但 5.7 的 `StaticClass()` 可链接 | `NewObject<UNiagaraSystemFactoryNew>()` 后交给 `IAssetTools::CreateAsset`。`CreateAsset` **不会**调用会弹模态窗的 `ConfigureProperties()`（`AssetTools.cpp:1730-1773`）【运行已验证】 |
| `UNiagaraSystemFactoryNew::InitializeSystem` | 同上 `:29` | ✅ `NIAGARAEDITOR_API` | 生成 System 的脚本源和图，并添加必需的 `SystemState` 模块（`NiagaraSystemFactoryNew.cpp:185-215`） |
| `UNiagaraEmitterFactoryNew::InitializeEmitter` | `NiagaraEmitterFactoryNew.h:27` | ✅ | `bAddDefaultModulesAndRenderers=false` 得到一个只有 4 个输出节点的空 Emitter。**注意：它会强制 `SimTarget=CPUSim`、`InterpolatedSpawnMode=Interpolation`**（`NiagaraEmitterFactoryNew.cpp:206-282`） |
| `UNiagaraScriptFactoryNew::InitializeScript` | `NiagaraScriptFactoryNew.h:23` | ❌ | 自己建模块图，见 3.2 节；也可以用 `UNiagaraModuleScriptFactory` + `CreateAsset` |
| `FNiagaraEditorUtilities::AddEmitterToSystem` | `NiagaraEditorUtilities.h:279` | ✅ | 推荐用这个。它会 Kill 实例、生成唯一名、调用 `AddEmitterHandle`、`RebuildEmitterNodes`、同步 overview 图（`NiagaraEditorUtilities.cpp:2110-2167`） |
| `UNiagaraSystem::AddEmitterHandle` | `NiagaraSystem.h:328` | ✅ `NIAGARA_API` | **会复制一份 Emitter**（`CreateWithParentAndOwner`，`NiagaraSystem.cpp:2824-2838`）。源 Emitter 的 `bIsInheritable==false` 时，会对副本 `DisableVersioning` 并 `RemoveParent`，得到一个不依赖父资产的独立 Emitter |
| `FNiagaraEmitterHandle::GetInstance()/GetEmitterData()` | `NiagaraEmitterHandle.h:82/86` | ✅ | 之后所有修改都应作用在**副本**上，也就是 `Handle.GetInstance().Emitter` |
| `UNiagaraEmitter::GetLatestEmitterData()` | `NiagaraEmitter.h:687` | ✅ | 版本化数据都在 `FVersionedNiagaraEmitterData`（`NiagaraEmitter.h:239` 起），包括 `SimTarget`、`bLocalSpace`、`CalculateBoundsMode`、`FixedBounds`、各脚本属性、`GraphSource`、渲染器等 |

> 网络旁证【网络】：ue-mcp 的注释说，System 里还没有任何 Emitter 时直接调 `System->AddEmitterHandle` 会崩，应改用 `AddEmitterToSystem`。STALKERonUE（5.3）、ChiR24/Unreal_mcp（5.0–5.8）、Claireon（5.5+）都用的是 `InitializeSystem` / `InitializeEmitter` / `AddEmitterToSystem` 这一组。

### 1.2 代码【运行已验证】

```cpp
// Build.cs (editor module):
// PrivateDependencyModuleNames: "UnrealEd","AssetTools","AssetRegistry","Niagara","NiagaraCore","NiagaraEditor",
//   "NiagaraShader","GraphEditor","Slate","SlateCore","RenderCore","RHI","ContentBrowser","ToolMenus","EditorFramework"
// ("ContentBrowser" is needed because NiagaraSystemFactoryNew.h includes FrontendFilterBase.h)

#include "AssetToolsModule.h"
#include "NiagaraSystem.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraSystemFactoryNew.h"
#include "NiagaraEmitterFactoryNew.h"
#include "NiagaraEditorUtilities.h"

UNiagaraSystem* CreateSystemAsset(const FString& PackagePath /*"/Game/FX/Rain"*/, const FString& Name /*"NS_Rain"*/)
{
	IAssetTools& AssetTools = FAssetToolsModule::GetModule().Get();
	// Non-MinimalAPI class, but StaticClass() links in 5.7. CreateAsset() does not call ConfigureProperties() (no modal UI).
	UNiagaraSystemFactoryNew* Factory = NewObject<UNiagaraSystemFactoryNew>();
	return Cast<UNiagaraSystem>(AssetTools.CreateAsset(Name, PackagePath, UNiagaraSystem::StaticClass(), Factory));
	// Alternative without the factory: CreatePackage + NewObject<UNiagaraSystem>(Pkg, *Name, RF_Public|RF_Standalone|RF_Transactional)
	// + UNiagaraSystemFactoryNew::InitializeSystem(Sys, true) + FAssetRegistryModule::AssetCreated(Sys).
	// (The factory also tries the project's default EffectType; on this project it ended up None.)
}

// Returns the *instance* that lives inside the system (the system duplicates the source emitter).
FNiagaraEmitterHandle* AddStandaloneEmitter(UNiagaraSystem& System, const TCHAR* EmitterName)
{
	UNiagaraEmitter* Src = NewObject<UNiagaraEmitter>(GetTransientPackage(), EmitterName, RF_Transactional);
	UNiagaraEmitterFactoryNew::InitializeEmitter(Src, /*bAddDefaultModulesAndRenderers*/ false);
	Src->SetUniqueEmitterName(EmitterName);   // RI parameter names are built from this name -> set it BEFORE adding
	Src->bIsInheritable = false;              // instance gets no parent -> no dependency on the source
	const FGuid Id = FNiagaraEditorUtilities::AddEmitterToSystem(System, *Src, Src->GetExposedVersion().VersionGuid);
	for (FNiagaraEmitterHandle& H : System.GetEmitterHandles())
	{
		if (H.GetId() == Id) { return &H; }
	}
	return nullptr;
}
```

**从模板 Emitter 起步**【仅读源码】：`/Niagara/DefaultAssets/Templates/Emitters/*` 下的模板可以直接作为 `AddEmitterToSystem` 的源。

- 如果模板 `bIsInheritable==true`，实例会保留指向 `/Niagara/...` 模板的 parent，加载时还会做继承合并。
- 想得到干净的独立副本，先 `DuplicateObject<UNiagaraEmitter>(Template, GetTransientPackage(), Name)`，把副本的 `bIsInheritable` 设为 `false`，再 `AddEmitterToSystem`。这与工厂在"不继承"分支的做法一致：`StaticDuplicateObject` + `DisableVersioning`（`NiagaraEmitterFactoryNew.cpp:178-183`）。

可用模板（本机 Content 实际存在）：

- `/Niagara/DefaultAssets/Templates/Emitters/`
  - Minimal（也是 `DefaultEmptyEmitter`）、Fountain、BlowingParticles、ConfettiBurst、DirectionalBurst、DynamicBeam
  - HangingParticulates、LocationBasedRibbon、OmnidirectionalBurst、RecycleParticlesInView、SimpleSpriteBurst
  - SingleLoopingParticle、StaticBeam、UpwardMeshBurst
- `/Niagara/DefaultAssets/Templates/Systems/`
  - AttributeReaderTrails、DirectionalBurst、DirectionalBurstLightweight、FountainLightweight、MinimalLightweight、RadialBurst、SimpleExplosion
- 对象路径格式为 `/Niagara/DefaultAssets/Templates/Emitters/Fountain.Fountain`。

### 1.3 版本化 Emitter 数据（5.x 起）

- `FVersionedNiagaraEmitter VE = Handle->GetInstance();`，`FVersionedNiagaraEmitterData* D = VE.GetEmitterData();`（`NiagaraTypes.h:1792`，已导出）。
- `UNiagaraEmitter` 上的 `SimTarget_DEPRECATED` 等字段已废弃，一律改 `D->...`。
- 5.7 中 `FNiagaraVariableAttributeBinding::SetValue` 改为接收 `FVersionedNiagaraEmitterBase`（`NiagaraCommon.h:1316`），用 `VE.ToBase()` 转换。旧的 `FVersionedNiagaraEmitter` 重载已标记 `UE_DEPRECATED(5.7)`。

---

## 2. 模块栈：模块、输入、User 参数、Emitter 属性、渲染器

### 2.1 找到各阶段的输出节点并添加模块【运行已验证】

```cpp
#include "NiagaraScriptSource.h"
#include "NiagaraGraph.h"
#include "NiagaraNodeOutput.h"
#include "NiagaraNodeFunctionCall.h"
#include "ViewModels/Stack/NiagaraStackGraphUtilities.h"

UNiagaraNodeOutput* GetOutputNode(UNiagaraScript* Script)   // exported; = Source->NodeGraph->FindEquivalentOutputNode(Usage, UsageId)
{
	return Script ? FNiagaraEditorUtilities::GetScriptOutputNode(*Script) : nullptr;
}

UNiagaraNodeFunctionCall* AddModule(UNiagaraNodeOutput* Out, UNiagaraScript* Module, int32 Index = INDEX_NONE)
{
	// Only the UNiagaraScript* overload is exported (StackGraphUtilities.h:288); FAssetData / Args overloads are not.
	return (Module && Out) ? FNiagaraStackGraphUtilities::AddScriptModuleToStack(Module, *Out, Index) : nullptr;
}
UNiagaraNodeFunctionCall* AddModule(UNiagaraNodeOutput* Out, const TCHAR* ObjectPath, int32 Index = INDEX_NONE)
{
	return AddModule(Out, LoadObject<UNiagaraScript>(nullptr, ObjectPath), Index);
}

// usage:
FVersionedNiagaraEmitterData* D = Handle->GetEmitterData();
UNiagaraNodeOutput* EmitterUpdateOut  = GetOutputNode(D->EmitterUpdateScriptProps.Script);
UNiagaraNodeOutput* ParticleSpawnOut  = GetOutputNode(D->SpawnScriptProps.Script);   // usage is ParticleSpawnScriptInterpolated; "equivalent" lookup handles it
UNiagaraNodeOutput* ParticleUpdateOut = GetOutputNode(D->UpdateScriptProps.Script);
AddModule(EmitterUpdateOut, TEXT("/Niagara/Modules/Emitter/EmitterState.EmitterState"));
UNiagaraNodeFunctionCall* SpawnRate = AddModule(EmitterUpdateOut, TEXT("/Niagara/Modules/Emitter/SpawnRate.SpawnRate"));
UNiagaraNodeFunctionCall* Gravity   = AddModule(ParticleUpdateOut, TEXT("/Niagara/Modules/Update/Forces/GravityForce.GravityForce"));
```

- 模块节点名就是函数名（例如 `GravityForce`）。同一模块加两次会自动变成 `GravityForce001`，这个名字会出现在 RI 参数名里。
- **未导出**（官方 UI 里有，但项目模块里不能直接调）：
  - `GetOrderedModuleNodes`、`RemoveModuleFromStack`、`GetModuleIsEnabled`、`ResetGraphForOutput`、`RelayoutGraph`、`CreateRapidIterationParameter`、`SetCustomExpressionForFunctionInput`
  - 需要排序或删除时，沿 ParameterMap 引脚链自己遍历，或者走 2.6 节的 ViewModel（模块项有导出的 `Delete()`）。
  - `SetModuleIsEnabled` 已导出。

### 2.2 设置字面量输入：手写 Rapid Iteration（RI）参数【运行已验证】

UI 设置 float/vector 这类"可 RI 类型"时，并不改图，而是把值写进所有受影响脚本的 `RapidIterationParameters`（`NiagaraStackFunctionInput.cpp:2178-2235`）。

- 参数名的格式是 `Constants.<Emitter唯一名>.<模块函数名>.<输入名>`。实测得到的例子：`Constants.RainDrops.GravityForce.Gravity`、`Constants.RainDrops.NS_Probe_FallSpeed.FallSpeed`。
- **"受影响脚本"** 的范围要照抄 `FindAffectedScripts`（`NiagaraStackGraphUtilities.cpp:3417`）：
  - System spawn/update 脚本，加上 `EmitterData->GetScripts(false)` 返回的所有脚本；
  - 再筛出 `ContainsUsage(模块所在 usage)` 为真的那些。
  - 这样会自动带上 **GPU compute 脚本**，以及"插值 spawn 脚本也包含 update"的情况（`NiagaraScript.cpp:1544-1577`）。
- 引擎工厂代码 `NiagaraEmitterFactoryNew.cpp:149-161` 只写了一个 CPU 脚本。那是因为它默认 CPUSim，**对 GPU Emitter 照抄会漏写 GPU 脚本**。

```cpp
#include "ViewModels/Stack/NiagaraParameterHandle.h"

void GetAffectedScripts(UNiagaraSystem& System, const FVersionedNiagaraEmitter& VE, ENiagaraScriptUsage Usage, TArray<UNiagaraScript*>& Out)
{
	TArray<UNiagaraScript*> Candidates = { System.GetSystemSpawnScript(), System.GetSystemUpdateScript() };
	if (FVersionedNiagaraEmitterData* D = VE.GetEmitterData())
	{
		TArray<UNiagaraScript*> EmitterScripts; D->GetScripts(EmitterScripts, false); Candidates.Append(EmitterScripts);
	}
	for (UNiagaraScript* S : Candidates) { if (S && S->ContainsUsage(Usage)) { Out.AddUnique(S); } }
}

template<typename T> // T must be float-based (float, FVector3f, FVector2f, FVector4f, FLinearColor, int32 ...), never double
void SetRapidIterationInput(UNiagaraSystem& System, const FVersionedNiagaraEmitter& VE, UNiagaraNodeFunctionCall& Module,
                            FName InputName, const FNiagaraTypeDefinition& Type, const T& Value)
{
	const ENiagaraScriptUsage Usage = FNiagaraStackGraphUtilities::GetOutputNodeUsage(Module);          // exported
	const FNiagaraParameterHandle Aliased = FNiagaraParameterHandle::CreateAliasedModuleParameterHandle(  // exported
		FNiagaraParameterHandle::CreateModuleParameterHandle(InputName), &Module);                        // "SpawnRate.SpawnRate"
	const bool bSystemScript = Usage == ENiagaraScriptUsage::SystemSpawnScript || Usage == ENiagaraScriptUsage::SystemUpdateScript;
	const FString EmitterName = VE.Emitter ? VE.Emitter->GetUniqueEmitterName() : FString();
	// replacement for the non-exported FNiagaraStackGraphUtilities::CreateRapidIterationParameter (StackGraphUtilities.cpp:2792)
	FNiagaraVariable RIVar = FNiagaraUtilities::ConvertVariableToRapidIterationConstantName(              // NIAGARA_API, NiagaraCommon.h:1559
		FNiagaraVariable(Type, Aliased.GetParameterHandleString()), bSystemScript ? nullptr : *EmitterName, Usage);
	RIVar.SetValue(Value);
	TArray<UNiagaraScript*> Scripts; GetAffectedScripts(System, VE, Usage, Scripts);
	for (UNiagaraScript* S : Scripts)
	{
		S->Modify();
		S->RapidIterationParameters.SetParameterData(RIVar.GetData(), RIVar, /*bAdd*/ true);
	}
}

// SetRapidIterationInput(*System, VE, *Gravity, TEXT("Gravity"), FNiagaraTypeDefinition::GetVec3Def(), FVector3f(0, 0, -1200));
```

**编译后读回的实测值**【运行已验证】：`RequestCompile(true)` + `WaitForCompilationComplete` 之后，从各脚本的 `RapidIterationParameters` 按名字取值：

- 粒子级，写入 3 个脚本（插值 spawn、particle update、GPU compute）：
  - GPU 脚本：`...NS_Probe_FallSpeed.FallSpeed = 900`、`...NS_Probe_WizardPins.Damping = 0.25`、`...GravityForce.Gravity = (0,0,-1200)`
  - ViewModel 写入的值也读到了：`...Drag.Drag = 2.0`、`...BoxLocation.Box Size = (3000,3000,10)`
- Emitter 级，写入 2 个脚本（System update 和 Emitter update）：`Constants.RainDrops.SpawnRate.Spawn Probability = 0.5`，在 **System update 脚本**中读回。名字里**仍然带 Emitter 唯一名**，因为模块位于 Emitter 阶段，不属于 System 阶段。

注意事项：

- **输入名用模块里显示的名字**，带空格，例如 `Box Size`、`Velocity Speed Scale`、`Lifetime`。各模块的输入清单见 2.7 节的实测结果。
- **只有"可 RI 类型"能这样设**（`IsRapidIterationType`，`StackGraphUtilities.cpp:2783`）：bool、enum、ParameterMap、UObject/DI 都不行。这些类型的值存放在 override pin 的 `DefaultValue` 上，推荐走 2.6 节的 `SetLocalValue`，它会自动选择正确的存储方式（实测枚举 `Coordinate Space` 设置成功）。
- 某输入一旦被链接或改成动态输入，RI 值就失效（会被 override pin 覆盖）。手写 RI 路线不会帮你清理残留的 RI 值，ViewModel 路线会。两条路线不要混用于同一个输入。

### 2.3 User 参数（供 Blueprint / Sequencer 驱动）【运行已验证】

```cpp
template<typename T>
FNiagaraVariable AddUserParameter(UNiagaraSystem& System, const FNiagaraTypeDefinition& Type, const TCHAR* Name /*"User.RainRate"*/, const T& Default)
{
	FNiagaraVariable Var(Type, FName(Name));
	Var.SetValue(Default);
	// FNiagaraUserRedirectionParameterStore::AddParameter is a NON-exported override; call through the exported base-class
	// reference so the call is a virtual dispatch (no link dependency).
	FNiagaraParameterStore& Store = System.GetExposedParameters();
	Store.AddParameter(Var, true, true);
	Store.SetParameterData(Var.GetData(), Var, false);
	FNiagaraEditorUtilities::UserParameters::GetScriptVariableForUserParameter(Var, System); // exported; creates editor metadata (UNiagaraScriptVariable)
	return Var;
}
```

运行时设置方式：

- Blueprint：`NiagaraComponent->SetVariableFloat(FName("RainRate"), v)`。名字可以不带 `User.`，User 参数存储会自动补前缀（`NiagaraUserRedirectionParameterStore.cpp:32`）。
- Sequencer：在 Niagara 组件轨道里加参数轨道，直接 key 这些 User 参数。

### 2.4 把模块输入链接到 User 参数【运行已验证】

```cpp
#include "NiagaraScriptVariable.h"

bool LinkInputToParameter(UNiagaraNodeFunctionCall& Module, FName InputName, const FNiagaraTypeDefinition& Type, const FNiagaraVariableBase& Param)
{
	const FNiagaraParameterHandle Aliased = FNiagaraParameterHandle::CreateAliasedModuleParameterHandle(
		FNiagaraParameterHandle::CreateModuleParameterHandle(InputName), &Module);
	FGuid VarId; // lets the override pin bind to the module's script variable (rename-safe)
	if (UNiagaraGraph* Called = Module.GetCalledGraph())
		if (UNiagaraScriptVariable* SV = Called->GetScriptVariable(FName(*(TEXT("Module.") + InputName.ToString()))))
			VarId = SV->Metadata.GetVariableGuid();
	UEdGraphPin& OverridePin = FNiagaraStackGraphUtilities::GetOrCreateStackFunctionInputOverridePin(Module, Aliased, Type, VarId, FGuid()); // exported, 5-arg form
	if (OverridePin.LinkedTo.Num() != 0) { return false; } // already linked / dynamic
	TSet<FNiagaraVariableBase> Known; Known.Add(Param);
	FNiagaraStackGraphUtilities::SetLinkedParameterValueForFunctionInput(OverridePin, Param, Known); // exported
	return true;
}
// LinkInputToParameter(*SpawnRate, TEXT("SpawnRate"), FNiagaraTypeDefinition::GetFloatDef(), UserRainRate);
```

实测编译产物（System update VM HLSL）中有 `Context.Map.SpawnRate.SpawnRate = Context.Map.User.RainRate;`，说明链接生效。

其他已导出的同类函数：

- `SetDataInterfaceValueForFunctionInput`：给 DI 类型的输入建 DI 对象，例如曲线、静态网格。
- `SetObjectAssetValueForFunctionInput`
- `SetDynamicInputForFunctionInput`：挂一个 Dynamic Input 脚本。

### 2.5 Emitter 属性与渲染器【运行已验证】

```cpp
const FVersionedNiagaraEmitter VE = Handle->GetInstance();
FVersionedNiagaraEmitterData* D = VE.GetEmitterData();
D->SimTarget = ENiagaraSimTarget::GPUComputeSim;                         // must be after InitializeEmitter (it forces CPUSim)
D->CalculateBoundsMode = ENiagaraEmitterCalculateBoundMode::Fixed;       // GPU emitters need Fixed (or Programmable) bounds
D->FixedBounds = FBox(FVector(-2000, -2000, -100), FVector(2000, 2000, 3000));
D->bLocalSpace = false;
D->GraphSource->MarkNotSynchronized(TEXT("sim target / bounds changed")); // what PostEditChangeVersionedProperty would do (NiagaraEmitter.cpp)
// System-level fixed bounds (optional; NiagaraSystem.h:806-808 / :776, both public):
// System->bFixedBounds = true; System->SetFixedBounds(FBox(...));

// Sprite renderer, velocity-aligned rain streaks
UNiagaraSpriteRendererProperties* Sprite = NewObject<UNiagaraSpriteRendererProperties>(VE.Emitter, NAME_None, RF_Transactional);
Sprite->Alignment  = ENiagaraSpriteAlignment::VelocityAligned;
Sprite->FacingMode = ENiagaraSpriteFacingMode::FaceCameraPosition;
Sprite->Material   = LoadObject<UMaterialInterface>(nullptr, TEXT("/Niagara/DefaultAssets/DefaultSpriteMaterial.DefaultSpriteMaterial"));
// optional custom attribute binding: Sprite->SpriteSizeBinding.SetValue(TEXT("Particles.RainSize"), VE.ToBase(), ENiagaraRendererSourceDataMode::Particles);
VE.Emitter->AddRenderer(Sprite, VE.Version);                               // NIAGARA_API

// Mesh renderer, frozen droplets
UNiagaraMeshRendererProperties* MeshR = NewObject<UNiagaraMeshRendererProperties>(VE.Emitter, NAME_None, RF_Transactional);
if (MeshR->Meshes.Num() == 0) { MeshR->Meshes.AddDefaulted(); }
MeshR->Meshes[0].Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
MeshR->OnMeshChanged();                                                    // NIAGARA_API
VE.Emitter->AddRenderer(MeshR, VE.Version);
```

- 修改既有渲染器时，用 `D->GetRenderers()` 遍历。
- 渲染器的可见性、排序等字段都是 public UPROPERTY，直接赋值即可（`NiagaraSpriteRendererProperties.h:158-351`）。
- `SubImageSize` 用于 SubUV 雨滴图集。

### 2.6 "像 UI 一样"设置输入：无头 View Model【运行已验证】

这是 Epic 自己在 commandlet 和合并管线里用的方式，参见：

- `NiagaraSystemAuditCommandlet.cpp:40-50`
- `NiagaraScriptMergeManager.cpp:1436-1470`

5.7 中 `FNiagaraSystemViewModel`、`UNiagaraStackViewModel`、`UNiagaraStackEntry`、`UNiagaraStackModuleItem`、`UNiagaraStackFunctionInput` 的相关方法都已 `NIAGARAEDITOR_API`。

```cpp
#include "ViewModels/NiagaraSystemViewModel.h"
#include "ViewModels/NiagaraEmitterHandleViewModel.h"
#include "ViewModels/Stack/NiagaraStackViewModel.h"
#include "ViewModels/Stack/NiagaraStackModuleItem.h"
#include "ViewModels/Stack/NiagaraStackFunctionInput.h"

TSharedRef<FNiagaraSystemViewModel> SVM = MakeShared<FNiagaraSystemViewModel>();
FNiagaraSystemViewModelOptions Opt;
Opt.bCanModifyEmittersFromTimeline = false; Opt.bCanSimulate = false; Opt.bCanAutoCompile = false;
Opt.bIsForDataProcessingOnly = true; Opt.MessageLogGuid = System->GetAssetGuid();
SVM->Initialize(*System, Opt);

UNiagaraStackFunctionInput* FindStackInput(FNiagaraSystemViewModel& SVM, int32 EmitterIdx, UNiagaraNodeFunctionCall* ModuleNode, const FString& InputName)
{
	UNiagaraStackEntry* Root = SVM.GetEmitterHandleViewModels()[EmitterIdx]->GetEmitterStackViewModel()->GetRootEntry();
	Root->RefreshChildren();                         // headless: refresh explicitly after graph edits
	TArray<UNiagaraStackEntry*> ToCheck; Root->GetUnfilteredChildren(ToCheck);
	while (ToCheck.Num() > 0)
	{
		UNiagaraStackEntry* E = ToCheck.Pop();
		if (UNiagaraStackModuleItem* M = Cast<UNiagaraStackModuleItem>(E); M && &M->GetModuleNode() == ModuleNode)
		{
			M->RefreshChildren();
			TArray<UNiagaraStackFunctionInput*> Inputs; M->GetParameterInputs(Inputs);
			for (UNiagaraStackFunctionInput* In : Inputs)
				if (In->GetInputParameterHandle().GetName().ToString() == InputName) { return In; }
			return nullptr;
		}
		E->GetUnfilteredChildren(ToCheck);
	}
	return nullptr;
}

// float / vector / enum all go through SetLocalValue (enum: write the int32; non-RI types land on the override pin)
if (UNiagaraStackFunctionInput* In = FindStackInput(*SVM, 0, Drag, TEXT("Drag")))
{
	TSharedRef<FStructOnScope> V = MakeShared<FStructOnScope>(In->GetInputType().GetStruct());
	*reinterpret_cast<float*>(V->GetStructMemory()) = 2.0f;
	In->SetLocalValue(V);                            // also: SetLinkedParameterValue / SetCustomExpression / SetDynamicInput / SetDataInterfaceValue
}
```

- 实测结果：`Drag.Drag` 设为 2.0、`BoxLocation.Box Size` 设为 (3000,3000,10)、`Gravity.Coordinate Space`（枚举）都成功，`GetValueMode()==Local(0)`。`SetCustomExpression` 之后 `ValueMode==Expression(5)`。
- 只有当前编辑条件下**可见**的输入才会出现在 `GetParameterInputs` 里。例如 InitializeParticle V2 默认只列出各种 `* Mode` 枚举，看不到 `Lifetime`；要先把 `Lifetime Mode` 设好，`RefreshChildren()` 之后才会出现。不过 RI 存储里已经有 `Constants.RainDrops.InitializeParticle.Lifetime`，因此用 2.2 节的手写 RI 写 `Lifetime` 也可以。
- `FNiagaraSystemViewModel::Cleanup()` 未导出，让 `TSharedRef` 自然析构即可（析构函数已导出）。审计 commandlet 也是这么做的。

### 2.7 实测模块输入名（UE 5.7，节选）

| 模块 | 输入（显示名 / 类型） |
|---|---|
| SpawnRate | `SpawnRate` float, `Spawn Probability` float, `SpawnGroup` int |
| InitializeParticle (V2) | `Lifetime Mode`, `Color Mode`, `Position Mode`, `Mass Mode`, `Sprite Size Mode`, `Sprite Rotation Mode`, `Sprite UV Mode`, `Mesh Scale Mode`… 等枚举，以及 `Material Random`；数值输入如 `Lifetime`、`Color` 取决于所选 Mode |
| BoxLocation | `Box Size` vec3, `Box Offset` vec3, `Box Origin` position, `Coordinate Space`, `Randomness Mode`, `Random Seed`, `Fixed Random Seed`, `Rotate Box On Axis`, `Normalized Box Rotation Angle`, `Surface Only Band Thickness`, `Surface Expansion Mode`, `Spawn Group Mask` |
| AddVelocity | `Velocity Mode`, `Velocity` vec3, `Velocity Speed`, `Velocity Speed Scale`, `Cone Axis`, `Cone Angle`, `Velocity Origin`… |
| GravityForce | `Gravity` vec3, `Coordinate Space` |
| Drag | `Drag` float, `Rotational Drag` float, `Ignore Mass` bool |

### 2.8 "Set Parameters"模块（声明自定义属性）【运行已验证】

```cpp
TArray<FNiagaraVariable> Vars = { FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Particles.RainPhase")) };
TArray<FString> Defaults = { TEXT("0.0") };
UNiagaraNodeAssignment* Assign = FNiagaraStackGraphUtilities::AddParameterModuleToStack(Vars, *ParticleSpawnOut, INDEX_NONE, Defaults); // exported
// stack input name is the full variable name: "Particles.RainPhase"; node function name like "SetVariables_<GUID>"
```

这是在图里引入新粒子属性的最稳妥方式。后面 Custom HLSL 要读这个属性时，它必须已经"出现过"，见第 3 节的坑。

---

## 3. 自定义逻辑（重点）

### 3.1 关键源码事实

- `UNiagaraNodeCustomHlsl` 是 `UCLASS(MinimalAPI)`（`NiagaraNodeCustomHlsl.h:13`）。
  - `SetCustomHlsl`（`:20`）、`RebuildSignatureFromPins`、`InitAsCustomHlslDynamicInput` **未导出**。
  - HLSL 文本存放在 `private: UPROPERTY() FString CustomHlsl;`（`:89`）。
  - include 路径存放在 `VirtualIncludeFilePaths` / `AbsoluteIncludeFilePaths`（`:93-98`，private）。
- 引脚**完全由 `Signature` 生成**：`UNiagaraNodeFunctionCall::Signature` 是 public UPROPERTY（`NiagaraNodeFunctionCall.h:77`）。没有 FunctionScript 时，`AllocateDefaultPins` 按 `Signature.Inputs/Outputs` 建引脚，再补两个 Add 引脚（`NiagaraNodeFunctionCall.cpp:365-517`）。
- `BuildParameterMapHistory` 要求"引脚数 == Signature 数 + 1（Add 引脚）"，而且顺序一致（`NiagaraNodeCustomHlsl.cpp:452-560`）。所以**先写 Signature，再 `FGraphNodeCreator::Finalize()`**，不要事后手动 CreatePin。
- ParameterMap 引脚的语义（`NiagaraHlslTranslator.cpp:8596` 起的 `ProcessCustomHlsl`，以及 `:8379` 的 `FinalResolveNamespacedTokens`）：
  - 名为 `MapIn` / `MapOut` 的 ParameterMap 引脚 token 会被替换成 `Context.MapXxx`。
  - 以合法命名空间开头的 token（`Particles.`、`User.`、`Engine.`、`Emitter.`、`System.`…）会被解析成 `Context.MapXxx.<变量>`。
  - 节点要求上下文，并以 `inout FSimulationContext Context` 形式调用。
  - 普通引脚改名为 `In_X` / `Out_X`。
  - 在 Emitter 上下文中，读取一个此前没在参数图历史里出现过的变量，会报 `Cannot use variable in custom expression, it hasn't been encountered yet`（`:8431`）。
- `UNiagaraNodeWithDynamicPins` **整个类没有 API 宏**，`RequestNewTypedPin` / `AddParameterPin` 都不可调。

### 3.2 生成模块脚本资产（所有路线共用）【运行已验证】

```cpp
#include "NiagaraNodeInput.h"
#include "NiagaraNodeCustomHlsl.h"
#include "EdGraphSchema_Niagara.h"

UNiagaraScript* CreateModuleScriptAsset(const FString& PackagePath, const FString& Name,
	TFunctionRef<void(UNiagaraGraph&, UNiagaraNodeInput& /*MapIn*/, UNiagaraNodeOutput& /*Output*/)> Build)
{
	UPackage* Pkg = CreatePackage(*(PackagePath / Name)); Pkg->FullyLoad();
	UNiagaraScript* Script = NewObject<UNiagaraScript>(Pkg, *Name, RF_Public | RF_Standalone | RF_Transactional);
	Script->SetUsage(ENiagaraScriptUsage::Module);                    // default ModuleUsageBitmask = particle spawn/update/event/simstage
	UNiagaraScriptSource* Source = NewObject<UNiagaraScriptSource>(Script, NAME_None, RF_Transactional);
	UNiagaraGraph* Graph = NewObject<UNiagaraGraph>(Source, NAME_None, RF_Transactional);
	Source->NodeGraph = Graph;

	FGraphNodeCreator<UNiagaraNodeOutput> OutC(*Graph);
	UNiagaraNodeOutput* Out = OutC.CreateNode(false);
	Out->SetUsage(ENiagaraScriptUsage::Module);
	Out->Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetParameterMapDef(), TEXT("Output")));
	OutC.Finalize();

	FGraphNodeCreator<UNiagaraNodeInput> InC(*Graph);
	UNiagaraNodeInput* In = InC.CreateNode(false);
	In->Input = FNiagaraVariable(FNiagaraTypeDefinition::GetParameterMapDef(), TEXT("MapIn"));
	In->Usage = ENiagaraInputNodeUsage::Parameter;
	InC.Finalize();

	Build(*Graph, *In, *Out);

	Script->SetLatestSource(Source);                                     // NIAGARA_API
	if (FVersionedNiagaraScriptData* Data = Script->GetLatestScriptData())
		Data->LibraryVisibility = ENiagaraScriptLibraryVisibility::Library;
	Script->RequestCompile(FGuid());                                     // standalone module compile (only affects the module asset's own status)
	FAssetRegistryModule::AssetCreated(Script);
	Pkg->MarkPackageDirty();
	return Script;
}

void SetCustomHlslText(UNiagaraNodeCustomHlsl* Node, const FString& Hlsl)
{
	FStrProperty* Prop = FindFProperty<FStrProperty>(UNiagaraNodeCustomHlsl::StaticClass(), TEXT("CustomHlsl")); // private UPROPERTY
	Node->Modify();
	Prop->SetPropertyValue_InContainer(Node, Hlsl);
	FPropertyChangedEvent Evt(Prop, EPropertyChangeType::ValueSet);
	static_cast<UObject*>(Node)->PostEditChangeProperty(Evt);         // virtual -> RefreshFromExternalChanges + NotifyGraphNeedsRecompile
}

UNiagaraNodeCustomHlsl* CreateCustomHlslNode(UNiagaraGraph& Graph, FName FunctionName,
	const TArray<FNiagaraVariable>& Inputs, const TArray<FNiagaraVariable>& Outputs, const FString& Hlsl)
{
	FGraphNodeCreator<UNiagaraNodeCustomHlsl> C(Graph);
	UNiagaraNodeCustomHlsl* Node = C.CreateNode(false);
	Node->Signature.Name = FunctionName;
	Node->Signature.Inputs = Inputs;
	Node->Signature.Outputs.Reset();
	for (const FNiagaraVariable& O : Outputs) { Node->Signature.Outputs.Add(O); }
	C.Finalize();                       // pins from Signature (+ "Add" pins)
	SetCustomHlslText(Node, Hlsl);
	return Node;
}
```

### 3.3 路线 A：只用 ParameterMap 的 Custom HLSL 模块（推荐，最省事）【运行已验证】

图结构：`Input(MapIn) → CustomHlsl(MapIn → MapOut) → Output`，不需要 Map Get / Map Set 节点。

```cpp
UNiagaraScript* Wind = CreateModuleScriptAsset(TEXT("/Game/FX/Rain/Modules"), TEXT("NM_RainWind"),
	[&](UNiagaraGraph& G, UNiagaraNodeInput& In, UNiagaraNodeOutput& Out)
{
	const UEdGraphSchema_Niagara* Schema = GetDefault<UEdGraphSchema_Niagara>();
	UNiagaraNodeCustomHlsl* N = CreateCustomHlslNode(G, TEXT("RainWind"),
		{ FNiagaraVariable(FNiagaraTypeDefinition::GetParameterMapDef(), TEXT("MapIn")) },
		{ FNiagaraVariable(FNiagaraTypeDefinition::GetParameterMapDef(), TEXT("MapOut")) },
		TEXT("float3 V = Particles.Velocity;\n")                       // bare namespaced tokens; no swizzle on them
		TEXT("V.x = V.x + User.RainWind * Engine.DeltaTime;\n")
		TEXT("Particles.Velocity = V;\n")
		TEXT("Particles.RainPhase = frac(Particles.RainPhase + Engine.DeltaTime);\n"));
	Schema->TryCreateConnection(In.GetOutputPin(0), N->GetInputPin(0));   // UNiagaraNode::GetInput/OutputPin are exported
	Schema->TryCreateConnection(N->GetOutputPin(0), Out.GetInputPin(0));
});
AddModule(ParticleUpdateOut, Wind);  // overload taking UNiagaraScript*
```

实测生成的 GPU HLSL（节选）：

```hlsl
void RainWindParamMap..._Func_(inout FSimulationContext Context)
  V.x = V.x + Context.MapUpdate.User.RainWind * Context.MapUpdate.Engine.DeltaTime;
  Context.MapUpdate.Particles.Velocity = V;
  Context.MapUpdate.Particles.RainPhase = frac(Context.MapUpdate.Particles.RainPhase + Context.MapUpdate.Engine.DeltaTime);
```

注意事项：

- **用裸名**（`Particles.Velocity`）。BuildParameterMapHistory 只对"以命名空间开头"的 token 登记外部读取，`MapIn.User.X` 这种写法不会登记，因此在 Emitter 上下文里可能报 "hasn't been encountered"。
- 被读取的 `User.*` 必须已经在 System 的 Exposed Parameters 里。自定义粒子属性（例如 `Particles.RainPhase`）要先在 Spawn 阶段用 2.8 节的 Set Parameters 声明。
- **这类模块资产的"独立编译"必然报错**（`Cannot access field 'Particles' of structure`），模块资产自身状态为 `NCS_Error`。原因是独立编译时没有粒子上下文，这只是外观问题。放进 Emitter 之后的系统编译是 `NCS_UpToDate`，而且 HLSL 正确【运行已验证】。网络上 DreamFX 说"Particles 命名空间不能用"，指的就是这种独立编译的情况。
- 不要对带命名空间的 token 直接做 swizzle，例如 `Particles.Velocity.z`。先拷到局部变量，这样最稳。

### 3.4 路线 B：经典 MapGet → CustomHlsl → MapSet（会暴露 `Module.*` 输入到栈 UI）【运行已验证】

`NiagaraEditor/Public/Widgets/Wizard/SNiagaraModuleWizard.h:123-128` 在 5.5+ 导出了一组工具函数，**5.7 已核对导出表**：

- `AddReadParameterPin`
- `AddWriteParameterPin`
- `CreateFunctionCallNode`
- `CreateDataInterfaceFunctionNode`
- `CreateOpNode`
- `SetDefaultBinding` / `SetTooltip`

**不要 include 这个头文件。** 其中的 `SetDefaultValue` 是 inline 的 `NIAGARAEDITOR_API` 模板，MSVC 会报 C2491（PinWright 的经验【网络】），而且模板里还调用了未导出的 `ScriptVariableChanged`。只在本地重新声明需要的两个函数即可：

```cpp
class UNiagaraNodeParameterMapGet;   // classes live in NiagaraEditor/Private; StaticClass() still links in 5.7
class UNiagaraNodeParameterMapSet;
namespace UE::Niagara::Wizard::Utilities
{
	NIAGARAEDITOR_API UEdGraphPin* AddReadParameterPin(const FNiagaraTypeDefinition& Type, const FName& Name, UNiagaraNodeParameterMapGet* MapGetNode);
	NIAGARAEDITOR_API UEdGraphPin* AddWriteParameterPin(const FNiagaraTypeDefinition& Type, const FName& Name, UNiagaraNodeParameterMapSet* MapSetNode);
}

UEdGraphPin* FindPin(UEdGraphNode* N, EEdGraphPinDirection Dir, FName Name)
{ for (UEdGraphPin* P : N->Pins) if (P->Direction == Dir && P->PinName == Name) return P; return nullptr; }
UEdGraphPin* FindAddPin(UEdGraphNode* N, EEdGraphPinDirection Dir)  // UNiagaraNodeWithDynamicPins::AddPinSubCategory == "DynamicAddPin" (not exported)
{ for (UEdGraphPin* P : N->Pins) if (P->Direction == Dir && P->PinType.PinSubCategory == FName(TEXT("DynamicAddPin"))) return P; return nullptr; }

UNiagaraScript* Damp = CreateModuleScriptAsset(TEXT("/Game/FX/Rain/Modules"), TEXT("NM_RainDamping"),
	[&](UNiagaraGraph& G, UNiagaraNodeInput& In, UNiagaraNodeOutput& Out)
{
	const UEdGraphSchema_Niagara* Schema = GetDefault<UEdGraphSchema_Niagara>();
	UClass* GetClass = FindObject<UClass>(nullptr, TEXT("/Script/NiagaraEditor.NiagaraNodeParameterMapGet"));
	UClass* SetClass = FindObject<UClass>(nullptr, TEXT("/Script/NiagaraEditor.NiagaraNodeParameterMapSet"));
	FGraphNodeCreator<UNiagaraNode> GC(G); UNiagaraNode* MapGet = GC.CreateNode(false, GetClass); GC.Finalize();
	FGraphNodeCreator<UNiagaraNode> SC(G); UNiagaraNode* MapSet = SC.CreateNode(false, SetClass); SC.Finalize();

	using namespace UE::Niagara::Wizard::Utilities;
	UEdGraphPin* RDamp = AddReadParameterPin(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Module.Damping"),   reinterpret_cast<UNiagaraNodeParameterMapGet*>(MapGet));
	UEdGraphPin* RVel  = AddReadParameterPin(FNiagaraTypeDefinition::GetVec3Def(),  TEXT("Particles.Velocity"), reinterpret_cast<UNiagaraNodeParameterMapGet*>(MapGet));
	UEdGraphPin* RDt   = AddReadParameterPin(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Engine.DeltaTime"),  reinterpret_cast<UNiagaraNodeParameterMapGet*>(MapGet));

	// !! AddRead/WriteParameterPin always create a NEW, uniquely named graph parameter (UNiagaraGraph::MakeUniqueParameterName,
	//    NiagaraNodeWithDynamicPins.cpp:405-426). Writing back to an attribute that is also read would silently become
	//    "Particles.Velocity001" (observed!). For the write side of an already-registered parameter, create the pin directly:
	FEdGraphPinType T = UEdGraphSchema_Niagara::TypeDefinitionToPinType(FNiagaraTypeDefinition::GetVec3Def());
	T.PinSubCategory = TEXT("ParameterPin");                                        // UNiagaraNodeParameterMapBase::ParameterPinSubCategory
	UEdGraphPin* WVel = MapSet->CreatePin(EGPD_Input, T, TEXT("Particles.Velocity"), MapSet->Pins.IndexOfByKey(FindAddPin(MapSet, EGPD_Input)));
	WVel->PinFriendlyName = FText::FromString(TEXT("Particles.Velocity"));
	WVel->PersistentGuid = FGuid::NewGuid();
	// (for a write-only attribute that is NOT read in this graph, AddWriteParameterPin is fine)

	UNiagaraNodeCustomHlsl* C = CreateCustomHlslNode(G, TEXT("RainDamping"),
		{ FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Damping")),
		  FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(),  TEXT("Velocity")),
		  FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("DeltaTime")) },
		{ FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(),  TEXT("OutVelocity")) },
		TEXT("OutVelocity = Velocity * exp(-Damping * DeltaTime);"));

	Schema->TryCreateConnection(In.GetOutputPin(0), MapGet->GetInputPin(0));
	Schema->TryCreateConnection(In.GetOutputPin(0), MapSet->GetInputPin(0));
	Schema->TryCreateConnection(RDamp, FindPin(C, EGPD_Input, TEXT("Damping")));
	Schema->TryCreateConnection(RVel,  FindPin(C, EGPD_Input, TEXT("Velocity")));
	Schema->TryCreateConnection(RDt,   FindPin(C, EGPD_Input, TEXT("DeltaTime")));
	Schema->TryCreateConnection(FindPin(C, EGPD_Output, TEXT("OutVelocity")), WVel);
	Schema->TryCreateConnection(MapSet->GetOutputPin(0), Out.GetInputPin(0));
	if (UEdGraphPin* Def = FindPin(MapGet, EGPD_Input, TEXT("Module.Damping")))   // MapGet keeps a paired default-value input pin
		Schema->TrySetDefaultValue(*Def, TEXT("0.5"));
});
// In the emitter, "Damping" then shows up as a normal stack input -> set via RI / SetLocalValue / link to User.*.
```

- 实测结果：这种模块**独立编译也成功**（`NCS_UpToDate`）。栈上会出现名为 `Damping` 的输入。
- GPU HLSL 中为 `RainDamping..._Func_(Constant132, Context.MapUpdate.Particles.Velocity, Context.MapUpdate.Engine.DeltaTime, ...)`，随后 `Context.MapUpdate.Particles.Velocity = ..._Output_OutVelocity;`。在 commandlet 编译中，RI 值被烘焙成了常量 `Constant132`。

### 3.5 路线 C："拖线到 + 号"技巧（通用，不依赖 wizard 头）【运行已验证】

- 动态引脚节点的 Add 引脚（PinCategory `Misc`、SubCategory `DynamicAddPin`）被连线后，节点会在**虚函数** `PinConnectionListChanged` 里把它改造成带类型的引脚，名字取自对端引脚，然后再补一个新的 Add 引脚（`NiagaraNodeWithDynamicPins.cpp:25-67`）。
- `UEdGraphSchema_Niagara::TryCreateConnection`（已导出，`EdGraphSchema_Niagara.cpp:1451-1570`）连线后会调用两端节点的 `PinConnectionListChanged`。
- 因此"代码连线到 + 号"等价于 UI 里拖线：

```cpp
// Custom HLSL with a typed input "FallSpeed" (from Signature) + ParameterMap pins; MapGet created via class lookup as above.
UEdGraphPin* GetAdd = FindAddPin(MapGet, EGPD_Output);
Schema->TryCreateConnection(GetAdd, FindPin(CustomNode, EGPD_Input, TEXT("FallSpeed")));
// -> MapGet grows an output pin "Module.FallSpeed" (non-namespaced names get "Module." prepended; NiagaraNodeParameterMapGet.cpp:211-266),
//    registers the graph parameter and creates the paired default pin.
```

- 实测得到 `Module.FallSpeed` 引脚，栈上出现 `FallSpeed` 输入；GPU HLSL 为 `RainFallSpeed..._Func_(float In_FallSpeed, inout FSimulationContext Context)`。
- 局限：新引脚的名字由对端决定。对端是 `Particles.X` 这类"有命名空间元数据"的名字时，会被截成 `X`，再加上 `Module.` 或 `Local.Module.` 前缀。所以这个技巧适合生成 `Module.*` 输入，读写粒子属性请用路线 A 或 B。

### 3.6 路线 D：栈上的自定义表达式（"New Expression"）【运行已验证】

`UNiagaraStackFunctionInput::SetCustomExpression`（`NiagaraStackFunctionInput.h:202`，已导出）可以给**任意模块输入**挂一个单表达式的 Custom HLSL 动态输入。和 2.8 节的 Set Parameters 配合，就是"每个属性一行 HLSL"：

```cpp
if (UNiagaraStackFunctionInput* Phase = FindStackInput(*SVM, 0, Assign, TEXT("Particles.RainPhase")))
	Phase->SetCustomExpression(TEXT("frac(Engine.Time * 0.37)"));
// generated: Out_CustomHLSLOutput = (float)(frac(Context.MapSpawn.Engine.Time * 0.37));
```

- 只能写一个表达式：翻译器会包成 `Out = (type)(expr);`（`NiagaraHlslTranslator.cpp:8904-8919`）。
- 底层函数 `FNiagaraStackGraphUtilities::SetCustomExpressionForFunctionInput` 未导出，必须走 ViewModel。

### 3.7 Scratch Pad【仅读源码 + 网络】

- `UNiagaraScratchPadViewModel::CreateNewScript(Usage, TargetUsage, OutputType)` 已导出（`NiagaraScratchPadViewModel.h:65`）。可以从 `SVM->GetScriptScratchPadViewModel()` 拿到 ScratchPad ViewModel，生成的脚本挂在 System 或 Emitter 内，不产生独立资产。
- 拿到 `GetOriginalScript()` 后，图的构造方式和 3.2–3.5 完全相同；改完调用 `ApplyChanges()`（已导出），再用 `AddScriptModuleToStack(Script, ...)` 放进栈。
- `UNiagaraStackFunctionInput::SetScratch()` 用于新建 Dynamic Input 类型的 scratch。
- 【网络】VibeUE 的做法是直接 `StaticDuplicateObject(DefaultModuleScript, EmitterData->ScratchPads, ...)` 并加入 `Container->Scripts`；aadeshrao123/Unreal-MCP 则加到 `System->ScratchPadScripts`。
- 对本项目的建议：**用独立模块资产（3.3/3.4）而不是 scratch pad**。理由有三：可以复用，能单独 diff，System 资产也更小。

### 3.8 自定义 C++ Data Interface（重计算 / 需要 CPU 数据）【仅读源码】

- 引擎自带模板插件 `Engine/Plugins/FX/ExampleCustomDataInterface`，参考 `NiagaraDataInterfaceMousePosition.h/.cpp` 和 `Shaders/Private/NiagaraDataInterfaceMousePosition.ush`。需要实现的部分：
  - `GetFunctionsInternal`：函数签名
  - `GetVMExternalFunction`：CPU VM 实现
  - `GetParameterDefinitionHLSL` + `AppendTemplateHLSL`：GPU HLSL 模板，在 .ush 里用 `{ParameterName}` 占位
  - `BuildShaderParameters` / `SetShaderParameters`：着色器参数
  - per-instance 数据和 RT 代理
  - `AppendCompileHash`：.ush 改动后触发重编
- 还要在模块启动时调用 `AddShaderSourceDirectoryMapping(TEXT("/Plugin/Xxx"), ShaderDir)`。**挂载 shader 目录的模块应在 `PostConfigInit` 阶段加载**，否则着色器编译可能早于映射。
- 生成 System 时：
  - 把 DI 设为模块输入，用 `SetDataInterfaceValueForFunctionInput`（已导出）；
  - 或在模块图里用 `CreateDataInterfaceFunctionNode(DIClass, FunctionName, Graph)`（wizard 工具，已导出）直接放一个 DI 函数节点；
  - Custom HLSL 也可以加一个 DI 类型的输入引脚，用 `MyDI.MyFunction(...)` 调用（翻译器在 `ProcessCustomHlsl` 的 DI 分支中处理）。
- 雨效里适合做成 DI 的部分：高度图或场景深度碰撞、雨滴与角色交互的力场数据（例如由 Blueprint 每帧写入的手部骨骼位置）。

### 3.9 Custom HLSL 的 include 文件【仅读源码】

- `VirtualIncludeFilePaths`（private `TArray<FString>`）可以用和 `CustomHlsl` 相同的反射方式写入，例如 `"/Project/Rain/RainFunctions.ush"`。这样节点体可以只写一行函数调用，长 HLSL 放进 .ush 版本管理。
- 引擎**不监视**这些文件，改 .ush 后需要强制重编，即 `System->RequestCompile(true)`。
- 也需要上一节说的 shader 目录映射。

### 3.10 ImportNodesFromText（兜底路线）【导出已验证，导入格式参考】

- `FEdGraphUtilities::ExportNodesToText` / `ImportNodesFromText` 是 `UNREALED_API`。
- 实测：把路线 C 生成的 MapGet + CustomHlsl 导出为 T3D（7716 字符），`CanImportNodesFromText` 返回 true。
- 格式为 `Begin Object Class=/Script/NiagaraEditor.NiagaraNodeParameterMapGet ... CustomProperties Pin (PinId=...,PinName="Module.FallSpeed",PinType.PinSubCategory="ParameterPin",PinType.PinSubCategoryObject="/Script/CoreUObject.ScriptStruct'/Script/Niagara.NiagaraFloat'",LinkedTo=(NiagaraNodeInput_0 <PinId>,),...)`。
- 能这样做，但不推荐手写这种文本：pin GUID 和图参数注册都容易出错。只建议用作"复制一个已有图片段"的手段。网络上也没有找到有人对 Niagara 这么做【网络】。

---

## 4. 编译、保存、无头运行

### 4.1 编译与检查结果【运行已验证】

```cpp
System->RequestCompile(/*bForce*/ true);                 // NIAGARA_API (NiagaraSystem.h:438)
System->WaitForCompilationComplete(/*bIncludingGPUShaders*/ true, /*bShowProgress*/ false); // :444

auto Report = [](UNiagaraScript* S, const TCHAR* Label)
{
	const FNiagaraVMExecutableData& Exe = S->GetVMExecutableData();
	UE_LOG(LogTemp, Display, TEXT("[%s] status=%d err=%s"), Label, (int32)S->GetLastCompileStatus(), *Exe.ErrorMsg); // 3 == NCS_UpToDate, 2 == NCS_Error
	for (const FNiagaraCompileEvent& Ev : Exe.LastCompileEvents) UE_LOG(LogTemp, Display, TEXT("  %s"), *Ev.Message);
	if (S->GetUsage() == ENiagaraScriptUsage::ParticleGPUComputeScript)
		if (const FNiagaraShaderScript* SS = S->GetRenderThreadScript())   // #include "NiagaraShared.h"
			for (const FString& E : SS->GetCompileErrors()) UE_LOG(LogTemp, Error, TEXT("  gpu: %s"), *E);
	// Exe.LastHlslTranslation / LastHlslTranslationGPU hold the generated HLSL (editor-only, filled on a real, non-DDC compile)
};
Report(System->GetSystemSpawnScript(), TEXT("SystemSpawn"));
Report(System->GetSystemUpdateScript(), TEXT("SystemUpdate"));
TArray<UNiagaraScript*> Scripts; Handle->GetEmitterData()->GetScripts(Scripts, false);
for (UNiagaraScript* S : Scripts) Report(S, TEXT("Emitter"));
```

- 在 commandlet 里，`System->IsReadyToRun()` **永远为 false**，因为 `IsReadyToRunInternal` 一开始就判断 `FApp::CanEverRender()`（`NiagaraSystem.cpp:1604`）。改看各脚本的 `GetLastCompileStatus()`，以及 `UNiagaraScript::IsReadyToRun(ENiagaraSimTarget::GPUComputeSim)`（实测为 1）。
- Emitter spawn/update 脚本（usage 9/10）的状态是 `NCS_Unknown`，这是正常的：它们被编译进 System 脚本，不单独编译。
- 无渲染的 commandlet 会完成**图到 HLSL 的翻译和 VM 编译**，但不会把 GPU HLSL 编译成 Metal 字节码。加 `-AllowCommandletRendering` 会尝试编译，而**本机因缺 Metal Toolchain 直接退出**。装好之后，打开编辑器或加该参数，即可完成真正的 GPU 着色器验证。

### 4.2 保存

```cpp
bool SavePackageOf(UObject* Asset)
{
	UPackage* Pkg = Asset->GetOutermost();
	const FString File = FPackageName::LongPackageNameToFilename(Pkg->GetName(), FPackageName::GetAssetPackageExtension());
	FSavePackageArgs Args; Args.TopLevelFlags = RF_Public | RF_Standalone; Args.SaveFlags = SAVE_NoError;
	return UPackage::SavePackage(Pkg, Asset, *File, Args);   // #include "UObject/SavePackage.h"
}
// or: UEditorAssetLibrary::SaveLoadedAsset(Asset) (EditorScriptingUtilities), unreal.EditorAssetLibrary.save_directory(...) from Python
```

- 先保存模块脚本资产，再保存 System。
- 实测：保存后在新进程里 `LoadObject` 回来，Emitter、渲染器、User 参数都在。再次 `RequestCompile(true)` 后，GPU 脚本状态仍是 `NCS_UpToDate`，HLSL 也一致。

### 4.3 无头运行方式（1、2a、2b 已在本机实测；2c 依赖 Metal Toolchain，未实测）

```bash
UE="/Users/Shared/Epic Games/UE_5.7"
PROJ="/Users/guocheng/ProjectRain/ProjectRain.uproject"

# 1) build the editor module (≈35 s first time, ≈11 s incremental here)
"$UE/Engine/Build/BatchFiles/Mac/Build.sh" ProjectRainEditor Mac Development -Project="$PROJ" -waitmutex

# 2a) custom commandlet (class UBuildRainCommandlet -> -run=BuildRain). The module MUST load at "Default" phase.
"$UE/Engine/Binaries/Mac/UnrealEditor-Cmd" "$PROJ" -run=BuildRain -unattended -nopause -nosplash -stdout -FullStdOutLogOutput -NoSound

# 2b) Python via commandlet (UPythonScriptCommandlet, parses -Script= manually)
"$UE/Engine/Binaries/Mac/UnrealEditor-Cmd" "$PROJ" -run=pythonscript -script="/abs/path/build_rain.py" -unattended -nopause -nosplash -stdout

# 2c) full editor (needs Metal Toolchain; NOT run here): runs the script after startup, editor stays open
"$UE/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor" "$PROJ" -ExecutePythonScript="/abs/path/build_rain.py"
```

- `-ExecutePythonScript` 的解析在 `PythonScriptPlugin/.../EditorPythonExecuter.cpp:144`。
- commandlet 名就是类名去掉 `Commandlet` 后缀。
- 首次启动 commandlet 约 20–40 秒，Niagara 编译只需 1–2 秒。

Commandlet 骨架：

```cpp
UCLASS() class UBuildRainCommandlet : public UCommandlet
{ GENERATED_BODY() public: virtual int32 Main(const FString& Params) override; };

int32 UBuildRainCommandlet::Main(const FString& Params)
{
	FString Path = TEXT("/Game/FX/Rain"); FParse::Value(*Params, TEXT("Path="), Path);
	return URainNiagaraBuilder::BuildRainSystem(Path, TEXT("NS_Rain")) ? 0 : 1;
}
```

---

## 5. Python 暴露情况（5.7）【运行已验证】

- **没有** `unreal.NiagaraEditorLibrary` 或任何能编辑模块栈、Emitter Handle 的 API。名字含 Niagara 的类里只有：
  - `NiagaraBakerFunctionLibrary`、`NiagaraClipboardEditorScriptingUtilities`（只是剪贴板值的 getter/setter）
  - `NiagaraDataChannelLibrary`、`NiagaraFunctionLibrary`（运行时）、`NiagaraSimCacheFunctionLibrary` 等
  - `UpgradeNiagaraScriptResults` / `NiagaraPythonEmitter` 只在模块版本升级脚本里使用（`UpgradeNiagaraScriptResults.h`）。
- 能直接用的部分：

```python
at = unreal.AssetToolsHelpers.get_asset_tools()
ns = at.create_asset("NS_Empty", "/Game/FX", unreal.NiagaraSystem, unreal.NiagaraSystemFactoryNew())       # OK
ms = at.create_asset("NM_Mod",   "/Game/FX", unreal.NiagaraScript, unreal.NiagaraModuleScriptFactory())   # OK (copy of DefaultModule)
em = at.create_asset("NE_Emit",  "/Game/FX", unreal.NiagaraEmitter, unreal.NiagaraEmitterFactoryNew())    # OK; verified content: 1 sprite renderer + 8 modules
#   (EmitterState, SpawnRate, SystemLocation, AddVelocity, SetVariables, ParticleState, Color, SolveForcesAndVelocity)
ns.get_editor_property("fixed_bounds"); ns.get_editor_property("warmup_time"); ns.get_editor_property("effect_type")   # OK
ns.get_editor_property("emitter_handles")   # FAILS: not exposed (same for exposed_parameters, bFixedBounds)
```

- **推荐的桥接方式**：在项目编辑器模块里写 `UCLASS() class URainNiagaraBuilder : public UBlueprintFunctionLibrary`，放若干 `UFUNCTION(BlueprintCallable)` 静态函数。Python 侧调用 `unreal.RainNiagaraBuilder.build_rain_system("/Game/FX/Rain", "NS_Rain")`。实测探针库 `unreal.NiagaraProbeLibrary.build_probe_system(...)` 返回 True，资产全部生成。
- 【网络】UE 5.8 新增了 `UNiagaraExternalEditUtilities`（`NiagaraEditor/Public/NiagaraExternalSystemEditorUtilities.h`），提供 `CreateNiagaraSystem / AddEmitter / AddModule / AddSetParametersModule / AddRenderer / SetStackInputData / AddUserVariable` 等函数，可在 Blueprint 中使用（部分标为 EXPERIMENTAL）。5.7 没有这些。如果以后升级引擎，可以把自写桥接层逐步替换过去。

---

## 6. 引擎模块资产路径（本机 Content 实测，含弃用信息）

所有路径用 `LoadObject<UNiagaraScript>(nullptr, Path)` 加载，格式为 `/Niagara/Modules/<子目录>/<名>.<名>`。下表来自探针的 `-Audit` 模式：读取每个资产最新版本的 `bDeprecated / DeprecationRecommendation / LibraryVisibility`，共 255 个脚本，`Saved/NiagaraModuleAudit.csv`。

**雨效相关、未弃用、在库中可见（Library）的模块：**

| 用途 | 对象路径 |
|---|---|
| Emitter 状态 / 发射 | `/Niagara/Modules/Emitter/EmitterState.EmitterState`, `/Niagara/Modules/Emitter/SpawnRate.SpawnRate`, `/Niagara/Modules/Emitter/SpawnBurst_Instantaneous.SpawnBurst_Instantaneous`, `/Niagara/Modules/Emitter/SpawnPerUnit.SpawnPerUnit`, `/Niagara/Modules/Emitter/SpawnPerFrame.SpawnPerFrame`, `/Niagara/Modules/Emitter/EmitterBounds.EmitterBounds` |
| System | `/Niagara/Modules/System/SystemState.SystemState`（`InitializeSystem` 会自动添加）, `/Niagara/Modules/System/CompleteIfUnused.CompleteIfUnused` |
| 初始化 | `/Niagara/Modules/Spawn/Initialization/V2/InitializeParticle.InitializeParticle`（**非 V2 版本已弃用**） |
| 发射位置 | `/Niagara/Modules/Spawn/Location/V2/ShapeLocation.ShapeLocation`（推荐）, `/Niagara/Modules/Spawn/Location/BoxLocation.BoxLocation`, `/Niagara/Modules/Spawn/Location/CylinderLocation.CylinderLocation`, `/Niagara/Modules/Spawn/Location/SphereLocation.SphereLocation`, `/Niagara/Modules/Spawn/Location/V2/SphereLocation.SphereLocation`, `/Niagara/Modules/Spawn/Location/SystemLocation.SystemLocation`, `/Niagara/Modules/Spawn/Location/V2/GridLocation.GridLocation`, `/Niagara/Modules/Spawn/Location/StaticMeshLocation.StaticMeshLocation`（Box/Cylinder/Sphere(V1/V2) 的可见性为 Hidden，属于"软弃用"，仍可用） |
| 初速度 | `/Niagara/Modules/Spawn/Velocity/AddVelocity.AddVelocity`, `/Niagara/Modules/Spawn/Velocity/AddVelocityInCone.AddVelocityInCone`, `/Niagara/Modules/Spawn/Velocity/AddVelocityFromPoint.AddVelocityFromPoint`, `/Niagara/Modules/Spawn/Velocity/LinearForce.LinearForce`, `/Niagara/Modules/Spawn/Velocity/PointForce.PointForce` |
| 力 | `/Niagara/Modules/Update/Forces/GravityForce.GravityForce`, `/Niagara/Modules/Update/Forces/Drag.Drag`（**`DragForce` 已弃用并指向它**）, `/Niagara/Modules/Update/Forces/AerodynamicDrag.AerodynamicDrag`, `/Niagara/Modules/Update/Forces/WindForce.WindForce`, `/Niagara/Modules/Update/Forces/V2/CurlNoiseForce.CurlNoiseForce`（V1 为 Hidden）, `/Niagara/Modules/Update/Forces/V2/PointAttractionForce.PointAttractionForce`（V1 为 Hidden）, `/Niagara/Modules/Update/Forces/VortexForce.VortexForce`, `/Niagara/Modules/Update/Forces/VectorNoiseForce.VectorNoiseForce`, `/Niagara/Modules/Update/Forces/LimitForce.LimitForce`, `/Niagara/Modules/Update/Forces/SpringForce.SpringForce`, `/Niagara/Modules/Update/Forces/LineAttractionForce.LineAttractionForce`, `/Niagara/Modules/Update/Forces/AccelerationForce.AccelerationForce` |
| 速度 | `/Niagara/Modules/Update/Velocity/ScaleVelocity.ScaleVelocity`, `/Niagara/Modules/Update/Velocity/InheritVelocity.InheritVelocity`（**`VortexVelocity` 已弃用，改用 `Update/Forces/VortexForce`**） |
| 求解 | `/Niagara/Modules/Solvers/SolveForcesAndVelocity.SolveForcesAndVelocity`, `/Niagara/Modules/PostSolvers/CalculateAccurateVelocity.CalculateAccurateVelocity` |
| 生命周期 / 剔除 | `/Niagara/Modules/Update/Lifetime/KillParticles.KillParticles`, `/Niagara/Modules/Update/Lifetime/KillParticlesInVolume.KillParticlesInVolume`, `/Niagara/Modules/Update/Lifetime/ParticleState.ParticleState`, `/Niagara/Modules/Update/Lifetime/FirstFrame.FirstFrame`, `/Niagara/Modules/Update/Camera/ViewRecycler.ViewRecycler`, `/Niagara/Modules/Update/Camera/ViewMask.ViewMask` |
| 碰撞 | `/Niagara/Modules/Collision/Collision.Collision`（GPU 可用深度缓冲或距离场；**`CollisionQuery` / `AnalyticalCollisionQuery` 已弃用并指向它**）, `/Niagara/Modules/Collision/SceneDepthTest.SceneDepthTest`, `/Niagara/Modules/Update/Utility/AsyncGPUTrace.AsyncGPUTrace`, `/Niagara/Modules/Math/AvoidDistanceFieldSurfaces_GPU.AvoidDistanceFieldSurfaces_GPU`, `/Niagara/Modules/Events/GenerateCollisionEvent.GenerateCollisionEvent`（事件只适用于 CPU） |
| 大小 / 颜色 / 外观 | `/Niagara/Modules/Update/Size/ScaleSpriteSize.ScaleSpriteSize`, `/Niagara/Modules/Update/Size/ScaleSpriteSizeBySpeed.ScaleSpriteSizeBySpeed`（`SpriteSizeScaleByVelocity` 已弃用）, `/Niagara/Modules/Update/Size/ScaleMeshSize.ScaleMeshSize`, `/Niagara/Modules/Update/Size/ScaleMeshSizeBySpeed.ScaleMeshSizeBySpeed`, `/Niagara/Modules/Update/Color/Color.Color`, `/Niagara/Modules/Update/Color/ScaleColor.ScaleColor`, `/Niagara/Modules/Update/Color/ScaleColorBySpeed.ScaleColorBySpeed`, `/Niagara/Modules/Update/Material/DynamicMaterialParameters.DynamicMaterialParameters`（`DynamicMatParams*` 均已弃用）, `/Niagara/Modules/Update/SubUV/V2/SubUVAnimation.SubUVAnimation`（V1 为 Hidden）, `/Niagara/Modules/Update/Renderers/Sprite/SpriteFacingAndAlignment.SpriteFacingAndAlignment` |
| 网格朝向 | `/Niagara/Modules/Spawn/Orientation/V3/InitialMeshOrientation.InitialMeshOrientation`（V1/V2 已弃用）, `/Niagara/Modules/Update/Orientation/V3/UpdateMeshOrientation.UpdateMeshOrientation`（V2 已弃用）, `/Niagara/Modules/Update/Orientation/V2/MeshRotationForce.MeshRotationForce` |
| 其他 | `/Niagara/Modules/Update/Utility/InterpolateOverTime.InterpolateOverTime`, `/Niagara/Modules/Update/Utility/VectorNoiseValue.VectorNoiseValue`, `/Niagara/Modules/Update/Utility/SampleGBuffer.SampleGBuffer`, `/Niagara/Modules/Update/Position/JitterPosition.JitterPosition`, `/Niagara/Modules/Update/Position/OffsetPosition.OffsetPosition`, `/Niagara/Modules/Update/Camera/CameraOffset.CameraOffset`, `/Niagara/Modules/Landscape/SampleLandscape.SampleLandscape` |

**已弃用（不要再用），以及审计给出的替代：**

- 生命周期 / System：
  - `Emitter/EmitterLifeCycle` → `Emitter/EmitterState`
  - `System/SystemLifeCycle`、`Scalability/SystemScalability` → `System/SystemState`
  - `Scalability/EmitterScalability`、`Scalability/NumInstancesCull`：已弃用，无替代
- 初始化 / 发射位置：
  - `Spawn/Initialization/InitializeParticle` → `.../V2/InitializeParticle`
  - `Spawn/Location/GridLocation` → `.../V2/GridLocation`
  - `Spawn/Location/SkeletalMeshSurfaceLocation`、`SkeletalMeshSkeletonLocation` → `.../V2/SkeletalMeshLocation`
  - `Spawn/Location/CurlNoiseLocation`：已弃用，无替代
- 力 / 速度：
  - `Update/Forces/DragForce` → `Update/Forces/Drag`
  - `Update/Velocity/VortexVelocity` → `Update/Forces/VortexForce`
  - `Update/Utility/CurlNoise` → `Update/Utility/VectorNoiseValue`
  - `Solvers/SolveRotationalForcesAndVelocity`：已弃用，无替代
- 颜色 / 大小 / 材质：
  - `Update/Color/ScaleColorByVelocity` → `ScaleColorBySpeed`
  - `Update/Size/SpriteSizeScaleByVelocity` → `ScaleSpriteSizeBySpeed`
  - `Update/Material/DynamicMatParams(1,2,3)` → `DynamicMaterialParameters`
- 碰撞：
  - `Collision/CollisionQuery`、`Collision/AnalyticalCollisionQuery` → `Collision/Collision`
  - `Collision/CollisionLinearImpulse`、`CollisionRest`、`DebugCollisionEvents`：已弃用，无替代
- 网格朝向：
  - `Spawn/Orientation/InitialMeshRotation` → `.../V2/InitialMeshOrientation`，后者再 → `.../V3/InitialMeshOrientation`
  - `Update/Orientation/MeshRotationRate` → `Update/Orientation/V2/UpdateMeshOrientation`，后者再 → `.../V3/UpdateMeshOrientation`
  - `Update/Orientation/OrientMeshToVector`、`MeshRotationForce` → 各自的 `V2` 版本
  - `Update/Orientation/InitialMeshRotationRate`、`Collision/InitialRotationalVelocity` → `Collision/AddRotationalVelocity`
- 工具类：
  - `Update/Utility/TemporalLerp_Float/Vector` → `InterpolateOverTime`
  - `Update/Utility/QueryGBuffer` → `SampleGBuffer`

---

## 7. 导出符号速查表（UE 5.7，`nm` 核对）

| 能直接调用（✅） | 不能直接调用（❌）→ 替代方案 |
|---|---|
| `FNiagaraStackGraphUtilities::AddScriptModuleToStack(UNiagaraScript*, …)`, `AddParameterModuleToStack`, `GetOrCreateStackFunctionInputOverridePin`(5 参), `SetLinkedParameterValueForFunctionInput`, `SetDataInterfaceValueForFunctionInput`, `SetObjectAssetValueForFunctionInput`, `SetDynamicInputForFunctionInput`, `SetModuleIsEnabled`, `GetStackFunctionInputs`, `GetStackFunctionStaticSwitchPins`, `GetOutputNodeUsage`, `SynchronizeReferencingMapPinsWithFunctionCall` | `CreateRapidIterationParameter` → `FNiagaraUtilities::ConvertVariableToRapidIterationConstantName`；`ResetGraphForOutput`、`RelayoutGraph`（不影响编译）、`RemoveModuleFromStack`、`GetOrderedModuleNodes` → 自己遍历引脚链或用 ViewModel；`SetCustomExpressionForFunctionInput` → `UNiagaraStackFunctionInput::SetCustomExpression` |
| `FNiagaraEditorUtilities::AddEmitterToSystem`, `GetScriptOutputNode`, `UserParameters::GetScriptVariableForUserParameter` | `FNiagaraEditorUtilities::AddParameter`、`KillSystemInstances` |
| `UNiagaraSystemFactoryNew::InitializeSystem`, `UNiagaraEmitterFactoryNew::InitializeEmitter`；5.7 中所有 UCLASS 的 `StaticClass()`（包括 `UNiagaraSystemFactoryNew`、`UNiagaraModuleScriptFactory`、Private 的 `UNiagaraNodeParameterMapGet/Set`） | `UNiagaraScriptFactoryNew::InitializeScript` → 自建图（3.2 节） |
| `UNiagaraNode::GetInputPin/GetOutputPin/ReallocatePins`, `UEdGraphSchema_Niagara::TryCreateConnection/TrySetDefaultValue/TypeDefinitionToPinType/PinToNiagaraVariable`, `UNiagaraGraph::FindEquivalentOutputNode/GetScriptVariable/RenameParameter` | `UNiagaraGraph::AddParameter/FindOutputNode/NotifyGraphNeedsRecompile` |
| `UE::Niagara::Wizard::Utilities::AddReadParameterPin/AddWriteParameterPin/CreateFunctionCallNode/CreateDataInterfaceFunctionNode/CreateOpNode`（自行声明） | `UNiagaraNodeWithDynamicPins::RequestNewTypedPin/AddParameterPin` → wizard 工具或 3.5 节的 Add 引脚技巧 |
| `UNiagaraNodeCustomHlsl`：`StaticClass` 和构造函数；虚函数（`PostEditChangeProperty`、`AllocateDefaultPins`、`PinConnectionListChanged`、`CommitEditablePinName`…）都能通过 vtable 调用 | `SetCustomHlsl`、`RebuildSignatureFromPins`、`InitAsCustomHlslDynamicInput` → 反射写 `CustomHlsl`，并在 Finalize 前写好 `Signature` |
| `FNiagaraSystemViewModel::Initialize/GetEmitterHandleViewModels`, `FNiagaraEmitterHandleViewModel::GetEmitterStackViewModel`, `UNiagaraStackEntry::GetUnfilteredChildren/RefreshChildren`, `UNiagaraStackModuleItem::GetModuleNode/GetParameterInputs/Delete`, `UNiagaraStackFunctionInput::SetLocalValue/SetLinkedParameterValue/SetCustomExpression/SetDynamicInput/SetDataInterfaceValue/SetScratch`, `UNiagaraScratchPadViewModel::CreateNewScript/ApplyScratchPadChanges` | `FNiagaraSystemViewModel::Cleanup` → 让对象析构即可 |
| Runtime：`UNiagaraSystem::AddEmitterHandle/RequestCompile/WaitForCompilationComplete/GetEmitterHandles/GetSystemSpawnScript…`, `UNiagaraEmitter::AddRenderer/GetLatestEmitterData/SetUniqueEmitterName`, `UNiagaraScript::SetLatestSource/RequestCompile/ContainsUsage/GetLastCompileStatus/GetLatestScriptData`, `FNiagaraParameterStore::AddParameter(虚)/SetParameterData`, `FNiagaraVariableAttributeBinding::SetValue`, `UNiagaraMeshRendererProperties::OnMeshChanged` | `FNiagaraUserRedirectionParameterStore::AddParameter`（override 未导出）→ 通过 `FNiagaraParameterStore&` 引用调用，走虚分派 |

有一个误区要说明：Mac 上 UBT 生成的 `Intermediate/Mac/Stubs/*.rsp` 里带 `-undefined dynamic_lookup`，但那只是 stub。真正的链接 rsp（`Intermediate/Build/Mac/arm64/UnrealEditor/Development/<Module>/*.dylib.rsp`）里没有这个参数，所以"能链接"确实意味着符号已导出。

---

## 8. 网络上的参考实现【网络，未逐一编译】

- **PinWright/pinwright-ue（5.5+）**：`Source/PinWright/Private/Handlers/Niagara/NiagaraCreateModuleScriptHandler.cpp`。目前公开代码里最完整的"代码生成 Custom HLSL 模块"实现：`Input → MapGet(Module.*) → CustomHlsl → MapSet → Output`。它通过重新声明 wizard 的 `AddReadParameterPin/AddWriteParameterPin` 加引脚，反射写 `CustomHlsl`，并在 Finalize 之前写 Signature。和本文 3.4 节一致。
- **kevinpbuckley/VibeUE**：`Source/VibeUE/Private/PythonAPI/UNiagaraScratchPadService.cpp`。做法是反射写 HLSL、手动重建 Signature，在 Emitter 的 `ScratchPads` 容器里复制 `DefaultModuleScript`。注释里的坑：
  - 加新引脚后再调 `PostEditChangeProperty` 会按旧 Signature `ReallocatePins`，新引脚丢失。本文做法是先写 Signature，因此不受影响。
  - 不要对栈上的调用节点调用 `RefreshFromExternalChanges`。
  - MapSet 上有重名引脚会导致 "System is invalid after compilation"。
- **aadeshrao123/Unreal-MCP**：Build.cs 把 `Plugins/FX/Niagara/Source/NiagaraEditor/Private` 加进 include 路径。这只能解决"编译"，非虚私有方法照样链接不到。
- **RedPandaProjects/STALKERonUE（5.3.2）**：`FRBMKParticlesFactory.cpp`。实际项目中的导入器，用的是 `InitializeSystem` + `FNiagaraSystemViewModel::AddEmptyEmitter`，RI 写法与 2.2 节相同，`AddRenderer` 也一样。
- **ChiR24/Unreal_mcp（5.0–5.8，带版本守卫）**：注释说 `GetOrCreateStackFunctionInputOverridePin` 从 5.3 起才能链接。
- **believer-oss/Claireon（5.5+）**：用 override pin 的 `DefaultValue` 设输入，再调 `MarkNodeRequiresSynchronization`。
- **Italink/UnrealClientProtocol**：5.5 起 `SetLinkedValueHandleForFunctionInput` 改名为 `SetLinkedParameterValueForFunctionInput`，并且参数集合改为 `TSet<FNiagaraVariableBase>`。
- **db-lyon/ue-mcp**：Emitter 部分可参考。它的 `CreateModuleFromHlsl` 没写 Signature、没有连线，是反例。
- **TypeDreamMoon/DreamFX**：要求 `SetCustomHlsl` / `RequestNewTypedPin` 带导出宏，只能在作者的私有引擎分支上用。它的 Compat 层基于 ViewModel，和 2.6 节的思路相同。
- **sideeffects/HoudiniNiagara（UE5.7 分支）**：自定义 DI 的 GPU HLSL 写法；5.6+ 需要 `#include "NiagaraCompileHashVisitor.h"`。
- **Python**（atomantic/UEMCP 等）：只能用工厂建空 System 和复制模板，结论与第 5 节一致。
- Epic 论坛没找到有价值的帖子；chongdashu/unreal-mcp、runreal/unreal-mcp 里没有 Niagara 代码。

---

## 9. 对 ProjectRain 的落地建议

1. **先处理环境**：
   - 执行 `xcodebuild -downloadComponent MetalToolchain`，否则编辑器无法编译任何着色器。
   - 把 `ProjectRainEditor` 的 `LoadingPhase` 改为 `Default`，或另建一个 `ProjectRainTools` 模块，用来放 Commandlet。
2. **代码结构**：
   - 在 `ProjectRainEditor` 里放 `URainNiagaraBuilder`（BlueprintCallable 函数库）、`UBuildRainCommandlet`，以及一个 `NiagaraAuthoringHelpers.h/.cpp`。助手函数直接照抄本文 1.2、2.1–2.8、3.2–3.4、4.1–4.2 节的已验证代码。
   - Python（`Scripts/build_rain.py`）只负责调用和参数编排，可以在编辑器里用 `-ExecutePythonScript`，CI 里用 `-run=pythonscript`。
   - 构建要**幂等**：每次先删除 `/Game/FX/Rain/Generated/*` 再全量生成，避免增量修改带来的残留，例如 RI 参数和 MapGet 唯一名后缀。
3. **自定义逻辑分层**：
   - 简单的属性公式用路线 D（Set Parameters + 表达式）。
   - 多语句的粒子逻辑，比如"冻结"减速曲线、受击扰动，用路线 A：只用 ParameterMap 的模块，读 `User.FreezeAmount` 等参数。
   - 需要在栈里可调的参数用路线 B（`Module.*` 输入）。
   - 需要外部数据（角色手部位置、深度或高度场）时，用自定义 DI（3.8 节）。
4. **验证闭环**：
   - 构建后按 4.1 节输出所有脚本的状态，`status!=3` 就视为失败。
   - GPU 脚本还要检查 `GetRenderThreadScript()->GetCompileErrors()`。这一项需要 Metal Toolchain，并且要在编辑器里或带 `-AllowCommandletRendering` 运行才有意义。
   - 可选：在 `LastHlslTranslationGPU` 里 grep 自定义函数名，做冒烟测试。

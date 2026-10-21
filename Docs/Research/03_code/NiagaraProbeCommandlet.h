#pragma once

#include "Commandlets/Commandlet.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "NiagaraProbeCommandlet.generated.h"

UCLASS()
class UNiagaraProbeCommandlet : public UCommandlet
{
	GENERATED_BODY()
public:
	virtual int32 Main(const FString& Params) override;
};

UCLASS()
class UNiagaraProbeLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	/** Python: unreal.NiagaraProbeLibrary.build_probe_system("/Game/Probe", "NS_FromPython") */
	UFUNCTION(BlueprintCallable, Category = "NiagaraProbe")
	static bool BuildProbeSystem(const FString& PackagePath, const FString& AssetName);
};

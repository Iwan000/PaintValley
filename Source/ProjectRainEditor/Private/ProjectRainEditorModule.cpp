// Project Rain editor module.
// Hosts a tiny file-based command queue so tools outside the editor can run Python in the live
// session (Python remote execution's multicast discovery does not work on this Mac):
//   drop  <Project>/Saved/RainCmd/<name>.py   ->   the editor runs it and writes <name>.out
#include "Modules/ModuleManager.h"
#include "Containers/Ticker.h"
#include "HAL/FileManager.h"
#include "IPythonScriptPlugin.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "RainMagicController.h"
#include "Editor/EditorPerformanceSettings.h"

DEFINE_LOG_CATEGORY_STATIC(LogRainCmd, Log, All);

class FProjectRainEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		// The editor is driven from outside (it is rarely the foreground app): never throttle, or
		// Niagara stops simulating in the level viewport.
		GetMutableDefault<UEditorPerformanceSettings>()->bThrottleCPUWhenNotForeground = false;
		TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FProjectRainEditorModule::Poll), 0.25f);
		PreviewHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FProjectRainEditorModule::PreviewTick), 0.0f);
	}

	virtual void ShutdownModule() override
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
		FTSTicker::GetCoreTicker().RemoveTicker(PreviewHandle);
	}

private:
	// Live preview of the rain performance in the level editor (PIE/renders use the normal actor tick).
	bool PreviewTick(float DeltaTime)
	{
		if (!GEditor || GEditor->PlayWorld)
		{
			return true;
		}
		if (UWorld* World = GEditor->GetEditorWorldContext().World())
		{
			for (TActorIterator<ARainMagicController> It(World); It; ++It)
			{
				It->EditorPreviewTick(DeltaTime);
			}
		}
		return true;
	}

	bool Poll(float)
	{
		IPythonScriptPlugin* Python = IPythonScriptPlugin::Get();
		if (!Python || !Python->IsPythonAvailable() || bRunning)
		{
			return true;
		}
		const FString Dir = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("RainCmd"));
		TArray<FString> Files;
		IFileManager::Get().FindFiles(Files, *(Dir / TEXT("*.py")), true, false);
		Files.Sort();
		for (const FString& File : Files)
		{
			const FString Path = Dir / File;
			const FString Base = FPaths::GetBaseFilename(File);
			bRunning = true;
			FPythonCommandEx Cmd;
			Cmd.Command = Path;
			Cmd.ExecutionMode = EPythonCommandExecutionMode::ExecuteFile;
			Cmd.FileExecutionScope = EPythonFileExecutionScope::Public;
			const bool bOk = Python->ExecPythonCommandEx(Cmd);
			bRunning = false;

			FString Out;
			for (const FPythonLogOutputEntry& Entry : Cmd.LogOutput)
			{
				Out += Entry.Output;
				Out += TEXT("\n");
			}
			Out += FString::Printf(TEXT("RESULT: %s\n%s\n"), *Cmd.CommandResult, bOk ? TEXT("SUCCESS") : TEXT("PYTHON_FAILED"));
			IFileManager::Get().Move(*(Dir / (Base + TEXT(".done"))), *Path, true);
			FFileHelper::SaveStringToFile(Out, *(Dir / (Base + TEXT(".out"))), FFileHelper::EEncodingOptions::ForceUTF8);
			UE_LOG(LogRainCmd, Display, TEXT("Ran %s (%s)"), *File, bOk ? TEXT("ok") : TEXT("failed"));
		}
		return true;
	}

	FTSTicker::FDelegateHandle TickHandle;
	FTSTicker::FDelegateHandle PreviewHandle;
	bool bRunning = false;
};

IMPLEMENT_MODULE(FProjectRainEditorModule, ProjectRainEditor);

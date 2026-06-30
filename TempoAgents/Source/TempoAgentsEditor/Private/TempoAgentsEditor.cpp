// Copyright Tempo Simulation, LLC. All Rights Reserved.

#include "TempoAgentsEditor.h"

#include "TempoAgentsEditorCommands.h"
#include "TempoAgentsEditorStyle.h"
#include "TempoAgentsEditorUtils.h"

#include "ToolMenus.h"

DEFINE_LOG_CATEGORY(LogTempoAgentsEditor);

#define LOCTEXT_NAMESPACE "FTempoAgentsEditorModule"

void FTempoAgentsEditorModule::StartupModule()
{
	FTempoAgentsEditorStyle::Initialize();
	FTempoAgentsEditorStyle::ReloadTextures();

	FTempoAgentsEditorCommands::Register();
	
	PluginCommands = MakeShareable(new FUICommandList);

	PluginCommands->MapAction(
		FTempoAgentsEditorCommands::Get().PluginAction,
		FExecuteAction::CreateLambda([]() {
			// We're calling the function but ignoring its boolean return value here
			// because the editor UI doesn't currently use it.
			UTempoAgentsEditorUtils::RunTempoZoneGraphBuilderPipeline();
		}),
		FCanExecuteAction());

	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FTempoAgentsEditorModule::RegisterMenus));
}

void FTempoAgentsEditorModule::ShutdownModule()
{
	UToolMenus::UnRegisterStartupCallback(this);

	UToolMenus::UnregisterOwner(this);

	FTempoAgentsEditorStyle::Shutdown();

	FTempoAgentsEditorCommands::Unregister();
}

void FTempoAgentsEditorModule::RegisterMenus()
{
	// No menu or toolbar entries — use the GIS tools panel "Build Zone Graph" button instead.
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FTempoAgentsEditorModule, TempoAgentsEditor)

// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoWorldUtils.h"

#include "TempoCoreUtils.h"

#include "EngineUtils.h"

AActor* GetActorWithName(const UWorld* World, const FString& Name)
{
	// Match the real FName first. AActor::GetActorLabel() (which GetActorIdentifier() returns, via
	// GetActorNameOrLabel()) derives its label from the CLASS name (GetDefaultActorLabel()), with a
	// disambiguating numeric suffix ONLY when the actor's FName carries an internal auto-uniquify
	// number -- a literal caller-supplied name (e.g. SpawnActor's `name` param, used with
	// NameMode=Required_ErrorAndReturnNull specifically so the caller can rely on it) does NOT have
	// one. So every actor of the same class spawned with an explicit literal name previously
	// materialized to the SAME colliding label (e.g. every runtime-spawned SplinePropLine showed as
	// "SplinePropLine"), and the first-match TActorIterator below would resolve a later request to
	// an unrelated, already-finished actor of that class -- observed live as FinishSpawningActor
	// finding "an" actor by that shared label whose DeferredSpawnTransforms entry had already been
	// consumed by an EARLIER actor's own finish call, producing "No deferred spawn transform
	// recorded" (AUT-3194). An exact FName match is unambiguous and doesn't have this problem, so it
	// takes priority; the label-based fallback stays for actors referenced by their editor label
	// (e.g. a hand-placed level actor with no meaningful FName to match on).
	for (TActorIterator<AActor> ActorIt(World); ActorIt; ++ActorIt)
	{
		if (ActorIt->GetFName().ToString().Equals(Name, ESearchCase::IgnoreCase))
		{
			return *ActorIt;
		}
	}

	for (TActorIterator<AActor> ActorIt(World); ActorIt; ++ActorIt)
	{
		if (UTempoCoreUtils::GetActorIdentifier(*ActorIt).Equals(Name, ESearchCase::IgnoreCase))
		{
			return *ActorIt;
		}
	}

	return nullptr;
}

UObject* GetAssetByPath(const FString& AssetPath)
{
	const FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	const IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	FString NormalizedPath = AssetPath;
	// Fully-qualified paths should be of the form PackageName.AssetName. If the asset name was not supplied
	// guess that it is the same as the last element of the package name.
	if (!NormalizedPath.Contains(TEXT(".")))
	{
		const FString AssetName = FPaths::GetBaseFilename(AssetPath);
		NormalizedPath = FString::Printf(TEXT("%s.%s"), *AssetPath, *AssetName);
	}

	const FAssetData AssetData = AssetRegistry.GetAssetByObjectPath(FSoftObjectPath(NormalizedPath));
	if (AssetData.IsValid())
	{
		return AssetData.GetAsset();
	}

	return nullptr;
}

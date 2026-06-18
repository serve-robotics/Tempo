// Copyright Tempo Simulation, LLC. All Rights Reserved


#include "TempoZoneGraphBuilder.h"

#include "TempoRoadInterface.h"
#include "TempoRoadModuleInterface.h"
#include "ZoneShapeUtilities.h"

#include "TempoCoreUtils.h"

bool FTempoZoneGraphBuilder::ShouldFilterLaneConnection(const UZoneShapeComponent& PolygonShapeComp, const UZoneShapeComponent& SourceShapeComp, const TArray<FLaneConnectionSlot>& SourceSlots, const int32 SourceSlotQueryIndex, const UZoneShapeComponent& DestShapeComp, const TArray<FLaneConnectionSlot>& DestSlots, const int32 DestSlotQueryIndex, const TArray<FLaneConnectionCandidate>& AllCandidates) const
{
	const AActor* IntersectionQueryActor = GetIntersectionQueryActor(PolygonShapeComp);

	const AActor* SourceRoadQueryActor = GetRoadQueryActor(SourceShapeComp);
	const AActor* DestRoadQueryActor = GetRoadQueryActor(DestShapeComp);

	// Generate lane connection info arrays upfront — needed by both the module-to-module
	// path below (which may delegate to the intersection) and the standard road path.
	const TArray<FTempoLaneConnectionInfo> SourceLaneConnectionInfos = GenerateTempoLaneConnectionInfoArray(SourceSlots);
	const TArray<FTempoLaneConnectionInfo> DestLaneConnectionInfos = GenerateTempoLaneConnectionInfoArray(DestSlots);

	// Module-to-module: neither actor implements UTempoRoadInterface, but both may implement
	// UTempoRoadModuleInterface. When an intersection polygon mediates the connection, delegate
	// to the intersection's ShouldFilterTempoLaneConnection (it knows the clockwise corner-arc
	// topology). Otherwise fall back to asking the source module directly.
	if (SourceRoadQueryActor == nullptr && DestRoadQueryActor == nullptr)
	{
		AActor* SrcOwner = SourceShapeComp.GetOwner();
		AActor* DstOwner = DestShapeComp.GetOwner();
		if (SrcOwner && DstOwner &&
			SrcOwner->Implements<UTempoRoadModuleInterface>() &&
			DstOwner->Implements<UTempoRoadModuleInterface>())
		{
			if (IntersectionQueryActor != nullptr)
			{
				return UTempoCoreUtils::CallBlueprintFunction(
					IntersectionQueryActor,
					ITempoIntersectionInterface::Execute_ShouldFilterTempoLaneConnection,
					SrcOwner, SourceLaneConnectionInfos, SourceSlotQueryIndex,
					DstOwner, DestLaneConnectionInfos, DestSlotQueryIndex,
					AllCandidates);
			}
			return ITempoRoadModuleInterface::Execute_ShouldFilterTempoLaneConnection(SrcOwner, DstOwner);
		}
	}

	if (IntersectionQueryActor == nullptr || SourceRoadQueryActor == nullptr || DestRoadQueryActor == nullptr)
	{
		return false;
	}

	bool bShouldFilterLaneConnection;
	{
		bShouldFilterLaneConnection = UTempoCoreUtils::CallBlueprintFunction(IntersectionQueryActor, ITempoIntersectionInterface::Execute_ShouldFilterTempoLaneConnection, SourceRoadQueryActor, SourceLaneConnectionInfos, SourceSlotQueryIndex, DestRoadQueryActor, DestLaneConnectionInfos, DestSlotQueryIndex, AllCandidates);
	}

	return bShouldFilterLaneConnection;
}

TArray<FTempoLaneConnectionInfo> FTempoZoneGraphBuilder::GenerateTempoLaneConnectionInfoArray(const TArray<FLaneConnectionSlot>& Slots) const
{
	TArray<FTempoLaneConnectionInfo> TempoLaneConnectionInfos;
	
	for (const auto& Slot : Slots)
	{
		const int32 LaneIndex = Slot.Index;
		FTempoLaneConnectionInfo LaneConnectionInfo(Slot, LaneIndex);
		
		TempoLaneConnectionInfos.Add(LaneConnectionInfo);
	}

	return TempoLaneConnectionInfos;
}

AActor* FTempoZoneGraphBuilder::GetIntersectionQueryActor(const UZoneShapeComponent& ZoneShapeComponent) const
{
	AActor* IntersectionQueryActor = ZoneShapeComponent.GetOwner();

	if (IntersectionQueryActor == nullptr)
	{
		return nullptr;
	}

	if (!IntersectionQueryActor->Implements<UTempoIntersectionInterface>())
	{
		return nullptr;
	}

	return IntersectionQueryActor;
}

AActor* FTempoZoneGraphBuilder::GetRoadQueryActor(const UZoneShapeComponent& ZoneShapeComponent) const
{
	AActor* RoadQueryActor = ZoneShapeComponent.GetOwner();

	if (RoadQueryActor == nullptr)
	{
		return nullptr;
	}

	if (!RoadQueryActor->Implements<UTempoRoadInterface>())
	{
		return nullptr;
	}

	return RoadQueryActor;
}

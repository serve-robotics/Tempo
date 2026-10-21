// Copyright Tempo Simulation, LLC. All Rights Reserved


#include "TempoZoneGraphBuilder.h"

#include "TempoRoadInterface.h"
#include "TempoRoadModuleInterface.h" // [SERVE-PATCH P13]
#include "ZoneShapeUtilities.h"

#include "TempoCoreUtils.h"

bool FTempoZoneGraphBuilder::ShouldFilterLaneConnection(const UZoneShapeComponent& PolygonShapeComp, const UZoneShapeComponent& SourceShapeComp, const TArray<FLaneConnectionSlot>& SourceSlots, const int32 SourceSlotQueryIndex, const UZoneShapeComponent& DestShapeComp, const TArray<FLaneConnectionSlot>& DestSlots, const int32 DestSlotQueryIndex, const TArray<FLaneConnectionCandidate>& AllCandidates) const
{
	const AActor* IntersectionQueryActor = GetIntersectionQueryActor(PolygonShapeComp);

	const AActor* SourceRoadQueryActor = GetRoadQueryActor(SourceShapeComp);
	const AActor* DestRoadQueryActor = GetRoadQueryActor(DestShapeComp);

	// [SERVE-PATCH P13] Shapes the intersection or its road modules own (corner arcs, crosswalks) have no road actor.
	// Still let the intersection judge a pair of them, with the owners as the connection actors.
	auto IsOwnedHere = [IntersectionQueryActor](const UZoneShapeComponent& Shape)
	{
		const AActor* Owner = Shape.GetOwner();
		return Owner != nullptr && (Owner == IntersectionQueryActor || Owner->Implements<UTempoRoadModuleInterface>());
	};
	if (SourceRoadQueryActor == nullptr && DestRoadQueryActor == nullptr && IsOwnedHere(SourceShapeComp) && IsOwnedHere(DestShapeComp))
	{
		SourceRoadQueryActor = SourceShapeComp.GetOwner();
		DestRoadQueryActor = DestShapeComp.GetOwner();
	}

	if (IntersectionQueryActor == nullptr || SourceRoadQueryActor == nullptr || DestRoadQueryActor == nullptr)
	{
		return false;
	}

	const TArray<FTempoLaneConnectionInfo>& SourceLaneConnectionInfos = GenerateTempoLaneConnectionInfoArray(SourceSlots, SourceShapeComp); // [SERVE-PATCH P13] + Shape
	const TArray<FTempoLaneConnectionInfo>& DestLaneConnectionInfos = GenerateTempoLaneConnectionInfoArray(DestSlots, DestShapeComp);

	bool bShouldFilterLaneConnection;
	{
		bShouldFilterLaneConnection = UTempoCoreUtils::CallBlueprintFunction(IntersectionQueryActor, ITempoIntersectionInterface::Execute_ShouldFilterTempoLaneConnection, SourceRoadQueryActor, SourceLaneConnectionInfos, SourceSlotQueryIndex, DestRoadQueryActor, DestLaneConnectionInfos, DestSlotQueryIndex, AllCandidates);
	}

	return bShouldFilterLaneConnection;
}

TArray<FTempoLaneConnectionInfo> FTempoZoneGraphBuilder::GenerateTempoLaneConnectionInfoArray(const TArray<FLaneConnectionSlot>& Slots, const UZoneShapeComponent& Shape) const // [SERVE-PATCH P13] + Shape
{
	TArray<FTempoLaneConnectionInfo> TempoLaneConnectionInfos;

	for (const auto& Slot : Slots)
	{
		const int32 LaneIndex = Slot.Index;
		FTempoLaneConnectionInfo LaneConnectionInfo(Slot, LaneIndex);
		LaneConnectionInfo.Shape = &Shape; // [SERVE-PATCH P13]

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

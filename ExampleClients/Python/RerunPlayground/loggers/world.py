# Copyright Tempo Simulation, LLC. All Rights Reserved

"""Ground-truth world logger.

Streams actor states near the tracked (ego) actor and logs each as one or more
oriented 3D boxes plus a transform. All values come straight from ActorState in
the right-handed proto frame.

Each box is built from a local_bounds (an axis-aligned box in its own local
frame, with scale baked in) placed by a world Transform3D — so no per-box
rotation is needed, Rerun's entity-hierarchy transform composition does it.

If ActorState.instance_bounds is non-empty (an Actor decomposed into several
per-instance/per-segment boxes -- see UTempoCoreUtils::GetActorLocalInstanceBounds,
e.g. a spline prop line's cuboid-per-run-segment decomposition), each entry is
logged as its OWN child entity with its OWN (Actor-relative) transform, which
Rerun composes with the parent Actor entity's world transform automatically --
so the whole decomposition is visible, not just the Actor's single overall
local_bounds. Falls back to the single local_bounds box (the original
behavior) for the many Actors that don't decompose at all.
"""

import rerun as rr

import tempo_sim.tempo_world as tw

from .. import conventions as conv
from .._compat import set_sim_time
from ..streaming import pump


def _log_actor_state(state):
    entity = conv.ground_truth_entity(state.name)
    rr.log(entity, conv.transform_to_rerun(state.transform))

    if state.instance_bounds:
        for index, instance in enumerate(state.instance_bounds):
            instance_entity = f"{entity}/instance_{index}"
            center, half = conv.box_center_half(instance.local_bounds)
            label = instance.tag if instance.tag else state.name
            rr.log(instance_entity, conv.transform_to_rerun(instance.transform))
            rr.log(instance_entity, rr.Boxes3D(centers=[center], half_sizes=[half], labels=[label]))
        return

    center, half = conv.box_center_half(state.local_bounds)
    rr.log(entity, rr.Boxes3D(centers=[center], half_sizes=[half], labels=[state.name]))


async def stream_ego(track_actor):
    """Stream the tracked actor itself (the near-query may not include its center)."""
    def handle(state):
        set_sim_time(conv.SIM_TIME, state.timestamp_s)
        _log_actor_state(state)

    await pump(tw.stream_actor_state(actor=track_actor), handle, label=f"ego:{track_actor}")


async def stream_ground_truth(cfg, track_actor):
    def handle(states):
        if not states.actor_states:
            return
        set_sim_time(conv.SIM_TIME, states.actor_states[0].timestamp_s)
        for state in states.actor_states:
            _log_actor_state(state)

    await pump(
        tw.stream_actor_states_near(
            near_actor=track_actor,
            search_radius_m=cfg.search_radius_m,
            include_static=cfg.include_static,
        ),
        handle, label="ground_truth",
    )

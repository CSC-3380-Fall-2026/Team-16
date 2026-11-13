"""Finish the blank exit signs at their authored locations."""

import re

import unreal


HOUSING = "/Game/MaterialInstances/Shared/Metals/MI_WhiteMetal"
BLANK = "/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"


def exit_housings(actors):
    """Find the sign housings by their source labels."""
    return [actor for actor in actors if re.fullmatch(r"ExitSign_Temp\d*", actor.get_actor_label())]


def finish_exit_signs(subsystem, actors):
    """Give each blank sign a finished housing and readable lettering on both sides."""
    housings = exit_housings(actors)
    if not housings:
        return 0
    material = unreal.load_asset(HOUSING)
    if not isinstance(material, unreal.MaterialInterface):
        raise RuntimeError(f"Could not load exit sign material {HOUSING}")
    by_label = {actor.get_actor_label(): actor for actor in actors}
    for housing in housings:
        mesh = housing.get_component_by_class(unreal.StaticMeshComponent)
        current = mesh.get_material(0)
        if not current or current.get_path_name() == BLANK:
            mesh.set_material(0, material)
        for side, yaw in (("Front", 0), ("Back", 180)):
            label = f"{housing.get_actor_label()}_Text{side}"
            sign = by_label.get(label)
            if sign is None:
                sign = subsystem.spawn_actor_from_class(unreal.TextRenderActor, housing.get_actor_location())
                if sign is None:
                    raise RuntimeError(f"Could not create {label}")
                sign.set_actor_label(label)
            sign.attach_to_actor(housing, "", unreal.AttachmentRule.KEEP_WORLD,
                                 unreal.AttachmentRule.KEEP_WORLD, unreal.AttachmentRule.KEEP_WORLD, False)
            # The housing is a 100 cm cube scaled down to the authored sign dimensions.
            sign.set_actor_relative_location(unreal.Vector(53 if side == "Front" else -53, 0, 0), False, True)
            sign.set_actor_relative_rotation(unreal.Rotator(yaw=yaw), False, True)
            text = sign.get_component_by_class(unreal.TextRenderComponent)
            text.set_absolute(False, False, True)
            text.set_world_scale3d(unreal.Vector(1, 1, 1))
            text.set_text("EXIT")
            text.set_horizontal_alignment(unreal.HorizTextAligment.EHTA_CENTER)
            text.set_vertical_alignment(unreal.VerticalTextAligment.EVRTA_TEXT_CENTER)
            text.set_world_size(16.0)
            text.set_text_render_color(unreal.Color(200, 15, 25, 255))
    return len(housings)


def check_exit_signs(actors, expected_count):
    """Return errors for missing signs, blank housings, or incomplete faces."""
    housings = exit_housings(actors)
    errors = []
    if len(housings) != expected_count:
        errors.append("Exit sign count differs from the build report")
    by_label = {actor.get_actor_label(): actor for actor in actors}
    for housing in housings:
        label = housing.get_actor_label()
        mesh = housing.get_component_by_class(unreal.StaticMeshComponent)
        material = mesh.get_material(0) if mesh else None
        if not material or material.get_path_name() == BLANK:
            errors.append(f"Blank exit sign housing: {label}")
        for side in ("Front", "Back"):
            sign = by_label.get(f"{label}_Text{side}")
            text = sign.get_component_by_class(unreal.TextRenderComponent) if sign else None
            if not text or str(text.get_editor_property("text")) != "EXIT" or sign.get_attach_parent_actor() != housing:
                errors.append(f"Missing exit sign lettering: {label} {side}")
    return errors

# Zet root motion uit op de GASP-idle-loop. Met root motion AAN stuurt de idle-montage de character-beweging
# aan (staande idle = blijf staan) -> je kunt niet weglopen. Een staande idle-loop hoort géén root motion te
# sturen, dus uitzetten lost de deadlock op.
import unreal

P = "/Game/Characters/UEFN_Mannequin/Animations/Idle/M_Neutral_Stand_Idle_Loop"
a = unreal.load_asset(P)
if not a:
    unreal.log_error("[idlefix] clip niet gevonden: " + P)
else:
    for prop in ("enable_root_motion", "force_root_lock", "root_motion_root_lock"):
        try:
            if prop == "enable_root_motion":
                a.set_editor_property(prop, False)
            unreal.log("[idlefix] %s -> aangepast" % prop)
        except Exception as e:
            unreal.log_warning("[idlefix] %s niet zetbaar: %s" % (prop, str(e)))
    unreal.EditorAssetLibrary.save_asset(P, only_if_is_dirty=False)
    unreal.log("[idlefix] enable_root_motion nu: " + str(a.get_editor_property("enable_root_motion")))
    unreal.log("[idlefix] KLAAR - opgeslagen")

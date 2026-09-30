# READ-ONLY: detailed dump of every remaining widget blueprint (tree, slots, brushes, fonts, colours) + T3D graphs,
# and PNG export of every texture used as a brush resource (to judge icon colours).
import json, os, traceback
import unreal

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "inspect")
os.makedirs(os.path.join(OUT, "tex"), exist_ok=True)
ASSETS = [
    "/Game/RoomPlanner/WBP_RoomPlannerWidget",
    "/Game/ColorCatalog/UI/WBP_ColorCatalog", "/Game/ColorCatalog/UI/WBP_ColorSwatchItem",
    "/Game/WBP_PreviewWindow", "/Game/WBP_ViewmodeOverlay", "/Game/WBP_ARExportModal", "/Game/WBP_NameOfCharacter",
    "/Game/FirstPerson/UI/CrowdTown/UserSettings/BP_UserSettings_UI",
    "/Game/FirstPerson/UI/CrowdTown/UserSettings/BP_DragPlayer_UI", "/Game/FirstPerson/UI/CrowdTown/UserSettings/BP_DropPlayerBox_UI",
    "/Game/FirstPerson/UI/CrowdTown/UserSettings/BP_Friend_UI", "/Game/FirstPerson/UI/CrowdTown/UserSettings/BP_Message_UI",
    "/Game/FirstPerson/UI/CrowdTown/UserSettings/BP_Notification_UI", "/Game/FirstPerson/UI/CrowdTown/UserSettings/BP_PendingFriend_UI",
    "/Game/FirstPerson/UI/CrowdTown/UserSettings/BP_Player_UI", "/Game/FirstPerson/UI/CrowdTown/UserSettings/BP_PopupMessage_UI",
    "/Game/FirstPerson/UI/CrowdTown/UserSettings/BP_PopupNotification_UI", "/Game/FirstPerson/UI/CrowdTown/UserSettings/BP_ReadyPlayer_UI",
    "/Game/FirstPerson/UI/CrowdTown/UserSettings/BP_SelectPredesignedAvatar_UI", "/Game/FirstPerson/UI/CrowdTown/UserSettings/BP_TeamPlayer_UI",
    "/Game/FirstPerson/UI/CrowdTown/PlayerCustomization/BP_Player_Customization_UI",
    "/Game/FirstPerson/UI/CrowdTown/PlayerCustomization/BP_Equpment_Selection_UI",
    "/Game/FirstPerson/UI/CrowdTown/Online_Offline_Selection/BP_GameMode_Selection_UI",
    "/Game/FirstPerson/UI/BP_GamePlay_UI",
]
textures = set()

def lc(c):
    try:
        s = c.get_editor_property("specified_color"); rule = str(c.get_editor_property("color_use_rule")).split(".")[-1]
        return [round(s.r, 3), round(s.g, 3), round(s.b, 3), round(s.a, 3), rule]
    except Exception:
        try: return [round(c.r, 3), round(c.g, 3), round(c.b, 3), round(c.a, 3)]
        except Exception: return str(c)

def brush(b):
    d = {"draw_as": str(b.get_editor_property("draw_as")).split(".")[-1], "tint": lc(b.get_editor_property("tint_color"))}
    try:
        r = b.get_editor_property("resource_object")
        if r:
            d["res"] = r.get_path_name()
            if isinstance(r, unreal.Texture2D): textures.add(r.get_path_name())
    except Exception: pass
    return d

def info(w):
    d = {"name": w.get_name(), "class": w.get_class().get_name(),
         "parent": w.get_parent().get_name() if w.get_parent() else None,
         "index": w.get_parent().get_child_index(w) if w.get_parent() else -1,
         "vis": str(w.get_visibility()).split(".")[-1].split(":")[0], "opacity": round(w.get_render_opacity(), 3)}
    s = w.get_editor_property("slot")
    if isinstance(s, unreal.CanvasPanelSlot):
        L = s.get_layout(); o, a = L.offsets, L.anchors
        d["slot"] = {"off": [round(o.left), round(o.top), round(o.right), round(o.bottom)],
                     "anch": [a.minimum.x, a.minimum.y, a.maximum.x, a.maximum.y], "al": [L.alignment.x, L.alignment.y],
                     "auto": s.get_auto_size(), "z": s.get_z_order()}
    elif s is not None:
        try:
            p = s.get_editor_property("padding"); d["slot"] = {"pad": [round(p.left), round(p.top), round(p.right), round(p.bottom)]}
        except Exception: pass
    try:
        if isinstance(w, unreal.TextBlock):
            f = w.get_editor_property("font")
            d.update(text=str(w.get_text())[:80], size=f.size, face=str(f.typeface_font_name),
                     family=f.font_object.get_name() if f.font_object else None, color=lc(w.get_editor_property("color_and_opacity")))
        if isinstance(w, (unreal.EditableText, unreal.EditableTextBox)):
            d["hint"] = str(w.get_editor_property("hint_text"))[:60]
        if isinstance(w, unreal.Image):
            d["brush"] = brush(w.get_editor_property("brush")); d["color"] = lc(w.get_editor_property("color_and_opacity"))
        if isinstance(w, unreal.Border):
            d["brush"] = brush(w.get_editor_property("background")); d["brush_color"] = lc(w.get_editor_property("brush_color"))
        if isinstance(w, unreal.Button):
            st = w.get_editor_property("widget_style")
            d["style"] = {k: brush(st.get_editor_property(k)) for k in ("normal", "hovered", "pressed")}
            d["color"] = lc(w.get_editor_property("color_and_opacity")); d["bg"] = lc(w.get_editor_property("background_color"))
        if isinstance(w, unreal.BackgroundBlur):
            d["blur"] = w.get_editor_property("blur_strength")
    except Exception as e:
        d["err"] = str(e)[:120]
    return d

result, log = {}, []
for path in ASSETS:
    key = path.rsplit("/", 1)[1]
    try:
        bp = unreal.EditorAssetLibrary.load_asset(path)
        prefix = bp.get_path_name() + ":WidgetTree."
        ws = [w for w in unreal.ObjectIterator(unreal.Widget) if w.get_path_name().startswith(prefix)]
        result[key] = [info(w) for w in ws]
        task = unreal.AssetExportTask()
        task.set_editor_property("object", bp); task.set_editor_property("filename", os.path.join(OUT, key + ".t3d"))
        task.set_editor_property("automated", True); task.set_editor_property("prompt", False)
        log.append("%s: %d widgets, t3d=%s" % (key, len(ws), unreal.Exporter.run_asset_export_task(task)))
    except Exception:
        log.append("%s FAILED %s" % (key, traceback.format_exc()[-300:]))
for tp in sorted(textures):
    try:
        tex = unreal.load_asset(tp.split(".")[0])
        task = unreal.AssetExportTask()
        task.set_editor_property("object", tex); task.set_editor_property("filename", os.path.join(OUT, "tex", tex.get_name() + ".png"))
        task.set_editor_property("automated", True); task.set_editor_property("prompt", False)
        unreal.Exporter.run_asset_export_task(task)
    except Exception as e:
        log.append("tex %s: %s" % (tp, e))
json.dump(result, open(os.path.join(OUT, "widgets.json"), "w", encoding="utf-8"), ensure_ascii=False, indent=0)
json.dump(sorted(textures), open(os.path.join(OUT, "textures.json"), "w", encoding="utf-8"), indent=0)
open(os.path.join(OUT, "log.txt"), "w", encoding="utf-8").write("\n".join(log) + "\ntextures: %d" % len(textures))

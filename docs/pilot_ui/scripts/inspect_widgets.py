# READ-ONLY inspector: widget tree (order, slots, fonts, styles) + full T3D export of the graphs, for the assets below.
# Nothing is saved; output goes to the scratchpad folder next to this script.
import json, os, traceback
import unreal

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "out")
os.makedirs(OUT, exist_ok=True)
ASSETS = {
    "BP_UserLogin_UI": "/Game/FirstPerson/UI/UserLogin/BP_UserLogin_UI",
    "BP_Online_Offline_Selection_UI": "/Game/FirstPerson/UI/CrowdTown/Online_Offline_Selection/BP_Online_Offline_Selection_UI",
}
log = []

def safe(fn, default=None):
    try:
        return fn()
    except Exception as e:
        return default if default is not None else "ERR: %s" % e

def color(c):
    try:
        sc = c.get_editor_property("specified_color")
        return [round(sc.r, 4), round(sc.g, 4), round(sc.b, 4), round(sc.a, 4)]
    except Exception:
        return str(c)

def brush(b):
    try:
        return {
            "draw_as": str(b.get_editor_property("draw_as")),
            "tint": color(b.get_editor_property("tint_color")),
            "size": [b.image_size.x, b.image_size.y],
            "resource": safe(lambda: b.get_editor_property("resource_object").get_path_name(), ""),
        }
    except Exception as e:
        return "ERR %s" % e

def slot_info(w):
    s = safe(lambda: w.get_editor_property("slot"), None)
    if not s or isinstance(s, str):
        return None
    d = {"class": s.get_class().get_name()}
    if isinstance(s, unreal.CanvasPanelSlot):
        L = s.get_layout()
        o, a, al = L.offsets, L.anchors, L.alignment
        d.update(offsets=[o.left, o.top, o.right, o.bottom],
                 anchors=[a.minimum.x, a.minimum.y, a.maximum.x, a.maximum.y],
                 alignment=[al.x, al.y], auto_size=s.get_auto_size(), z=s.get_z_order())
    for prop in ("padding", "horizontal_alignment", "vertical_alignment", "size"):
        v = safe(lambda: s.get_editor_property(prop), None)
        if v is not None and not isinstance(v, str):
            if isinstance(v, unreal.Margin):
                v = [v.left, v.top, v.right, v.bottom]
            elif isinstance(v, unreal.SlateChildSize):
                v = [str(v.size_rule), v.value]
            d[prop] = str(v) if not isinstance(v, list) else v
    return d

def widget_info(w):
    d = {
        "name": w.get_name(),
        "class": w.get_class().get_name(),
        "parent": (w.get_parent().get_name() if w.get_parent() else None),
        "index": (w.get_parent().get_child_index(w) if w.get_parent() else -1),
        "visibility": str(safe(lambda: w.get_visibility())),
        "slot": slot_info(w),
    }
    cls = d["class"]
    if isinstance(w, unreal.TextBlock):
        d["text"] = str(w.get_text())
        f = safe(lambda: w.get_editor_property("font"), None)
        if f and not isinstance(f, str):
            d["font"] = {"size": f.size, "typeface": str(f.typeface_font_name),
                         "family": safe(lambda: f.font_object.get_path_name(), "")}
        d["color"] = color(safe(lambda: w.get_editor_property("color_and_opacity")))
    if isinstance(w, unreal.EditableText) or isinstance(w, unreal.EditableTextBox):
        d["hint"] = str(safe(lambda: w.get_editor_property("hint_text")))
        d["is_password"] = safe(lambda: w.get_editor_property("is_password"))
    if isinstance(w, unreal.Image):
        d["brush"] = brush(safe(lambda: w.get_editor_property("brush")))
    if isinstance(w, unreal.Border):
        d["brush"] = brush(safe(lambda: w.get_editor_property("background")))
    if isinstance(w, unreal.Button):
        st = safe(lambda: w.get_editor_property("widget_style"), None)
        if st and not isinstance(st, str):
            d["style_normal"] = brush(st.get_editor_property("normal"))
    if isinstance(w, unreal.BackgroundBlur):
        d["blur"] = safe(lambda: w.get_editor_property("blur_strength"))
    d["is_variable"] = safe(lambda: w.get_editor_property("is_variable"))
    return d

result = {}
for key, path in ASSETS.items():
    try:
        bp = unreal.EditorAssetLibrary.load_asset(path)
        bp_path = bp.get_path_name()
        prefix = bp_path + ":WidgetTree."
        widgets = [w for w in unreal.ObjectIterator(unreal.Widget) if w.get_path_name().startswith(prefix)]
        result[key] = {"path": bp_path, "widget_count": len(widgets), "widgets": [widget_info(w) for w in widgets]}
        log.append("%s: %d widgets" % (key, len(widgets)))
        # Full-text export of the blueprint (graphs, nodes, pins) via the generic T3D exporter.
        for ext in ("t3d",):
            task = unreal.AssetExportTask()
            task.set_editor_property("object", bp)
            task.set_editor_property("filename", os.path.join(OUT, "%s.%s" % (key, ext)))
            task.set_editor_property("automated", True)
            task.set_editor_property("prompt", False)
            task.set_editor_property("replace_identical", True)
            ok = unreal.Exporter.run_asset_export_task(task)
            log.append("%s export .%s -> %s" % (key, ext, ok))
    except Exception:
        log.append("%s FAILED:\n%s" % (key, traceback.format_exc()))

with open(os.path.join(OUT, "widgets_dump.json"), "w", encoding="utf-8") as f:
    json.dump(result, f, ensure_ascii=False, indent=1)
with open(os.path.join(OUT, "inspect_log.txt"), "w", encoding="utf-8") as f:
    f.write("\n".join(log))
unreal.log("DUMP DONE: " + " | ".join(log))


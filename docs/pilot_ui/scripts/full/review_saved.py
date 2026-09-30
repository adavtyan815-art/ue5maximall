# READ-ONLY review: renders the hidden pages of the SAVED BP_UserSettings_UI (no restyle pass). Pages are brought
# into view in memory only; nothing is saved. Always closes the editor.
import os, sys, traceback
import unreal
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from maxi_ui_lib import *   # noqa: E402
OUT = os.path.join(HERE, "out")
lines = []
try:
    path = "/Game/FirstPerson/UI/CrowdTown/UserSettings/BP_UserSettings_UI"
    t = Tree(path)
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    cls = unreal.load_class(None, path + ".BP_UserSettings_UI_C")
    scene = lin("#5A5752")
    def shot(name):
        unreal.BlueprintEditorLibrary.compile_blueprint(t.bp)
        ok = unreal.MaxiUiDesignTools.render_widget_to_png(world, cls, unreal.IntPoint(1920, 1080), os.path.join(OUT, "saved_%s.png" % name), scene, [], [])
        lines.append("%s: %s" % (name, ok))
    panel = t.w("UserSettingsPanel")
    canvas(panel.slot, (0, 0.5, 0, 0.5), (0, -541), size=(1922, 1081))
    sw = t.w("UserSettingsSwitcher")
    for i in range(sw.get_num_widgets()):
        sw.set_active_widget_index(i); shot("main_%d" % i)
    canvas(panel.slot, (-1, 0.5, 0, 0.5), (-2, -541, 4, 1081))
    sec = t.w("SecondarySettingSwitcher")
    t.w("SecondarySettingsPanel").set_render_opacity(1.0)
    for i in range(sec.get_num_widgets()):
        sec.set_active_widget_index(i); shot("secondary_%d" % i)
except Exception:
    lines.append(traceback.format_exc())
finally:
    open(os.path.join(OUT, "review_saved.txt"), "w", encoding="utf-8").write("\n".join(lines))
    unreal.SystemLibrary.quit_editor()

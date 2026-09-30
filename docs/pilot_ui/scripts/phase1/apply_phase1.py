# Phase 1 of the UI redesign (TZ_UMG_phase1_hud_panels.md, approved 30.09.2026):
# ≡ in the salon (BP_Burger), private room + legacy private-room panel, profile, saves; UMaxiPanelGroup.
# Existing widgets keep their names; graphs and animations are not touched. Saves only after every blueprint compiled.
import os, sys, traceback
import unreal

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
OUT = os.path.join(HERE, "out_phase1")
os.makedirs(OUT, exist_ok=True)

# ------------------------------------------------------------------ preflight
_missing = []
for cname, methods in {
    "SizeBox": ["set_max_desired_height", "clear_width_override", "set_height_override"],
    "ScrollBox": [], "EditableTextBox": [], "MaxiPanelGroup": [], "MaxiVisibilityToggleButton": [],
    "MaxiUiDesignTools": ["set_brush_image_size", "register_missing_widget_guids", "render_widget_to_png", "set_button_style_property"],
    "Button": ["set_color_and_opacity", "set_background_color"],
}.items():
    cls = getattr(unreal, cname, None)
    if cls is None: _missing.append(cname); continue
    _missing += ["%s.%s" % (cname, m) for m in methods if not hasattr(cls, m)]
for sname, props in (("EditableTextBoxStyle", ["background_image_normal", "background_image_hovered", "background_image_focused",
                                               "padding", "foreground_color", "focused_foreground_color", "text_style"]),
                     ("TextBlockStyle", ["font", "color_and_opacity"])):
    inst = getattr(unreal, sname)()
    for p in props:
        try: inst.set_editor_property(p, inst.get_editor_property(p))
        except Exception as e: _missing.append("%s.%s (%s)" % (sname, p, e))
if _missing:
    raise RuntimeError("missing Python API: %s" % _missing)

from maxi_ui_lib import *   # noqa: E402  (after preflight: the lib loads F_MaxiMall)
if not FONT:
    raise RuntimeError("F_MaxiMall not found")

SEL = "/Game/FirstPerson/UI/CrowdTown/Online_Offline_Selection/BP_Online_Offline_Selection_UI"
BURGER = "/Game/FirstPerson/UI/CrowdTown/Online_Offline_Selection/BP_Burger"
PROFILE = "/Game/FirstPerson/UI/CrowdTown/Online_Offline_Selection/WBP_Male_Female"
SAVES = "/Game/UISaveSystem/NewFolder/WBP_SaveSystem"
SAVE_ITEM = "/Game/UISaveSystem/NewFolder/WBP_SaveHistoryItem"

# ================================================================ BP_Burger: ≡ identical to the menu's
def restyle_burger():
    t = Tree(BURGER)
    root, btn, bars = t.w("CanvasPanel_35"), t.w("Button_58"), t.w("VerticalBox_68")
    for n in ("Button_65", "Button", "Button_1"):
        t.w(n).set_visibility(VIS_COLLAPSED)
    button_style(btn, GLASS(s(12)), GLASS_HOVER(s(12)))
    for i in (1, 2):
        bar = t.new(unreal.Image, "Maxi_BurgerBar%d" % i)
        bar.set_brush(sized(brush_round(WHITE, 1.0, s(1)), s(15), s(1.6)))
        vslot(bars.add_child(bar), top=0 if i == 1 else s(4), h=H_CENTER)
    content_slot(btn)
    blur = glass_blur(t, "Maxi_BurgerBlur", s(12))
    reparent(btn, blur)
    canvas(root.add_child(blur), (0, 0, 0, 0), (s(16), s(16)), size=(s(40), s(40)), align=(0, 0))
    return t

# ================================================================ selection UI: private room, legacy panel, panel group
def restyle_selection():
    t = Tree(SEL)
    root = t.w("CanvasPanel_38")

    # --- MyPrivateRoom (code field, «Войти в комнату», «Создать комнату»)
    room = t.w("MyPrivateRoom")
    for n in ("Image_5", "Image_6"):
        t.w(n).set_visibility(VIS_COLLAPSED)
    body = side_panel(t, "Maxi_PR", room, t.w("TextBlock_9"), t.w("Button_1"))
    add(body, caption(t, "Maxi_PR_CodeCaption", "Код комнаты"))
    code_field = t.w("Border_11")
    enter = t.w("SaveButton")
    enter.remove_from_parent()                      # the enter button leaves the field row
    field_border(code_field)
    editable_text(t.w("PrivateRoomCode"), hint="Код комнаты, 8 цифр")
    size_box = t.w("SizeBox_4"); size_box.clear_width_override()
    vslot(size_box.slot, v=V_CENTER, fill=True)
    add(body, sizebox(t, "Maxi_PR_CodeField", h=s(36), content=code_field), gap=s(8))
    primary(enter, t.w("TextBlock_234"), "Войти в комнату")
    add(body, sizebox(t, "Maxi_PR_EnterSize", h=s(36), content=enter))
    add(body, divider(t, "Maxi_PR_Divider"))
    create = t.w("Button_198")
    secondary(create, t.w("TextBlock_10"), "Создать комнату")
    add(body, sizebox(t, "Maxi_PR_CreateSize", h=s(32), content=create))

    # --- privateroom_panel (legacy «Create / Join a private battle»), shown after «Создать комнату»
    panel_canvas = t.w("CanvasPanel_6")             # animated by Show_PrivateRoom_Panel: keep, only make it full screen
    canvas(panel_canvas.slot, (0, 0, 1, 1), (0, 0, 0, 0))
    for n in ("Image_131", "Image_28", "Image_151"):
        t.w(n).set_visibility(VIS_COLLAPSED)
    title = t.new(unreal.TextBlock, "Maxi_PP_Title"); title.set_text("Приватная комната")
    close = t.w("Close")
    text(t.w("TextBlock_56"), T_GLYPH, WHITE, value="×", justify=JUST_CENTER)
    body2 = side_panel(t, "Maxi_PP", panel_canvas, title, close, glyph=False)
    join, create2 = t.w("join"), t.w("Create")
    primary(join, t.w("TextBlock_3"), "Войти в комнату")
    add(body2, sizebox(t, "Maxi_PP_JoinSize", h=s(36), content=join))
    secondary(create2, t.w("TextBlock_4"), "Создать комнату")
    add(body2, sizebox(t, "Maxi_PP_CreateSize", h=s(32), content=create2), gap=s(10))
    for n in ("Border_1", "HorizontalBox_0"):
        t.w(n).set_visibility(VIS_COLLAPSED)

    # --- UMaxiPanelGroup: one open panel at a time, its menu item highlighted
    group = t.new(unreal.MaxiPanelGroup, "Maxi_PanelGroup")
    group.set_editor_property("panel_names", ["MyProfile", "MySaveSystem", "MyPrivateRoom", "privateroom_panel"])
    group.set_editor_property("highlight_button_names", ["ProfileRoomButton", "SaveSytemButton", "PrivateRommButton", "PrivateRommButton"])
    group.set_visibility(VIS_HIT_INVISIBLE)
    canvas(root.add_child(group), (0, 0, 0, 0), (0, 0), size=(1, 1), align=(0, 0))
    return t

# ================================================================ WBP_Male_Female (Профиль)
def restyle_profile():
    t = Tree(PROFILE)
    root = t.w("CanvasPanel_64")
    for n in ("Image_78", "Button_83"):
        t.w(n).set_visibility(VIS_COLLAPSED)
    body = side_panel(t, "Maxi_PF", root, t.w("TextBlock_115"), t.w("Button"))

    add(body, t.w("TextBlock")); text(t.w("TextBlock"), T_CAPTION, SECONDARY, upper=True)
    row = t.new(unreal.HorizontalBox, "Maxi_PF_GenderRow")
    for i, (bname, lname) in enumerate((("Button_198", "TextBlock_0"), ("Button_1", "TextBlock_1"))):
        b = t.w(bname); tool(b, t.w(lname))
        vslot(row.add_child(sizebox(t, "Maxi_PF_Gender%d" % i, h=s(38), content=b)), left=0 if i == 0 else s(6), fill=True)
    add(body, row, gap=s(8))

    add(body, t.w("TextBlock_2")); text(t.w("TextBlock_2"), T_CAPTION, SECONDARY, upper=True)
    name_field = t.new(unreal.Border, "Maxi_PF_NameField")
    edit = t.w("EditableText_Name"); editable_text(edit)
    reparent(edit, name_field); field_border(name_field)
    add(body, sizebox(t, "Maxi_PF_NameSize", h=s(36), content=name_field), gap=s(8))

    ok = t.w("Button_2")
    style = primary(ok, t.w("TextBlock_3"))
    add(body, sizebox(t, "Maxi_PF_OkSize", h=s(36), content=ok))
    note = t.w("TextBlock_4")                        # shown/hidden by the graph: plain text, no plate
    text(note, T_NOTE, SECONDARY, justify=JUST_LEFT)
    add(body, note, gap=s(10))
    return t, style

def set_profile_style_var(t, style):
    # The graph re-applies the BP variable WidgetStyle to Button_2 when the name changes: give it the new style.
    cls = unreal.load_class(None, PROFILE + ".WBP_Male_Female_C")
    cdo = unreal.get_default_object(cls)
    try:
        cdo.set_editor_property("widget_style", style); log("profile WidgetStyle default set via Python"); return True
    except Exception as e:
        log("Python could not set WidgetStyle (%s); using C++ reflection" % e)
    if not unreal.MaxiUiDesignTools.set_button_style_property(cdo, "WidgetStyle", style):
        raise RuntimeError("cannot set WidgetStyle default")
    log("profile WidgetStyle default set via C++")
    return True

# ================================================================ WBP_SaveSystem (Мои проекты)
def restyle_saves():
    t = Tree(SAVES)
    root = t.w("CanvasPanel_32")
    for n in ("Image_0", "Image_1", "Image_4"):
        t.w(n).set_visibility(VIS_COLLAPSED)
    body = side_panel(t, "Maxi_SV", root, t.w("TextBlock_55"), t.w("BackButton"), max_h=s(810 - 68 - 16))
    text(t.w("TextBlock_55"), T_PANEL_TITLE, WHITE, value="Мои проекты", justify=JUST_LEFT)

    welcome = t.w("FirstTimeWelcomeMessage")         # C++ toggles this Border: it is the plate
    note_border(welcome)
    text(t.w("TextBlock_0"), T_NOTE, SECONDARY, value="Добро пожаловать! Создайте свой первый дизайн и нажмите «Сохранить», чтобы начать.",
         justify=JUST_LEFT, wrap=True)
    add(body, welcome)

    cap = t.w("TextBlock_192"); text(cap, T_CAPTION, SECONDARY, value="Сохранить проект", upper=True)
    add(body, cap)
    row_border = t.w("Border_11")
    row_border.set_brush(brush_none()); row_border.set_padding(M(0))
    row_border.get_child_at(0).slot.set_padding(M(0))
    inp = t.w("SaveNameInput")
    est = inp.get_editor_property("widget_style")
    field_brush = brush_round(WHITE, 0.08, s(11))
    for p in ("background_image_normal", "background_image_hovered", "background_image_focused"):
        est.set_editor_property(p, field_brush)
    est.set_editor_property("padding", M(s(12), s(9.5), s(12), s(9.5)))
    est.set_editor_property("foreground_color", sc(WHITE))
    est.set_editor_property("focused_foreground_color", sc(WHITE))
    ts = est.get_editor_property("text_style"); ts.set_editor_property("font", font(*T_FIELD)); ts.set_editor_property("color_and_opacity", sc(WHITE))
    est.set_editor_property("text_style", ts)
    inp.set_editor_property("widget_style", est)
    inp.set_editor_property("hint_text", "Введите имя сохранения...")
    sb4 = t.w("SizeBox_4"); sb4.clear_width_override(); sb4.set_height_override(s(36))
    vslot(sb4.slot, v=V_CENTER, fill=True)
    save = t.w("SaveButton")
    primary(save, t.w("TextBlock_234"), "Сохранить")
    save.get_child_at(0).slot.set_padding(M(s(16), 0, s(16), 0))
    save_size = sizebox(t, "Maxi_SV_SaveSize", h=s(36), content=save)
    vslot(t.w("HorizontalBox_3").add_child(save_size), left=s(8), v=V_CENTER)
    add(body, row_border, gap=s(8))

    # last save: caption + card inside LastSaveContainer (C++ toggles the container)
    last = t.w("LastSaveContainer")
    cap2 = t.w("TextBlock_190"); text(cap2, T_CAPTION, SECONDARY, upper=True)
    card = t.new(unreal.Border, "Maxi_SV_LastCard")
    card.set_brush(brush_round(WHITE, 0.06, s(12), 0.14, outline_w=1.0)); card.set_padding(M(s(8)))
    card_row = t.new(unreal.HorizontalBox, "Maxi_SV_LastRow"); card.set_content(card_row)
    thumb = t.w("LastSaveThumbnail")
    thumb.set_brush(brush_round(WHITE, 1.0, s(8), base=thumb.get_editor_property("brush")))
    vslot(card_row.add_child(sizebox(t, "Maxi_SV_LastThumb", w=s(56), h=s(56), content=thumb)), v=V_CENTER)
    texts = t.new(unreal.VerticalBox, "Maxi_SV_LastTexts")
    name_tb, date_tb = t.w("LastSaveName"), t.w("LastSaveDate")
    text(name_tb, T_NAME, WHITE, justify=JUST_LEFT); text(date_tb, T_META, SECONDARY, justify=JUST_LEFT)
    vslot(reparent(name_tb, texts)); vslot(reparent(date_tb, texts), top=s(1))
    vslot(card_row.add_child(texts), left=s(12), v=V_CENTER, fill=True)
    load = t.w("LastSaveLoadButton")
    secondary(load, t.w("TextBlock"), "Загрузить")
    load.get_child_at(0).slot.set_padding(M(s(10), 0, s(10), 0))
    vslot(card_row.add_child(sizebox(t, "Maxi_SV_LoadSize", h=s(32), content=load)), left=s(12), v=V_CENTER)
    t.w("VerticalBox_135").set_visibility(VIS_COLLAPSED)
    vslot(reparent(cap2, last))
    vslot(last.add_child(card), top=s(8))
    add(body, last)

    # history: caption + scroll inside SaveHistoryContainer (C++ toggles the Border)
    hist = t.w("SaveHistoryContainer")
    hist.set_brush(brush_none()); hist.set_padding(M(0))
    scroll = t.w("SaveHistoryScrollBox")
    scroll.set_editor_property("scroll_bar_visibility", VIS_COLLAPSED)
    col = t.new(unreal.VerticalBox, "Maxi_SV_HistoryCol")
    cap3 = t.w("TextBlock_193"); text(cap3, T_CAPTION, SECONDARY, upper=True)
    vslot(reparent(cap3, col))
    scroll.remove_from_parent()
    vslot(col.add_child(scroll), top=s(8), fill=True)
    hist.set_content(col)
    hist.get_child_at(0).slot.set_padding(M(0))
    add(body, hist, fill=True)
    return t

def set_save_grid_defaults():
    cls = unreal.load_class(None, SAVES + ".WBP_SaveSystem_C")
    cdo = unreal.get_default_object(cls)
    for prop, val in (("save_history_columns", 2), ("save_history_item_width", 170.0),
                      ("save_history_item_height", 184.0), ("save_history_slot_padding", s(4))):
        cdo.set_editor_property(prop, val)
    log("save grid defaults set")

# ================================================================ WBP_SaveHistoryItem (card of the builder catalog)
def restyle_save_item():
    t = Tree(SAVE_ITEM)
    root = t.w("CanvasPanel_54")
    card = t.new(unreal.Border, "Maxi_SI_Card")
    card.set_brush(brush_round(WHITE, 0.06, s(13), 0.14, outline_w=1.0)); card.set_padding(M(s(6), s(6), s(6), s(9)))
    col = t.new(unreal.VerticalBox, "Maxi_SI_Col"); card.set_content(col)
    thumb = t.w("ThumbnailImage")
    thumb.set_brush(brush_round(WHITE, 1.0, s(9), base=thumb.get_editor_property("brush")))
    vslot(col.add_child(sizebox(t, "Maxi_SI_Thumb", h=s(64), content=thumb)))
    name_tb, date_tb = t.w("SaveNameText"), t.w("SaveDateText")
    text(name_tb, T_CARD_NAME, WHITE, justify=JUST_LEFT); text(date_tb, T_CARD_META, SECONDARY, justify=JUST_LEFT)
    vslot(reparent(name_tb, col), top=s(6), left=s(3), right=s(3))
    vslot(reparent(date_tb, col), top=s(2), left=s(3), right=s(3))
    row = t.new(unreal.HorizontalBox, "Maxi_SI_Row")
    load, dele = t.w("LoadButton"), t.w("DeleteButton")
    link(load, t.w("TextBlock_3"), "Загрузить", spec=T_LINK_MEDIUM, color=WHITE)
    vslot(reparent(load, row), v=V_CENTER, fill=True, h=H_LEFT)
    st = dele.get_editor_property("widget_style")          # Normal = Recycle icon texture; keep it, change tint and size
    icon_normal, icon_hover = st.get_editor_property("normal"), st.get_editor_property("normal")
    icon_normal.set_editor_property("tint_color", sc(SECONDARY)); icon_hover.set_editor_property("tint_color", sc(WHITE))
    button_style(dele, sized(icon_normal, s(14), s(14)), sized(icon_hover, s(14), s(14)))
    vslot(reparent(dele, row), v=V_CENTER)
    vslot(col.add_child(row), top=s(6), left=s(3), right=s(3))
    canvas(root.add_child(card), (0, 0, 1, 1), (0, 0, 0, 0))
    return t

# ================================================================ run
def render_checks():
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    scene = lin("#5A5752")
    jobs = [(BURGER, "after_burger.png", [], []),
            (SEL, "after_menu_private.png", ["MyPrivateRoom"], []),
            (SEL, "after_menu_privateroom_panel.png", ["privateroom_panel"], []),
            (SEL, "after_menu_profile.png", ["MyProfile"], []),
            (SEL, "after_menu_saves.png", ["MySaveSystem"], []),
            (PROFILE, "after_profile.png", [], []),
            (SAVES, "after_saves.png", [], []),
            (SAVE_ITEM, "after_save_item.png", [], [])]
    for path, name, show, hide in jobs:
        cls = unreal.load_class(None, path + "." + path.rsplit("/", 1)[1] + "_C")
        size = unreal.IntPoint(170, 184) if path == SAVE_ITEM else unreal.IntPoint(1920, 1080)   # item: its real grid size
        ok = unreal.MaxiUiDesignTools.render_widget_to_png(world, cls, size, os.path.join(OUT, name), scene, show, hide)
        log("render %s: %s" % (name, ok))

SAVE_ASSETS = os.environ.get("MAXI_UI_SAVE", "1") == "1"
saved = False
try:
    trees = []
    for builder in (restyle_burger, restyle_selection, restyle_save_item):
        t = builder(); finish(t); trees.append(t)
    # The profile graph rebuilds Button_2's style from its own Normal brush on text change (Make/Break ButtonStyle),
    # so the new white brush survives; only the hover shade is lost after typing. Nothing else to set.
    t_profile, profile_style = restyle_profile(); finish(t_profile); trees.append(t_profile)
    t_saves = restyle_saves(); finish(t_saves); trees.append(t_saves)
    set_save_grid_defaults()
    if SAVE_ASSETS:
        for t in trees:
            log("save %s: %s" % (t.bp.get_name(), unreal.EditorAssetLibrary.save_loaded_asset(t.bp, False)))
        saved = True
    log("DONE (saved=%s)" % saved)
except Exception:
    log("FAILED\n" + traceback.format_exc())
finally:
    open(os.path.join(OUT, "apply_log.txt"), "w", encoding="utf-8").write("\n".join(LOG))

if "FAILED" not in "\n".join(LOG):
    try:
        render_checks()
    except Exception:
        log("RENDER FAILED\n" + traceback.format_exc())
    open(os.path.join(OUT, "apply_log.txt"), "w", encoding="utf-8").write("\n".join(LOG))

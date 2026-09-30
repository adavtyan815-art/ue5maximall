# Full UI redesign (after the pilot and phase 1): every remaining widget blueprint restyled to the handoff 2026-09-27
# design system. Visual only: widget names, graphs, bindings and animations are kept; layout is kept except where noted.
#  - Legacy side panels (room planner, colour catalog, configurator, AR modal): dense glass sidebar, round header buttons,
#    glass/primary buttons, white text, 1 px dividers, fields.
#  - Room planner state colours become white multipliers (idle 0.12 / selected 0.33) over a white base brush; icon buttons
#    get the icon as a child image; UMaxiStyleOverrides restyles the frames the planner code builds with a fixed brush.
#  - Account / social / template widgets: generic in-place pass (fonts, text colours, glass instead of white plates,
#    buttons, fields, white versions of dark icons, the template's full-screen picture hidden).
import os, sys, traceback
import unreal

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
OUT = os.path.join(HERE, "out")
os.makedirs(OUT, exist_ok=True)

for cname in ("MaxiStyleOverrides", "MaxiPanelGroup", "MaxiUiDesignTools", "CircularThrobber", "ProgressBar", "EditableTextBox", "TileView"):
    if getattr(unreal, cname, None) is None:
        raise RuntimeError("missing class " + cname)

from maxi_ui_lib import *   # noqa: E402

STYLES = "/Game/FirstPerson/UI/Styles"
asset_tools = unreal.AssetToolsHelpers.get_asset_tools()

# ------------------------------------------------------------------ white icons
ICON_SOURCES = {   # original texture name -> generated white file
    "46178_2": "T_Icon_46178_2_W", "AccountSettings": "T_Icon_AccountSettings_W", "Back": "T_Icon_Back_W",
    "fluent_delete-32-regular__1_": "T_Icon_fluent_delete_32_regular__1_W", "Group_1171275103__1_": "T_Icon_Group_1171275103__1_W",
    "Mask_group__3__1": "T_Icon_Mask_group__3__1_W", "Mask_group__3__2": "T_Icon_Mask_group__3__2_W",
    "Mask_group__4__2": "T_Icon_Mask_group__4__2_W", "Mask_group__4__3": "T_Icon_Mask_group__4__3_W",
}
WHITE_ICONS = {}
def import_icons():
    for src, name in ICON_SOURCES.items():
        t = unreal.AssetImportTask()
        t.set_editor_property("filename", os.path.join(HERE, "icons", name + ".png"))
        t.set_editor_property("destination_path", STYLES + "/Icons"); t.set_editor_property("destination_name", name)
        t.set_editor_property("automated", True); t.set_editor_property("replace_existing", True); t.set_editor_property("save", False)
        asset_tools.import_asset_tasks([t])
        tex = unreal.load_asset(STYLES + "/Icons/" + name)
        tex.set_editor_property("compression_settings", E(unreal.TextureCompressionSettings, "TC_EDITOR_ICON"))
        tex.set_editor_property("lod_group", E(unreal.TextureGroup, "TEXTUREGROUP_UI"))
        tex.set_editor_property("mip_gen_settings", E(unreal.TextureMipGenSettings, "TMGS_NO_MIPMAPS"))
        WHITE_ICONS[src] = tex
    log("white icons: %d" % len(WHITE_ICONS))

def res_name(brush):
    try:
        r = brush.get_editor_property("resource_object")
        return r.get_name() if r else None
    except Exception:
        return None

def white_icon_brush(brush, size=None, hexstr=WHITE):
    """Same brush with a white version of a dark glyph texture (if there is one) and a tint."""
    name = res_name(brush)
    if name in WHITE_ICONS:
        brush.set_editor_property("resource_object", WHITE_ICONS[name])
    brush.set_editor_property("tint_color", sc(hexstr))
    brush.set_editor_property("draw_as", DRAW_IMAGE)
    return sized(brush, size[0], size[1]) if size else brush

def icon_brush_from(tex_name, size):
    b = unreal.SlateBrush()
    b.set_editor_property("resource_object", WHITE_ICONS[tex_name])
    b.set_editor_property("draw_as", DRAW_IMAGE)
    b.set_editor_property("tint_color", sc(WHITE))
    return sized(b, size, size)

# ------------------------------------------------------------------ colour helpers for the generic pass
def lum_of(color):
    try:
        c = color.get_editor_property("specified_color")
        if str(color.get_editor_property("color_use_rule")).endswith("FOREGROUND"):
            return None
    except Exception:
        c = color
    return 0.2126 * c.r + 0.7152 * c.g + 0.0722 * c.b, c.a

def sat_of(color):
    try: c = color.get_editor_property("specified_color")
    except Exception: c = color
    return max(c.r, c.g, c.b) - min(c.r, c.g, c.b)

PRIMARY_WORDS = {"войти", "подтвердить", "сохранить", "готово", "submit", "accept", "принять", "утвердить", "хорошо",
                 "ok", "изменить", "ready", "готов", "отправить", "применить", "установить длину"}

def first_text(w):
    try:
        for c in w.get_all_children():
            if isinstance(c, unreal.TextBlock): return c
            ft = first_text(c) if isinstance(c, unreal.PanelWidget) else None
            if ft: return ft
    except Exception:
        pass
    return None

def map_font(tb, force_face=None, force_size=None):
    f = tb.get_editor_property("font")
    old, face = f.size, str(f.typeface_font_name)
    size = force_size or (17 if old >= 24 else 15 if old >= 20 else 13.5 if old >= 16 else 13 if old >= 13 else 12 if old >= 11 else 10.5)
    if force_face: new_face = force_face
    elif face in ("Bold", "SemiBold", "Black", "Heavy") or old >= 16: new_face = "SemiBold" if size >= 15 else "Medium"
    else: new_face = "Regular"
    tb.set_font(font(size, new_face, -13 if size >= 15 else 0))

def generic_text(tb, on_primary=False):
    map_font(tb)
    col = tb.get_editor_property("color_and_opacity")
    l = lum_of(col)
    if on_primary:
        tb.set_color_and_opacity(sc(INK)); return
    if l is None:
        return                                           # foreground-driven: follows the button
    lum, a = l
    if sat_of(col) > 0.35 and a > 0.5:
        return                                           # a status colour (error red, online green): keep
    tb.set_color_and_opacity(sc(SECONDARY) if 0.18 < lum < 0.55 else sc(WHITE))

def generic_button(t, b):
    st = b.get_editor_property("widget_style")
    normal = st.get_editor_property("normal")
    draw = str(normal.get_editor_property("draw_as"))
    rn = res_name(normal)
    child = b.get_child_at(0)
    if draw.endswith("NO_DRAW_TYPE") and rn is None:
        if child is not None and first_text(b) is not None:     # a text link
            link(b); map_font(first_text(b), force_face="Medium")
        return "invisible"
    if rn is not None and child is None:
        # icon drawn by the brush itself: white version if dark, glass hover
        n = white_icon_brush(st.get_editor_property("normal"))
        h = white_icon_brush(st.get_editor_property("hovered"))
        button_style(b, n, h)
        return "icon"
    label = first_text(b)
    text = str(label.get_text()).strip().lower() if label else ""
    pad = M(s(10), s(3), s(10), s(3))
    if text in PRIMARY_WORDS or any(text.startswith(w + " ") for w in ("войти", "подтвердить", "сохранить")):
        primary(b); content_slot(b, padding=pad)
        for tb in _texts_under(b): generic_text(tb, on_primary=True)
        return "primary"
    secondary(b); content_slot(b, padding=pad)
    for tb in _texts_under(b):
        generic_text(tb); tb.set_color_and_opacity(sc(WHITE))
    return "secondary"

def _texts_under(w):
    out = []
    try:
        for c in w.get_all_children():
            if isinstance(c, unreal.TextBlock): out.append(c)
            elif isinstance(c, unreal.PanelWidget): out += _texts_under(c)
    except Exception:
        pass
    return out

def glass_brush(radius=s(14), a=0.72, hexstr=GLASS_HEX):
    return brush_round(hexstr, a, radius, 0.14, outline_w=1.0)

def field_box(box):
    """EditableTextBox: field look (white 8 %, R11), white text."""
    est = box.get_editor_property("widget_style")
    fb = brush_round(WHITE, 0.08, s(11))
    for p in ("background_image_normal", "background_image_hovered", "background_image_focused"):
        est.set_editor_property(p, fb)
    try: est.set_editor_property("background_image_read_only", brush_round(WHITE, 0.04, s(11)))
    except Exception: pass
    est.set_editor_property("padding", M(s(12), s(8), s(12), s(8)))
    est.set_editor_property("foreground_color", sc(WHITE)); est.set_editor_property("focused_foreground_color", sc(WHITE))
    ts = est.get_editor_property("text_style"); ts.set_editor_property("font", font(*T_FIELD)); ts.set_editor_property("color_and_opacity", sc(WHITE))
    est.set_editor_property("text_style", ts)
    box.set_editor_property("widget_style", est)

def is_fill(w):
    """A plate that fills its canvas (dialog / card background) rather than a small indicator."""
    s_ = w.get_editor_property("slot")
    if not isinstance(s_, unreal.CanvasPanelSlot): return False
    L = s_.get_layout(); a = L.anchors; o = L.offsets
    if a.minimum.x == 0 and a.minimum.y == 0 and a.maximum.x == 1 and a.maximum.y == 1: return True
    return a.minimum.x == a.maximum.x and a.minimum.y == a.maximum.y and o.right >= 150 and o.bottom >= 150

TEMPLATE_PICTURES = {"IMG_Multiplayscape"}   # full-screen picture of the template: hidden (the scene is not covered)

def auto_restyle(t, skip=()):
    """Generic in-place pass over every widget of the blueprint."""
    counts = {}
    ws = [w for w in unreal.ObjectIterator(unreal.Widget) if w.get_path_name().startswith(t.prefix + ".")]
    buttons = [w for w in ws if isinstance(w, unreal.Button)]
    in_button = set()
    for b in buttons:
        for tb in _texts_under(b): in_button.add(tb.get_name())
    for w in ws:
        n = w.get_name()
        if n in skip or n.startswith("Maxi_"): continue
        kind = None
        try:
            if isinstance(w, unreal.Button):
                kind = "button:" + generic_button(t, w)
            elif isinstance(w, unreal.TextBlock):
                if n not in in_button: generic_text(w); kind = "text"
            elif isinstance(w, unreal.EditableTextBox):
                field_box(w); kind = "field"
            elif isinstance(w, unreal.EditableText):
                editable_text(w); kind = "edit"
            elif isinstance(w, unreal.Border):
                b = w.get_editor_property("background"); draw = str(b.get_editor_property("draw_as"))
                if res_name(b) is None and not draw.endswith("NO_DRAW_TYPE"):
                    l = lum_of(b.get_editor_property("tint_color"))
                    if l and l[1] > 0.05:
                        if l[0] < 0.03 and l[1] < 0.85: w.set_brush(brush_solid("#000000", 0.45))   # scrim
                        else: w.set_brush(glass_brush())
                        kind = "border"
            elif isinstance(w, unreal.Image):
                b = w.get_editor_property("brush"); rn = res_name(b)
                if rn in TEMPLATE_PICTURES:
                    w.set_visibility(VIS_COLLAPSED); kind = "picture-hidden"
                elif rn in WHITE_ICONS:
                    w.set_brush(white_icon_brush(b)); kind = "icon"
                elif rn is None and not str(b.get_editor_property("draw_as")).endswith("NO_DRAW_TYPE") \
                        and (sat_of(b.get_editor_property("tint_color")) <= 0.35 or is_fill(w)):   # small coloured indicators stay
                    l = lum_of(b.get_editor_property("tint_color"))
                    if l and l[1] > 0.05:
                        if "line" in n.lower() or "divider" in n.lower() or "separator" in n.lower():
                            w.set_brush(sized(brush_solid(WHITE, 0.14), 1, 1)); kind = "divider"
                        elif l[0] < 0.03 and l[1] < 0.85:
                            w.set_brush(brush_round("#000000", 0.45, 0)); kind = "scrim"
                        else:
                            w.set_brush(glass_brush()); kind = "plate"
            elif isinstance(w, unreal.CircularThrobber):
                img = w.get_editor_property("image"); img.set_editor_property("tint_color", sc(WHITE)); w.set_editor_property("image", img); kind = "throbber"
            elif isinstance(w, unreal.ProgressBar):
                w.set_fill_color_and_opacity(lin(WHITE)); kind = "progress"
            elif isinstance(w, unreal.BackgroundBlur):
                w.set_blur_strength(BLUR); kind = "blur"
        except Exception as e:
            log("  ! %s.%s: %s" % (t.bp.get_name(), n, e))
        if kind: counts[kind] = counts.get(kind, 0) + 1
    log("%s generic: %s" % (t.bp.get_name(), counts))

def leftover_fonts(t):
    """Texts not handled explicitly: F_MaxiMall with the size map; dark ink becomes white."""
    n = 0
    for w in unreal.ObjectIterator(unreal.TextBlock):
        if not w.get_path_name().startswith(t.prefix + "."): continue
        f = w.get_editor_property("font")
        if f.font_object == FONT: continue
        generic_text(w); n += 1
    if n: log("%s leftover texts: %d" % (t.bp.get_name(), n))

# ------------------------------------------------------------------ legacy side panel (planner, colour catalog, configurator, AR)
def sidebar_background(img):
    """The 426 px full-height white strip becomes a dense glass sidebar (README: denser glass over the light 2D plan)."""
    b = brush_round("#181818", 0.90, s(20), 0.14, outline_w=1.0)
    o = b.get_editor_property("outline_settings")
    o.set_editor_property("corner_radii", unreal.Vector4(0, s(20), s(20), 0)); b.set_editor_property("outline_settings", o)
    img.set_brush(b)

def round_icon_button(t, btn, icon_tex, glyph=None, size=s(16), name=None):
    """Header button of the builder: 32 px circle (white 10 %, hover 18 %) with a white icon or a glyph."""
    button_style(btn, brush_round(WHITE, 0.10, s(16), pill=True), brush_round(WHITE, 0.18, s(16), pill=True))
    if btn.get_child_at(0) is None:
        if icon_tex:
            img = t.new(unreal.Image, name or ("Maxi_%s_Icon" % btn.get_name()))
            img.set_brush(icon_brush_from(icon_tex, size)); btn.add_child(img)
        elif glyph:
            g = t.new(unreal.TextBlock, name or ("Maxi_%s_Glyph" % btn.get_name()))
            text(g, T_GLYPH, WHITE, value=glyph, justify=JUST_CENTER); btn.add_child(g)
    content_slot(btn)

def header_title(tb):
    text(tb, T_PANEL_TITLE, WHITE, justify=JUST_LEFT)

def divider_image(img):
    img.set_brush(sized(brush_solid(WHITE, 0.14), 1, 1))
    img.set_color_and_opacity(unreal.LinearColor(1, 1, 1, 1))

# multiplier scheme for buttons whose colour the code sets per state
BASE_N, BASE_H = 0.667, 1.0
def state_button(btn, radius=s(8), label_spec=T_PRIMARY):
    button_style(btn, brush_round(WHITE, BASE_N, radius), brush_round(WHITE, BASE_H, radius))
    btn.set_background_color(unreal.LinearColor(1, 1, 1, 0.12))
    content_slot(btn, padding=M(s(10), s(4), s(10), s(4)))
    for tb in _texts_under(btn): text(tb, label_spec, WHITE, justify=JUST_CENTER)

def state_icon_button(t, btn, size=s(18)):
    """Icon drawn by the brush -> child image (not dimmed by the state colour) + base brush for the state colour."""
    st = btn.get_editor_property("widget_style")
    rn = res_name(st.get_editor_property("normal"))
    button_style(btn, brush_round(WHITE, BASE_N, s(8)), brush_round(WHITE, BASE_H, s(8)))
    btn.set_background_color(unreal.LinearColor(1, 1, 1, 0.12))
    if btn.get_child_at(0) is None and rn in WHITE_ICONS:
        img = t.new(unreal.Image, "Maxi_%s_Icon" % btn.get_name()); img.set_brush(icon_brush_from(rn, size)); btn.add_child(img)
    content_slot(btn, padding=M(s(6)))

def set_cdo(bp_path, values):
    cls = unreal.load_class(None, bp_path + "." + bp_path.rsplit("/", 1)[1] + "_C")
    cdo = unreal.get_default_object(cls)
    done = []
    for k, v in values.items():
        try: cdo.set_editor_property(k, v); done.append(k)
        except Exception as e: log("  ! CDO %s.%s: %s" % (bp_path.rsplit("/", 1)[1], k, e))
    log("%s CDO: %s" % (bp_path.rsplit("/", 1)[1], ", ".join(done)))

def mult(a): return unreal.LinearColor(1, 1, 1, a)

# ================================================================ room planner
PLANNER = "/Game/RoomPlanner/WBP_RoomPlannerWidget"
def restyle_planner():
    t = Tree(PLANNER)
    root = t.w("CanvasPanel_31")
    img0 = t.w("Image_0"); sidebar_background(img0)
    canvas(img0.slot, (0, 0, 0, 1), (0, 0, 426, 0))
    # header
    icon = t.w("Image_138"); icon.set_brush(white_icon_brush(icon.get_editor_property("brush"), (s(20), s(20)), SECONDARY))
    header_title(t.w("TextBlock_0"))
    round_icon_button(t, t.w("BtnHelp"), None, glyph="?")
    round_icon_button(t, t.w("BackButton"), "Back", size=s(12))
    for n in ("Image_line_1", "Image_line_2", "Image_line_3", "Image_line_4", "Image_line_5", "Image_line_6", "Image_line_7"):
        divider_image(t.w(n))
    seg = brush_round(WHITE, 0.08, s(11))
    for n in ("ViewRow", "ToolsRow"):
        b = t.w(n); b.set_brush(seg); b.set_padding(M(s(3)))
    # state-coloured text buttons
    for n in ("Btn_2DView", "Btn_3DView", "BtnDrawWallTool", "BtnSelectTool", "BtnCatalogInterior", "BtnCatalogCabinets",
              "BtnFinishPaint", "BtnClearFinish", "BtnPresetRoom", "BtnAddDoor", "BtnAddWindow", "BtnRotateRight", "BtnRotateLeft",
              "BtnCancelPlacement", "BtnApplyProperties"):
        state_button(t.w(n))
    # the single white button of the panel (README: «Сохранить»)
    save = t.w("BtnSave"); primary(save); content_slot(save, padding=M(s(12), s(4), s(12), s(4)))
    text(t.w("TextBlock_3"), T_PRIMARY, INK, justify=JUST_CENTER)
    # icon buttons
    for n in ("BtnClearLayout", "BtnDeleteTool", "BtnSwingLeft", "BtnSwingRight", "BtnSwingInward", "BtnSwingOutward"):
        state_icon_button(t, t.w(n))
    # fields and labels
    for n in ("EditableTxtProp1", "EditableTxtProp2", "EditableTxtProp3", "EditableTxtOpeningHeight", "EditableTxtOpeningWidth",
              "EditableTxtOpeningWidth_1", "EditableTxtOpeningSillHeight", "EditableTxtOpeningHeight_1"):
        field_box(t.w(n))
    for n in ("Border_wall_size", "Border_AddDoor", "Border_AddWindow"):
        t.w(n).set_brush(brush_none())
    for n in ("LblWallSize", "LblDoor", "LblWindow", "LblSwing", "LblRotate", "TOTALFLOORAREA", "TOTALWALLPERIMETER"):
        text(t.w(n), T_META, SECONDARY)
    text(t.w("TxtSelectionTitle"), T_NAME, WHITE)
    for n in ("TxtFloorArea", "TxtPerimeter"):
        text(t.w(n), (12.5, "SemiBold", 0), WHITE)
    for n in ("TxtFinishAreas", "TxtFinishInfo", "TxtOperationMessage"):
        text(t.w(n), T_NOTE, SECONDARY)
    for n in ("TxtSelectedDims", "TxtDistLeft", "TxtDistRight", "TxtDistFloor", "TxtDistNeighbor"):
        text(t.w(n), (12, "SemiBold", 0), WHITE)
    # guidance hint (glass pill) and its close button
    hint = t.w("Border_0"); hint.set_brush(glass_brush(s(14), 0.72))
    text(t.w("TxtGuidanceHint"), (13, "Regular", 0), WHITE)
    hide = t.w("BtnHideHelp"); round_icon_button(t, hide, None)
    text(t.w("TextBlock_160"), T_GLYPH, WHITE, value="×", justify=JUST_CENTER)
    # live length chip (the code copies its background to the message chip)
    chip = t.w("LiveLengthPanel"); chip.set_brush(brush_round(GLASS_HEX, 0.8, s(8), 0.14, outline_w=1.0))
    text(t.w("TxtLiveLength"), (12, "SemiBold", 0), WHITE)
    # frames the planner code builds with a fixed white brush + tile catalog colours
    ov = t.new(unreal.MaxiStyleOverrides, "Maxi_StyleOverrides")
    ov.set_editor_property("frame_names", ["CategoryStrip", "ToolsRow", "ViewSegment"])
    ov.set_editor_property("frame_brush", seg)
    ov.set_editor_property("frame_padding", M(s(3)))
    ov.set_editor_property("defaults_class_path", "/Script/awsTutorial.PlannerTileCatalogWidget")
    ov.set_editor_property("default_colors", {
        "PanelColor": lin("#181818", 0.90), "TextColor": lin(WHITE), "SecondaryTextColor": lin(SECONDARY),
        "DividerColor": lin(WHITE, 0.14), "ButtonColor": lin(WHITE, 0.12), "CardOutlineColor": lin(WHITE, 0.14),
        "ActiveCardColor": lin(WHITE)})
    ov.set_visibility(VIS_HIT_INVISIBLE)
    canvas(root.add_child(ov), (0, 0, 0, 0), (0, 0), size=(1, 1))
    return t

def planner_cdo():
    set_cdo(PLANNER, {
        "active_tab_color": mult(0.33), "inactive_tab_color": mult(0.12), "active_tool_color": mult(0.33),
        "idle_control_color": mult(0.12), "outline_selected_navigation": False,
        "catalog_button_normal_color": sc(WHITE, 0.06), "catalog_button_hovered_color": sc(WHITE, 0.12),
        "catalog_button_pressed_color": sc(WHITE, 0.16), "catalog_text_color": sc(WHITE),
        "catalog_text_font": font(12, "Medium"),
        "dimension_text_color": lin(WHITE), "dimension_text_background_color": lin(GLASS_HEX, 0.8),
        "dimension_font": font(12, "SemiBold"), "dimension_line_color": lin(INK)})

# ================================================================ colour catalog
COLOR_CATALOG = "/Game/ColorCatalog/UI/WBP_ColorCatalog"
SWATCH = "/Game/ColorCatalog/UI/WBP_ColorSwatchItem"
def restyle_color_catalog():
    t = Tree(COLOR_CATALOG)
    img0 = t.w("Image_0"); sidebar_background(img0); canvas(img0.slot, (0, 0, 0, 1), (0, 0, 426, 0))
    header_title(t.w("TextBlock_0"))
    back = t.w("Button_Back"); round_icon_button(t, back, "Back", size=s(12))
    canvas(back.slot, (0, 0, 0, 0), (360, 40), size=(s(32), s(32)))
    for n in ("Image", "Image_140", "Image_1", "Image_2"):
        divider_image(t.w(n))
    b = t.w("Border_270"); b.set_brush(brush_round(WHITE, 0.08, s(11))); b.set_padding(M(s(3)))
    for n in ("Button_RAL", "Button_NCS", "Button_CategoryAll"):
        state_button(t.w(n), label_spec=T_PRIMARY)
    for n in ("Button_CategoryRed", "Button_CategoryOrange", "Button_CategoryYellow", "Button_CategoryBlue", "Button_CategoryGreen",
              "Button_CategoryViolet", "Button_CategoryBrown", "Button_CategoryBeige", "Button_CategoryNeutral"):
        btn = t.w(n); st = btn.get_editor_property("widget_style")
        tint = st.get_editor_property("normal").get_editor_property("tint_color").get_editor_property("specified_color")
        fill = brush_round(WHITE, 1.0, s(11)); fill.set_editor_property("tint_color", unreal.SlateColor(specified_color=tint))
        hov = brush_round(WHITE, 1.0, s(11), 1.0, outline_w=2.0); hov.set_editor_property("tint_color", unreal.SlateColor(specified_color=tint))
        button_style(btn, fill, hov)
    search_field(t)
    text(t.w("Text_ActiveColor"), T_CARD_TITLE, WHITE)
    icon = t.w("Image_138"); icon.set_brush(sized(icon.get_editor_property("brush"), s(20), s(20)))
    return t

def search_field(t):
    """The search row was 30 px high: too low for the field padding, the hint was clipped. 40 px, centred between the dividers."""
    field_box(t.w("EditableText_Search"))
    est = t.w("EditableText_Search").get_editor_property("widget_style")
    est.set_editor_property("padding", M(s(12), s(6), s(12), s(6)))
    t.w("EditableText_Search").set_editor_property("widget_style", est)
    canvas(t.w("HorizontalBox_279").slot, (0, 0, 0, 0), (20, 327.5), size=(381, 40))

def restyle_swatch():
    t = Tree(SWATCH)
    sel = t.w("Border_Selection"); sel.set_brush(brush_round(WHITE, 0.0, s(11), 0.14, outline_w=1.0))
    box = t.w("Image_ColorBox"); box.set_brush(brush_round(WHITE, 1.0, s(9)))
    text(t.w("Text_ColorCode"), T_CARD_META, SECONDARY, justify=JUST_CENTER)
    return t

def color_catalog_cdo():
    set_cdo(COLOR_CATALOG, {"active_tab_color": mult(0.33), "inactive_tab_color": mult(0.12),
                            "active_category_all_color": mult(0.33), "inactive_category_all_color": mult(0.12)})

# ================================================================ configurator (WBP_PreviewWindow)
PREVIEW = "/Game/WBP_PreviewWindow"
def restyle_preview():
    t = Tree(PREVIEW)
    img0 = t.w("Image_0"); sidebar_background(img0)
    close = t.w("Btn_CloseUI"); round_icon_button(t, close, "Back", size=s(12))
    canvas(close.slot, (0, 0, 0, 0), (360, 36), size=(s(32), s(32)))
    header_title(t.w("TextBlock_55"))
    t.w("Image_1").set_brush(sized(t.w("Image_1").get_editor_property("brush"), s(24), s(24)))
    text(t.w("Txt_ProductName_1"), T_CARD_TITLE, WHITE); text(t.w("Txt_SKU"), T_META, SECONDARY)
    text(t.w("TextBlock_107"), T_META, SECONDARY); text(t.w("Txt_SelectedMeshName"), T_NAME, WHITE)
    for n in ("Btn_ARFullScene", "Btn_ARSelected", "Btn_Viewmode", "Txt_BtnURL", "Btn_ColorCatalog"):
        btn = t.w(n); secondary(btn); content_slot(btn, padding=M(s(12), s(8), s(12), s(8)))
        for tb in _texts_under(btn): text(tb, T_SECONDARY, WHITE, justify=JUST_CENTER)
    text(t.w("Txt_Warning"), T_CARD_TITLE, WHITE, justify=JUST_CENTER)
    return t

def preview_cdo():
    values = {}
    for prefix in ("size", "color"):
        values.update({"%s_button_normal_color" % prefix: sc(WHITE, 0.08), "%s_button_hovered_color" % prefix: sc(WHITE, 0.16),
                       "%s_button_pressed_color" % prefix: sc(WHITE, 0.16), "active_%s_button_normal_color" % prefix: sc(WHITE, 1.0),
                       "active_%s_button_hovered_color" % prefix: sc("#E6E6E6"), "active_%s_button_pressed_color" % prefix: sc("#E6E6E6")})
    # only the size buttons have a label; colour buttons are thumbnails
    values.update({"size_text_color": sc(WHITE), "active_size_text_color": sc(INK), "size_text_font": font(12.5, "Medium")})
    set_cdo(PREVIEW, values)

# ================================================================ view mode overlay, AR modal, nameplate
VIEWMODE = "/Game/WBP_ViewmodeOverlay"
def restyle_viewmode():
    t = Tree(VIEWMODE)
    for n in ("Btn_ResetView", "Btn_Back"):
        btn = t.w(n); button_style(btn, GLASS(s(24), 0.62), GLASS_HOVER(s(24)))
        content_slot(btn, padding=M(s(16), s(10), s(16), s(10)))
        for tb in _texts_under(btn): text(tb, (13.5, "SemiBold", -13), WHITE, justify=JUST_CENTER)
    bar = t.w("Border_0"); bar.set_brush(brush_round(GLASS_HEX, 0.62, s(24), 0.14, outline_w=1.0, pill=True))
    bar.set_padding(M(s(14), s(8), s(14), s(8)))
    text(t.w("Txt_ControlsHint"), (12, "Regular", 0), SECONDARY); text(t.w("AutoRotateIndicator"), (12, "SemiBold", 0), WHITE)
    pm = t.w("Img_PivotMarker"); pm.set_brush(white_icon_brush(pm.get_editor_property("brush")))
    return t

AR = "/Game/WBP_ARExportModal"
def restyle_ar():
    t = Tree(AR)
    img0 = t.w("Image_0"); sidebar_background(img0); canvas(img0.slot, (0, 0, 0, 1), (0, 0, 426, 0))
    header_title(t.w("TextBlock_0"))
    close = t.w("Btn_CloseModal"); round_icon_button(t, close, "Back", size=s(12))
    canvas(close.slot, (0, 0, 0, 0), (360, 44), size=(s(32), s(32)))
    field_box(t.w("Txt_DirectURL"))
    divider_image(t.w("Image_418"))
    cp = t.w("Btn_CopyURL"); secondary(cp); content_slot(cp, padding=M(s(12), s(8), s(12), s(8)))
    text(t.w("TextBlock"), T_SECONDARY, WHITE, value="Копировать ссылку", justify=JUST_CENTER)
    text(t.w("Txt_Status"), T_NOTE, SECONDARY)
    pb = t.w("ProgressBar_Export"); pb.set_fill_color_and_opacity(lin(WHITE))
    st = pb.get_editor_property("widget_style")
    st.set_editor_property("background_image", brush_round(WHITE, 0.12, s(4))); st.set_editor_property("fill_image", brush_round(WHITE, 1.0, s(4)))
    pb.set_editor_property("widget_style", st)
    return t

NAMEPLATE = "/Game/WBP_NameOfCharacter"
def restyle_nameplate():
    t = Tree(NAMEPLATE)
    text(t.w("TextBlock_36"), (12, "SemiBold", 0), WHITE, justify=JUST_CENTER)   # world-space label: not scaled (README)
    return t

# ================================================================ account / social / template widgets: generic pass
GENERIC = [
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
def restyle_generic(path):
    t = Tree(path)
    auto_restyle(t)
    return t

# ================================================================ run
def render_checks(paths):
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    scene = lin("#5A5752")
    for p in paths:
        name = p.rsplit("/", 1)[1]
        cls = unreal.load_class(None, p + "." + name + "_C")
        size = unreal.IntPoint(96, 120) if name == "WBP_ColorSwatchItem" else unreal.IntPoint(1920, 1080)
        ok = unreal.MaxiUiDesignTools.render_widget_to_png(world, cls, size, os.path.join(OUT, "after_%s.png" % name), scene, [], [])
        log("render %s: %s" % (name, ok))

SAVE_ASSETS = os.environ.get("MAXI_UI_SAVE", "1") == "1"
ONLY = [x for x in os.environ.get("MAXI_UI_ONLY", "").split(",") if x]
saved = False
done_paths = []
try:
    import_icons()
    trees = []
    builders = [(PLANNER, restyle_planner), (COLOR_CATALOG, restyle_color_catalog), (SWATCH, restyle_swatch), (PREVIEW, restyle_preview),
                (VIEWMODE, restyle_viewmode), (AR, restyle_ar), (NAMEPLATE, restyle_nameplate)] + [(p, (lambda p=p: restyle_generic(p))) for p in GENERIC]
    for path, builder in builders:
        if ONLY and path.rsplit("/", 1)[1] not in ONLY:
            continue
        t = builder(); leftover_fonts(t)
        # class defaults BEFORE compiling: the compiled class copies only the properties that differ at compile time
        {PLANNER: planner_cdo, COLOR_CATALOG: color_catalog_cdo, PREVIEW: preview_cdo}.get(path, lambda: None)()
        finish(t); trees.append(t); done_paths.append(path)
    if SAVE_ASSETS:
        for tex in WHITE_ICONS.values():
            unreal.EditorAssetLibrary.save_loaded_asset(tex, False)
        for t in trees:
            log("save %s: %s" % (t.bp.get_name(), unreal.EditorAssetLibrary.save_loaded_asset(t.bp, False)))
        saved = True
    log("DONE (saved=%s, widgets=%d)" % (saved, len(trees)))
except Exception:
    log("FAILED\n" + traceback.format_exc())
finally:
    open(os.path.join(OUT, "apply_log.txt"), "w", encoding="utf-8").write("\n".join(LOG))

def review_user_settings():
    """Dry run only: bring each hidden page of the account widget into view (in memory), recompile, render."""
    path = GENERIC[0]
    t = Tree(path)
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    cls = unreal.load_class(None, path + ".BP_UserSettings_UI_C")
    scene = lin("#5A5752")
    def shot(name):
        unreal.BlueprintEditorLibrary.compile_blueprint(t.bp)
        unreal.MaxiUiDesignTools.render_widget_to_png(world, cls, unreal.IntPoint(1920, 1080), os.path.join(OUT, "review_%s.png" % name), scene, [], [])
    panel = t.w("UserSettingsPanel")
    canvas(panel.slot, (0, 0.5, 0, 0.5), (0, -541), size=(1922, 1081))
    sw = t.w("UserSettingsSwitcher")
    for i in range(sw.get_num_widgets()):
        sw.set_active_widget_index(i); shot("main_%d" % i)
    canvas(panel.slot, (-1, 0.5, 0, 0.5), (-2, -541, 4, 1081))
    sec = t.w("SecondarySettingSwitcher")
    t.w("SecondarySettingsPanel").set_render_opacity(1.0)          # faded in by an animation in the game
    for i in range(sec.get_num_widgets()):
        sec.set_active_widget_index(i); shot("secondary_%d" % i)
    t.w("SecondarySettingsPanel").set_render_opacity(0.0)
    rs = t.w("ReadyStateWidgetSwitcher")
    for i in range(rs.get_num_widgets()):
        rs.set_active_widget_index(i); shot("ready_%d" % i)
    log("review renders done")

if "FAILED" not in "\n".join(LOG):
    try:
        render_checks(done_paths)
        if not SAVE_ASSETS and GENERIC[0] in done_paths:
            review_user_settings()
    except Exception:
        log("RENDER FAILED\n" + traceback.format_exc())
    open(os.path.join(OUT, "apply_log.txt"), "w", encoding="utf-8").write("\n".join(LOG))

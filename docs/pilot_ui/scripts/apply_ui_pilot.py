# UI pilot (design handoff 2026-09-27): restyles BP_Online_Offline_Selection_UI (menu, variant A) and BP_UserLogin_UI
# (login card) per docs/pilot_ui/TZ_UMG_pilot_menu_login.md. Existing widgets keep their names, so the event graphs,
# bound events and widget animations keep working. Widget blueprints are saved only after both compile.
import os, traceback
import unreal

HERE = os.path.dirname(os.path.abspath(__file__))   # inputs: fonts/, T_UI_RadialGlow.png
OUT = os.path.join(HERE, "out")                      # outputs: apply_log.txt, render_*.png
os.makedirs(OUT, exist_ok=True)
LOG = []
def log(msg):
    LOG.append(str(msg)); unreal.log("[UIPILOT] " + str(msg))

STYLES = "/Game/FirstPerson/UI/Styles"
SEL_PATH = "/Game/FirstPerson/UI/CrowdTown/Online_Offline_Selection/BP_Online_Offline_Selection_UI"
LOGIN_PATH = "/Game/FirstPerson/UI/UserLogin/BP_UserLogin_UI"
K = 4.0 / 3.0                      # mock (1440) -> UMG (1920x1080)
def s(v): return round(v * K, 2)

# ---------------------------------------------------------------- enums / structs
def E(enum_cls, *names):
    for n in names:
        if hasattr(enum_cls, n):
            return getattr(enum_cls, n)
    raise ValueError("%s has none of %s: %s" % (enum_cls, names, [n for n in dir(enum_cls) if n.isupper()]))

H_FILL = E(unreal.HorizontalAlignment, "H_ALIGN_FILL", "HALIGN_FILL")
H_LEFT = E(unreal.HorizontalAlignment, "H_ALIGN_LEFT", "HALIGN_LEFT")
H_CENTER = E(unreal.HorizontalAlignment, "H_ALIGN_CENTER", "HALIGN_CENTER")
H_RIGHT = E(unreal.HorizontalAlignment, "H_ALIGN_RIGHT", "HALIGN_RIGHT")
V_FILL = E(unreal.VerticalAlignment, "V_ALIGN_FILL", "VALIGN_FILL")
V_CENTER = E(unreal.VerticalAlignment, "V_ALIGN_CENTER", "VALIGN_CENTER")
DRAW_NONE = E(unreal.SlateBrushDrawType, "NO_DRAW_TYPE", "NONE")
DRAW_IMAGE = E(unreal.SlateBrushDrawType, "IMAGE")
DRAW_ROUNDED = E(unreal.SlateBrushDrawType, "ROUNDED_BOX")
ROUND_FIXED = E(unreal.SlateBrushRoundingType, "FIXED_RADIUS")
ROUND_HALF = E(unreal.SlateBrushRoundingType, "HALF_HEIGHT_RADIUS")
SIZE_FILL = E(unreal.SlateSizeRule, "FILL")
SIZE_AUTO = E(unreal.SlateSizeRule, "AUTOMATIC", "AUTO")
JUST_LEFT = E(unreal.TextJustify, "LEFT")
JUST_CENTER = E(unreal.TextJustify, "CENTER")
TO_UPPER = E(unreal.TextTransformPolicy, "TO_UPPER")
VIS_COLLAPSED = unreal.SlateVisibility.COLLAPSED
VIS_SELF_HIT = unreal.SlateVisibility.SELF_HIT_TEST_INVISIBLE
FG_RULE = E(unreal.SlateColorStylingMode, "USE_COLOR_FOREGROUND")

def lin(hexstr, a=1.0):
    def c(v):
        v = v / 255.0
        return v / 12.92 if v <= 0.04045 else ((v + 0.055) / 1.055) ** 2.4
    h = hexstr.lstrip("#")
    return unreal.LinearColor(c(int(h[0:2], 16)), c(int(h[2:4], 16)), c(int(h[4:6], 16)), a)

def sc(hexstr, a=1.0):
    col = unreal.SlateColor()
    col.set_editor_property("specified_color", lin(hexstr, a))
    return col

def sc_fg():
    col = unreal.SlateColor()
    col.set_editor_property("color_use_rule", FG_RULE)
    return col

def M(l, t=None, r=None, b=None):
    if t is None: t = r = b = l
    return unreal.Margin(l, t, r, b)

def V2(x, y): return unreal.Vector2D(x, y)

def brush_none():
    b = unreal.SlateBrush(); b.set_editor_property("draw_as", DRAW_NONE); return b

def brush_solid(hexstr, a=1.0):
    b = unreal.SlateBrush()
    b.set_editor_property("draw_as", DRAW_IMAGE)
    b.set_editor_property("tint_color", sc(hexstr, a))
    return b

def sized(brush, w, h):
    return unreal.MaxiUiDesignTools.set_brush_image_size(brush, V2(w, h))

def brush_round(hexstr, a, radius, outline_a=0.0, outline_hex="#FFFFFF", outline_w=0.0, pill=False):
    b = unreal.SlateBrush()
    b.set_editor_property("draw_as", DRAW_ROUNDED)
    b.set_editor_property("tint_color", sc(hexstr, a))
    o = unreal.SlateBrushOutlineSettings()
    o.set_editor_property("corner_radii", unreal.Vector4(radius, radius, radius, radius))
    o.set_editor_property("rounding_type", ROUND_HALF if pill else ROUND_FIXED)
    o.set_editor_property("color", sc(outline_hex, outline_a))
    o.set_editor_property("width", outline_w)
    b.set_editor_property("outline_settings", o)
    return b

# ---------------------------------------------------------------- design tokens (TZ section 2)
GLASS = lambda r: brush_round("#262626", 0.62, r, 0.14, outline_w=1.0)
GLASS_HOVER = lambda r: brush_round("#FFFFFF", 0.12, r, 0.14, outline_w=1.0)
BLUR = 16.0
WHITE, SECONDARY, INK = "#FFFFFF", "#C4C4C4", "#0E0E0E"

# ---------------------------------------------------------------- preflight: every Python API used below must exist
_SLOT = ["set_padding", "set_horizontal_alignment", "set_vertical_alignment"]
NEEDED = {
    "PanelWidget": ["add_child", "get_child_at", "get_all_children"],
    "Widget": ["remove_from_parent", "get_parent", "set_visibility", "set_render_opacity"],
    "ContentWidget": ["set_content"],
    "BackgroundBlur": ["set_blur_strength", "set_apply_alpha_to_blur", "set_corner_radius", "set_padding"],
    "Border": ["set_brush", "set_padding", "set_horizontal_alignment", "set_vertical_alignment"],
    "SizeBox": ["set_width_override", "set_height_override"],
    "Image": ["set_brush"],
    "Button": ["set_color_and_opacity", "set_background_color"],
    "TextBlock": ["set_font", "set_color_and_opacity", "set_text", "set_auto_wrap_text"],
    "VerticalBoxSlot": _SLOT + ["set_size"],
    "HorizontalBoxSlot": _SLOT + ["set_size"],
    "ButtonSlot": _SLOT,
    "CanvasPanelSlot": ["set_anchors", "set_alignment", "set_auto_size", "set_position", "set_size", "set_offsets", "set_z_order"],
    "MaxiUiDesignTools": ["setup_composite_font", "register_missing_widget_guids", "render_widget_to_png", "set_brush_image_size"],
    "MaxiVisibilityToggleButton": [],
    "CircularThrobber": [],
    "UnrealEditorSubsystem": ["get_editor_world"],
}
_missing = []
for cname, methods in NEEDED.items():
    cls = getattr(unreal, cname, None)
    if cls is None:
        _missing.append(cname); continue
    _missing += ["%s.%s" % (cname, m) for m in methods if not hasattr(cls, m)]
# struct properties written below, checked on fresh instances
for sname, props in (("SlateBrush", ["draw_as", "tint_color", "outline_settings", "resource_object"]),
                     ("SlateBrushOutlineSettings", ["corner_radii", "rounding_type", "color", "width"]),
                     ("SlateColor", ["specified_color", "color_use_rule"]),
                     ("ButtonStyle", ["normal", "hovered", "pressed", "disabled", "normal_padding", "pressed_padding",
                                      "normal_foreground", "hovered_foreground", "pressed_foreground", "disabled_foreground"]),
                     ("EditableTextStyle", ["font", "color_and_opacity"]),
                     ("SlateFontInfo", ["font_object", "typeface_font_name", "size", "letter_spacing"])):
    inst = getattr(unreal, sname)()
    for p in props:
        try:
            inst.set_editor_property(p, inst.get_editor_property(p))
        except Exception as e:
            _missing.append("%s.%s (%s)" % (sname, p, e))
if _missing:
    raise RuntimeError("missing Python API: %s" % _missing)

# ---------------------------------------------------------------- assets: fonts, glow texture
asset_tools = unreal.AssetToolsHelpers.get_asset_tools()

def import_file(src, dest_dir, name):
    t = unreal.AssetImportTask()
    t.set_editor_property("filename", src)
    t.set_editor_property("destination_path", dest_dir)
    t.set_editor_property("destination_name", name)
    t.set_editor_property("automated", True)
    t.set_editor_property("replace_existing", True)
    t.set_editor_property("save", False)
    asset_tools.import_asset_tasks([t])
    a = unreal.load_asset(dest_dir + "/" + name)
    if not a:
        raise RuntimeError("import failed: " + src)
    return a

FACES = ["Regular", "Medium", "SemiBold"]
latin = [import_file(os.path.join(HERE, "fonts", "InstrumentSans-%s.ttf" % f), STYLES + "/Fonts", "FF_InstrumentSans_" + f) for f in FACES]
cyr = [import_file(os.path.join(HERE, "fonts", "Inter-%s.ttf" % f), STYLES + "/Fonts", "FF_Inter_" + f) for f in FACES]
log("font faces: %s" % [a.get_class().get_name() for a in latin + cyr])

FONT = unreal.load_asset(STYLES + "/Fonts/F_MaxiMall")
if not FONT:
    FONT = asset_tools.create_asset("F_MaxiMall", STYLES + "/Fonts", unreal.Font, unreal.FontFactory())
ok = unreal.MaxiUiDesignTools.setup_composite_font(FONT, FACES, latin, cyr,
                                                   [unreal.IntPoint(0x0400, 0x052F), unreal.IntPoint(0x2116, 0x2116)], "Cyrillic (Inter)")
log("F_MaxiMall composite font: %s" % ok)
if not ok:
    raise RuntimeError("SetupCompositeFont failed")

GLOW = import_file(os.path.join(HERE, "T_UI_RadialGlow.png"), STYLES + "/Textures", "T_UI_RadialGlow")
GLOW.set_editor_property("compression_settings", E(unreal.TextureCompressionSettings, "TC_EDITOR_ICON"))
GLOW.set_editor_property("lod_group", E(unreal.TextureGroup, "TEXTUREGROUP_UI"))
GLOW.set_editor_property("mip_gen_settings", E(unreal.TextureMipGenSettings, "TMGS_NO_MIPMAPS"))
GLOW.set_editor_property("srgb", True)

def font(size, face, spacing=0):
    f = unreal.SlateFontInfo()
    f.set_editor_property("font_object", FONT)
    f.set_editor_property("typeface_font_name", face)
    f.set_editor_property("size", size)
    f.set_editor_property("letter_spacing", spacing)
    return f

# typography (TZ section 2): UMG size = mock px, letter spacing = em * 1000 * 4/3
T_CAPTION = (10, "SemiBold", 107)
T_ITEM = (13.5, "Medium", -13)
T_CLOSE = (13.5, "Medium", 0)
T_TITLE = (15, "SemiBold", -20)
T_FIELD = (13, "Regular", 0)
T_LINK = (12, "Regular", 0)
T_PRIMARY = (13, "SemiBold", 0)
T_FOOT = (12, "Regular", 0)
T_SECONDARY = (12.5, "Medium", 0)

# ---------------------------------------------------------------- widget helpers
class Tree:
    def __init__(self, path):
        self.bp = unreal.load_asset(path)
        self.bp.modify()
        self.prefix = self.bp.get_path_name() + ":WidgetTree"
        self.tree = unreal.find_object(None, self.prefix)
        self.new_count = 0

    def w(self, name):
        obj = unreal.find_object(None, self.prefix + "." + name)
        if obj is None:
            raise RuntimeError("widget not found: " + name)
        return obj

    def new(self, cls, name):
        if unreal.find_object(None, self.prefix + "." + name) is not None:
            raise RuntimeError("name already used: " + name)
        self.new_count += 1
        return unreal.new_object(cls, outer=self.tree, name=name)

def text(tb, spec, color_hex=None, fg=False, value=None, justify=None, upper=False):
    size, face, spacing = spec
    tb.set_font(font(size, face, spacing))
    tb.set_color_and_opacity(sc_fg() if fg else sc(color_hex or WHITE))
    if value is not None:
        tb.set_text(value)
    if justify is not None:
        tb.set_editor_property("justification", justify)
    if upper:
        tb.set_editor_property("text_transform_policy", TO_UPPER)

def reparent(child, parent):
    child.remove_from_parent()
    return parent.add_child(child)

def vslot(slot, top=0.0, left=0.0, right=0.0, bottom=0.0, h=H_FILL, v=V_FILL, fill=False):
    slot.set_padding(M(left, top, right, bottom))
    slot.set_horizontal_alignment(h)
    slot.set_vertical_alignment(v)
    slot.set_size(unreal.SlateChildSize(value=1.0, size_rule=SIZE_FILL if fill else SIZE_AUTO))

def canvas(slot, anchors, pos, size=None, align=(0, 0), auto=False, z=None):
    slot.set_anchors(unreal.Anchors(minimum=V2(anchors[0], anchors[1]), maximum=V2(anchors[2], anchors[3])))
    slot.set_alignment(V2(*align))
    slot.set_auto_size(auto)
    if anchors[0] == anchors[2] and anchors[1] == anchors[3]:
        slot.set_position(V2(*pos))
        if size is not None:
            slot.set_size(V2(*size))
    else:
        slot.set_offsets(M(*pos) if isinstance(pos, tuple) and len(pos) == 4 else M(0))
    if z is not None:
        slot.set_z_order(z)

def button_style(btn, normal, hovered, pressed=None, fg=None, fg_hover=None, padding=M(0)):
    st = btn.get_editor_property("widget_style")
    st.set_editor_property("normal", normal)
    st.set_editor_property("hovered", hovered)
    st.set_editor_property("pressed", pressed if pressed is not None else hovered)
    st.set_editor_property("disabled", normal)
    st.set_editor_property("normal_padding", padding)
    st.set_editor_property("pressed_padding", padding)
    if fg is not None:
        st.set_editor_property("normal_foreground", fg)
        st.set_editor_property("hovered_foreground", fg_hover or fg)
        st.set_editor_property("pressed_foreground", fg_hover or fg)
        st.set_editor_property("disabled_foreground", sc(WHITE, 0.45))
    btn.set_editor_property("widget_style", st)
    # old buttons carry tints (e.g. blue links); the design colours come from the style and the text only
    btn.set_color_and_opacity(unreal.LinearColor(1, 1, 1, 1))
    btn.set_background_color(unreal.LinearColor(1, 1, 1, 1))

def content_slot(btn, h=H_CENTER, v=V_CENTER, padding=M(0)):
    c = btn.get_child_at(0)
    if c is not None:
        c.slot.set_padding(padding)
        c.slot.set_horizontal_alignment(h)
        c.slot.set_vertical_alignment(v)
    return c

def glass_blur(t, name, radius, content=None):
    blur = t.new(unreal.BackgroundBlur, name)
    blur.set_blur_strength(BLUR)
    blur.set_apply_alpha_to_blur(True)
    blur.set_corner_radius(unreal.Vector4(radius, radius, radius, radius))
    blur.set_padding(M(0))
    blur.set_editor_property("low_quality_fallback_brush", brush_none())
    if content is not None:
        blur.set_content(content)
    return blur

def divider(t, name):
    img = t.new(unreal.Image, name)
    img.set_brush(sized(brush_solid(WHITE, 0.14), 1, 1))
    return img

def sizebox(t, name, w=None, h=None, content=None):
    sb = t.new(unreal.SizeBox, name)
    if w is not None: sb.set_width_override(w)
    if h is not None: sb.set_height_override(h)
    if content is not None:
        reparent(content, sb) if content.get_parent() else sb.add_child(content)
    return sb

# ================================================================ BP_Online_Offline_Selection_UI (menu, variant A)
def restyle_menu():
    t = Tree(SEL_PATH)
    root, room = t.w("CanvasPanel_38"), t.w("MyMainRoom")

    # old white strip and item icons are not part of the design
    for n in ("Image_59", "Image_62", "Image_63", "Image_1", "Image_2", "Image_3", "Image_4", "Image_7"):
        t.w(n).set_visibility(VIS_COLLAPSED)

    # panel: SizeBox(314.7) > BackgroundBlur(R24) > Border(glass, padding 13.3) > VerticalBox
    lst = t.new(unreal.VerticalBox, "Maxi_MenuList")
    glass = t.new(unreal.Border, "Maxi_MenuGlass")
    glass.set_brush(GLASS(s(18))); glass.set_padding(M(s(10))); glass.set_content(lst)
    panel = sizebox(t, "Maxi_MenuSize", w=s(236))
    panel.add_child(glass_blur(t, "Maxi_MenuBlur", s(18), glass))
    canvas(room.add_child(panel), (0, 0, 0, 0), (0, 0), align=(0, 0), auto=True, z=1)
    canvas(room.slot, (0, 0, 0, 0), (s(16), s(68)), align=(0, 0), auto=True, z=0)

    def caption(name, value, first):
        tb = t.new(unreal.TextBlock, name)
        text(tb, T_CAPTION, SECONDARY, value=value, upper=True)
        vslot(lst.add_child(tb), top=s(8) if first else s(16), left=s(10), right=s(10), bottom=s(4))

    def row(name, btn_name, label_name, value=None, spec=T_ITEM):
        btn = t.w(btn_name)
        box = sizebox(t, name, h=s(36))
        reparent(btn, box)
        button_style(btn, brush_none(), brush_round(WHITE, 0.12, s(10)), fg=sc(WHITE), padding=M(s(10), 0, s(10), 0))
        content_slot(btn, h=H_LEFT, v=V_CENTER)
        text(t.w(label_name), spec, WHITE, value=value, justify=JUST_LEFT)
        return box

    groups = [
        ("Maxi_CapRooms", "Комнаты", [("Maxi_RowPrivate", "PrivateRommButton", "PrivateRoomText", None),
                                      ("Maxi_RowPublic", "PublicRoomButton", "TextBlock_5", "Публичная комната")]),
        ("Maxi_CapProject", "Проект", [("Maxi_RowConstructor", "Button_0", "TextBlock_2", None),
                                       ("Maxi_RowProjects", "SaveSytemButton", "TextBlock_6", "Мои проекты")]),
        ("Maxi_CapAccount", "Аккаунт", [("Maxi_RowProfile", "ProfileRoomButton", "TextBlock_0", None),
                                        ("Maxi_RowAccount", "AccountSettingsButton", "TextBlock_7", None)]),
    ]
    for gi, (cap, cap_text, rows) in enumerate(groups):
        caption(cap, cap_text, gi == 0)
        for (rname, bname, lname, value) in rows:
            vslot(lst.add_child(row(rname, bname, lname, value)), top=s(1))
    vslot(lst.add_child(divider(t, "Maxi_MenuDivider")), top=s(8), left=s(6), right=s(6), bottom=s(8))
    vslot(lst.add_child(row("Maxi_RowClose", "ExitButton", "TextBlock_8", "Закрыть", T_CLOSE)))

    # ≡ button: BackgroundBlur(R16) > MaxiVisibilityToggleButton(target MyMainRoom) > two bars
    toggle = t.new(unreal.MaxiVisibilityToggleButton, "Maxi_MenuToggle")
    toggle.set_editor_property("target_widget_name", "MyMainRoom")
    button_style(toggle, GLASS(s(12)), GLASS_HOVER(s(12)))
    bars = t.new(unreal.VerticalBox, "Maxi_MenuBars")
    for i in (1, 2):
        bar = t.new(unreal.Image, "Maxi_MenuBar%d" % i)
        bar.set_brush(sized(brush_round(WHITE, 1.0, s(1)), s(15), s(1.6)))
        vslot(bars.add_child(bar), top=0 if i == 1 else s(4), h=H_CENTER)
    toggle.add_child(bars)
    content_slot(toggle, h=H_CENTER, v=V_CENTER)
    blur = glass_blur(t, "Maxi_MenuToggleBlur", s(12), toggle)
    canvas(root.add_child(blur), (0, 0, 0, 0), (s(16), s(16)), size=(s(40), s(40)), align=(0, 0), z=0)

    # full-screen panels opened from the menu stay above the ≡ button, as they stayed above the old strip
    for n in ("MyProfile", "MyPrivateRoom", "MySaveSystem"):
        t.w(n).slot.set_z_order(1)
    return t

# ================================================================ BP_UserLogin_UI (login card)
STATES = [
    dict(vb="Login", title="TextBlock_168", header=None,
         fields=[("Border_3", "Login_Username"), ("Border_2", "Login_Password")],
         link=("Login_ForgotPassword", "TextBlock_170", "CanvasPanel_197"),
         primary=("Login_Button", "TextBlock_287", "CanvasPanel_100"),
         foot=("TextBlock_77", "Login_SignUp", "TextBlock_406", "HorizontalBox_0")),
    dict(vb="SignUp", title="TextBlock", header=None,
         fields=[("Border_4", "SignUp_Username"), ("Border_8", "SignUp_Email"), ("Border_6", "SignUp_Password")],
         link=None, primary=("SignUp_Button", "TextBlock_208", "CanvasPanel_1"),
         foot=("TextBlock_283", "SignUp_Login", "TextBlock_112", "HorizontalBox_3")),
    dict(vb="ConfirmSignUp", title="TextBlock_7", header=("HorizontalBox_7", "ConfirmSignUp_Back", "Image_117"),
         fields=[("Border_13", "ConfirmSignUp_Code")], link=None,
         primary=("ConfirmSignUp_Confirm", "TextBlock_11", "CanvasPanel_3"),
         foot=("TextBlock_8", "ConfirmSignUp_Login", "TextBlock_9", "HorizontalBox_6")),
    dict(vb="MFA", title="TextBlock_13", header=("HorizontalBox_12", "MFA_Back", "Image_2"),
         fields=[("Border_12", "MFA_Code")], link=None,
         primary=("MFA_Confirm", "TextBlock_15", "CanvasPanel_4"),
         foot=("TextBlock_16", "MFA_Login", "TextBlock_17", "HorizontalBox_14")),
    dict(vb="ForgotPassword", title="TextBlock_1", header=None,
         fields=[("Border_9", "ForgotPassword_Username")], link=None,
         primary=("ForgotPassword_ForgotPassword", "ForgotPassword_Login", "CanvasPanel"),
         foot=("TextBlock_3", "ForgotPassword_SignUp", "TextBlock_4", "HorizontalBox_4")),
    dict(vb="ConfirmForgotPassword", title="TextBlock_2", header=("HorizontalBox_8", "ConfirmForgotPassword_Back", "Image_1"),
         fields=[("Border_11", "ConfirmForgotPassword_Code"), ("Border_10", "ConfirmForgotPassword_Password")], link=None,
         primary=("ConfirmForgotPassword_ForgotPassword", "Text", "CanvasPanel_2"),
         foot=("TextBlock_5", "ConfirmForgotPassword_Login", "TextBlock_6", "HorizontalBox")),
]
EYES = {"Login_PasswordVisibilityToggle": "Image_106", "SignUp_PasswordVisibilityToggle": "Image_530",
        "ConfirmForgotPassword_PasswordVisibilityToggle": "Image"}
RESENDS = {"ConfirmSignUp_Resend": "TextBlock_10", "ConfirmForgotPassword_Resend": "TextBlock_268"}

def link_style(t, btn_name, label_name):
    btn = t.w(btn_name)
    button_style(btn, brush_none(), brush_none(), fg=sc(SECONDARY), fg_hover=sc(WHITE))
    content_slot(btn, h=H_CENTER, v=V_CENTER)
    text(t.w(label_name), T_LINK, fg=True)
    return btn

def field(t, border_name, edit_name):
    border = t.w(border_name)
    border.set_brush(brush_round(WHITE, 0.08, s(11)))
    border.set_padding(M(s(12), 0, s(12), 0))
    border.set_horizontal_alignment(H_FILL)
    border.set_vertical_alignment(V_CENTER)
    edit = t.w(edit_name)
    st = edit.get_editor_property("widget_style")
    st.set_editor_property("font", font(*T_FIELD))
    st.set_editor_property("color_and_opacity", sc(WHITE))
    edit.set_editor_property("widget_style", st)
    edit.set_editor_property("justification", JUST_LEFT)
    # the content slot keeps its own alignment and padding; set them there too so every field centres its text
    inner = border.get_child_at(0)
    inner.slot.set_padding(M(s(12), 0, s(12), 0))
    inner.slot.set_horizontal_alignment(H_FILL)
    inner.slot.set_vertical_alignment(V_CENTER)
    hb = edit.get_parent()
    if isinstance(hb, unreal.HorizontalBox):
        # the text field fills the row; the eye / "Отправить повторно" button sits on its right
        extras = [c for c in hb.get_all_children() if c != edit]
        for c in extras:
            reparent(c, hb)
        vslot(edit.slot, v=V_CENTER, fill=True)
        for c in extras:
            vslot(c.slot, left=s(8), v=V_CENTER)
            name = c.get_name()
            if name in EYES:
                button_style(c, brush_none(), brush_none())
                # the eye brush comes from a Blueprint binding, so its size is fixed by a SizeBox around the image
                img = t.w(EYES[name])
                eye_box = sizebox(t, "Maxi_%s_IconSize" % name, w=s(15), h=s(15), content=img)
                c.add_child(eye_box)
                content_slot(c)
                img.set_render_opacity(0.75)          # the icon material is white; 75 % reads as #C4C4C4 on the field
            elif name in RESENDS:
                link_style(t, name, RESENDS[name])
    return border

def restyle_login():
    t = Tree(LOGIN_PATH)
    root, content = t.w("CanvasPanel_37"), t.w("CanvasPanel_80")

    # background: dark "opening" of screen 01 (Salon Home, lines 23-24) instead of the white fill
    t.w("Image_60").set_brush(brush_solid(INK, 1.0))
    glow = t.new(unreal.Image, "Maxi_OpeningGlow")
    gb = brush_solid(WHITE, 0.06)
    gb.set_editor_property("resource_object", GLOW)
    glow.set_brush(gb)
    canvas(root.add_child(glow), (0.27, 0, 0.73, 1), (0, 0, 0, 0), z=-9)
    for name, anchor_x, x in (("Maxi_OpeningLineL", 0.27, 0), ("Maxi_OpeningLineR", 0.73, -1)):
        line = t.new(unreal.Image, name)
        line.set_brush(brush_solid(WHITE, 0.16))
        canvas(root.add_child(line), (anchor_x, 0, anchor_x, 1), (x, 0, 1, 0), z=-9)

    for st in STATES:
        S = st["vb"]
        vb = t.w(S)
        vb.slot.set_auto_size(True)
        body = t.new(unreal.VerticalBox, "Maxi_%s_Body" % S)
        glass = t.new(unreal.Border, "Maxi_%s_Glass" % S)
        glass.set_brush(GLASS(s(18))); glass.set_padding(M(s(14))); glass.set_content(body)
        card = sizebox(t, "Maxi_%s_Card" % S, w=s(320))
        card.add_child(glass_blur(t, "Maxi_%s_Blur" % S, s(18), glass))
        first = [True]
        def add(widget, **kw):
            slot = reparent(widget, body) if widget.get_parent() else body.add_child(widget)
            vslot(slot, top=0 if first[0] else s(10), **kw)
            first[0] = False
            return slot

        brand = t.new(unreal.TextBlock, "Maxi_%s_Brand" % S)
        text(brand, T_CAPTION, SECONDARY, value="MaxiMall", upper=True)
        add(brand)

        title = t.w(st["title"])
        text(title, T_TITLE, WHITE, justify=JUST_LEFT)
        if st["header"]:
            hb_name, back_name, img_name = st["header"]
            hb, back = t.w(hb_name), t.w(back_name)
            back.remove_from_parent(); title.remove_from_parent()
            button_style(back, brush_round(WHITE, 0.10, s(16), pill=True), brush_round(WHITE, 0.18, s(16), pill=True))
            content_slot(back)
            arrow = t.w(img_name)
            arrow_brush = sized(arrow.get_editor_property("brush"), s(12), s(12))
            arrow_brush.set_editor_property("tint_color", sc(WHITE))
            arrow.set_brush(arrow_brush)
            vslot(hb.add_child(sizebox(t, "Maxi_%s_BackSize" % S, w=s(32), h=s(32), content=back)), v=V_CENTER)
            vslot(hb.add_child(title), left=s(10), v=V_CENTER, fill=True)
            add(hb)
        else:
            add(title)

        fields = t.new(unreal.VerticalBox, "Maxi_%s_Fields" % S)
        for i, (border_name, edit_name) in enumerate(st["fields"]):
            box = sizebox(t, "Maxi_%s_Field%d" % (S, i + 1), h=s(36))
            border = field(t, border_name, edit_name)
            reparent(border, box)
            vslot(fields.add_child(box), top=0 if i == 0 else s(6))
        add(fields)

        if st["link"]:
            btn_name, label_name, old_parent = st["link"]
            add(link_style(t, btn_name, label_name), h=H_RIGHT)
            t.w(old_parent).set_visibility(VIS_COLLAPSED)

        btn_name, label_name, old_parent = st["primary"]
        btn = t.w(btn_name)
        button_style(btn, brush_round(WHITE, 1.0, s(11)), brush_round("#E6E6E6", 1.0, s(11)))
        content_slot(btn)
        text(t.w(label_name), T_PRIMARY, INK, justify=JUST_CENTER)
        add(sizebox(t, "Maxi_%s_PrimarySize" % S, h=s(36), content=btn))
        t.w(old_parent).set_visibility(VIS_COLLAPSED)

        add(divider(t, "Maxi_%s_Divider" % S))

        cap_name, sec_name, sec_label, old_parent = st["foot"]
        text(t.w(cap_name), T_FOOT, SECONDARY, justify=JUST_LEFT)
        add(t.w(cap_name))
        sec = t.w(sec_name)
        button_style(sec, brush_round(WHITE, 0.08, s(10), 0.20, outline_w=1.0), brush_round(WHITE, 0.16, s(10), 0.20, outline_w=1.0))
        content_slot(sec)
        text(t.w(sec_label), T_SECONDARY, WHITE, justify=JUST_CENTER)
        add(sizebox(t, "Maxi_%s_SecondarySize" % S, h=s(32), content=sec))
        t.w(old_parent).set_visibility(VIS_COLLAPSED)

        vb.add_child(card)

    # shared error text under the card (it has no visibility logic, so no background plate)
    err = t.w("ErrorMessage")
    text(err, T_FOOT, SECONDARY, justify=JUST_CENTER)
    err.set_auto_wrap_text(True)
    canvas(err.slot, (0.5, 0.5, 0.5, 0.5), (0, 240), size=(s(320), 60), align=(0.5, 0))

    # loading overlay: white spinner and «Подождите…»
    thr = t.w("CircularThrobber_72")
    img = thr.get_editor_property("image")
    img.set_editor_property("tint_color", sc(WHITE))
    thr.set_editor_property("image", img)
    thr.set_editor_property("radius", 18.0)
    canvas(thr.slot, (0.5, 0.5, 0.5, 0.5), (0, -12), size=(48, 48), align=(0.5, 0.5))
    wait = t.w("TextBlock_483")
    text(wait, T_FOOT, SECONDARY, value="Подождите…", justify=JUST_CENTER)
    canvas(wait.slot, (0.5, 0.5, 0.5, 0.5), (0, 24), align=(0.5, 0), auto=True)
    return t

# ================================================================ run
def render_checks():
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    states = ["Login", "SignUp", "ConfirmSignUp", "MFA", "ForgotPassword", "ConfirmForgotPassword"]
    jobs = [(SEL_PATH, "render_menu.png", lin("#5A5752"), [], [])]   # mid-tone behind the menu, like the salon scene
    for st in states:
        jobs.append((LOGIN_PATH, "render_login_%s.png" % st, lin(INK), [st], [x for x in states if x != st]))
    for path, name, bg, show, hide in jobs:
        cls = unreal.load_class(None, path + "." + path.rsplit("/", 1)[1] + "_C")
        ok = unreal.MaxiUiDesignTools.render_widget_to_png(world, cls, unreal.IntPoint(1920, 1080), os.path.join(OUT, name), bg, show, hide)
        log("render %s: %s" % (name, ok))

saved = False
try:
    results = []
    for builder in (restyle_menu, restyle_login):
        t = builder()
        added = unreal.MaxiUiDesignTools.register_missing_widget_guids(t.bp, True)
        log("%s: %d new widgets, %d GUIDs registered" % (t.bp.get_name(), t.new_count, added))
        unreal.BlueprintEditorLibrary.compile_blueprint(t.bp)
        results.append(t)
    for a in latin + cyr + [FONT, GLOW]:
        log("save %s: %s" % (a.get_name(), unreal.EditorAssetLibrary.save_loaded_asset(a, False)))
    for t in results:
        log("save %s: %s" % (t.bp.get_name(), unreal.EditorAssetLibrary.save_loaded_asset(t.bp, False)))
    saved = True
    log("DONE")
except Exception:
    log("FAILED\n" + traceback.format_exc())
finally:
    open(os.path.join(OUT, "apply_log.txt"), "w", encoding="utf-8").write("\n".join(LOG))

if saved:
    try:
        render_checks()
    except Exception:
        log("RENDER FAILED\n" + traceback.format_exc())
    open(os.path.join(OUT, "apply_log.txt"), "w", encoding="utf-8").write("\n".join(LOG))
unreal.SystemLibrary.quit_editor()

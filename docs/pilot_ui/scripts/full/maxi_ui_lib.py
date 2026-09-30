# Shared tokens and atoms for scripted UMG restyles (design handoff 2026-09-27, pilot rules in
# docs/pilot_ui/TZ_UMG_pilot_menu_login.md). Mock values are in 1440 px; s() converts to UMG (1920x1080).
# Font sizes stay in mock px (Slate renders at 96 DPI); letter spacing = em * 1000 * 4/3.
import unreal

K = 4.0 / 3.0
def s(v): return round(v * K, 2)

LOG = []
def log(msg):
    LOG.append(str(msg)); unreal.log("[MAXIUI] " + str(msg))

# ------------------------------------------------------------------ enums
def E(enum_cls, *names):
    for n in names:
        if hasattr(enum_cls, n):
            return getattr(enum_cls, n)
    raise ValueError("%s has none of %s" % (enum_cls, names))

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
FG_RULE = E(unreal.SlateColorStylingMode, "USE_COLOR_FOREGROUND")
VIS_COLLAPSED = unreal.SlateVisibility.COLLAPSED
VIS_HIT_INVISIBLE = unreal.SlateVisibility.HIT_TEST_INVISIBLE

# ------------------------------------------------------------------ colours, structs
def lin(hexstr, a=1.0):
    def c(v):
        v = v / 255.0
        return v / 12.92 if v <= 0.04045 else ((v + 0.055) / 1.055) ** 2.4
    h = hexstr.lstrip("#")
    return unreal.LinearColor(c(int(h[0:2], 16)), c(int(h[2:4], 16)), c(int(h[4:6], 16)), a)

def sc(hexstr, a=1.0):
    col = unreal.SlateColor(); col.set_editor_property("specified_color", lin(hexstr, a)); return col

def sc_fg():
    col = unreal.SlateColor(); col.set_editor_property("color_use_rule", FG_RULE); return col

def M(l, t=None, r=None, b=None):
    if t is None: t = r = b = l
    return unreal.Margin(l, t, r, b)

def V2(x, y): return unreal.Vector2D(x, y)

WHITE, SECONDARY, INK, GLASS_HEX = "#FFFFFF", "#C4C4C4", "#0E0E0E", "#262626"
BLUR = 16.0

# ------------------------------------------------------------------ brushes
def sized(brush, w, h):
    return unreal.MaxiUiDesignTools.set_brush_image_size(brush, V2(w, h))

def brush_none():
    b = unreal.SlateBrush(); b.set_editor_property("draw_as", DRAW_NONE); return b

def brush_solid(hexstr, a=1.0):
    b = unreal.SlateBrush(); b.set_editor_property("draw_as", DRAW_IMAGE); b.set_editor_property("tint_color", sc(hexstr, a)); return b

def brush_round(hexstr, a, radius, outline_a=0.0, outline_hex=WHITE, outline_w=0.0, pill=False, base=None):
    b = base if base is not None else unreal.SlateBrush()
    b.set_editor_property("draw_as", DRAW_ROUNDED)
    b.set_editor_property("tint_color", sc(hexstr, a))
    o = unreal.SlateBrushOutlineSettings()
    o.set_editor_property("corner_radii", unreal.Vector4(radius, radius, radius, radius))
    o.set_editor_property("rounding_type", ROUND_HALF if pill else ROUND_FIXED)
    o.set_editor_property("color", sc(outline_hex, outline_a))
    o.set_editor_property("width", outline_w)
    b.set_editor_property("outline_settings", o)
    return b

GLASS = lambda r, a=0.62: brush_round(GLASS_HEX, a, r, 0.14, outline_w=1.0)
GLASS_HOVER = lambda r: brush_round(WHITE, 0.12, r, 0.14, outline_w=1.0)

# ------------------------------------------------------------------ fonts (F_MaxiMall from the pilot)
FONT = unreal.load_asset("/Game/FirstPerson/UI/Styles/Fonts/F_MaxiMall")
T_CAPTION = (10, "SemiBold", 107)
T_ITEM = (13.5, "Medium", -13)
T_CLOSE = (13.5, "Medium", 0)
T_PANEL_TITLE = (17, "SemiBold", -20)
T_CARD_TITLE = (15, "SemiBold", -20)
T_NAME = (13.5, "SemiBold", 0)
T_META = (11.5, "Regular", 0)
T_FIELD = (13, "Regular", 0)
T_LINK = (12, "Regular", 0)
T_LINK_MEDIUM = (12, "Medium", 0)
T_PRIMARY = (13, "SemiBold", 0)
T_TOOL = (13, "Medium", 0)
T_NOTE = (12, "Regular", 0)
T_SECONDARY = (12.5, "Medium", 0)
T_CARD_NAME = (12, "Medium", 0)
T_CARD_META = (10, "SemiBold", 80)
T_GLYPH = (16, "Regular", 0)

def font(size, face, spacing=0):
    f = unreal.SlateFontInfo()
    f.set_editor_property("font_object", FONT)
    f.set_editor_property("typeface_font_name", face)
    f.set_editor_property("size", size)
    f.set_editor_property("letter_spacing", spacing)
    return f

def text(tb, spec, color_hex=WHITE, fg=False, value=None, justify=None, upper=False, wrap=None):
    tb.set_font(font(*spec))
    tb.set_color_and_opacity(sc_fg() if fg else sc(color_hex))
    if value is not None: tb.set_text(value)
    if justify is not None: tb.set_editor_property("justification", justify)
    if upper: tb.set_editor_property("text_transform_policy", TO_UPPER)
    if wrap is not None: tb.set_auto_wrap_text(wrap)

# ------------------------------------------------------------------ tree
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
            raise RuntimeError("widget not found: %s in %s" % (name, self.bp.get_name()))
        return obj

    def new(self, cls, name):
        if unreal.find_object(None, self.prefix + "." + name) is not None:
            raise RuntimeError("name already used: " + name)
        self.new_count += 1
        return unreal.new_object(cls, outer=self.tree, name=name)

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
        slot.set_offsets(M(*pos) if len(pos) == 4 else M(0))
    if z is not None:
        slot.set_z_order(z)

def sizebox(t, name, w=None, h=None, content=None, max_h=None):
    sb = t.new(unreal.SizeBox, name)
    if w is not None: sb.set_width_override(w)
    if h is not None: sb.set_height_override(h)
    if max_h is not None: sb.set_max_desired_height(max_h)
    if content is not None:
        if content.get_parent(): reparent(content, sb)
        else: sb.add_child(content)
    return sb

# ------------------------------------------------------------------ buttons
def button_style(btn, normal, hovered, pressed=None, fg=None, fg_hover=None, padding=M(0), disabled=None):
    st = btn.get_editor_property("widget_style")
    st.set_editor_property("normal", normal)
    st.set_editor_property("hovered", hovered)
    st.set_editor_property("pressed", pressed if pressed is not None else hovered)
    st.set_editor_property("disabled", disabled if disabled is not None else normal)
    st.set_editor_property("normal_padding", padding)
    st.set_editor_property("pressed_padding", padding)
    if fg is not None:
        st.set_editor_property("normal_foreground", fg)
        st.set_editor_property("hovered_foreground", fg_hover or fg)
        st.set_editor_property("pressed_foreground", fg_hover or fg)
        st.set_editor_property("disabled_foreground", sc(WHITE, 0.45))
    btn.set_editor_property("widget_style", st)
    btn.set_color_and_opacity(unreal.LinearColor(1, 1, 1, 1))     # old buttons carry colour tints
    btn.set_background_color(unreal.LinearColor(1, 1, 1, 1))
    return st

def content_slot(btn, h=H_CENTER, v=V_CENTER, padding=M(0)):
    c = btn.get_child_at(0)
    if c is not None:
        c.slot.set_padding(padding); c.slot.set_horizontal_alignment(h); c.slot.set_vertical_alignment(v)
    return c

def primary(btn, label=None, value=None):
    st = button_style(btn, brush_round(WHITE, 1.0, s(11)), brush_round("#E6E6E6", 1.0, s(11)),
                      disabled=brush_round(WHITE, 0.45, s(11)))
    content_slot(btn)
    if label is not None: text(label, T_PRIMARY, INK, value=value, justify=JUST_CENTER)
    return st

def secondary(btn, label=None, value=None):
    button_style(btn, brush_round(WHITE, 0.08, s(10), 0.20, outline_w=1.0), brush_round(WHITE, 0.16, s(10), 0.20, outline_w=1.0))
    content_slot(btn)
    if label is not None: text(label, T_SECONDARY, WHITE, value=value, justify=JUST_CENTER)

def tool(btn, label=None, value=None):
    # "Инструмент" button of the room builder (Room Builder, lines 81-84), inactive state
    button_style(btn, brush_round(WHITE, 0.06, s(11), 0.14, outline_w=1.0), brush_round(WHITE, 0.12, s(11), 0.14, outline_w=1.0))
    content_slot(btn)
    if label is not None: text(label, T_TOOL, WHITE, value=value, justify=JUST_CENTER)

def link(btn, label=None, value=None, spec=T_LINK, color=SECONDARY):
    button_style(btn, brush_none(), brush_none(), fg=sc(color), fg_hover=sc(WHITE))
    content_slot(btn)
    if label is not None: text(label, spec, fg=True, value=value)

def round_button(t, btn, glyph_name=None):
    """32 px round button of the builder header (white 10 %, hover 18 %), with a «×» glyph when the button has no content."""
    button_style(btn, brush_round(WHITE, 0.10, s(16), pill=True), brush_round(WHITE, 0.18, s(16), pill=True))
    if btn.get_child_at(0) is None and glyph_name:
        g = t.new(unreal.TextBlock, glyph_name)
        text(g, T_GLYPH, WHITE, value="×", justify=JUST_CENTER)
        btn.add_child(g)
    content_slot(btn)

# ------------------------------------------------------------------ containers
def glass_blur(t, name, radius, content=None):
    blur = t.new(unreal.BackgroundBlur, name)
    blur.set_blur_strength(BLUR)
    blur.set_apply_alpha_to_blur(True)
    blur.set_corner_radius(unreal.Vector4(radius, radius, radius, radius))
    blur.set_padding(M(0))
    blur.set_editor_property("low_quality_fallback_brush", brush_none())
    if content is not None: blur.set_content(content)
    return blur

def divider(t, name):
    img = t.new(unreal.Image, name)
    img.set_brush(sized(brush_solid(WHITE, 0.14), 1, 1))
    return img

def caption(t, name, value):
    tb = t.new(unreal.TextBlock, name)
    text(tb, T_CAPTION, SECONDARY, value=value, upper=True)
    return tb

def field_border(border, radius=None):
    border.set_brush(brush_round(WHITE, 0.08, radius or s(11)))
    border.set_padding(M(s(12), 0, s(12), 0))
    border.set_horizontal_alignment(H_FILL)
    border.set_vertical_alignment(V_CENTER)
    inner = border.get_child_at(0)
    if inner is not None:   # the content slot keeps its own alignment and padding
        inner.slot.set_padding(M(s(12), 0, s(12), 0)); inner.slot.set_horizontal_alignment(H_FILL); inner.slot.set_vertical_alignment(V_CENTER)

def editable_text(edit, hint=None):
    st = edit.get_editor_property("widget_style")
    st.set_editor_property("font", font(*T_FIELD))
    st.set_editor_property("color_and_opacity", sc(WHITE))
    edit.set_editor_property("widget_style", st)
    edit.set_editor_property("justification", JUST_LEFT)
    if hint is not None: edit.set_editor_property("hint_text", hint)

def note_border(border):
    border.set_brush(brush_round(WHITE, 0.06, s(10)))
    border.set_padding(M(s(10), s(8), s(10), s(8)))
    inner = border.get_child_at(0)
    if inner is not None: inner.slot.set_padding(M(s(10), s(8), s(10), s(8)))

def side_panel(t, prefix, parent_canvas, title_widget, close_button, glyph=True, max_h=None, pos=(s(264), s(68)), width=s(300)):
    """Glass side panel right of the menu: SizeBox > BackgroundBlur > Border(α0.72) > VerticalBox body. Returns body."""
    body = t.new(unreal.VerticalBox, prefix + "_Body")
    glass = t.new(unreal.Border, prefix + "_Glass")
    glass.set_brush(GLASS(s(18), 0.72)); glass.set_padding(M(s(14))); glass.set_content(body)
    size = sizebox(t, prefix + "_Size", w=width, max_h=max_h)
    size.add_child(glass_blur(t, prefix + "_Blur", s(18), glass))
    canvas(parent_canvas.add_child(size), (0, 0, 0, 0), pos, align=(0, 0), auto=True, z=5)
    header = t.new(unreal.HorizontalBox, prefix + "_Header")
    text(title_widget, T_PANEL_TITLE, WHITE, justify=JUST_LEFT)
    vslot(reparent(title_widget, header) if title_widget.get_parent() else header.add_child(title_widget), v=V_CENTER, fill=True)
    round_button(t, close_button, prefix + "_CloseGlyph" if glyph else None)
    close_size = sizebox(t, prefix + "_CloseSize", w=s(32), h=s(32), content=close_button)
    vslot(header.add_child(close_size), left=s(10), v=V_CENTER)
    vslot(body.add_child(header))
    return body

def add(body, widget, first=False, gap=None, **kw):
    slot = reparent(widget, body) if widget.get_parent() else body.add_child(widget)
    vslot(slot, top=0 if first else (s(14) if gap is None else gap), **kw)
    return slot

# ------------------------------------------------------------------ finish
def finish(t, compile_now=True):
    added = unreal.MaxiUiDesignTools.register_missing_widget_guids(t.bp, True)
    log("%s: %d new widgets, %d GUIDs registered" % (t.bp.get_name(), t.new_count, added))
    if compile_now:
        unreal.BlueprintEditorLibrary.compile_blueprint(t.bp)

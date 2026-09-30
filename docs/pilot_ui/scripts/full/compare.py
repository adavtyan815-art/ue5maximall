# Compares the redesigned widgets with their original dumps: names, bound events, animation targets, property bindings.
import json, re, sys, io, os
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8")
# Work root: the folder above full/ (holds ui-impl/, ui-audit/out/, full/inspect/, full/verify/); MAXI_UI_WORK overrides it.
S = os.environ.get("MAXI_UI_WORK") or os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
new = json.load(open(os.path.join(S, "full", "verify", "widgets.json"), encoding="utf-8"))

def names_from(path, key):
    d = json.load(open(path, encoding="utf-8"))
    if key not in d: return None
    v = d[key]
    return {w["name"] for w in (v["widgets"] if isinstance(v, dict) else v)}

def t3d(path):
    raw = open(path, "rb").read()
    t = raw.decode("utf-16") if raw[:2] in (b"\xff\xfe", b"\xfe\xff") else raw.decode("utf-8", "replace")
    events, dp = set(), None
    for m in re.finditer(r'(DelegatePropertyName|ComponentPropertyName)="([^"]+)"', t):
        if m.group(1) == "DelegatePropertyName": dp = m.group(2)
        else: events.add("%s.%s" % (m.group(2), dp))
    anims = set(re.findall(r'AnimationBindings\(\d+\)=\(WidgetName="([^"]*)"', t))
    binds = set(re.findall(r'Bindings\(\d+\)=\(ObjectName="([^"]+)",PropertyName="([^"]+)"', t))
    return events, anims, binds

ORIG = {   # widget -> (names dump, key, original t3d)
    "BP_UserLogin_UI": (os.path.join(S, "ui-impl", "widgets_dump2.json"), os.path.join(S, "ui-impl", "BP_UserLogin_UI_v2.t3d")),
    "BP_Online_Offline_Selection_UI": (os.path.join(S, "ui-impl", "widgets_dump2.json"), os.path.join(S, "ui-impl", "BP_Online_Offline_Selection_UI_v2.t3d")),
}
for k in ("BP_Burger", "WBP_Male_Female", "WBP_SaveSystem", "WBP_SaveHistoryItem"):
    ORIG[k] = (os.path.join(S, "ui-audit", "out", "widgets_dump.json"), os.path.join(S, "ui-audit", "out", k + ".t3d"))
for k in json.load(open(os.path.join(S, "full", "inspect", "widgets.json"), encoding="utf-8")):
    ORIG[k] = (os.path.join(S, "full", "inspect", "widgets.json"), os.path.join(S, "full", "inspect", k + ".t3d"))

bad = 0
for k in sorted(new):
    cur = {w["name"] for w in new[k]}
    if k not in ORIG:
        print("%-32s (untouched, no original dump: %d widgets)" % (k, len(cur))); continue
    on, ot = ORIG[k]
    orig = names_from(on, k)
    missing = sorted(orig - cur)
    added_other = sorted(n for n in cur - orig if not n.startswith("Maxi_"))
    e0, a0, b0 = t3d(ot); e1, a1, b1 = t3d(os.path.join(S, "full", "verify", k + ".t3d"))
    ok = not missing and not added_other and e0 == e1 and a0 <= set(cur) | {""} and b0 == b1
    bad += 0 if ok else 1
    print("%-32s %s  widgets %3d->%3d  events %2d %s  anim targets ok=%s  bindings %d %s%s%s" % (
        k, "OK " if ok else "BAD", len(orig), len(cur), len(e1), "=" if e0 == e1 else "!=", a0 <= set(cur) | {""},
        len(b1), "=" if b0 == b1 else "!=", ("  missing=%s" % missing) if missing else "", ("  foreign=%s" % added_other) if added_other else ""))
print("problems: %d" % bad)

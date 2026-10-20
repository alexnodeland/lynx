"""XIAO ESP32S3 Sense enclosure, rev D — builds a FreeCAD document.

Run inside FreeCAD (Python console, or the FreeCAD MCP):
    exec(open("<this folder>/build_revD.py").read())

Everything is positioned assembled: Base, Lid, BatteryTray, PanBase, Yoke are printable
Part Design bodies; Jig_* objects are non-printing reference dummies (Seeed's own STEP
of the board, a pin-fin heatsink, the sticker antenna + coax, a 401723 LiPo) used to
check clearances. FitCoupon_* are small test prints cut from the real parts.

Assembly order: battery -> tray (clicks into the wall grooves) -> board (onto the tray
posts, gripped by crush ribs) -> antenna sticker on the far wall -> lid (2 x M2 x 8).
Pan/tilt: SG90/MG90S pan servo in PanBase (horn under the Yoke); MG90S tilt servo on the
Yoke's right upright (horn, trimmed to 16 mm, in the Base's right cheek); the left cheek
pivots on an M3 x 8 screw through the Yoke's left upright. Signals: D0 = pan, D1 = tilt,
servo power from the XIAO 5V pin (USB) with a 470-1000 uF capacitor across it.

Board geometry is from Seeed's "Seeed Studio XIAO-ESP32-S3-Sense.step".
Heatsink and antenna sizes are user estimates — change HS_* / ANT_* below.
"""
import math
import os

import FreeCAD as App
import Part
import Sketcher

HERE = "/Users/alexandernodeland/Documents/xiao-esp32s3-sense-enclosure"
SENSE_STEP = os.path.join(HERE, "reference", "Seeed Studio XIAO-ESP32-S3-Sense.step")
OUT = os.path.join(HERE, "xiao-revD-combined.FCStd")
OUT_ENCL = os.path.join(HERE, "xiao-revD-enclosure.FCStd")
OUT_BOARD = os.path.join(HERE, "xiao-revD-board.FCStd")
V = App.Vector

# ---------------- parameters (mm) ----------------
FIT = 0.0                                     # + loosens every fit clearance, - tightens
PRODUCT_NAME = "lynx"                         # engraved on the lid
WALL, FLOOR, LID_T = 1.6, 1.5, 1.5
EXTRA_L, EXTRA_W = 3.1, 2.3          # room for the Adafruit 1317 LiPo (26.0 x 19.75 x 3.8)
OUT_L, OUT_W = 26.8 + EXTRA_L, 21.6 + EXTRA_W
OUTER_R, CAV_R = 3.5, 1.9
HS_L, HS_W, HS_H = 10.0, 10.0, 10.0          # heatsink (user: "about 1 cm3")
HS_GAP = 0.2                                  # heatsink fins to tray
BAT_L, BAT_W, BAT_H, BAT_GAP = 26.1, 19.8, 3.8, 0.3   # Adafruit 1317, 3.7 V 150 mAh (19.75 x 26.02 x 3.8)
TRAY_T, TRAY_CLEAR = 1.0, 0.4 + FIT           # tray plate; per-side clearance past the crush ribs
M2_PILOT, M2_CLEAR, M2_HEAD = 1.6 + FIT, 2.4 + FIT, 4.0   # self-tapping M2 x 8
ANT_L, ANT_W, ANT_T = 25.4, 12.7, 0.2         # 1 x 0.5 in sticker antenna (stands on the far wall)
ANT_RECESS, CABLE_GAP, CABLE_D = 0.3, 1.4, 1.13  # wall pocket depth, wall-to-board gap, 1.13 mm coax
LID_CLEAR = 0.5                               # camera top to lid underside

# Seeed STEP frame -> enclosure frame: rotate +90 about X, then translate.
# STEP: board in XZ, Y up; USB at +X.  Enclosure: board in XY, Z up, USB at +X.
OX = WALL + CABLE_GAP + EXTRA_L + 8.97   # board keeps its USB end at the camera wall; the extra length is at the far end
OY = 4.68 + EXTRA_W / 2           # board centred across the wider case
TRAY_Z0 = FLOOR + BAT_H + BAT_GAP
TRAY_TOP = TRAY_Z0 + TRAY_T
ZB = TRAY_TOP + HS_GAP + HS_H    # main-board underside height
OZ = ZB + 0.25                   # STEP y = -0.25 is the PCB underside


def s2e(xs, ys, zs):
    """STEP point -> enclosure point."""
    return V(xs + OX, -zs + OY, ys + OZ)


STACK_TOP = s2e(0, 13.71, 0).z                       # camera top
BASE_H = max(STACK_TOP + LID_CLEAR, FLOOR + 0.1 + ANT_L + 0.3)   # wall top = lid underside
ANT_Z0 = BASE_H - 0.3 - ANT_L                        # antenna top tucked under the lid tab
CAM_C = s2e((4.75 + 12.75) / 2, 0, (-10.75 - 2.75) / 2)   # lens axis
USB_C = s2e((6.51 + 13.81) / 2, (0.01 + 4.21) / 2, (-10.59 - 1.65) / 2)
SD_Y = (s2e(0, 0, -0.88).y + s2e(0, 0, -12.97).y) / 2
SD_Z = s2e(0, (6.6 + 8.25) / 2, 0).z
UFL = s2e((-8.45 - 5.45) / 2, 2.25, (-12.39 - 9.29) / 2)
HS_C = s2e((-5.03 + 5.57) / 2, 0, (-12.41 + 0.19) / 2)    # over the RF shield
CY = OUT_W / 2
BTN = [s2e((9.41 + 12.01) / 2, 1.73, zc) for zc in ((-0.99 + 0.61) / 2, (-12.84 - 11.24) / 2)]  # BOOT/RESET
CARD_TOP = s2e(0, 8.25, 0).z
EXP_TOP = s2e(0, 5.18, 0).z
BOSS = [V(OUT_L / 2, WALL + 1.0, 0), V(OUT_L / 2, OUT_W - WALL - 1.0, 0)]   # lid screw axes


# ---------------- helpers ----------------
def planes(body):
    return {x.Role: x for x in body.Origin.OriginFeatures}


def sketch(body, name, plane, offset=0.0):
    sk = body.newObject("Sketcher::SketchObject", name)
    sk.AttachmentSupport = [(planes(body)[plane], "")]
    sk.MapMode = "FlatFace"
    sk.AttachmentOffset = App.Placement(V(0, 0, offset), App.Rotation())
    return sk


def closed(sk, geos):
    ids = [sk.addGeometry(g) for g in geos]
    for i in range(len(ids)):
        sk.addConstraint(Sketcher.Constraint("Coincident", ids[i], 2, ids[(i + 1) % len(ids)], 1))


def c_profile(sk, cx, cy, ro, ri, a0, a1):
    """C-ring between radii ri..ro from angle a0 to a1 (CCW). Both arcs run CCW, so the inner
    arc is joined by its END to the incoming radial line (chaining end->start would fold the
    profile into a figure-8: a valid-looking solid that never meshes watertight)."""
    C = V(cx, cy, 0)
    P = lambda r, a: V(cx + r * math.cos(a), cy + r * math.sin(a), 0)
    outer = sk.addGeometry(Part.ArcOfCircle(Part.Circle(C, V(0, 0, 1), ro), a0, a1))
    l1 = sk.addGeometry(Part.LineSegment(P(ro, a1), P(ri, a1)))
    inner = sk.addGeometry(Part.ArcOfCircle(Part.Circle(C, V(0, 0, 1), ri), a0, a1))
    l2 = sk.addGeometry(Part.LineSegment(P(ri, a0), P(ro, a0)))
    for g1, v1, g2, v2 in ((outer, 2, l1, 1), (l1, 2, inner, 2), (inner, 1, l2, 1), (l2, 2, outer, 1)):
        sk.addConstraint(Sketcher.Constraint("Coincident", g1, v1, g2, v2))


def rrect(sk, x0, y0, x1, y1, r):
    if r <= 0:
        p = [V(x0, y0, 0), V(x1, y0, 0), V(x1, y1, 0), V(x0, y1, 0)]
        closed(sk, [Part.LineSegment(p[i], p[(i + 1) % 4]) for i in range(4)])
        return
    c = lambda x, y, a0, a1: Part.ArcOfCircle(Part.Circle(V(x, y, 0), V(0, 0, 1), r), a0, a1)
    closed(sk, [Part.LineSegment(V(x0 + r, y0, 0), V(x1 - r, y0, 0)), c(x1 - r, y0 + r, -math.pi / 2, 0),
                Part.LineSegment(V(x1, y0 + r, 0), V(x1, y1 - r, 0)), c(x1 - r, y1 - r, 0, math.pi / 2),
                Part.LineSegment(V(x1 - r, y1, 0), V(x0 + r, y1, 0)), c(x0 + r, y1 - r, math.pi / 2, math.pi),
                Part.LineSegment(V(x0, y1 - r, 0), V(x0, y0 + r, 0)), c(x0 + r, y0 + r, math.pi, 1.5 * math.pi)])


def slot(sk, cx, y0, y1, w):
    r = w / 2
    closed(sk, [Part.LineSegment(V(cx + r, y0 + r, 0), V(cx + r, y1 - r, 0)),
                Part.ArcOfCircle(Part.Circle(V(cx, y1 - r, 0), V(0, 0, 1), r), 0, math.pi),
                Part.LineSegment(V(cx - r, y1 - r, 0), V(cx - r, y0 + r, 0)),
                Part.ArcOfCircle(Part.Circle(V(cx, y0 + r, 0), V(0, 0, 1), r), math.pi, 2 * math.pi)])


def feature(body, kind, name, profile, **props):
    f = body.newObject(f"PartDesign::{kind}", name)
    f.Profile = profile
    for k, v in props.items():
        setattr(f, k, v)
    profile.Visibility = False
    return f


def dressup_soft(body, kind, name, pred, sizes):
    """Cosmetic dress-up: try each size, keep the first that recomputes valid, else drop it."""
    for sz in sizes:
        f = dressup(body, kind, name, pred, sz)
        if "Invalid" not in f.State and f.Shape.isValid():
            return f
        body.removeObject(f)
        body.Document.removeObject(f.Name)
        body.Document.recompute()
    return None


def dressup(body, kind, name, pred, size):
    doc = body.Document
    doc.recompute()
    tip = body.Tip
    sel = [f"Edge{i + 1}" for i, e in enumerate(tip.Shape.Edges) if pred(e)]
    if not sel:
        raise RuntimeError(f"{name}: no edges matched")
    f = body.newObject(f"PartDesign::{kind}", name)
    f.Base = (tip, sel)
    if kind == "Fillet":
        f.Radius = size
    else:
        f.Size = size
    doc.recompute()
    return f


def on_outer(e, eps=0.01):
    b = e.BoundBox
    return b.XMin < eps or b.XMax > OUT_L - eps or b.YMin < eps or b.YMax > OUT_W - eps


def at_z(e, z):
    return abs(e.BoundBox.ZMin - z) < 1e-4 and abs(e.BoundBox.ZMax - z) < 1e-4


# ---------------- document ----------------
for d in list(App.listDocuments().values()):
    if d.FileName.startswith(os.path.join(HERE, "xiao-revD-")) or d.Name in (
            "Combined", "Enclosure", "Board", "BatteryTray", "Battery", "Swivel", "Magnet", "Yoke", "PanBase",
            "ServoPan", "ServoTilt", "USBCable", "ServoShroud", "LightPipe", "SpringReliefs",
            "PanCap", "VentMesh", "Screws", "TestPrints"):
        App.closeDocument(d.Name)
doc = App.newDocument("Combined")

# ---------------- jigs ----------------
jig = doc.addObject("App::DocumentObjectGroup", "Jigs")
board = Part.read(SENSE_STEP)
board.Placement = App.Placement(V(OX, OY, OZ), App.Rotation(V(1, 0, 0), 90))
j = doc.addObject("Part::Feature", "Jig_XIAO_Sense")
j.Shape = board
jig.addObject(j)
# pin-fin heatsink: base plate against the board underside, 3 x 4 fins hanging down
HS_PLATE, FIN_L, FIN_T = 1.5, 2.4, 1.2
hx0, hy0, hz0 = HS_C.x - HS_L / 2, HS_C.y - HS_W / 2, TRAY_TOP + HS_GAP
hs = Part.makeBox(HS_L, HS_W, HS_PLATE, V(hx0, hy0, hz0 + HS_H - HS_PLATE))
for i in range(3):
    for k in range(4):
        fx = hx0 + (i + 0.5) * HS_L / 3 - FIN_L / 2
        fy = hy0 + (k + 0.5) * HS_W / 4 - FIN_T / 2
        hs = hs.fuse(Part.makeBox(FIN_L, FIN_T, HS_H - HS_PLATE, V(fx, fy, hz0)))
hs = hs.removeSplitter()
j = doc.addObject("Part::Feature", "Jig_Heatsink")
j.Shape = hs
jig.addObject(j)
bat = Part.makeBox(BAT_L, BAT_W, BAT_H, V(WALL + 0.3, CY - BAT_W / 2, FLOOR))
bat = bat.makeFillet(1.6, [e for e in bat.Edges if e.BoundBox.ZLength > BAT_H - 1e-3])   # pouch corners
# BAT_L is the overall length including the protection board / lead exit
j = doc.addObject("Part::Feature", "Jig_LiPo_401723")
j.Shape = bat.removeSplitter()
jig.addObject(j)

# ---------------- BASE ----------------
base = doc.addObject("PartDesign::Body", "Base")
sk = sketch(base, "SkOuter", "XY_Plane")
rrect(sk, 0, 0, OUT_L, OUT_W, OUTER_R)
feature(base, "Pad", "Shell", sk, Length=BASE_H)
sk = sketch(base, "SkCavity", "XY_Plane", BASE_H)
rrect(sk, WALL, WALL, OUT_L - WALL, OUT_W - WALL, CAV_R)
feature(base, "Pocket", "Cavity", sk, Length=BASE_H - FLOOR)

# tray grooves: the battery tray's bumps click in here (no ledges, so the cell passes)
TRAY_SNAP_Z = TRAY_Z0 + TRAY_T / 2
sk = sketch(base, "SkTrayGrooves", "YZ_Plane", OUT_L / 2)
for y in (WALL, OUT_W - WALL):
    sk.addGeometry(Part.Circle(V(y, TRAY_SNAP_Z, 0), V(0, 0, 1), 0.5))
feature(base, "Pocket", "TrayGrooves", sk, Midplane=True, Length=OUT_L - 2 * WALL - 2 * CAV_R)

# crush ribs gripping the PCB edges
# (board location moved to fences on the battery tray: wall ribs would block the wider cell)

# lid screw bosses on the long walls, hanging above the expansion board
BOSS_Z0 = EXP_TOP + 0.5
sk = sketch(base, "SkBosses", "XY_Plane", BOSS_Z0)
for b in BOSS:
    sk.addGeometry(Part.Circle(b, V(0, 0, 1), 2.0))
feature(base, "Pad", "ScrewBosses", sk, Length=BASE_H - BOSS_Z0)
sk = sketch(base, "SkPilots", "XY_Plane", BASE_H)
for b in BOSS:
    sk.addGeometry(Part.Circle(b, V(0, 0, 1), M2_PILOT / 2))
feature(base, "Pocket", "ScrewPilots", sk, Length=7.5)

# antenna pocket: the sticker stands on the far (-X) wall, flush in a 0.3 mm recess
sk = sketch(base, "SkAntennaPocket", "XY_Plane", FLOOR)
rrect(sk, WALL - ANT_RECESS, CY - ANT_W / 2 - 0.25, WALL + 0.2, CY + ANT_W / 2 + 0.25, 0)
feature(base, "Pocket", "AntennaPocket", sk, Reversed=True, Length=BASE_H - FLOOR)

# USB-C (plug overmold clearance) and microSD (card at the same end, above USB)
sk = sketch(base, "SkUSB", "YZ_Plane", OUT_L - WALL / 2)
rrect(sk, USB_C.y - 5.5 - FIT, USB_C.z - 2.9 - FIT, USB_C.y + 5.5 + FIT, USB_C.z + 2.9 + FIT, 1.5)
feature(base, "Pocket", "USB_C_Slot", sk, Midplane=True, Length=WALL + 2)
sk = sketch(base, "SkSD", "YZ_Plane", OUT_L - WALL / 2)
rrect(sk, SD_Y - 6.6 - FIT, SD_Z - 1.2 - FIT, SD_Y + 6.6 + FIT, SD_Z + 1.2 + FIT, 0.6)
feature(base, "Pocket", "MicroSD_Slot", sk, Midplane=True, Length=WALL + 2)
# USB-C strain-relief collar: a 1 mm raised frame round the plug opening
sk = sketch(base, "SkUSBCollar", "YZ_Plane", OUT_L)
rrect(sk, USB_C.y - 7.0, USB_C.z - 4.2, USB_C.y + 7.0, USB_C.z + 4.2, 2.5)
rrect(sk, USB_C.y - 5.5 - FIT, USB_C.z - 2.9 - FIT, USB_C.y + 5.5 + FIT, USB_C.z + 2.9 + FIT, 1.5)
feature(base, "Pad", "USBCollar", sk, Length=1.0)

# airflow through mesh grilles: intakes low at the heatsink, exhausts high (chimney)
GRILLE_HOLE, GRILLE_PITCH = 1.0, 1.6


def grille(sk, x0, x1, z0, z1):
    """Square-hole mesh field, centred in the box; holes 1.0 mm, 0.6 mm webs."""
    nx = int((x1 - x0 - GRILLE_HOLE) // GRILLE_PITCH) + 1
    nz = int((z1 - z0 - GRILLE_HOLE) // GRILLE_PITCH) + 1
    ox = x0 + (x1 - x0 - (nx - 1) * GRILLE_PITCH) / 2
    oz = z0 + (z1 - z0 - (nz - 1) * GRILLE_PITCH) / 2
    h = GRILLE_HOLE / 2
    for a in range(nx):
        for b in range(nz):
            cx, cz = ox + a * GRILLE_PITCH, oz + b * GRILLE_PITCH
            rrect(sk, cx - h, cz - h, cx + h, cz + h, 0)


VENT_W = 1.4
VENT_GROUPS = [((HS_C.x - 3.2, HS_C.x, HS_C.x + 3.2), TRAY_TOP + 1.2, ZB - 0.8),      # intake, at the heatsink
               ((4.2, 6.6, 9.0), EXP_TOP + 1.0, BASE_H - 2.4),                       # exhausts, above the board
               ((OUT_L - 9.0, OUT_L - 6.6, OUT_L - 4.2), EXP_TOP + 1.0, BASE_H - 2.4)]
sk = sketch(base, "SkVentSlots", "XZ_Plane", 0)
for xs, z0, z1 in VENT_GROUPS:
    for cx in xs:
        slot(sk, cx, z0, z1, VENT_W)
feature(base, "Pocket", "VentSlots", sk, Type="ThroughAll", Midplane=True)
# stainless mesh inserts (0.3 mm), pressed against the inside of each wall behind the slots
mesh = []
for xs, z0, z1 in VENT_GROUPS:
    for y0 in (WALL, OUT_W - WALL - 0.3):
        mesh.append(Part.makeBox(xs[-1] - xs[0] + VENT_W + 2.0, 0.3, z1 - z0 + 2.0,
                                 V(xs[0] - VENT_W / 2 - 1.0, y0, z0 - 1.0)))
j = doc.addObject("Part::Feature", "Jig_VentMesh")
j.Shape = Part.makeCompound(mesh)
jig.addObject(j)

dressup(base, "Chamfer", "BottomChamfer", lambda e: e.BoundBox.ZMax < 1e-6 and on_outer(e) and e.BoundBox.XMax < OUT_L + 1e-3, 0.5)
dressup(base, "Chamfer", "SeamChamfer", lambda e: e.BoundBox.ZMin > BASE_H - 1e-3 and on_outer(e), 0.3)

# tilt cheeks under the floor, flush with both end walls; axis along X at (CY, TZ)
TZ, CHEEK_R, CHEEK_T = -8.0, 7.0, 2.5
for name, x0 in (("CheekPivot", 0.0), ("CheekDrive", OUT_L - CHEEK_T)):
    sk = sketch(base, "Sk" + name, "YZ_Plane", x0)
    closed(sk, [Part.LineSegment(V(CY + CHEEK_R, 0.3, 0), V(CY - CHEEK_R, 0.3, 0)),
                Part.LineSegment(V(CY - CHEEK_R, 0.3, 0), V(CY - CHEEK_R, TZ, 0)),
                Part.ArcOfCircle(Part.Circle(V(CY, TZ, 0), V(0, 0, 1), CHEEK_R), math.pi, 2 * math.pi),
                Part.LineSegment(V(CY + CHEEK_R, TZ, 0), V(CY + CHEEK_R, 0.3, 0))])
    feature(base, "Pad", name, sk, Length=CHEEK_T)
# pivot: M3 self-tap pilot; drive: horn pocket (double horn trimmed to 16 mm) + screwdriver hole
sk = sketch(base, "SkPivotPilot", "YZ_Plane", 0.0)
sk.addGeometry(Part.Circle(V(CY, TZ, 0), V(0, 0, 1), 0.8 + FIT / 2))         # M2 self-tap pilot
feature(base, "Pocket", "PivotPilot", sk, Reversed=True, Length=CHEEK_T)
HORN_L, HORN_W, HORN_T = 16.0, 5.6, 1.5


def slot_h(sk, cy, cz, L, W):
    closed(sk, [
        Part.LineSegment(V(cy - L / 2 + W / 2, cz - W / 2, 0), V(cy + L / 2 - W / 2, cz - W / 2, 0)),
        Part.ArcOfCircle(Part.Circle(V(cy + L / 2 - W / 2, cz, 0), V(0, 0, 1), W / 2), -math.pi / 2, math.pi / 2),
        Part.LineSegment(V(cy + L / 2 - W / 2, cz + W / 2, 0), V(cy - L / 2 + W / 2, cz + W / 2, 0)),
        Part.ArcOfCircle(Part.Circle(V(cy - L / 2 + W / 2, cz, 0), V(0, 0, 1), W / 2), math.pi / 2, 1.5 * math.pi)])


sk = sketch(base, "SkTiltHorn", "YZ_Plane", OUT_L)
slot_h(sk, CY, TZ, HORN_L + 0.6 + 2 * FIT, HORN_W + 2 * FIT)
feature(base, "Pocket", "TiltHornPocket", sk, Length=1.2)
sk = sketch(base, "SkHornScrewAccess", "YZ_Plane", OUT_L)
sk.addGeometry(Part.Circle(V(CY, TZ, 0), V(0, 0, 1), 2.0))
feature(base, "Pocket", "HornScrewAccess", sk, Length=CHEEK_T + 0.1)
# servo-cable exit (grommet hole) low on the -Y wall at the antenna end
sk = sketch(base, "SkCableExit", "XZ_Plane", -(OUT_W - WALL / 2))      # back (+Y) wall only, toward the pod
sk.addGeometry(Part.Circle(V(4.0, ZB + 0.6, 0), V(0, 0, 1), 2.25))
feature(base, "Pocket", "ServoCableExit", sk, Midplane=True, Length=WALL + 1.0)

# ---------------- LID ----------------
lid = doc.addObject("PartDesign::Body", "Lid")
TOP = BASE_H + LID_T
sk = sketch(lid, "SkLidPlate", "XY_Plane", BASE_H)
rrect(sk, 0, 0, OUT_L, OUT_W, OUTER_R)
feature(lid, "Pad", "LidPlate", sk, Length=LID_T)
sk = sketch(lid, "SkScrewHoles", "XY_Plane", TOP)
for b in BOSS:
    sk.addGeometry(Part.Circle(b, V(0, 0, 1), M2_CLEAR / 2))
feature(lid, "Pocket", "ScrewHoles", sk, Type="ThroughAll")
sk = sketch(lid, "SkCounterbores", "XY_Plane", TOP)
for b in BOSS:
    sk.addGeometry(Part.Circle(b, V(0, 0, 1), M2_HEAD / 2 + 0.2))
feature(lid, "Pocket", "Counterbores", sk, Length=0.8)
# antenna retaining tab: presses the sticker's top edge
sk = sketch(lid, "SkAntennaTab", "XY_Plane", BASE_H)
rrect(sk, WALL - ANT_RECESS + 0.05, CY - 5.0, WALL + 0.6, CY + 5.0, 0)
feature(lid, "Pad", "AntennaTab", sk, Reversed=True, Length=0.25)

# camera cradle (8 mm module) + lens bezel and hole
sk = sketch(lid, "SkCameraCradle", "XY_Plane", BASE_H)
# three walls (the end wall is the fourth): 8 mm module + 0.15 clearance, 0.8 walls
a, w = 4.15 + FIT, 0.8
rrect(sk, CAM_C.x - a - w, CAM_C.y - a - w, CAM_C.x - a, CAM_C.y + a + w, 0)
rrect(sk, CAM_C.x - a, CAM_C.y + a, CAM_C.x + a, CAM_C.y + a + w, 0)
rrect(sk, CAM_C.x - a, CAM_C.y - a - w, CAM_C.x + a, CAM_C.y - a, 0)
feature(lid, "Pad", "CameraCradle", sk, Reversed=True, Length=BASE_H - STACK_TOP + 1.2)
sk = sketch(lid, "SkBezel", "XY_Plane", TOP)
sk.addGeometry(Part.Circle(V(CAM_C.x, CAM_C.y, 0), V(0, 0, 1), 4.6))
sk.addGeometry(Part.Circle(V(CAM_C.x, CAM_C.y, 0), V(0, 0, 1), 3.6))
feature(lid, "Pad", "LensBezel", sk, Length=1.0)
sk = sketch(lid, "SkLensHole", "XY_Plane", TOP)
sk.addGeometry(Part.Circle(V(CAM_C.x, CAM_C.y, 0), V(0, 0, 1), 3.6))
feature(lid, "Pocket", "LensHole", sk, Type="ThroughAll")
# BOOT/RESET: paperclip guide tubes ending just above the microSD card
sk = sketch(lid, "SkButtonGuides", "XY_Plane", BASE_H)
for b in BTN:
    sk.addGeometry(Part.Circle(V(b.x, b.y, 0), V(0, 0, 1), 1.2))
feature(lid, "Pad", "ButtonGuides", sk, Reversed=True, Length=BASE_H - (CARD_TOP + 0.4))
sk = sketch(lid, "SkButtonHoles", "XY_Plane", TOP)
for b in BTN:
    sk.addGeometry(Part.Circle(V(b.x, b.y, 0), V(0, 0, 1), 0.6 + FIT / 2))
feature(lid, "Pocket", "ButtonHoles", sk, Type="ThroughAll")

dressup(lid, "Fillet", "LidEdgeFillet", lambda e: at_z(e, TOP) and on_outer(e), 1.0)
circ = lambda r, z: (lambda e: isinstance(e.Curve, Part.Circle) and abs(e.Curve.Radius - r) < 1e-3
                     and abs(e.Curve.Center.z - z) < 1e-3)
dressup(lid, "Chamfer", "LensBevel", circ(3.6, TOP + 1.0), 0.6)
dressup(lid, "Chamfer", "PinLeadIns", circ(0.6 + FIT / 2, TOP), 0.3)

import Draft  # noqa: E402  (needs the GUI-less Draft module)
ss = Draft.make_shapestring(String=PRODUCT_NAME, FontFile="/System/Library/Fonts/Supplemental/Arial Bold.ttf",
                            Size=2.0, Tracking=0)
ss.Placement = App.Placement(V(3.0, WALL + 0.9, TOP), App.Rotation())        # along the lid's front edge
lid.addObject(ss)
doc.recompute()
feature(lid, "Pocket", "LabelEngrave", ss, Length=0.4)
# second mark: low on the front face, right-hand side
fm = Draft.make_shapestring(String=PRODUCT_NAME, FontFile="/System/Library/Fonts/Supplemental/Arial Bold.ttf",
                            Size=1.8, Tracking=0)
doc.recompute()
fw = fm.Shape.BoundBox.XLength
fm.Placement = App.Placement(V(OUT_L - 3.8 - fw, 0, 2.4), App.Rotation(V(1, 0, 0), 90))   # reads from the front (-Y)
base.addObject(fm)
doc.recompute()
fk = base.newObject("PartDesign::Pocket", "FrontMarkEngrave")
fk.Profile = fm
fk.Length = 0.3
fm.Visibility = False
doc.recompute()

# antenna jig: sticker on the far wall (adhesive side to the wall)
AX = WALL - ANT_RECESS + 0.05
ant = Part.makeBox(ANT_T, ANT_W, ANT_L, V(AX, CY - ANT_W / 2, ANT_Z0))
j = doc.addObject("Part::Feature", "Jig_FPC_Antenna")
j.Shape = ant
jig.addObject(j)
# coax: leaves the top-middle of the face, down the cable gap, under the expansion
# board, into a U.FL plug seated on the board's socket
r = CABLE_D / 2
gx = WALL + CABLE_GAP / 2
plug_z = UFL.z + 0.6
pts = [V(AX + ANT_T, CY, ANT_Z0 + ANT_L - 2.0), V(gx, CY, ANT_Z0 + ANT_L - 2.0),
       V(gx, UFL.y, ANT_Z0 + ANT_L - 6.0), V(gx, UFL.y, plug_z), V(UFL.x - 1.0, UFL.y, plug_z)]
wire = Part.makeSphere(r, pts[1])
for a, b in zip(pts, pts[1:]):
    wire = wire.fuse(Part.makeCylinder(r, (b - a).Length, a, b - a)).fuse(Part.makeSphere(r, b))
plug = Part.makeCylinder(1.0, 1.0, V(UFL.x, UFL.y, UFL.z), V(0, 0, 1))
plug = plug.fuse(Part.makeBox(1.6, 1.2, 0.9, V(UFL.x - 2.0, UFL.y - 0.6, UFL.z + 0.15)))
j = doc.addObject("Part::Feature", "Jig_Antenna_Coax")
j.Shape = wire.fuse(plug).removeSplitter()
jig.addObject(j)

# ---------------- BATTERY TRAY ----------------
tray = doc.addObject("PartDesign::Body", "BatteryTray")
tx0, ty0 = WALL + TRAY_CLEAR, WALL + TRAY_CLEAR
tx1, ty1 = OUT_L - WALL - TRAY_CLEAR, OUT_W - WALL - TRAY_CLEAR
sk = sketch(tray, "SkTrayPlate", "XY_Plane", TRAY_Z0)
rrect(sk, tx0, ty0, tx1, ty1, CAV_R - TRAY_CLEAR)
feature(tray, "Pad", "TrayPlate", sk, Length=TRAY_T)
sk = sketch(tray, "SkTrayBumps", "YZ_Plane", OUT_L / 2)
for y in (ty0, ty1):
    sk.addGeometry(Part.Circle(V(y, TRAY_SNAP_Z, 0), V(0, 0, 1), 0.45))
feature(tray, "Pad", "SnapBumps", sk, Midplane=True, Length=6.0)
# four board posts at the PCB corners (clear of the heatsink)
bx0, bx1 = s2e(-8.67, 0, 0).x, s2e(12.28, 0, 0).x
by0, by1 = s2e(0, 0, 2.78).y, s2e(0, 0, -15.0).y
sk = sketch(tray, "SkPosts", "XY_Plane", TRAY_TOP)
for x in (bx0 + 0.4, bx1 - 2.4):
    for y in (by0 + 0.3, by1 - 2.1):
        rrect(sk, x, y, x + 2.0, y + 1.8, 0)
feature(tray, "Pad", "BoardPosts", sk, Length=ZB - TRAY_TOP)
# L-fences at the four board corners locate the board (0.15 mm clearance), clear of its 1.9 mm corner radii
FG, FT, FH = 0.15 + FIT / 2, 0.7, 1.0
sk = sketch(tray, "SkBoardFences", "XY_Plane", TRAY_TOP)
for xe, sx in ((bx0, -1), (bx1, 1)):
    for ye, sy in ((by0, -1), (by1, 1)):
        xo = xe + sx * FG                               # fence inner face, x side
        yo = ye + sy * FG
        rrect(sk, min(xo, xo + sx * FT), min(ye - sy * 2.2, ye - sy * 4.2), max(xo, xo + sx * FT), max(ye - sy * 2.2, ye - sy * 4.2), 0)
        rrect(sk, min(xe - sx * 2.2, xe - sx * 4.2), min(yo, yo + sy * FT), max(xe - sx * 2.2, xe - sx * 4.2), max(yo, yo + sy * FT), 0)
feature(tray, "Pad", "BoardFences", sk, Length=ZB - TRAY_TOP + FH)
# battery-lead notch at the USB-end corner (the cell's tab end)
sk = sketch(tray, "SkLeadNotch", "XY_Plane", TRAY_TOP)
rrect(sk, tx1 - 3.0, CY - 2.5, tx1 + 1.0, CY + 2.5, 0)
feature(tray, "Pocket", "LeadNotch", sk, Type="ThroughAll")

# ---------------- PAN/TILT TURRET (SG90 / MG90S) ----------------
# SG90 local frame: body 22.5 (x) x 12.2 (y) x 22.7 (z) from z=0, shaft at x = -5.35, up = +z.
SG_L, SG_W, SG_H, SG_SHAFT = 22.8, 12.2, 22.7, -5.35   # Waveshare MG90S 15024: 22.8 x 12.2 (stated)
SG_TAB_L, SG_TAB_Z0, SG_TAB_T, SG_HOLE = 32.2, 15.9, 2.5, 13.9
SG_HORN_TOP = 31.0                      # horn arm face, local z


def sg90(placement, horn_len):
    b = Part.makeBox(SG_L, SG_W, SG_H, V(-SG_L / 2, -SG_W / 2, 0))
    b = b.fuse(Part.makeBox(SG_TAB_L, SG_W, SG_TAB_T, V(-SG_TAB_L / 2, -SG_W / 2, SG_TAB_Z0)))
    for hx in (-SG_HOLE, SG_HOLE):
        b = b.cut(Part.makeCylinder(1.0, 5, V(hx, 0, SG_TAB_Z0 - 1)))
    b = b.fuse(Part.makeCylinder(SG_W / 2, 4.2, V(SG_SHAFT, 0, SG_H)))
    b = b.fuse(Part.makeCylinder(2.4, 2.6, V(SG_SHAFT, 0, SG_H + 4.2)))
    hw = HORN_W - 0.6
    h = Part.makeCylinder(3.5, SG_HORN_TOP - 26.9, V(SG_SHAFT, 0, 26.9))
    h = h.fuse(Part.makeBox(horn_len - hw, hw, HORN_T, V(SG_SHAFT - (horn_len - hw) / 2, -hw / 2, SG_HORN_TOP - HORN_T)))
    for cx in (SG_SHAFT - (horn_len - hw) / 2, SG_SHAFT + (horn_len - hw) / 2):
        h = h.fuse(Part.makeCylinder(hw / 2, HORN_T, V(cx, 0, SG_HORN_TOP - HORN_T)))
    b = b.removeSplitter()
    h = h.removeSplitter()
    b.Placement, h.Placement = placement, placement
    return b, h


# tilt servo: local z -> -X, local x -> +Z, horn face on the drive cheek's outer face
TILT_PL = App.Placement(V(OUT_L + SG_HORN_TOP, CY, TZ - SG_SHAFT), App.Rotation(V(0, 1, 0), -90))
tilt_body, tilt_horn = sg90(TILT_PL, HORN_L)
tilt_horn.rotate(V(0, CY, TZ), V(1, 0, 0), 90)            # horn arm along Y, in the cheek pocket
# pan servo: upright, shaft under the enclosure centre, horn face under the yoke plate
PX = OUT_L / 2
YOKE_T, YOKE_Z = 3.5, TZ - CHEEK_R - 7.0                     # yoke plate top (3.5 mm: stiff, not bulky)
PAN_PL = App.Placement(V(PX - SG_SHAFT, CY, YOKE_Z - YOKE_T - SG_HORN_TOP), App.Rotation())
pan_body, pan_horn = sg90(PAN_PL, 32.0)
for n, shp in (("Jig_Servo_Tilt", tilt_body), ("Jig_Horn_Tilt", tilt_horn), ("Jig_Servo_Pan", pan_body),
               ("Jig_Horn_Pan", pan_horn)):
    j = doc.addObject("Part::Feature", n)
    j.Shape = shp
    jig.addObject(j)

# ---- YOKE: plate on the pan horn, pivot upright (left), tilt-servo upright (right) ----
yk = doc.addObject("PartDesign::Body", "Yoke")
LX0, LX1 = -0.5 - 3.0, -0.5                 # pivot upright (3 mm), 0.5 clear of the cheek
RX1 = OUT_L + SG_HORN_TOP - SG_TAB_Z0 - SG_TAB_T     # servo tabs bear on the upright's +X face
RX0 = RX1 - 3.0
YW = CHEEK_R + 1.5
sk = sketch(yk, "SkYokePlate", "XY_Plane", YOKE_Z - YOKE_T)
rrect(sk, LX0, CY - YW, RX1, CY + YW, 2.0)
feature(yk, "Pad", "YokePlate", sk, Length=YOKE_T)
TILT_ZC = TZ - SG_SHAFT                                    # servo body centre height
UP_TOP = TILT_ZC + SG_TAB_L / 2 + 1.5
sk = sketch(yk, "SkPivotUpright", "YZ_Plane", LX0)          # same height as the servo side: a U that wraps the camera
rrect(sk, CY - YW, YOKE_Z - 0.2, CY + YW, UP_TOP, 3.0)
sk.addGeometry(Part.Circle(V(CY, TZ, 0), V(0, 0, 1), 1.15 + FIT / 2))        # M2 clearance (pivot = M2 x 8)
feature(yk, "Pad", "PivotUpright", sk, Length=LX1 - LX0)
sk = sketch(yk, "SkServoUpright", "YZ_Plane", RX0)
rrect(sk, CY - YW, YOKE_Z - 0.2, CY + YW, UP_TOP, 3.0)
rrect(sk, CY - SG_W / 2 - 0.2 - FIT, TILT_ZC - SG_L / 2 - 0.2 - FIT,
      CY + SG_W / 2 + 0.2 + FIT, TILT_ZC + SG_L / 2 + 0.2 + FIT, 0)
feature(yk, "Pad", "ServoUpright", sk, Length=RX1 - RX0)
sk = sketch(yk, "SkTiltServoPilots", "YZ_Plane", RX1)
for dz in (-SG_HOLE, SG_HOLE):
    sk.addGeometry(Part.Circle(V(CY, TILT_ZC + dz, 0), V(0, 0, 1), 0.8 + FIT / 2))
feature(yk, "Pocket", "TiltServoPilots", sk, Length=RX1 - RX0)
# pan horn pocket under the plate + screwdriver access from above
sk = sketch(yk, "SkPanHorn", "XY_Plane", YOKE_Z - YOKE_T)
slot_h(sk, PX, CY, 32.6 + 2 * FIT, HORN_W + 2 * FIT)
feature(yk, "Pocket", "PanHornPocket", sk, Reversed=True, Length=1.2)
sk = sketch(yk, "SkPanScrewAccess", "XY_Plane", YOKE_Z)
sk.addGeometry(Part.Circle(V(PX, CY, 0), V(0, 0, 1), 2.0))
feature(yk, "Pocket", "PanScrewAccess", sk, Type="ThroughAll")
# rounded inside corners where the uprights meet the plate
dressup_soft(yk, "Fillet", "YokeInsideCorners",
        lambda e: at_z(e, YOKE_Z) and e.BoundBox.XLength < 1e-4 and (abs(e.BoundBox.XMin - LX1) < 1e-3
                                                                  or abs(e.BoundBox.XMin - RX0) < 1e-3), (2.5, 1.5, 0.8))
dressup_soft(yk, "Fillet", "YokeOuterCorners",
             lambda e: at_z(e, YOKE_Z - YOKE_T) and e.BoundBox.XLength < 1e-4 and (abs(e.BoundBox.XMin - LX0) < 1e-3
                                                                                or abs(e.BoundBox.XMin - RX1) < 1e-3), (4.0, 3.0, 2.0))

# ---- tilt-servo SHROUD: finned slip-on cover over the servo body beyond its tabs ----
sh = doc.addObject("PartDesign::Body", "ServoShroud")
SH_X0 = RX1 + 0.2                                          # against the upright: covers the tabs
SH_WALL = 1.3
SH_X1 = OUT_L + SG_HORN_TOP + SH_WALL + 0.3                # servo bottom + clearance + wall
SH_HY, SH_HZ = SG_W / 2 + 0.25 + SH_WALL, SG_TAB_L / 2 + 0.3 + SH_WALL
TAB_POCKET = SG_TAB_T + 0.3
sk = sketch(sh, "SkShroud", "YZ_Plane", SH_X0)
rrect(sk, CY - SH_HY, TILT_ZC - SH_HZ, CY + SH_HY, TILT_ZC + SH_HZ, 2.0)
feature(sh, "Pad", "ShroudShell", sk, Length=SH_X1 - SH_X0)
sk = sketch(sh, "SkShroudBore", "YZ_Plane", SH_X0)
rrect(sk, CY - SG_W / 2 - 0.25 - FIT, TILT_ZC - SG_L / 2 - 0.25 - FIT, CY + SG_W / 2 + 0.25 + FIT, TILT_ZC + SG_L / 2 + 0.25 + FIT, 0.4)
feature(sh, "Pocket", "ShroudBore", sk, Reversed=True, Length=SH_X1 - SH_X0 - SH_WALL)
sk = sketch(sh, "SkShroudTabs", "YZ_Plane", SH_X0)
rrect(sk, CY - SG_W / 2 - 0.25 - FIT, TILT_ZC - SG_TAB_L / 2 - 0.3 - FIT, CY + SG_W / 2 + 0.25 + FIT,
      TILT_ZC + SG_TAB_L / 2 + 0.3 + FIT, 0.4)
feature(sh, "Pocket", "ShroudTabPocket", sk, Reversed=True, Length=TAB_POCKET)
# the servo's two tab screws go through the shroud: counterbored access from the finned face
sk = sketch(sh, "SkShroudScrewAccess", "YZ_Plane", SH_X1)
for dz in (-SG_HOLE, SG_HOLE):
    sk.addGeometry(Part.Circle(V(CY, TILT_ZC + dz, 0), V(0, 0, 1), 2.2))
feature(sh, "Pocket", "ShroudScrewAccess", sk, Length=SH_X1 - SH_X0 - TAB_POCKET - SH_WALL)
sk = sketch(sh, "SkShroudScrewClear", "YZ_Plane", SH_X1)
for dz in (-SG_HOLE, SG_HOLE):
    sk.addGeometry(Part.Circle(V(CY, TILT_ZC + dz, 0), V(0, 0, 1), 1.15 + FIT / 2))
feature(sh, "Pocket", "ShroudScrewClear", sk, Type="ThroughAll")
# fins on the outer end face
sk = sketch(sh, "SkFins", "YZ_Plane", SH_X1)
for k in range(4):                                         # vertical fins
    yc = CY - SH_HY + 2.4 + k * (2 * SH_HY - 4.8) / 3
    rrect(sk, yc - 0.45, TILT_ZC - SH_HZ + 2.5, yc + 0.45, TILT_ZC + SH_HZ - 2.5, 0.4)
feature(sh, "Pad", "Fins", sk, Length=1.0)
# servo-cable exit at the top of the end face (the SG90 lead leaves the case bottom, high in this pose)
sk = sketch(sh, "SkShroudCable", "XY_Plane", TILT_ZC + SH_HZ)
rrect(sk, SH_X1 - 6.5, CY - 2.2, SH_X1 - 1.4, CY + 2.2, 1.0)
feature(sh, "Pocket", "ShroudCableExit", sk, Length=3.0)
dressup_soft(sh, "Fillet", "ShroudOuterEdges", lambda e: abs(e.BoundBox.XMin - SH_X1) < 1e-3 and abs(e.BoundBox.XMax - SH_X1) < 1e-3
        and (abs(e.BoundBox.YLength - (2 * SH_HY - 4.0)) < 0.05 or abs(e.BoundBox.ZLength - (2 * SH_HZ - 4.0)) < 0.05), (1.2, 0.8, 0.5))

# ---- PAN BASE: houses the pan servo; magnet + two screw ears underneath ----
pb = doc.addObject("PartDesign::Body", "PanBase")
PCX = PX - SG_SHAFT                          # servo body centre
SV_TOP = PAN_PL.Base.z + SG_TAB_Z0           # tab underside = housing top
SV_BOT = PAN_PL.Base.z
MAG_D, MAG_T, MAG_DEPTH = 12.7, 3.175, 3.3
FLOOR_B = SV_BOT - 0.3 - MAG_DEPTH - 0.9     # housing underside
HX0, HX1 = PCX - SG_TAB_L / 2 - 2.0, PCX + SG_TAB_L / 2 + 2.0
HY = SG_W / 2 + 2.0
sk = sketch(pb, "SkHousing", "XY_Plane", FLOOR_B)
rrect(sk, HX0, CY - HY, HX1, CY + HY, 2.5)
feature(pb, "Pad", "Housing", sk, Length=SV_TOP - FLOOR_B)
sk = sketch(pb, "SkFlange", "XY_Plane", FLOOR_B)
rrect(sk, HX0 - 8, CY - HY - 4, HX1 + 8, CY + HY + 4, 4.0)
feature(pb, "Pad", "Flange", sk, Length=3.0)
sk = sketch(pb, "SkServoBay", "XY_Plane", SV_TOP)
rrect(sk, PCX - SG_L / 2 - 0.2 - FIT, CY - SG_W / 2 - 0.2 - FIT, PCX + SG_L / 2 + 0.2 + FIT, CY + SG_W / 2 + 0.2 + FIT, 0)
feature(pb, "Pocket", "ServoBay", sk, Length=SV_TOP - SV_BOT + 0.3)
sk = sketch(pb, "SkPanServoPilots", "XY_Plane", SV_TOP)
for dx in (-SG_HOLE, SG_HOLE):
    sk.addGeometry(Part.Circle(V(PCX + dx, CY, 0), V(0, 0, 1), 0.8 + FIT / 2))
feature(pb, "Pocket", "PanServoPilots", sk, Length=6.0)
sk = sketch(pb, "SkPanCableSlot", "XZ_Plane", -(CY + HY))
rrect(sk, PCX + SG_L / 2 - 4.0, SV_BOT + 1.0, PCX + SG_L / 2, SV_BOT + 4.0, 0)
feature(pb, "Pocket", "PanCableSlot", sk, Midplane=True, Length=2 * HY)          # +Y wall only
sk = sketch(pb, "SkMagnet", "XY_Plane", FLOOR_B)
sk.addGeometry(Part.Circle(V(PCX, CY, 0), V(0, 0, 1), 13.1 / 2))
feature(pb, "Pocket", "MagnetPocket", sk, Reversed=True, Length=MAG_DEPTH)
sk = sketch(pb, "SkMagnetRibs", "XY_Plane", FLOOR_B + 0.6)
for k in range(6):
    a = math.radians(60 * k)
    sk.addGeometry(Part.Circle(V(PCX + 6.55 * math.cos(a), CY + 6.55 * math.sin(a), 0), V(0, 0, 1), 0.4 - FIT))
feature(pb, "Pad", "MagnetCrushRibs", sk, Length=MAG_T - 0.6)
sk = sketch(pb, "SkEarScrews", "XY_Plane", FLOOR_B + 3.0)
for x in (HX0 - 4.0, HX1 + 4.0):
    sk.addGeometry(Part.Circle(V(x, CY, 0), V(0, 0, 1), 1.75))
feature(pb, "Pocket", "EarScrewHoles", sk, Type="ThroughAll")
dressup(pb, "Chamfer", "EarCountersinks", circ(1.75, FLOOR_B + 3.0), 1.4)
dressup(pb, "Chamfer", "MagnetLeadIn", circ(13.1 / 2, FLOOR_B), 0.4)

# ---------------- USB-C CABLE MANAGEMENT ----------------
# Power enters a fixed pod on the PanBase (panel-mount USB-C socket); a short flexible
# cable runs pod -> yoke-plate clip (pan loop) -> upright clip -> enclosure strain-relief
# clip (tilt loop) -> right-angle plug in the board's USB-C port.
CBL_D = 4.0                                 # flexible USB-C cable
PM_HOLE_D, PM_LIP_D, PM_NUT_D = 12.6, 16.0, 18.0   # Adafruit 6069 round panel USB-C (12-18 mm hole, nut)


def c_clip(body, name, z0, centre, opening_deg, stem_from, length=4.0):
    """C-clip in an XY sketch at z0: ring round `centre`, opening toward opening_deg,
    stem (a rectangle from `stem_from`, a (x0, y0, x1, y1) box) tying it to the part."""
    sk = sketch(body, "Sk" + name, "XY_Plane", z0)
    ro, ri, half = CBL_D / 2 + 1.4, CBL_D / 2 + 0.1 + FIT / 2, math.radians(50)
    a0, a1 = math.radians(opening_deg) + half, math.radians(opening_deg) - half + 2 * math.pi
    c_profile(sk, centre[0], centre[1], ro, ri, a0, a1)
    f = feature(body, "Pad", name, sk, Length=length)
    # the stem is its own extrude: overlapping wires in one sketch made a non-manifold mesh
    sk2 = sketch(body, "Sk" + name + "Stem", "XY_Plane", z0)
    rrect(sk2, *stem_from, 0)
    feature(body, "Pad", name + "Stem", sk2, Length=length)
    return f


# enclosure strain-relief clip under the USB-C port (moves with the camera)
CLIP_E = (OUT_L + 3.9, CY)                  # straight under the right-angle plug's cable exit
c_clip(base, "ClipUSBStrain", 4.0, CLIP_E, 0, (OUT_L - 0.2, CY - 1.6, CLIP_E[0] - CBL_D / 2 - 1.0, CY + 1.6))
# yoke clips: on the servo upright's +Y edge, and on the plate's +Y edge near the pan axis
CLIP_Y1 = (RX0 + 1.5, CY + YW + CBL_D / 2 + 1.4 - 0.8)      # ring overlaps the upright by 0.8 (not tangent)
c_clip(yk, "ClipUpright", -6.0, CLIP_Y1, 90, (RX0, CY + YW - 0.2, RX1, CLIP_Y1[1] - CBL_D / 2 - 1.0))
# pan-loop clip: the cable runs along X under the plate edge here, so this clip's axis is X
CLIP_Y2 = (PX + 2.0, CY + YW + CBL_D / 2 + 1.4 - 0.8, YOKE_Z - YOKE_T - CBL_D / 2 - 1.4 + 0.8)   # ring overlaps the plate by 0.8
sk = sketch(yk, "SkClipPanLoop", "YZ_Plane", CLIP_Y2[0] - 2.0)
ro, ri, half = CBL_D / 2 + 1.4, CBL_D / 2 + 0.1 + FIT / 2, math.radians(50)
a0, a1 = math.radians(-90) + half, math.radians(-90) - half + 2 * math.pi       # opening points down
c_profile(sk, CLIP_Y2[1], CLIP_Y2[2], ro, ri, a0, a1)
feature(yk, "Pad", "ClipPanLoop", sk, Length=4.0)
sk = sketch(yk, "SkClipPanLoopStem", "YZ_Plane", CLIP_Y2[0] - 2.0)
rrect(sk, CY + YW - 3.0, YOKE_Z - YOKE_T - 0.01 - 0.6, CLIP_Y2[1] - CBL_D / 2 - 0.3, YOKE_Z - YOKE_T + 0.01, 0)
feature(yk, "Pad", "ClipPanLoopStem", sk, Length=4.0)

# power pod on the PanBase's +Y side: open-bottom shell, socket in the outer face, cable out the top
POD_X0, POD_X1 = PCX - 12.0, PCX + 12.0
POD_Y0, POD_Y1 = CY + HY + 1.8, CY + HY + 15.0            # 1.8 mm gap: the pan cap's skirt drops past it
POD_TOP = FLOOR_B + 23.0
sk = sketch(pb, "SkPod", "XY_Plane", FLOOR_B)
rrect(sk, POD_X0, POD_Y0, POD_X1, POD_Y1, 2.0)
feature(pb, "Pad", "PowerPod", sk, Length=POD_TOP - FLOOR_B)
sk = sketch(pb, "SkPodHollow", "XY_Plane", FLOOR_B)
rrect(sk, POD_X0 + 1.6, POD_Y0 + 0.01, POD_X1 - 1.6, POD_Y1 - 1.6, 0.8)
feature(pb, "Pocket", "PodHollow", sk, Reversed=True, Length=POD_TOP - FLOOR_B - 1.6)
POD_EXIT = (PCX - 4.0, (POD_Y0 + POD_Y1) / 2 - 0.8)
sk = sketch(pb, "SkPodExit", "XY_Plane", POD_TOP)
sk.addGeometry(Part.Circle(V(POD_EXIT[0], POD_EXIT[1], 0), V(0, 0, 1), CBL_D / 2 + 0.6))
feature(pb, "Pocket", "PodCableExit", sk, Type="ThroughAll")
# spring seat: a boss on the pod top with a counterbore concentric with the cable exit; the spring's
# end coils drop in, so the spring is held on the cable's axis instead of floating beside it
SPR_L, SPR_R, SPR_WIRE = 8.0, 2.6, 0.5                    # coil-centre radius 2.6 -> 4.7 ID, 5.7 OD
SEAT_H, SEAT_DEPTH = 3.0, 2.5
sk = sketch(pb, "SkSpringSeat", "XY_Plane", POD_TOP)
sk.addGeometry(Part.Circle(V(POD_EXIT[0], POD_EXIT[1], 0), V(0, 0, 1), 4.2))
feature(pb, "Pad", "SpringSeat", sk, Length=SEAT_H)
sk = sketch(pb, "SkSpringSeatBore", "XY_Plane", POD_TOP + SEAT_H)
sk.addGeometry(Part.Circle(V(POD_EXIT[0], POD_EXIT[1], 0), V(0, 0, 1), SPR_R + SPR_WIRE / 2 + 0.2 + FIT / 2))
feature(pb, "Pocket", "SpringSeatBore", sk, Length=SEAT_DEPTH)
sk = sketch(pb, "SkSpringSeatExit", "XY_Plane", POD_TOP + SEAT_H)
sk.addGeometry(Part.Circle(V(POD_EXIT[0], POD_EXIT[1], 0), V(0, 0, 1), CBL_D / 2 + 0.6))
feature(pb, "Pocket", "SpringSeatExit", sk, Type="ThroughAll")
SPR_Z0 = POD_TOP + SEAT_H - SEAT_DEPTH + SPR_WIRE / 2      # bottom coil rests on the counterbore floor
SOCK_Z = FLOOR_B + 10.5
sk = sketch(pb, "SkPanelSocket", "XZ_Plane", -POD_Y1)
sk.addGeometry(Part.Circle(V(PCX, SOCK_Z, 0), V(0, 0, 1), PM_HOLE_D / 2 + FIT / 2))
feature(pb, "Pocket", "PanelSocketHole", sk, Midplane=True, Length=3.4)        # the pod's outer wall only

# cable jigs (rest pose): right-angle plug, panel socket, internal cable, outside cable
plug = Part.makeBox(6.6, 8.4, 2.6, V(OUT_L - 6.6 + 1.0, USB_C.y - 4.2, USB_C.z - 1.3))       # tongue in the port
plug = plug.fuse(Part.makeBox(5.0, 10.6, 5.6, V(OUT_L + 1.4, USB_C.y - 5.3, USB_C.z - 2.8)))     # overmold head
plug = plug.fuse(Part.makeBox(4.6, 7.0, 7.0, V(OUT_L + 1.6, USB_C.y - 3.5, USB_C.z - 9.8)))      # right-angle leg
plug = plug.removeSplitter()
try:
    plug = plug.makeFillet(0.6, [e for e in plug.Edges if e.BoundBox.XMin > OUT_L + 1.3 and e.Length > 1.5])
except Part.OCCError:
    pass                                     # rounding is cosmetic on a reference part
ax = V(0, 1, 0)
sock = Part.makeCylinder(PM_HOLE_D / 2 - 0.2, 9.0, V(PCX, POD_Y1 - 9.0, SOCK_Z), ax)                  # threaded body
sock = sock.fuse(Part.makeCylinder(PM_LIP_D / 2, 1.2, V(PCX, POD_Y1, SOCK_Z), ax))                    # front lip
nut = Part.makeCylinder(PM_NUT_D / 2, 2.4, V(PCX, POD_Y1 - 1.6 - 2.4, SOCK_Z), ax)
sock = sock.fuse(nut).cut(Part.makeBox(8.4, 3.0, 2.6, V(PCX - 4.2, POD_Y1 - 1.8, SOCK_Z - 1.3)))      # the C port


def tube(pts, d=CBL_D):
    # cubic B-spline with the waypoints as poles (ends clamped): stays inside the waypoints'
    # hull, so it can't overshoot into a clip or a cheek the way an interpolating spline does
    poles = [pts[0]] * 2 + pts + [pts[-1]] * 2
    spl = Part.BSplineCurve()
    spl.buildFromPoles(poles, False, 3)
    edge = spl.toShape()
    try:
        w = Part.Wire(edge)
        prof = Part.Wire(Part.makeCircle(d / 2, pts[0], edge.tangentAt(edge.FirstParameter)))
        t = w.makePipeShell([prof], True, True)
        if t.isValid():
            return t
    except Part.OCCError:
        pass
    samples = edge.discretize(60)            # fallback: a compound of short cylinders (no booleans)
    return Part.makeCompound([Part.makeCylinder(d / 2, (b - a).Length, a, b - a) for a, b in zip(samples, samples[1:])])


def tube_chain(spans, d=CBL_D):
    # a compound, not a fused solid: fusing tangent pipe sections hung OCC (2026-10-20)
    segs = []
    for sp in spans:
        segs.append(Part.makeCylinder(d / 2, (sp[1] - sp[0]).Length, sp[0], sp[1] - sp[0]) if len(sp) == 2 else tube(sp, d))
        segs.append(Part.makeSphere(d / 2, sp[-1]))
    return Part.makeCompound(segs)


plug_exit = V(OUT_L + 3.9, USB_C.y, USB_C.z - 9.8)
YC1 = CLIP_Y1[1]
# straight runs exactly on each clip's axis, smooth spans between them
E_TOP, E_BOT = V(CLIP_E[0], CY, 8.6), V(CLIP_E[0], CY, 3.4)
Y1_TOP, Y1_BOT = V(CLIP_Y1[0], YC1, -1.0), V(CLIP_Y1[0], YC1, -7.0)
Y2_IN, Y2_OUT = V(CLIP_Y2[0] + 2.6, CLIP_Y2[1], CLIP_Y2[2]), V(CLIP_Y2[0] - 2.6, CLIP_Y2[1], CLIP_Y2[2])
# the cable runs straight up through the whole spring before it starts to curve
POD_IN, POD_DN = V(POD_EXIT[0], POD_EXIT[1], SPR_Z0 + SPR_L + 1.0), V(POD_EXIT[0], POD_EXIT[1], POD_TOP - 1.0)
cable_spans = [[plug_exit, E_TOP], [E_TOP, E_BOT],
               [E_BOT, E_BOT + V(0, 0, -1.5), V(CLIP_E[0] + 1.5, CY + 7.0, 1.4), V(RX0 - 2.5, YC1 + 0.3, 2.0), Y1_TOP + V(0, 0, 1.5), Y1_TOP],
               [Y1_TOP, Y1_BOT],
               [Y1_BOT, Y1_BOT + V(0, 0, -2.0), V(CLIP_Y1[0], YC1, CLIP_Y2[2] + 2.0), Y2_IN + V(3.0, 0, 0), Y2_IN],
               [Y2_IN, Y2_OUT],
               [Y2_OUT, Y2_OUT + V(-2.5, 0, 0), V(POD_EXIT[0] - 4.0, (CLIP_Y2[1] + POD_EXIT[1]) / 2 + 2.0, max(CLIP_Y2[2], POD_IN.z) + 4.0),   # stays above the spring
                POD_IN + V(0, 0, 2.5), POD_IN],
               [POD_IN, POD_DN]]
cable_pts = [p_ for sp in cable_spans for p_ in sp]
# the power cable: an ordinary USB-C cable - plug shell in the socket, moulded grip, tapered
# strain relief, 3.8 mm round cable lying away across the desk
UC_D, GRIP_L, RELIEF_L = 3.8, 13.0, 8.0
y_face = POD_Y1 + 1.2
ext_plug = Part.makeBox(8.3, 6.6, 2.5, V(PCX - 4.15, y_face - 6.6, SOCK_Z - 1.25))           # shell (in the socket)
grip = Part.makeBox(10.6, GRIP_L, 5.6, V(PCX - 5.3, y_face, SOCK_Z - 2.8))
try:
    grip = grip.makeFillet(1.6, [e for e in grip.Edges if abs(e.BoundBox.YLength - GRIP_L) < 1e-3])
except Part.OCCError:
    pass
relief = Part.makeCone(2.7, UC_D / 2, RELIEF_L, V(PCX, y_face + GRIP_L, SOCK_Z), V(0, 1, 0))
ext_plug = Part.makeCompound([ext_plug, grip, relief])
ext_pts = [V(PCX, y_face + GRIP_L + RELIEF_L, SOCK_Z), V(PCX, y_face + GRIP_L + RELIEF_L + 8.0, SOCK_Z - 0.6),
           V(PCX - 5.0, y_face + GRIP_L + RELIEF_L + 22.0, FLOOR_B + UC_D / 2),
           V(PCX - 30.0, y_face + GRIP_L + RELIEF_L + 38.0, FLOOR_B + UC_D / 2)]
j = doc.addObject("Part::Feature", "Jig_USBC_PowerPlug")
j.Shape = ext_plug
jig.addObject(j)
for n, shp in (("Jig_USB_Plug_RA", plug.removeSplitter()), ("Jig_USB_PanelSocket", sock.removeSplitter()),
               ("Jig_USB_Cable_Internal", tube_chain(cable_spans)), ("Jig_USB_Cable_Outside", tube_chain([ext_pts], UC_D))):
    j = doc.addObject("Part::Feature", n)
    j.Shape = shp
    jig.addObject(j)
CABLE_PTS, EXT_PTS = cable_pts, ext_pts      # exported for the video script

# light pipe: clear printed L-rod. The board's LED sits under the microSD card, so the pipe
# runs sideways (under the card) to the wall, then straight up to a flush dot in the lid.
LED_EST = s2e(10.6, 1.25, -2.6)              # charge LED (not in Seeed's model: estimated)
LP_R = 0.9
LP_TOP = V(LED_EST.x - 1.6, WALL + 1.3, BASE_H + LID_T)
LP_Z = (USB_C.z + 1.3 + CARD_TOP - 1.65) / 2        # between the plug tongue and the card's underside
LP_ELBOW = V(LP_TOP.x, LP_TOP.y, LP_Z)
LP_TIP = V(LED_EST.x, LED_EST.y, LP_Z)
lp = doc.addObject("PartDesign::Body", "LightPipe")
sk = sketch(lp, "SkPipeRiser", "XY_Plane", LP_ELBOW.z - LP_R)
sk.addGeometry(Part.Circle(V(LP_TOP.x, LP_TOP.y, 0), V(0, 0, 1), LP_R))
feature(lp, "Pad", "Riser", sk, Length=LP_TOP.z - (LP_ELBOW.z - LP_R))
dxy = V(LP_TIP.x - LP_ELBOW.x, LP_TIP.y - LP_ELBOW.y, 0)
arm = Part.makeCylinder(LP_R, dxy.Length, LP_ELBOW, dxy)
arm_obj = doc.addObject("Part::Feature", "LightPipeArmTool")
arm_obj.Shape = arm
arm_obj.Visibility = False
doc.recompute()
lp_shape = lp.Shape.fuse(arm).removeSplitter()
sk = sketch(lid, "SkLightPipeHole", "XY_Plane", TOP)
sk.addGeometry(Part.Circle(V(LP_TOP.x, LP_TOP.y, 0), V(0, 0, 1), LP_R + 0.1 + FIT / 2))
feature(lid, "Pocket", "LightPipeHole", sk, Type="ThroughAll")
doc.recompute()
lpj = doc.addObject("Part::Feature", "LightPipePrint")      # the printable L-rod (one solid)
lpj.Shape = lp_shape
for helper in (lp, arm_obj):                 # construction only: the print is LightPipePrint
    doc.removeObject(helper.Name) if helper.TypeId == "Part::Feature" else None
lp.Visibility = False

# base label on the pod's outer face: "USB-C  5V" (engraved 0.4 mm)
import Draft  # noqa: E402
lab = Draft.make_shapestring(String="USB-C  5V", FontFile="/System/Library/Fonts/Supplemental/Arial Bold.ttf",
                             Size=2.2, Tracking=0)
doc.recompute()
lw = lab.Shape.BoundBox.XLength
lab.Placement = App.Placement(V(PCX + lw / 2, POD_Y1, SOCK_Z + PM_LIP_D / 2 + 0.6),   # above the socket
                              App.Rotation(V(0, 0, 1), 180).multiply(App.Rotation(V(1, 0, 0), 90)))
pb.addObject(lab)
doc.recompute()
pk = pb.newObject("PartDesign::Pocket", "PodLabel")
pk.Profile = lab
pk.Length = 0.4
lab.Visibility = False
doc.recompute()

# spring strain reliefs (steel compression springs, 4.5 mm ID) at the pod's two cable exits
def spring(origin, axis, length=10.0, coils=7, wire=0.5, r=2.6):
    hx = Part.makeHelix(length / coils, length, r)
    prof = Part.Wire(Part.makeCircle(wire / 2, V(r, 0, 0), V(0, 1, 0)))
    sw = Part.Wire(hx).makePipeShell([prof], True, True)
    rotn = App.Rotation(V(0, 0, 1), axis)
    sw.Placement = App.Placement(origin, rotn)
    return sw


springs = Part.makeCompound([spring(V(POD_EXIT[0], POD_EXIT[1], SPR_Z0), V(0, 0, 1), length=SPR_L, coils=6,
                                     wire=SPR_WIRE, r=SPR_R)])   # the power cable has its own moulded relief
j = doc.addObject("Part::Feature", "Jig_SpringReliefs")
j.Shape = springs
jig.addObject(j)

pc = doc.addObject("PartDesign::Body", "PanCap")
# cap with a 5 mm skirt over the housing; two M2 x 8 go in sideways through the skirt into the
# housing's solid ends. The pan servo keeps its own two M2 x 8 tab screws (same screw type).
CAP_IN_TOP = SV_BOT + SG_H + 0.3               # just over the servo case top
CAP_T, COLLAR_H, SKIRT, SKIRT_T = 2.0, 2.2, 5.0, 1.4
sk = sketch(pc, "SkCap", "XY_Plane", SV_TOP - SKIRT)
rrect(sk, HX0 - SKIRT_T, CY - HY - SKIRT_T, HX1 + SKIRT_T, CY + HY + SKIRT_T, 3.5)
feature(pc, "Pad", "CapShell", sk, Length=CAP_IN_TOP + CAP_T - (SV_TOP - SKIRT))
sk = sketch(pc, "SkCapSkirt", "XY_Plane", SV_TOP - SKIRT)
rrect(sk, HX0 - 0.15 - FIT, CY - HY - 0.15 - FIT, HX1 + 0.15 + FIT, CY + HY + 0.15 + FIT, 2.5)
feature(pc, "Pocket", "SkirtRecess", sk, Reversed=True, Length=SKIRT)
sk = sketch(pc, "SkCapHollow", "XY_Plane", SV_TOP)
rrect(sk, PCX - SG_TAB_L / 2 - 0.3 - FIT, CY - SG_W / 2 - 0.3 - FIT, PCX + SG_TAB_L / 2 + 0.3 + FIT, CY + SG_W / 2 + 0.3 + FIT, 0.6)
feature(pc, "Pocket", "CapHollow", sk, Reversed=True, Length=CAP_IN_TOP - SV_TOP)
sk = sketch(pc, "SkCollar", "XY_Plane", CAP_IN_TOP + CAP_T)
sk.addGeometry(Part.Circle(V(PX, CY, 0), V(0, 0, 1), 8.0))
sk.addGeometry(Part.Circle(V(PX, CY, 0), V(0, 0, 1), SG_W / 2 + 0.5))
feature(pc, "Pad", "BearingCollar", sk, Length=COLLAR_H)
sk = sketch(pc, "SkCapSpindle", "XY_Plane", CAP_IN_TOP + CAP_T + COLLAR_H)
sk.addGeometry(Part.Circle(V(PX, CY, 0), V(0, 0, 1), SG_W / 2 + 0.5))
feature(pc, "Pocket", "SpindleHole", sk, Type="ThroughAll")
CS_Z, CS_Y = SV_TOP - SKIRT / 2 - 0.3, CY + 3.5           # clear of the vertical servo-screw pilots at y = CY
sk = sketch(pc, "SkCapSideScrews", "YZ_Plane", PX)
sk.addGeometry(Part.Circle(V(CS_Y, CS_Z, 0), V(0, 0, 1), 1.15 + FIT / 2))
feature(pc, "Pocket", "CapSideScrewHoles", sk, Type="ThroughAll", Midplane=True)
dressup_soft(pc, "Chamfer", "CollarChamfer", circ(8.0, CAP_IN_TOP + CAP_T + COLLAR_H), (0.6, 0.4))
CS_DEPTH = PCX - SG_L / 2 - 0.2 - HX0 - 0.4                # stop short of the servo bay
for name, x0, rev in (("CapPilotL", HX0, True), ("CapPilotR", HX1, False)):
    sk = sketch(pb, "Sk" + name, "YZ_Plane", x0)
    sk.addGeometry(Part.Circle(V(CS_Y, CS_Z, 0), V(0, 0, 1), 0.8 + FIT / 2))
    feature(pb, "Pocket", name, sk, Reversed=rev, Length=CS_DEPTH)
# screw heads (reference parts): cap sides (2), pan-servo tabs (2, under the cap), shroud (2)
heads = [Part.makeCylinder(1.9, 1.3, V(x, CS_Y, CS_Z), V(sgn, 0, 0)) for x, sgn in ((HX0 - SKIRT_T, -1), (HX1 + SKIRT_T, 1))]
heads += [Part.makeCylinder(1.9, 1.3, V(PCX + dx, CY, SV_TOP + SG_TAB_T)) for dx in (-SG_HOLE, SG_HOLE)]
heads += [Part.makeCylinder(1.9, 1.3, V(SH_X1 - (SH_X1 - SH_X0 - TAB_POCKET - SH_WALL), CY, TILT_ZC + dz), V(1, 0, 0))
          for dz in (-SG_HOLE, SG_HOLE)]
j = doc.addObject("Part::Feature", "Jig_Screws")
j.Shape = Part.makeCompound(heads)
jig.addObject(j)
REPORT_SCREWS = {"cap side engagement": CS_DEPTH, "M2x8 grip at cap": 8 - SKIRT_T}

mag = Part.makeCylinder(MAG_D / 2, MAG_T, V(PCX, CY, FLOOR_B))
j = doc.addObject("Part::Feature", "Jig_Magnet")
j.Shape = mag
jig.addObject(j)

doc.recompute()
try:
    import FreeCADGui as Gui
    for n, c, t in (("Base", (0.86, 0.86, 0.84), 0), ("Lid", (0.85, 0.88, 0.92), 45),
                    ("PanBase", (0.25, 0.25, 0.28), 0), ("Jig_XIAO_Sense", (0.1, 0.45, 0.2), 0),
                    ("Jig_Heatsink", (0.75, 0.75, 0.8), 0), ("Jig_FPC_Antenna", (0.95, 0.75, 0.2), 0),
                    ("Jig_Antenna_Coax", (0.15, 0.15, 0.15), 0), ("BatteryTray", (0.9, 0.55, 0.2), 0), ("Yoke", (0.35, 0.35, 0.4), 0),
                    ("ServoShroud", (0.2, 0.2, 0.22), 0), ("PanCap", (0.22, 0.22, 0.25), 0),
                    ("Jig_VentMesh", (0.6, 0.6, 0.62), 0), ("Jig_Screws", (0.7, 0.7, 0.72), 0), ("LightPipePrint", (0.85, 0.95, 1.0), 40),
                    ("Jig_SpringReliefs", (0.75, 0.75, 0.78), 0),
                    ("Jig_Servo_Tilt", (0.15, 0.3, 0.75), 0), ("Jig_Servo_Pan", (0.15, 0.3, 0.75), 0),
                    ("Jig_Horn_Tilt", (0.95, 0.95, 0.95), 0), ("Jig_Horn_Pan", (0.95, 0.95, 0.95), 0),
                    ("Jig_Magnet", (0.7, 0.7, 0.75), 0), ("Jig_USB_Plug_RA", (0.12, 0.12, 0.13), 0),
                    ("Jig_USB_PanelSocket", (0.6, 0.6, 0.62), 0), ("Jig_USB_Cable_Internal", (0.12, 0.12, 0.13), 0),
                    ("Jig_USB_Cable_Outside", (0.12, 0.12, 0.13), 0), ("Jig_USBC_PowerPlug", (0.12, 0.12, 0.13), 0),
                    ("Jig_LiPo_401723", (0.55, 0.55, 0.62), 0)):
        vo = doc.getObject(n).ViewObject
        vo.ShapeColor = c
        vo.Transparency = t
    Gui.SendMsgToActiveView("ViewFit")
except Exception:
    pass
doc.saveAs(OUT)

# ---------------- checks ----------------
REPORT = {}
for n in ("Base", "Lid", "BatteryTray", "Yoke", "PanBase", "ServoShroud", "LightPipePrint", "PanCap"):
    s = doc.getObject(n).Shape
    REPORT[n] = (s.isValid(), len(s.Solids), round(s.Volume))
parts = {n: doc.getObject(n).Shape for n in ("Base", "Lid", "BatteryTray", "Yoke", "PanBase", "ServoShroud",
                                             "LightPipePrint", "PanCap")}
jigs = {n: doc.getObject(n).Shape for n in ("Jig_XIAO_Sense", "Jig_Heatsink", "Jig_FPC_Antenna", "Jig_Antenna_Coax",
                                            "Jig_LiPo_401723", "Jig_Servo_Tilt", "Jig_Servo_Pan",
                                            "Jig_Horn_Tilt", "Jig_Horn_Pan", "Jig_Magnet", "Jig_USB_Plug_RA",
                                            "Jig_USB_PanelSocket")}
REPORT["coax x board"] = round(jigs["Jig_Antenna_Coax"].common(jigs["Jig_XIAO_Sense"]).Volume, 3)
for jn, js in jigs.items():
    for pn, ps in parts.items():
        REPORT[f"{jn} x {pn}"] = round(js.common(ps).Volume, 3)
REPORT["Lid x Base"] = round(parts["Lid"].common(parts["Base"]).Volume, 3)
REPORT["Tray x Base (bumps in grooves = 0)"] = round(parts["BatteryTray"].common(parts["Base"]).Volume, 3)
REPORT["Tray x Lid"] = round(parts["BatteryTray"].common(parts["Lid"]).Volume, 3)
REPORT["magnet x PanBase (crush only)"] = round(jigs["Jig_Magnet"].common(parts["PanBase"]).Volume, 3)
REPORT["servo/horn x own mount (rest)"] = {
    "pan body x PanBase": round(jigs["Jig_Servo_Pan"].common(parts["PanBase"]).Volume, 3),
    "tilt body x Yoke": round(jigs["Jig_Servo_Tilt"].common(parts["Yoke"]).Volume, 3),
    "tilt horn x Base": round(jigs["Jig_Horn_Tilt"].common(parts["Base"]).Volume, 3),
    "pan horn x Yoke": round(jigs["Jig_Horn_Pan"].common(parts["Yoke"]).Volume, 3),
}
# pan/tilt sweep: tilt (Base+Lid+tray+tilt horn) about the X axis at TZ, then pan everything
# above the pan horn about Z through PX; fixed = PanBase + pan servo body
tilting = [parts["Base"], parts["Lid"], parts["BatteryTray"], parts["LightPipePrint"], jigs["Jig_USB_Plug_RA"],
           jigs["Jig_Horn_Tilt"]]
yoke_set = [parts["Yoke"], jigs["Jig_Servo_Tilt"], jigs["Jig_Horn_Pan"], parts["ServoShroud"]]
fixed = [parts["PanBase"], jigs["Jig_Servo_Pan"], parts["PanCap"]]
hits = []
for pan in (-90, -45, 0, 45, 90):
    for tilt in (-60, -30, 0, 30, 60):
        tl = []
        for p_ in tilting:
            c = p_.copy(); c.rotate(V(0, CY, TZ), V(1, 0, 0), tilt); c.rotate(V(PX, CY, 0), V(0, 0, 1), pan); tl.append(c)
        yk_ = []
        for p_ in yoke_set:
            c = p_.copy(); c.rotate(V(PX, CY, 0), V(0, 0, 1), pan); yk_.append(c)
        worst = 0.0
        for a in tl[:5]:
            for b in yk_ + fixed:
                worst = max(worst, a.common(b).Volume)
        for b in [yk_[0], yk_[2]] + fixed:     # tilt horn sits on its own servo's spline: skip that pair
            worst = max(worst, tl[5].common(b).Volume)
        for a in yk_[:2] + yk_[3:]:
            for b in fixed:
                worst = max(worst, a.common(b).Volume)
        worst = max(worst, yk_[2].common(fixed[0]).Volume, yk_[2].common(fixed[2]).Volume)   # pan horn sits on its own spline: vs PanBase only
        if worst > 0.05:
            hits.append((pan, tilt, round(worst, 2)))
REPORT["pan/tilt collisions (pan, tilt, mm3)"] = hits
# spring constraint: concentric with the cable, seated in its counterbore, cable straight through it
spr = doc.getObject("Jig_SpringReliefs").Shape
cab_ = doc.getObject("Jig_USB_Cable_Internal").Shape
axis_pts = [V(POD_EXIT[0], POD_EXIT[1], SPR_Z0 + k * SPR_L / 8) for k in range(9)]
REPORT["spring check"] = {
    "cable on the spring axis along its length": all(any(sol.isInside(p_, 0.01, True) for sol in cab_.Solids) for p_ in axis_pts),
    "cable x spring (should be 0)": round(cab_.common(spr).Volume, 3),
    "spring seated (x PanBase, should be 0)": round(spr.common(parts["PanBase"]).Volume, 3),
    "spring floor gap (mm)": round(spr.BoundBox.ZMin - (POD_TOP + SEAT_H - SEAT_DEPTH), 3),
}
# access checks at the rest pose: a straight USB-C plug (12 x 6.5 overmold, 40 mm long incl. strain relief)
# pushed into the board's port, and the microSD card sliding in from outside
plug_path = Part.makeBox(40.0, 12.0, 6.5, V(OUT_L + 1.0, USB_C.y - 6.0, USB_C.z - 3.25))
sd_path = Part.makeBox(30.0, 12.6, 2.2, V(OUT_L + 1.0, SD_Y - 6.3, SD_Z - 1.1))
blockers = {n: doc.getObject(n).Shape for n in ("Yoke", "ServoShroud", "Jig_Servo_Tilt", "Jig_USB_Plug_RA")}
REPORT["straight USB-C plug path blocked by"] = {n: round(plug_path.common(b_).Volume, 2) for n, b_ in blockers.items()
                                                if n != "Jig_USB_Plug_RA" and plug_path.common(b_).Volume > 0.01}
REPORT["microSD path blocked by"] = {n: round(sd_path.common(b_).Volume, 2) for n, b_ in blockers.items()
                                     if sd_path.common(b_).Volume > 0.01}
REPORT["PanCap x pan servo / x PanBase"] = (round(parts["PanCap"].common(jigs["Jig_Servo_Pan"]).Volume, 3),
                                            round(parts["PanCap"].common(parts["PanBase"]).Volume, 3))
REPORT["vent mesh x board / tray / battery"] = tuple(round(doc.getObject("Jig_VentMesh").Shape.common(doc.getObject(n).Shape).Volume, 3)
                                                     for n in ("Jig_XIAO_Sense", "BatteryTray", "Jig_LiPo_401723"))
REPORT["shroud x tilt servo / x Yoke"] = (round(parts["ServoShroud"].common(jigs["Jig_Servo_Tilt"]).Volume, 3),
                                          round(parts["ServoShroud"].common(parts["Yoke"]).Volume, 3))
REPORT["light pipe x board / card / lid"] = (round(parts["LightPipePrint"].common(jigs["Jig_XIAO_Sense"]).Volume, 3),
                                             round(parts["LightPipePrint"].common(parts["Base"]).Volume, 3),
                                             round(parts["LightPipePrint"].common(parts["Lid"]).Volume, 3))
cab = doc.getObject("Jig_USB_Cable_Internal").Shape
REPORT["internal cable x parts at rest (clips grip ~0)"] = {n: round(cab.common(p_).Volume, 2) for n, p_ in parts.items()}
REPORT["internal cable x servos"] = (round(cab.common(jigs["Jig_Servo_Tilt"]).Volume, 2), round(cab.common(jigs["Jig_Servo_Pan"]).Volume, 2))

# fit coupons: small test prints cut from the real parts
def coupon(name, src, box):
    c = doc.addObject("Part::Feature", name)
    c.Shape = doc.getObject(src).Shape.common(box)
    return c
coupon("FitCoupon_USBEnd", "Base", Part.makeBox(OUT_L - 15.5 + 2, OUT_W, BASE_H - ZB + 1.0, V(15.5, 0, ZB - 1.0)))
coupon("FitCoupon_Boss", "Base", Part.makeBox(8, 6, BASE_H - BOSS_Z0 + 1, V(OUT_L / 2 - 4, 0, BOSS_Z0 - 1)))
coupon("FitCoupon_LidCorner", "Lid", Part.makeBox(8, 6, 3, V(OUT_L / 2 - 4, 0, BASE_H - 0.5)))
doc.recompute()
for n in ("FitCoupon_USBEnd", "FitCoupon_Boss", "FitCoupon_LidCorner"):
    doc.getObject(n).Visibility = False      # cut copies of real parts: hidden in the assembly
doc.save()
REPORT["BASE_H"], REPORT["ZB"], REPORT["CAM_C"] = round(BASE_H, 2), round(ZB, 2), (round(CAM_C.x, 2), round(CAM_C.y, 2))
REPORT["errors"] = [o.Name for o in doc.Objects if "Invalid" in o.State or "Error" in o.State]


# ---------------- pages: Enclosure and Board as their own documents ----------------
def page(name, path, objs):
    nd = App.newDocument(name)
    for n in objs:
        c = nd.copyObject(doc.getObject(n), True)
        try:
            c.ViewObject.ShapeColor = doc.getObject(n).ViewObject.ShapeColor
            c.ViewObject.Transparency = 0
        except Exception:
            pass
    nd.recompute()
    nd.saveAs(path)
    return nd


enc = page("Enclosure", OUT_ENCL, ("Base", "Lid"))
tst = page("TestPrints", os.path.join(HERE, "xiao-revD-test-prints.FCStd"),
           ("FitCoupon_USBEnd", "FitCoupon_Boss", "FitCoupon_LidCorner"))
tpg = page("BatteryTray", os.path.join(HERE, "xiao-revD-batterytray.FCStd"), ("BatteryTray",))
bpg = page("Battery", os.path.join(HERE, "xiao-revD-battery.FCStd"), ("Jig_LiPo_401723",))
mpg = page("Magnet", os.path.join(HERE, "xiao-revD-magnet.FCStd"), ("Jig_Magnet",))
ypg = page("Yoke", os.path.join(HERE, "xiao-revD-yoke.FCStd"), ("Yoke",))
ppg = page("PanBase", os.path.join(HERE, "xiao-revD-panbase.FCStd"), ("PanBase",))
s1 = page("ServoPan", os.path.join(HERE, "xiao-revD-servo-pan.FCStd"), ("Jig_Servo_Pan", "Jig_Horn_Pan"))
cpg = page("USBCable", os.path.join(HERE, "xiao-revD-usb-cable.FCStd"),
           ("Jig_USB_Plug_RA", "Jig_USB_PanelSocket", "Jig_USB_Cable_Internal", "Jig_USB_Cable_Outside",
            "Jig_USBC_PowerPlug"))
shp_ = page("ServoShroud", os.path.join(HERE, "xiao-revD-servo-shroud.FCStd"), ("ServoShroud",))
capp = page("PanCap", os.path.join(HERE, "xiao-revD-pancap.FCStd"), ("PanCap",))
meshp = page("VentMesh", os.path.join(HERE, "xiao-revD-vent-mesh.FCStd"), ("Jig_VentMesh",))
scrp = page("Screws", os.path.join(HERE, "xiao-revD-screws.FCStd"), ("Jig_Screws",))
lpp = page("LightPipe", os.path.join(HERE, "xiao-revD-lightpipe.FCStd"), ("LightPipePrint",))
sprp = page("SpringReliefs", os.path.join(HERE, "xiao-revD-spring-reliefs.FCStd"), ("Jig_SpringReliefs",))
s2 = page("ServoTilt", os.path.join(HERE, "xiao-revD-servo-tilt.FCStd"), ("Jig_Servo_Tilt", "Jig_Horn_Tilt"))
brd = page("Board", OUT_BOARD, ("Jig_XIAO_Sense", "Jig_Heatsink", "Jig_FPC_Antenna", "Jig_Antenna_Coax"))
REPORT["pages"] = {d.Name: [o.Name for o in d.Objects if o.TypeId in ("PartDesign::Body", "Part::Feature")]
                   for d in (enc, tst, brd, tpg, bpg, mpg, ypg, ppg, s1, s2, cpg, shp_, lpp, sprp, capp, meshp, scrp, doc)}
REPORT["page_errors"] = [f"{d.Name}:{o.Name}" for d in (enc, tst, brd, tpg, bpg, mpg, ypg, ppg, s1, s2, cpg, shp_, lpp, sprp, capp, meshp, scrp) for o in d.Objects
                         if "Invalid" in o.State or "Error" in o.State]

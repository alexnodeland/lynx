# SPDX-License-Identifier: CERN-OHL-S-2.0
"""Generate the lynx servo-carrier board: schematic, PCB, checks and fabrication files.

Run with KiCad's own Python (it has pcbnew):
    just pcb          # or: ~/Applications/KiCad.app/Contents/Frameworks/Python.framework/Versions/3.9/bin/python3.9 make_carrier.py

The circuit is hardware/electronics/README.md: J1 (JST-SH, 4-pin lead from the XIAO: +5V, GND, PAN, TILT)
feeds two 3-pin servo headers through 220 ohm series resistors, with 470 uF + 100 nF across the 5 V rail.
Everything here is regenerated from this file; edit the tables below, not the outputs.
"""

from __future__ import annotations

import os
import re
import shutil
import subprocess
import sys
import uuid
from pathlib import Path

HERE = Path(__file__).resolve().parent
NAME = "lynx-carrier"
KICAD = Path(os.environ.get("KICAD_APP", Path.home() / "Applications/KiCad.app"))
SHARE = KICAD / "Contents/SharedSupport"
CLI = KICAD / "Contents/MacOS/kicad-cli"
FREEROUTING = Path(os.environ.get("FREEROUTING_JAR", HERE.parents[2] / ".cache/freerouting-2.4.1.jar"))
JAVA = os.environ.get("JAVA", "/opt/homebrew/opt/openjdk/bin/java")

BOARD_W, BOARD_H = 18.0, 14.0  # mm, from the power pod's cavity in the CAD
ORIGIN = (100.0, 100.0)  # where the board's top-left corner sits on the PCB canvas

# ref, lib symbol, value, footprint (lib:name), LCSC part, MPN, schematic position (mm), board position (x, y, rot)
PARTS = [
    ("J1", "Connector_Generic:Conn_01x04", "XIAO lead", "Connector_JST:JST_SH_SM04B-SRSS-TB_1x04-1MP_P1.00mm_Horizontal",
     "C160390", "JST SM04B-SRSS-TB", (50.8, 76.2), (12.9, 3.1, 180)),
    ("J2", "Connector_Generic:Conn_01x03", "PAN servo", "Connector_PinHeader_2.54mm:PinHeader_1x03_P2.54mm_Vertical",
     "", "2.54 mm 1x3 header", (127.0, 63.5), (2.4, 11.6, 90)),
    ("J3", "Connector_Generic:Conn_01x03", "TILT servo", "Connector_PinHeader_2.54mm:PinHeader_1x03_P2.54mm_Vertical",
     "", "2.54 mm 1x3 header", (127.0, 88.9), (11.3, 11.6, 90)),
    ("R1", "Device:R", "220", "Resistor_SMD:R_0603_1608Metric", "C22962", "Yageo RC0603FR-07220RL", (88.9, 63.5), (11.6, 7.6, 0)),
    ("R2", "Device:R", "220", "Resistor_SMD:R_0603_1608Metric", "C22962", "Yageo RC0603FR-07220RL", (88.9, 88.9), (15.2, 7.6, 0)),
    ("C1", "Device:C_Polarized", "470u 6.3V", "Capacitor_THT:CP_Radial_D6.3mm_P2.50mm", "", "Panasonic EEU-FR0J471",
     (63.5, 101.6), (2.75, 4.3, 0)),  # origin is pad 1; the body is centred 1.25 mm to its right
    ("C2", "Device:C", "100n", "Capacitor_SMD:C_0603_1608Metric", "C14663", "Samsung CL10B104KO8NNNC", (76.2, 101.6), (8.2, 7.9, 90)),
]

# what each pin connects to (net names; "+5V" and "GND" become power symbols in the schematic)
NETS = {
    ("J1", "1"): "+5V", ("J1", "2"): "GND", ("J1", "3"): "PAN_IN", ("J1", "4"): "TILT_IN",
    ("R1", "1"): "PAN_IN", ("R1", "2"): "PAN", ("R2", "1"): "TILT_IN", ("R2", "2"): "TILT",
    ("J2", "1"): "PAN", ("J2", "2"): "+5V", ("J2", "3"): "GND",
    ("J3", "1"): "TILT", ("J3", "2"): "+5V", ("J3", "3"): "GND",
    ("C1", "1"): "+5V", ("C1", "2"): "GND", ("C2", "1"): "+5V", ("C2", "2"): "GND",
}
POWER = {"+5V": "power:+5V", "GND": "power:GND"}
TRACK = {"+5V": 0.5, "GND": 0.5}  # mm; signals get 0.25


def uid() -> str:
    return str(uuid.uuid4())


# ---------------------------------------------------------------- tiny s-expression helpers


def extract_symbol(lib_file: Path, name: str) -> str:
    """The `(symbol "name" ...)` block from a .kicad_sym library, found by paren matching."""
    text = lib_file.read_text()
    m = re.search(r'\n\t\(symbol "' + re.escape(name) + r'"\n', text)
    if not m:
        raise SystemExit(f"symbol {name} not in {lib_file.name}")
    i, depth = m.start() + 1, 0
    for j in range(i, len(text)):
        if text[j] == "(":
            depth += 1
        elif text[j] == ")":
            depth -= 1
            if depth == 0:
                return text[i : j + 1]
    raise SystemExit("unbalanced library file")


def pins_of(block: str) -> dict[str, tuple[float, float]]:
    """pin number -> connection point (symbol coordinates, y up)."""
    pins = {}
    for m in re.finditer(r'\(pin \w+ \w+\s+\(at ([-\d.]+) ([-\d.]+) [-\d.]+\).*?\(number "([^"]+)"', block, re.S):
        pins[m.group(3)] = (float(m.group(1)), float(m.group(2)))
    return pins


# ---------------------------------------------------------------- schematic


def write_schematic(path: Path, root: str) -> None:
    libs, pinmap = {}, {}
    for lib_id in {p[1] for p in PARTS} | set(POWER.values()) | {"power:PWR_FLAG"}:
        lib, sym = lib_id.split(":")
        block = extract_symbol(SHARE / "symbols" / f"{lib}.kicad_sym", sym)
        pinmap[lib_id] = pins_of(block)
        libs[lib_id] = block.replace(f'(symbol "{sym}"', f'(symbol "{lib_id}"', 1)

    out = [
        "(kicad_sch",
        "\t(version 20250114)",
        '\t(generator "lynx-make_carrier")',
        '\t(generator_version "9.0")',
        f'\t(uuid "{root}")',
        '\t(paper "A4")',
        '\t(title_block (title "lynx servo carrier") (rev "1") (company "lynx open hardware")'
        ' (comment 1 "Generated by make_carrier.py: edit that, not this file") (comment 2 "CERN-OHL-S-2.0"))',
        "\t(lib_symbols",
        *("\t\t" + libs[k].replace("\n", "\n\t") for k in sorted(libs)),
        "\t)",
    ]

    def place(lib_id: str, ref: str, value: str, at: tuple[float, float], fp: str = "", extra: dict | None = None,
              in_bom: bool = True, hide_ref: bool = False) -> None:
        props = {"Reference": ref, "Value": value, "Footprint": fp, "Datasheet": "~", **(extra or {})}
        out.append("\t(symbol")
        out.append(f'\t\t(lib_id "{lib_id}") (at {at[0]:.2f} {at[1]:.2f} 0) (unit 1)')
        out.append(f"\t\t(exclude_from_sim no) (in_bom {'yes' if in_bom else 'no'}) (on_board {'yes' if in_bom else 'no'}) (dnp no)")
        out.append(f'\t\t(uuid "{uid()}")')
        for k, (name, val) in enumerate(props.items()):
            hidden = name not in ("Reference", "Value") or (name == "Reference" and hide_ref)
            dy = {0: -3.81, 1: 3.81}.get(k, 0)
            out.append(f'\t\t(property "{name}" "{val}" (at {at[0] + 2.54:.2f} {at[1] + dy:.2f} 0)'
                       f' (effects (font (size 1.27 1.27)) (justify left){" (hide yes)" if hidden else ""}))')
        for num in pinmap[lib_id]:
            out.append(f'\t\t(pin "{num}" (uuid "{uid()}"))')
        out.append(f'\t\t(instances (project "{NAME}" (path "/{root}" (reference "{ref}") (unit 1))))')
        out.append("\t)")

    def label(net: str, x: float, y: float) -> None:
        out.append(f'\t(label "{net}" (at {x:.2f} {y:.2f} 0) (effects (font (size 1.27 1.27)) (justify left bottom))'
                   f' (uuid "{uid()}"))')

    npwr = 0
    flagged = set()
    for ref, lib_id, value, fp, lcsc, mpn, sch_at, _ in PARTS:
        place(lib_id, ref, value, sch_at, f"{fp}", {"LCSC": lcsc, "MPN": mpn})
        for num, (px, py) in pinmap[lib_id].items():
            x, y = sch_at[0] + px, sch_at[1] - py  # symbol y is up, the sheet's is down
            net = NETS[(ref, num)]
            if net in POWER:  # a power symbol whose pin sits on this pin end
                npwr += 1
                place(POWER[net], f"#PWR{npwr:02d}", net, (x, y), in_bom=False, hide_ref=True)
                if net not in flagged:  # power arrives through a connector pin: tell ERC it's driven
                    flagged.add(net)
                    place("power:PWR_FLAG", f"#FLG{len(flagged):02d}", "PWR_FLAG", (x, y), in_bom=False, hide_ref=True)
            else:
                label(net, x, y)
    out += ["\t(sheet_instances (path \"/\" (page \"1\")))", "\t(embedded_fonts no)", ")"]
    path.write_text("\n".join(out) + "\n")


# ---------------------------------------------------------------- PCB


def build_pcb(path: Path) -> None:
    import pcbnew

    mm = pcbnew.FromMM
    board = pcbnew.NewBoard(str(path))  # bound to the .kicad_pro, so its rules and net classes apply
    board.SetCopperLayerCount(2)
    ox, oy = ORIGIN

    nets = {}
    def pcb_net(name: str) -> str:  # schematic local labels are sheet-qualified: /PAN_IN
        return name if name in POWER else "/" + name

    for name in sorted(set(NETS.values())):
        n = pcbnew.NETINFO_ITEM(board, pcb_net(name))
        board.Add(n)
        nets[name] = n


    # outline with rounded corners
    r = 1.0
    pts = [(r, 0), (BOARD_W - r, 0), (BOARD_W, r), (BOARD_W, BOARD_H - r), (BOARD_W - r, BOARD_H), (r, BOARD_H), (0, BOARD_H - r), (0, r)]
    for i in range(0, 8, 2):  # straight edges between the arcs
        a, b = pts[i], pts[(i + 1) % 8]
        s = pcbnew.PCB_SHAPE(board, pcbnew.SHAPE_T_SEGMENT)
        s.SetStart(pcbnew.VECTOR2I(mm(ox + a[0]), mm(oy + a[1])))
        s.SetEnd(pcbnew.VECTOR2I(mm(ox + b[0]), mm(oy + b[1])))
        s.SetLayer(pcbnew.Edge_Cuts)
        s.SetWidth(mm(0.1))
        board.Add(s)
    for cx, cy, ang in [(BOARD_W - r, r, 0), (BOARD_W - r, BOARD_H - r, 90), (r, BOARD_H - r, 180), (r, r, 270)]:
        s = pcbnew.PCB_SHAPE(board, pcbnew.SHAPE_T_ARC)
        s.SetCenter(pcbnew.VECTOR2I(mm(ox + cx), mm(oy + cy)))
        import math

        a0 = math.radians(ang - 90)
        s.SetStart(pcbnew.VECTOR2I(mm(ox + cx + r * math.cos(a0)), mm(oy + cy + r * math.sin(a0))))
        s.SetArcAngleAndEnd(pcbnew.EDA_ANGLE(90, pcbnew.DEGREES_T))
        s.SetLayer(pcbnew.Edge_Cuts)
        s.SetWidth(mm(0.1))
        board.Add(s)

    for ref, _lib, value, fp, lcsc, mpn, _sch, (x, y, rot) in PARTS:
        lib, name = fp.split(":")
        f = pcbnew.FootprintLoad(str(SHARE / "footprints" / f"{lib}.pretty"), name)
        f.SetReference(ref)
        f.SetValue(value)
        f.SetFPIDAsString(fp)
        f.SetPosition(pcbnew.VECTOR2I(mm(ox + x), mm(oy + y)))
        f.SetOrientationDegrees(rot)
        for field, val in (("LCSC", lcsc), ("MPN", mpn)):  # always present, so the board matches the schematic
            f.SetField(field, val)
            f.GetField(field).SetVisible(False)
        f.Reference().SetLayer(pcbnew.F_Fab)  # too small a board for designators on the silk: they live in the fab drawing
        f.Value().SetVisible(False)
        for pad in f.Pads():
            net = NETS.get((ref, pad.GetNumber()))
            if net:
                pad.SetNet(nets[net])
                if net == "GND" and pad.GetAttribute() == pcbnew.PAD_ATTRIB_SMD:
                    pad.SetLocalZoneConnection(pcbnew.ZONE_CONNECTION_FULL)  # small SMD pads: solid, not starved spokes
        board.Add(f)

    # silkscreen: what goes where
    def text(s: str, x: float, y: float, size: float = 0.7, layer=pcbnew.F_SilkS) -> None:
        t = pcbnew.PCB_TEXT(board)
        t.SetText(s)
        t.SetPosition(pcbnew.VECTOR2I(mm(ox + x), mm(oy + y)))
        t.SetTextSize(pcbnew.VECTOR2I(mm(size), mm(size)))
        t.SetTextThickness(mm(0.12))
        t.SetLayer(layer)
        t.SetMirrored(layer == pcbnew.B_SilkS)
        board.Add(t)

    text("PAN", 4.9, 9.4, 0.8)
    text("TILT", 13.8, 9.4, 0.8)
    text("lynx r1", 13.0, 5.6, 0.8, pcbnew.B_SilkS)

    # stitching vias: tie the two ground pours together where the parts leave room
    for vx, vy in [(1.0, 8.7), (17.3, 7.6), (9.7, 9.2), (13.5, 6.3)]:  # the last one sits just below J1 GND, which is top-only
        v = pcbnew.PCB_VIA(board)
        v.SetPosition(pcbnew.VECTOR2I(mm(ox + vx), mm(oy + vy)))
        v.SetWidth(mm(0.6))
        v.SetDrill(mm(0.3))
        v.SetNet(nets["GND"])
        board.Add(v)

    # the +5V trunk, laid by hand along the top edge on B.Cu: the router can't find a way between the
    # two servo headers once PAN crosses under them, so C1 and J1 are joined before it runs (locked, so it keeps it)
    def track(layer, pts, w=0.5):
        for (x0, y0), (x1, y1) in zip(pts, pts[1:]):
            t = pcbnew.PCB_TRACK(board)
            t.SetStart(pcbnew.VECTOR2I(mm(ox + x0), mm(oy + y0)))
            t.SetEnd(pcbnew.VECTOR2I(mm(ox + x1), mm(oy + y1)))
            t.SetWidth(mm(w))
            t.SetLayer(layer)
            t.SetNet(nets["+5V"])
            t.SetLocked(True)
            board.Add(t)

    track(pcbnew.B_Cu, [(2.75, 4.3), (2.75, 2.6), (14.4, 2.6)])
    v = pcbnew.PCB_VIA(board)
    v.SetPosition(pcbnew.VECTOR2I(mm(ox + 14.4), mm(oy + 2.6)))
    v.SetWidth(mm(0.6))
    v.SetDrill(mm(0.3))
    v.SetNet(nets["+5V"])
    v.SetLocked(True)
    board.Add(v)
    track(pcbnew.F_Cu, [(14.4, 2.6), (14.4, 5.1)])

    # ground pours on both layers
    for layer in (pcbnew.F_Cu, pcbnew.B_Cu):
        z = pcbnew.ZONE(board)
        z.SetLayer(layer)
        z.SetNet(nets["GND"])
        z.SetLocalClearance(mm(0.25))
        z.SetMinThickness(mm(0.2))
        z.SetPadConnection(pcbnew.ZONE_CONNECTION_THERMAL)
        z.SetIslandRemovalMode(pcbnew.ISLAND_REMOVAL_MODE_ALWAYS)
        poly = z.Outline()
        poly.NewOutline()
        for px, py in [(0.4, 0.4), (BOARD_W - 0.4, 0.4), (BOARD_W - 0.4, BOARD_H - 0.4), (0.4, BOARD_H - 0.4)]:
            poly.Append(mm(ox + px), mm(oy + py))
        board.Add(z)

    pcbnew.SaveBoard(str(path), board)


def stitch_islands(board) -> None:
    """Fill the pours; any piece of a GND pour that no via or through-hole pad ties to the other layer
    (it only touches a one-sided SMD pad) gets a stitching via inside it. Repeat until none are left."""
    import pcbnew

    mm = pcbnew.FromMM
    gnd = board.FindNet("GND")
    for _ in range(6):
        filler = pcbnew.ZONE_FILLER(board)
        filler.Fill(board.Zones())
        anchors = [v.GetPosition() for v in board.GetTracks() if isinstance(v, pcbnew.PCB_VIA) and v.GetNetCode() == gnd.GetNetCode()]
        anchors += [p.GetPosition() for f in board.GetFootprints() for p in f.Pads()
                    if p.GetNetCode() == gnd.GetNetCode() and p.GetAttribute() == pcbnew.PAD_ATTRIB_PTH]
        added = False
        for z in board.Zones():
            if z.GetNetCode() != gnd.GetNetCode():
                continue
            for layer in (pcbnew.F_Cu, pcbnew.B_Cu):
                if not z.IsOnLayer(layer):
                    continue
                polys = z.GetFilledPolysList(layer)
                for i in range(polys.OutlineCount()):
                    if any(polys.Contains(a, i) for a in anchors):
                        continue
                    # a via spot inside this island whose whole pad sits in GND fill on both layers,
                    # so it clears every other net's copper (the fills already keep their clearance)
                    import math
                    fills = [g.GetFilledPolysList(l) for g in board.Zones() if g.GetNetCode() == gnd.GetNetCode()
                             for l in (pcbnew.F_Cu, pcbnew.B_Cu) if g.IsOnLayer(l)]
                    def fits(p):
                        ring = [p] + [pcbnew.VECTOR2I(p.x + int(mm(0.35) * math.cos(a * math.pi / 4)),
                                                      p.y + int(mm(0.35) * math.sin(a * math.pi / 4))) for a in range(8)]
                        return all(polys.Contains(q, i) for q in ring) and all(f.Contains(q) for f in fills for q in ring)
                    bb = polys.Outline(i).BBox()
                    step = mm(0.1)
                    cands = [pcbnew.VECTOR2I(x, y) for x in range(bb.GetX(), bb.GetRight(), step)
                             for y in range(bb.GetY(), bb.GetBottom(), step)]
                    cands.sort(key=lambda p: (p.x - bb.Centre().x) ** 2 + (p.y - bb.Centre().y) ** 2)
                    c = next((p for p in cands if fits(p)), None)
                    if c is None:
                        continue  # nowhere to hold a via; the removal pass drops it
                    v = pcbnew.PCB_VIA(board)
                    v.SetPosition(c)
                    v.SetWidth(mm(0.6))
                    v.SetDrill(mm(0.3))
                    v.SetNet(gnd)
                    board.Add(v)
                    anchors.append(c)
                    added = True
        if not added:
            return


def autoroute(pcb: Path) -> None:
    import pcbnew

    board = pcbnew.LoadBoard(str(pcb))
    dsn, ses = pcb.with_suffix(".dsn"), pcb.with_suffix(".ses")
    if not pcbnew.ExportSpecctraDSN(board, str(dsn)):
        raise SystemExit("DSN export failed")
    if not FREEROUTING.exists():
        raise SystemExit(f"Freerouting not found at {FREEROUTING} (just pcb downloads it)")
    subprocess.run([JAVA, "-jar", str(FREEROUTING), "-de", str(dsn), "-do", str(ses), "-mp", "100", "--gui.enabled=false"],
                   check=True, stdout=open(pcb.with_suffix(".route.log"), "w"), stderr=subprocess.STDOUT, timeout=600)
    if not pcbnew.ImportSpecctraSES(board, str(ses)):
        raise SystemExit("SES import failed")
    stitch_islands(board)
    pcbnew.SaveBoard(str(pcb), board)
    dsn.unlink(missing_ok=True)
    ses.unlink(missing_ok=True)


def run(*args: str) -> subprocess.CompletedProcess:
    return subprocess.run([str(CLI), *args], capture_output=True, text=True)


def main() -> None:
    root = "5f1c2a00-0000-4000-8000-6c796e780001"  # fixed: stable diffs between runs
    sch, pcb, pro = HERE / f"{NAME}.kicad_sch", HERE / f"{NAME}.kicad_pcb", HERE / f"{NAME}.kicad_pro"
    import json

    # JLCPCB's standard 2-layer process, with a little margin (kicad-cli's DRC reads the rules from here)
    pro.write_text(json.dumps({
        "board": {"design_settings": {"rules": {
            "min_clearance": 0.15, "min_track_width": 0.15, "min_copper_edge_clearance": 0.3,
            "min_via_diameter": 0.5, "min_via_annular_width": 0.1, "min_through_hole_diameter": 0.3,
            "min_hole_clearance": 0.25, "min_hole_to_hole": 0.25, "min_silk_clearance": 0.0,
            "min_text_height": 0.8, "min_text_thickness": 0.12}}},
        "net_settings": {"classes": [{"name": "Default", "clearance": 0.15, "track_width": 0.25,
                                      "via_diameter": 0.6, "via_drill": 0.3, "wire_width": 6, "bus_width": 12,
                                      "schematic_color": "rgba(0, 0, 0, 0.000)", "pcb_color": "rgba(0, 0, 0, 0.000)",
                                      "priority": 2147483647},
                                     {"name": "Power", "clearance": 0.2, "track_width": 0.5, "via_diameter": 0.8,
                                      "via_drill": 0.4, "wire_width": 6, "bus_width": 12,
                                      "schematic_color": "rgba(0, 0, 0, 0.000)", "pcb_color": "rgba(0, 0, 0, 0.000)",
                                      "priority": 0}],
                         "netclass_patterns": [{"netclass": "Power", "pattern": "+5V"}, {"netclass": "Power", "pattern": "GND"}],
                         "meta": {"version": 4}},
        "meta": {"filename": f"{NAME}.kicad_pro", "version": 3},
    }, indent=2) + "\n")
    # project library tables (KiCad's standard libraries, via its own path variables: portable to any install)
    syms = sorted({p[1].split(":")[0] for p in PARTS} | {"power"})
    fps = sorted({p[3].split(":")[0] for p in PARTS})
    (HERE / "sym-lib-table").write_text("(sym_lib_table\n  (version 7)\n" + "".join(
        f'  (lib (name "{n}")(type "KiCad")(uri "${{KICAD10_SYMBOL_DIR}}/{n}.kicad_sym")(options "")(descr ""))\n' for n in syms) + ")\n")
    (HERE / "fp-lib-table").write_text("(fp_lib_table\n  (version 7)\n" + "".join(
        f'  (lib (name "{n}")(type "KiCad")(uri "${{KICAD10_FOOTPRINT_DIR}}/{n}.pretty")(options "")(descr ""))\n' for n in fps) + ")\n")
    write_schematic(sch, root)
    build_pcb(pcb)
    autoroute(pcb)

    out = HERE / "fab"
    shutil.rmtree(out, ignore_errors=True)
    (out / "gerbers").mkdir(parents=True)
    erc = run("sch", "erc", "--severity-all", "--exit-code-violations", "-o", str(out / "erc.rpt"), str(sch))
    drc = run("pcb", "drc", "--schematic-parity", "--refill-zones", "--severity-all", "--exit-code-violations",
              "-o", str(out / "drc.rpt"), str(pcb))
    steps = [
        ("schematic PDF", ["sch", "export", "pdf", "-o", str(out / f"{NAME}-schematic.pdf"), str(sch)]),
        ("BOM", ["sch", "export", "bom", "-o", str(out / f"{NAME}-bom.csv"), "--fields",
                 "Reference,Value,Footprint,MPN,LCSC,${QUANTITY}", "--labels", "Designator,Value,Footprint,MPN,LCSC,Qty",
                 "--group-by", "Value,Footprint", "--exclude-dnp", str(sch)]),
        ("gerbers", ["pcb", "export", "gerbers", "-o", str(out / "gerbers"), "--layers",
                     "F.Cu,B.Cu,F.Paste,B.Paste,F.SilkS,B.SilkS,F.Mask,B.Mask,Edge.Cuts", str(pcb)]),
        ("drill", ["pcb", "export", "drill", "-o", str(out / "gerbers") + "/", "--generate-map", "--map-format", "gerberx2", str(pcb)]),
        ("placement", ["pcb", "export", "pos", "-o", str(out / f"{NAME}-cpl.csv"), "--format", "csv", "--units", "mm",
                       "--side", "both", str(pcb)]),
        ("board PDF", ["pcb", "export", "pdf", "-o", str(out / f"{NAME}-board.pdf"), "--layers",
                       "F.Cu,B.Cu,F.SilkS,B.SilkS,Edge.Cuts", "--mode-separate", str(pcb)]),
        ("STEP", ["pcb", "export", "step", "-o", str(out / f"{NAME}.step"), "--subst-models", str(pcb)]),
        ("render top", ["pcb", "render", "-o", str(out / f"{NAME}-top.png"), "--side", "top", "--width", "1200",
                        "--height", "900", "--quality", "high", str(pcb)]),
        ("render bottom", ["pcb", "render", "-o", str(out / f"{NAME}-bottom.png"), "--side", "bottom", "--width", "1200",
                           "--height", "900", "--quality", "high", str(pcb)]),
    ]
    failed = []
    for label, args in steps:
        r = run(*args)
        if r.returncode != 0:
            failed.append(f"{label}: {(r.stderr or r.stdout).strip()[:200]}")
    shutil.make_archive(str(out / f"{NAME}-gerbers"), "zip", out / "gerbers")
    print(f"ERC: {'clean' if erc.returncode == 0 else 'VIOLATIONS (fab/erc.rpt)'}")
    print(f"DRC + schematic parity: {'clean' if drc.returncode == 0 else 'VIOLATIONS (fab/drc.rpt)'}")
    for f in failed:
        print("failed:", f)
    sys.exit(0 if erc.returncode == 0 and drc.returncode == 0 and not failed else 1)


if __name__ == "__main__":
    main()

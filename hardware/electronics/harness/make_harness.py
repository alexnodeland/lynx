# SPDX-License-Identifier: CERN-OHL-S-2.0
"""Draw the lynx wiring-harness diagram (option B, no PCB) -> lynx-harness.svg / .png.

    python make_harness.py
"""
import os

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
from matplotlib.patches import FancyBboxPatch  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
INK, MUTED, LINE, BG = "#1C1D21", "#6B6E76", "#C9CACF", "#FFFFFF"
COL = {"+5V": "#D64541", "GND": "#1C1D21", "PAN": "#E08A1E", "TILT": "#D9B70F"}
F = "Helvetica Neue"

fig = plt.figure(figsize=(16, 9), dpi=100)
ax = fig.add_axes([0, 0, 1, 1])
ax.set_xlim(0, 1600)
ax.set_ylim(900, 0)
ax.axis("off")
fig.patch.set_facecolor(BG)


def box(x, y, w, h, title, sub):
    ax.add_patch(FancyBboxPatch((x, y), w, h, boxstyle="round,pad=0,rounding_size=18", fc="#F6F6F8", ec=LINE, lw=2))
    ax.text(x + 22, y + 42, title, fontsize=21, fontweight="bold", color=INK, family=F)
    ax.text(x + 22, y + 74, sub, fontsize=14, color=MUTED, family=F)


def wire(pts, net, lw=5):
    xs, ys = zip(*pts)
    ax.plot(xs, ys, color=COL[net], lw=lw, solid_capstyle="round", solid_joinstyle="round", zorder=3)


def splice(x, y, label):
    ax.add_patch(FancyBboxPatch((x - 16, y - 9), 32, 18, boxstyle="round,pad=0,rounding_size=6", fc="#BFC3CC",
                                ec=INK, lw=1.2, zorder=5))
    ax.text(x, y - 18, label, fontsize=11, color=MUTED, ha="center", family=F)


ax.text(80, 80, "lynx — wiring harness (no PCB)", fontsize=34, fontweight="bold", color=INK, family=F)
ax.text(80, 118, "Solder splices + 3 mm heat-shrink. Same nets as the carrier PCB.", fontsize=17, color=MUTED, family=F)

box(80, 260, 300, 380, "XIAO ESP32-S3", "Sense, edge pads")
box(1180, 190, 340, 250, "Pan servo (M1)", "MG90S, in the base")
box(1180, 500, 340, 250, "Tilt servo (M2)", "MG90S, on the yoke")
pads = {"+5V": 380, "GND": 450, "PAN": 520, "TILT": 590}
labels = {"+5V": "5V", "GND": "GND", "PAN": "D0", "TILT": "D1"}
for net, y in pads.items():
    ax.plot([380], [y], "o", ms=11, mfc="white", mec=INK, mew=2, zorder=6)
    ax.text(360, y + 6, labels[net], fontsize=16, ha="right", color=INK, family=F)

# 4-wire lead from the XIAO to the splice block (follows the USB cable through the clips)
SX = 760
for net, y in pads.items():
    wire([(380, y), (SX - 60, y)], net)
ax.text(560, 340, "4-wire lead, ~120 mm\n(runs with the USB-C cable)", fontsize=14, color=MUTED, ha="center", family=F)
# splices: +5V and GND fan out to both servos; signals get a 220 Ω in-line resistor
srv = {"M1": 300, "M2": 610}
for net, y in (("+5V", 380), ("GND", 450)):
    wire([(SX - 60, y), (SX, y)], net)
    splice(SX, y, "splice")
    for m, by in srv.items():
        py = by + (40 if net == "+5V" else 80)
        fx = SX + (100 if net == "+5V" else 150)        # separate fan-out columns: no ambiguous corners
        wire([(SX, y), (fx, y), (fx, py), (1180, py)], net, lw=4)
for net, y, m in (("PAN", 520, "M1"), ("TILT", 590, "M2")):
    by = srv[m] + 0
    wire([(SX - 60, y), (SX + 40, y), (SX + 40, by), (980, by)], net)
    ax.add_patch(FancyBboxPatch((980, by - 11), 60, 22, boxstyle="round,pad=0,rounding_size=4", fc="#E9DFC8", ec=INK, lw=1.2, zorder=5))
    ax.text(1010, by + 5, "220 Ω", fontsize=11, ha="center", color=INK, family=F, zorder=6)
    wire([(1040, by), (1180, by)], net)
for m, by in srv.items():
    for k, (nm, net) in enumerate((("signal (orange)", "PAN" if m == "M1" else "TILT"), ("+5V (red)", "+5V"), ("GND (brown)", "GND"))):
        yy = by + 40 * k
        ax.plot([1180], [yy], "o", ms=10, mfc="white", mec=INK, mew=2, zorder=6)
        ax.text(1200, yy + 6, nm, fontsize=14, color=INK, family=F)
# bulk capacitor across the +5V/GND splices
ax.add_patch(FancyBboxPatch((SX - 70, 690), 140, 70, boxstyle="round,pad=0,rounding_size=12", fc="white", ec=LINE, lw=2))
ax.text(SX, 722, "C1 470 µF 6.3 V", fontsize=14, ha="center", fontweight="bold", color=INK, family=F)
ax.text(SX, 748, "+ to 5V splice, − to GND", fontsize=12, ha="center", color=MUTED, family=F)
wire([(SX - 30, 690), (SX - 30, 380)], "+5V", 3)
wire([(SX + 30, 690), (SX + 30, 450)], "GND", 3)
for k, (net, name) in enumerate((("+5V", "5 V"), ("GND", "Ground"), ("PAN", "D0 · pan"), ("TILT", "D1 · tilt"))):
    x = 80 + 230 * k
    ax.plot([x, x + 46], [830, 830], color=COL[net], lw=6, solid_capstyle="round")
    ax.text(x + 60, 836, name, fontsize=15, color=INK, family=F)
ax.text(1520, 836, "CERN-OHL-S-2.0", fontsize=11, color=MUTED, ha="right", family=F)
for ext in ("svg", "png"):
    fig.savefig(os.path.join(HERE, f"lynx-harness.{ext}"), facecolor=BG)
print("wrote lynx-harness.svg/.png")

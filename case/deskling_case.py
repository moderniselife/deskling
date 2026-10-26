#!/usr/bin/env python3
"""
Deskling desk case -- parametric, 3D-printable housing for the Guition
JC8012P4A1C 10.1" panel (used landscape).

    python3 case/deskling_case.py              # build, check, export, render
    python3 case/deskling_case.py --no-render  # skip the PNG renders (faster)

Requires: build123d (pip install --break-system-packages build123d),
numpy, matplotlib (renders only: scipy optional for soft shadows).

Outputs (next to this file):
    stl/*.stl      print-ready meshes, already in print orientation
    step/*.step    same parts as STEP (print orientation) + full assembly
    renders/*.png  preview renders

The parts
---------
    bezel_onepiece   front "face" of Deskling, needs a 270 mm bed (Snapmaker U1)
    bezel_left/right same bezel split down the middle with blind dovetails,
                     each half fits a 250 mm bed (Bambu A1 / P1S)
    back_cover       pillowy back shell, screws to the 6 panel standoffs and to
                     8 M3 heat-set inserts in the bezel (bridges the split seam)
    stand            cradle ("feet") holding the unit tilted back TILT_DEG
    cheek_insert     press-fit blush cheek (print 2, peach)
    fit_test_template  cheap 2 mm plate to check panel size, standoff pattern
                     and module position before committing to the big prints

Coordinate systems
------------------
DEVICE frame (bezel, back cover, panel): origin at the centre of the panel's
back plane. +X = to the user's right when looking at the screen, +Y = up,
+Z = towards the user. The panel back plane is Z=0, the glass front is
Z=PANEL_T. NOTE the vendor drawing gives the standoff pattern "viewed from the
back", so X is mirrored when converting (see standoff_positions()).

WORLD frame (stand, renders): +X right, +Y away from the user, +Z up, desk at
Z=0. The unit is tilted back by TILT_DEG.

All dimensions are millimetres.
"""
from __future__ import annotations

import argparse
import math
import os
import sys
import time
from pathlib import Path

from build123d import (
    Axis, Box, BuildSketch, Circle, Compound, Cylinder, Location, Locations,
    Plane, Polygon, Pos, RectangleRounded, Rot, Solid, Sphere, Spline, Text,
    Vector, Wire, chamfer, export_step, export_stl, extrude, fillet, mirror,
    offset, sweep, Align, Edge,
)

# =============================================================================
# PARAMETERS  (mm / degrees)
# =============================================================================

# ---- printers ---------------------------------------------------------------
BED_ANY = 250.0          # every "split" part must fit this cube (A1 / P1S / U1)
BED_U1 = 268.0           # one-piece bezel must fit this (U1 is 270, keep 1 mm margin)

# ---- panel (glass + metal frame) -------------------------------------------
PANEL_W = 242.8          # vendor drawing
PANEL_H = 158.7
PANEL_T = 7.0
PANEL_W_MEAS = 245.0     # user's calipers (design the pocket for the larger)
PANEL_H_MEAS = 158.0
PANEL_T_MEAS = 7.5
GLASS_W = 239.3          # cover glass, centred on the frame
GLASS_H = 155.2
ACTIVE_W = 216.58        # active display area, assumed centred...
ACTIVE_H = 135.36
ACTIVE_TOL = 2.0         # ...+/- this much in X and Y

POCKET_W = 245.6         # bezel pocket the panel drops into
POCKET_H = 160.0
POCKET_D = 7.8           # lip underside to panel back plane (foam gap = POCKET_D - panel thickness)
POCKET_R = 0.8           # pocket corner radius (panel corners are near-square)

LIP_T = 2.0              # bezel lip thickness in front of the glass
WINDOW_OVERLAP = 4.0     # lip covers this much glass on every side
WINDOW_W = GLASS_W - 2 * WINDOW_OVERLAP   # 231.3
WINDOW_H = GLASS_H - 2 * WINDOW_OVERLAP   # 147.2
WINDOW_R = 6.0           # soft window corners (checked against the active area)
WINDOW_CHAMFER = 1.2     # 45 deg chamfer on the window's front edge (less shadowing)

FOAM_SEAT_D = 0.4        # shallow channel in the lip underside to locate foam tape (0 = off)
FOAM_SEAT_W = 2.5        # channel width (use 2-3 mm foam tape)
FOAM_SEAT_IN = 1.3       # channel starts this far outside the window edge

# ---- electronics module on the panel back ----------------------------------
MOD_W = 138.35
MOD_H = 93.5
MOD_D = 15.5             # depth behind the panel back plane
MOD_OFFSET = 6.0         # module may sit up to this far off-centre in X and Y
PLUG_LEN = 30.0          # USB-C plug overmould sticking out of the module's LEFT side (front view)
PLUG_T = 7.0             # plug overmould thickness (Z)
CABLE_D = 6.0            # USB cable diameter

# ---- panel standoffs (brass, threaded) --------------------------------------
SO_X_BACKVIEW = (0.0, 90.0, 212.8)   # hole X positions in each row, viewed FROM THE BACK
SO_ROW_PITCH = 128.7                 # distance between the two rows
SO_D = 7.5                           # boss diameter
SO_H = 4.5                           # boss height above the panel back plane (MEASURE!)
PANEL_SCREW = "M2.5"                 # thread in the standoffs ("M2.5" or "M3" -- see README)

# ---- screws / inserts ---------------------------------------------------------
SCREW_CLEAR = {"M2.5": 2.9, "M3": 3.4}   # clearance hole diameters
CBORE_D = 6.4            # counterbore for M2.5 or M3 socket/button heads
SCREW_FLOOR = 3.0        # plastic under each screw head
PANEL_SCREW_LEN = 6.0    # recommended screw length into the standoffs
INSERT_HOLE_D = 4.0      # M3 heat-set insert hole (M3 x 5.7 x 4.6 OD "standard" inserts)
INSERT_DEPTH = 6.5
BEZEL_SCREW_LEN = 8.0    # M3 x 8 through back cover into the inserts

# ---- bezel ("face") --------------------------------------------------------------
BEZEL_W = 265.0
BEZEL_TOP = 16.5         # border above the pocket
BEZEL_CHIN = 26.0        # border below the pocket (chunkier chin)
BEZEL_R = 18.0           # outer corner radius (front view)
BEZEL_SKIRT = 6.0        # bezel continues this far behind the panel back plane
FRONT_CHAMFER = 4.5      # front edge = 45 deg chamfer (prints face-down without supports)...
FRONT_FILLET_OUT = 4.0   # ...softened with fillets so it reads as a round toy edge
FRONT_FILLET_IN = 1.5
REAR_FILLET = 2.5

EARS = True
EAR_X = 72.0             # ear centres, +/- from the middle
EAR_R = 20.0
EAR_RISE = 14.0          # how far the ears rise above the top edge
EAR_FILLET = 8.0         # concave blend into the head

CHEEK_D = 18.0           # blush cheek recess diameter
CHEEK_DEPTH = 2.0
CHEEK_X = 100.0
CHEEK_Y = -88.0          # device Y (chin)
CHEEK_PRESS = 0.10       # insert is this much smaller in diameter (press fit)
CHEEK_PROUD = 0.6        # insert stands this much proud, as a soft pillow

# heat-set insert positions in the bezel (device coords).  4 above, 4 below the
# pocket; the +/-30 pair bridges the split seam.
TOP_SCREW_X = (-95.0, -30.0, 30.0, 95.0)
TOP_SCREW_Y = POCKET_H / 2 + 4.5
CHIN_SCREW_X = (-85.0, -30.0, 30.0, 85.0)
CHIN_SCREW_Y = -(POCKET_H / 2 + 8.0)

# ---- split joint ---------------------------------------------------------------
DOVETAIL_NECK = 7.0      # dovetail width at the seam
DOVETAIL_FLARE = 10.0    # width at the far end
DOVETAIL_DEPTH = 6.0     # how far it reaches into the other half
DOVETAIL_CLEAR = 0.15    # per side
SEAM_CHAMFER = 0.4       # V-groove at the seam on the front face
DOVETAIL_Z_TOP = 3.0     # tongue stops this far forward (front face stays a clean hairline)

# ---- back cover ---------------------------------------------------------------------
BC_WALL = 2.4
BC_MOD_CLEAR = 2.5       # module back face to inner back wall
BC_W = 248.0             # must be <= BED_ANY (one piece on every printer)
BC_R = 12.0              # corner radius (back view)
BC_CHAMFER = 4.5         # back edge = chamfer + fillets (prints back-face-down)
BC_FILLET_OUT = 4.0
BC_FILLET_IN = 1.5
BC_RIM_CHAMFER = 0.6
BC_EDGE_MARGIN = 1.2     # flat land outside each counterbore before the edge chamfer
PANEL_COL_D = 10.0       # columns reaching the standoffs
BEZEL_COL_D = 9.0        # columns over the bezel inserts
CABLE_SLOT_X = (-112.0, -96.0)   # cable exit slot in the bottom wall (ports side = -X)
CABLE_SLOT_DEPTH = 7.5   # from the rim, fits a 6 mm cable
GRILLE_HOLE_D = 3.0
GRILLE_PITCH = 5.4
GRILLE_HEART_W = 42.0
GRILLE_CENTRES = ((45.0, 4.0), (-45.0, 4.0))   # device X: +X is behind the speaker (left in back view)
WORDMARK = "deskling"
WORDMARK_SIZE = 11.0
WORDMARK_DEPTH = 0.6
WORDMARK_Y = -40.0
WORDMARK_FONT = "DejaVu Sans"

# ---- stand ---------------------------------------------------------------------------
TILT_DEG = 18.0
STAND_W = 236.0
STAND_D = 150.0
STAND_H = 14.0           # height of the base cushion
STAND_R = 38.0           # plan-view corner radius
STAND_TOP_FILLET = 6.0
STAND_FLOOR = 3.4        # case's lowest point sits this high (=> ~3 mm floor under the groove)
STAND_FRONT = 16.0       # base front edge to the bezel's front-bottom edge
STAND_CLEAR = 0.4        # groove clearance (drop-in fit)
BACKREST_W = 190.0
BACKREST_UP = 44.0       # supports this far up the back (along the case)
BACKREST_T = 9.0
BACKREST_R = 24.0        # soft top corners
TAIL_W = 30.0            # little round tail at the back
TAIL_L = 20.0
TAIL_H = 9.0
TAIL_OUT = 8.0
TOE_X = 62.0
TOE_W = 44.0
TOE_L = 26.0
TOE_H = 10.0
TOE_OUT = 11.0           # how far the toes poke out in front of the base
COIN_D = 26.5            # coin pockets underneath (UK 2p / EUR 2 / US quarter stacks)
COIN_DEPTH = 7.0
FOOT_D = 12.9            # recesses for 12.7 mm stick-on rubber bumpons
FOOT_DEPTH = 1.0
STAND_CABLE_W = 9.0
STAND_CABLE_FLOOR = 2.4

# ---- fit-test template ---------------------------------------------------------------
TPL_T = 2.0
TPL_FENCE_T = 1.6
TPL_FENCE_H = 4.0
TPL_BAND = 13.0

# ---- general FDM ---------------------------------------------------------------------
FIT = 0.2                # generic fit clearance
BOTTOM_CHAMFER = 0.6     # elephant-foot chamfer on bed edges
TESS_TOL = 0.04          # STL export chord tolerance
TESS_ANG = 0.1

# ---- mass model (only for the stability check) -------------------------------------
DEVICE_MASS_G = 550.0    # panel + module, estimate
PLA_DENSITY = 1.24e-3    # g/mm^3
PRINT_FILL = 0.45        # effective solid fraction of a printed part (walls + 15 % infill)

# =============================================================================
# DERIVED
# =============================================================================
HERE = Path(__file__).resolve().parent
Z_FRONT = POCKET_D + LIP_T                 # bezel front face
Z_REAR = -BEZEL_SKIRT                      # bezel rear face == back cover rim plane
BEZ_TOP = POCKET_H / 2 + BEZEL_TOP
BEZ_BOT = -(POCKET_H / 2 + BEZEL_CHIN)
BEZEL_H = BEZ_TOP - BEZ_BOT
BEZ_CY = (BEZ_TOP + BEZ_BOT) / 2
EAR_CY = BEZ_TOP + EAR_RISE - EAR_R
BC_Z_BACK = -(MOD_D + BC_MOD_CLEAR + BC_WALL)
BC_TOP = TOP_SCREW_Y + CBORE_D / 2 + BC_EDGE_MARGIN + BC_CHAMFER
BC_BOT = CHIN_SCREW_Y - CBORE_D / 2 - BC_EDGE_MARGIN - BC_CHAMFER
BC_H = BC_TOP - BC_BOT
BC_CY = (BC_TOP + BC_BOT) / 2
TILT = math.radians(TILT_DEG)


def standoff_positions():
    """Standoff centres in DEVICE (front-view) coordinates.

    The drawing is viewed from the back: x_back = 0 is the left-most hole as
    seen from behind, which is the RIGHT-most hole as seen from the front.
    """
    span = max(SO_X_BACKVIEW) - min(SO_X_BACKVIEW)
    pts = []
    for xb in SO_X_BACKVIEW:
        x_back_centred = xb - span / 2
        x_front = -x_back_centred           # mirror X: back view -> front view
        for sy in (-1, 1):
            pts.append((x_front, sy * SO_ROW_PITCH / 2))
    return pts


def insert_positions():
    return [(x, TOP_SCREW_Y) for x in TOP_SCREW_X] + [(x, CHIN_SCREW_Y) for x in CHIN_SCREW_X]


# =============================================================================
# HELPERS
# =============================================================================
def edges_at_z(shape, z, tol=1e-3):
    return shape.edges().filter_by(
        lambda e: abs(e.bounding_box().min.Z - z) < tol and abs(e.bounding_box().max.Z - z) < tol)


def rrect_prism(w, h, r, z0, z1, cx=0.0, cy=0.0):
    with BuildSketch() as sk:
        with Locations((cx, cy)):
            RectangleRounded(w, h, r) if r > 0 else _rect(w, h)
    return Pos(0, 0, z0) * extrude(sk.sketch, z1 - z0)


def _rect(w, h):
    from build123d import Rectangle
    return Rectangle(w, h)


def box(x0, x1, y0, y1, z0, z1):
    return Pos((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2) * Box(x1 - x0, y1 - y0, z1 - z0)


def cyl(x, y, z0, z1, d):
    return Pos(x, y, (z0 + z1) / 2) * Cylinder(d / 2, z1 - z0)


def soft_edge(solid, z_edge, inward, c, r_out, r_in):
    """Chamfer the edge loop at height z_edge by c, then fillet both new edges.

    inward = -1 if the material is below z_edge (front face on top), +1 if the
    material is above (back face at the bottom).  A 45 deg chamfer on the bed
    side needs no supports; the fillets make it read as a soft round edge.
    """
    solid = chamfer(edges_at_z(solid, z_edge), c)
    solid = fillet(edges_at_z(solid, z_edge + inward * c), r_out)
    solid = fillet(edges_at_z(solid, z_edge), r_in)
    return solid


def heart_points(width, n=120):
    s = width / 32.0
    pts = []
    for i in range(n):
        t = 2 * math.pi * i / n
        x = 16 * math.sin(t) ** 3
        y = 13 * math.cos(t) - 5 * math.cos(2 * t) - 2 * math.cos(3 * t) - math.cos(4 * t)
        pts.append((x * s, (y + 2.5) * s))
    return pts


def heart_hole_centres(cx, cy, width, pitch, hole_d):
    from matplotlib.path import Path as MPath
    poly = MPath(heart_points(width))
    out = []
    rows = int(width / pitch) + 3
    for j in range(-rows, rows + 1):
        y = j * pitch * math.sqrt(3) / 2
        xo = (pitch / 2) if j % 2 else 0.0
        for i in range(-rows, rows + 1):
            x = i * pitch + xo
            if poly.contains_point((x, y)) and _inside_with_margin(poly, x, y, hole_d / 2 + 0.6):
                out.append((cx + x, cy + y))
    return out


def _inside_with_margin(poly, x, y, m):
    for a in range(0, 360, 30):
        if not poly.contains_point((x + m * math.cos(math.radians(a)), y + m * math.sin(math.radians(a)))):
            return False
    return True


def dev2world_loc():
    """Location that maps DEVICE coords into WORLD coords (before the lift)."""
    return Rot(-TILT_DEG, 0, 0) * Rot(90, 0, 0)


def dev2world_pt(x, y, z):
    yw = -z * math.cos(TILT) + y * math.sin(TILT)
    zw = z * math.sin(TILT) + y * math.cos(TILT)
    return x, yw, zw


# =============================================================================
# PARTS
# =============================================================================
def bezel_outline():
    with BuildSketch() as sk:
        with Locations((0, BEZ_CY)):
            RectangleRounded(BEZEL_W, BEZEL_H, BEZEL_R)
        if EARS:
            for sx in (-1, 1):
                with Locations((sx * EAR_X, EAR_CY)):
                    Circle(EAR_R)
            vs = [v for v in sk.vertices()
                  if abs(v.Y - BEZ_TOP) < 0.01 and abs(v.X) < BEZEL_W / 2 - BEZEL_R - 0.5]
            fillet(vs, EAR_FILLET)
    return sk.sketch


def make_bezel_envelope():
    body = Pos(0, 0, Z_REAR) * extrude(bezel_outline(), Z_FRONT - Z_REAR)
    body = soft_edge(body, Z_FRONT, -1, FRONT_CHAMFER, FRONT_FILLET_OUT, FRONT_FILLET_IN)
    body = fillet(edges_at_z(body, Z_REAR), REAR_FILLET)
    return body


def make_bezel():
    env = make_bezel_envelope()
    b = env
    # panel pocket, open to the back
    b = b - rrect_prism(POCKET_W, POCKET_H, POCKET_R, Z_REAR - 1, POCKET_D)
    # window
    b = b - rrect_prism(WINDOW_W, WINDOW_H, WINDOW_R, POCKET_D - 1, Z_FRONT + 1)
    win_edges = edges_at_z(b, Z_FRONT).filter_by(
        lambda e: abs(e.center().X) < WINDOW_W / 2 + 0.01 and abs(e.center().Y) < WINDOW_H / 2 + 0.01)
    b = chamfer(win_edges, WINDOW_CHAMFER)
    # foam seat channel in the lip underside
    if FOAM_SEAT_D > 0:
        wi = WINDOW_W + 2 * FOAM_SEAT_IN
        hi = WINDOW_H + 2 * FOAM_SEAT_IN
        ring = rrect_prism(wi + 2 * FOAM_SEAT_W, hi + 2 * FOAM_SEAT_W, WINDOW_R + FOAM_SEAT_IN + FOAM_SEAT_W,
                           POCKET_D - 0.5, POCKET_D + FOAM_SEAT_D) - \
            rrect_prism(wi, hi, WINDOW_R + FOAM_SEAT_IN, POCKET_D - 1, POCKET_D + 1)
        b = b - ring
    # blush cheek recesses (front face)
    for sx in (-1, 1):
        rec = cyl(sx * CHEEK_X, CHEEK_Y, Z_FRONT - CHEEK_DEPTH, Z_FRONT + 1, CHEEK_D)
        b = b - rec
    cheek_edges = edges_at_z(b, Z_FRONT).filter_by(
        lambda e: abs(abs(e.center().X) - CHEEK_X) < CHEEK_D and abs(e.center().Y - CHEEK_Y) < CHEEK_D)
    b = chamfer(cheek_edges, 0.5)
    # heat-set insert holes (rear face)
    for (x, y) in insert_positions():
        b = b - cyl(x, y, Z_REAR - 1, Z_REAR + INSERT_DEPTH, INSERT_HOLE_D)
    ins_edges = edges_at_z(b, Z_REAR).filter_by(
        lambda e: any(abs(e.center().X - x) < 3 and abs(e.center().Y - y) < 3 for x, y in insert_positions()))
    b = chamfer(ins_edges, 0.5)
    return b, env


def dovetail_prism(grow, z0, z1, yc):
    n = DOVETAIL_NECK / 2 + grow
    f = DOVETAIL_FLARE / 2 + grow
    d = DOVETAIL_DEPTH + grow
    pts = [(-0.5, yc - n), (d, yc - f), (d, yc + f), (-0.5, yc + n)]
    with BuildSketch() as sk:
        Polygon(*pts, align=None)
    return Pos(0, 0, z0) * extrude(sk.sketch, z1 - z0)


def dovetail_centres():
    return [(POCKET_H / 2 + BEZEL_TOP / 2 - 0.5), -(POCKET_H / 2 + BEZEL_CHIN / 2)]


def split_bezel(bezel):
    big = 400
    left = bezel & box(-big, 0, -big, big, -big, big)
    right = bezel & box(0, big, -big, big, -big, big)
    for yc in dovetail_centres():
        tongue = dovetail_prism(0.0, Z_REAR, DOVETAIL_Z_TOP, yc) & bezel
        # 45 deg underside (bed side when printed face-down) so the tongue is not a
        # flat cantilever: depth 0 at DOVETAIL_Z_TOP, full depth DOVETAIL_DEPTH further back
        with BuildSketch(Plane.XZ) as wsk:
            Polygon((-1, DOVETAIL_Z_TOP), (0, DOVETAIL_Z_TOP),
                    (DOVETAIL_DEPTH + 1, DOVETAIL_Z_TOP - DOVETAIL_DEPTH - 1),
                    (DOVETAIL_DEPTH + 1, Z_REAR - 1), (-1, Z_REAR - 1), align=None)
        tongue = tongue & extrude(wsk.sketch, big, both=True)
        groove = dovetail_prism(DOVETAIL_CLEAR, Z_REAR - 1, DOVETAIL_Z_TOP + DOVETAIL_CLEAR, yc)
        left = left + (tongue & box(0, big, -big, big, -big, big))
        right = right - groove
    # tiny chamfer where the halves meet on the front face: hides elephant's foot
    # and turns the seam into a deliberate hairline
    out = []
    for half in (left, right):
        es = edges_at_z(half, Z_FRONT).filter_by(
            lambda e: abs(e.bounding_box().min.X) < 1e-3 and abs(e.bounding_box().max.X) < 1e-3)
        try:
            half = chamfer(es, SEAM_CHAMFER) if SEAM_CHAMFER > 0 and len(es) else half
        except Exception as ex:
            print(f"  (seam chamfer skipped: {ex})")
        out.append(half)
    return out[0], out[1]


def make_back_cover():
    outer = rrect_prism(BC_W, BC_H, BC_R, BC_Z_BACK, Z_REAR, cy=BC_CY)
    outer = soft_edge(outer, BC_Z_BACK, +1, BC_CHAMFER, BC_FILLET_OUT, BC_FILLET_IN)
    outer = chamfer(edges_at_z(outer, Z_REAR), BC_RIM_CHAMFER)
    rim_face = outer.faces().sort_by(Axis.Z)[-1]
    shell = offset(outer, amount=-BC_WALL, openings=rim_face)

    # internal columns, clipped to the outer envelope
    cols = None
    clear = SCREW_CLEAR[PANEL_SCREW]
    for (x, y) in standoff_positions():
        c = cyl(x, y, BC_Z_BACK + 0.5, -SO_H, PANEL_COL_D)
        cols = c if cols is None else cols + c
    for (x, y) in insert_positions():
        c = cyl(x, y, BC_Z_BACK + 0.5, Z_REAR, BEZEL_COL_D)
        # rib out to the nearest (top/bottom) wall so the column is not a free-standing post
        y_wall = BC_TOP - BC_WALL / 2 if y > 0 else BC_BOT + BC_WALL / 2
        c = c + box(x - BEZEL_COL_D / 2, x + BEZEL_COL_D / 2, min(y, y_wall), max(y, y_wall),
                    BC_Z_BACK + 0.5, Z_REAR)
        cols = cols + c
    bc = shell + (cols & outer)

    # holes: panel screws (counterbored from the back)
    for (x, y) in standoff_positions():
        bc = bc - cyl(x, y, BC_Z_BACK - 1, -SO_H - SCREW_FLOOR, CBORE_D)
        bc = bc - cyl(x, y, BC_Z_BACK - 1, -SO_H + 1, clear)
    # holes: M3 into bezel inserts
    for (x, y) in insert_positions():
        bc = bc - cyl(x, y, BC_Z_BACK - 1, Z_REAR - SCREW_FLOOR, CBORE_D)
        bc = bc - cyl(x, y, BC_Z_BACK - 1, Z_REAR + 1, SCREW_CLEAR["M3"])

    # cable exit slot (bottom wall, ports side), U-shaped, open to the rim
    x0, x1 = CABLE_SLOT_X
    r = 3.0
    zb = Z_REAR - CABLE_SLOT_DEPTH
    ys = (BC_BOT - 2, BC_BOT + BC_WALL + 6)
    slot = box(x0 + r, x1 - r, *ys, zb, Z_REAR + 1) + box(x0, x1, *ys, zb + r, Z_REAR + 1)
    for xc in (x0 + r, x1 - r):
        slot = slot + Pos(xc, (ys[0] + ys[1]) / 2, zb + r) * Rot(90, 0, 0) * Cylinder(r, ys[1] - ys[0])
    bc = bc - slot

    # heart grilles
    for (gx, gy) in GRILLE_CENTRES:
        holes = None
        for (hx, hy) in heart_hole_centres(gx, gy, GRILLE_HEART_W, GRILLE_PITCH, GRILLE_HOLE_D):
            h = cyl(hx, hy, BC_Z_BACK - 1, BC_Z_BACK + BC_WALL + 1, GRILLE_HOLE_D)
            holes = h if holes is None else holes + h
        bc = bc - holes

    # debossed wordmark, readable from behind
    if WORDMARK:
        try:
            t = Text(WORDMARK, font_size=WORDMARK_SIZE, font=WORDMARK_FONT)
            t = mirror(t, Plane.YZ)
            t = Pos(0, WORDMARK_Y, BC_Z_BACK - 0.01) * extrude(t, WORDMARK_DEPTH + 0.01)
            bc = bc - t
        except Exception as ex:  # font problems should never break the build
            print(f"  (wordmark skipped: {ex})")
    return bc, outer


def make_cheek_insert():
    d = CHEEK_D - CHEEK_PRESS
    h = CHEEK_DEPTH + CHEEK_PROUD
    c = Pos(0, 0, h / 2) * Cylinder(d / 2, h)
    c = fillet(edges_at_z(c, h), 1.2)
    c = chamfer(edges_at_z(c, 0), 0.4)
    return c


# ---------------------------------------------------------------------------
# assembly placement (world) and stand
# ---------------------------------------------------------------------------
def case_lift_and_shift():
    """World translation applied after dev2world rotation.

    Lowest case point sits at Z = STAND_FLOOR. The bezel's front-bottom corner
    is used to place the base's front edge.
    """
    # candidate lowest points of the case cross-section
    cands = [(BEZ_BOT, Z_REAR), (BEZ_BOT, Z_FRONT), (BC_BOT, BC_Z_BACK)]
    zmin = min(dev2world_pt(0, y, z)[2] for y, z in cands)
    lift = STAND_FLOOR - zmin
    y_front = dev2world_pt(0, BEZ_BOT, Z_FRONT)[1]
    # put the base front edge at world Y = -STAND_D/2
    shift_y = (-STAND_D / 2 + STAND_FRONT) - y_front
    return shift_y, lift


def to_world(shape):
    sy, lift = case_lift_and_shift()
    return Pos(0, sy, lift) * dev2world_loc() * shape


def to_world_pt(x, y, z):
    sy, lift = case_lift_and_shift()
    xw, yw, zw = dev2world_pt(x, y, z)
    return xw, yw + sy, zw + lift


def groove_cutter(length):
    """Case bottom cross-section (+clearance), swept upward so it can drop in."""
    c = STAND_CLEAR
    pts = [(BEZ_BOT - c, Z_FRONT + c), (BEZ_BOT - c, Z_REAR - c), (BC_BOT - c, Z_REAR - c),
           (BC_BOT - c, BC_Z_BACK - c), (BEZ_TOP, BC_Z_BACK - c), (BEZ_TOP, Z_FRONT + c)]
    with BuildSketch(Plane.YZ) as sk:
        Polygon(*pts, align=None)
    g = extrude(sk.sketch, length / 2, both=True)
    return to_world(g)


def make_stand():
    base = rrect_prism(STAND_W, STAND_D, STAND_R, 0, STAND_H)
    base = fillet(edges_at_z(base, STAND_H), STAND_TOP_FILLET)
    base = chamfer(edges_at_z(base, 0), BOTTOM_CHAMFER)

    # toes peeking out in front
    toes = None
    for sx in (-1, 1):
        t = rrect_prism(TOE_W, TOE_L, TOE_L / 2 - 0.01, 0, TOE_H,
                        cx=sx * TOE_X, cy=-STAND_D / 2 - TOE_OUT + TOE_L / 2)
        t = fillet(edges_at_z(t, TOE_H), 4.5)
        t = chamfer(edges_at_z(t, 0), BOTTOM_CHAMFER)
        toes = t if toes is None else toes + t

    # backrest, built in device coords right behind the back cover
    c = STAND_CLEAR
    y0, y1 = BEZ_BOT - 30, BEZ_BOT + BACKREST_UP
    z1 = BC_Z_BACK - c + 0.01
    z0 = z1 - BACKREST_T
    br = rrect_prism(BACKREST_W, y1 - y0, BACKREST_R, z0, z1, cy=(y0 + y1) / 2)
    br = fillet(edges_at_z(br, z0), 3.5)
    br = to_world(br) & box(-200, 200, -200, 200, 0.01, 300)

    tail = rrect_prism(TAIL_W, TAIL_L, TAIL_L / 2 - 0.01, 0, TAIL_H, cy=STAND_D / 2 + TAIL_OUT - TAIL_L / 2)
    tail = fillet(edges_at_z(tail, TAIL_H), 4.0)
    tail = chamfer(edges_at_z(tail, 0), BOTTOM_CHAMFER)

    stand = base + toes + tail + br
    stand = stand - groove_cutter(STAND_W + 40)

    # cable channel: drops from the back-cover slot, then runs out the back
    cx = (CABLE_SLOT_X[0] + CABLE_SLOT_X[1]) / 2
    stand = stand - box(cx - STAND_CABLE_W / 2, cx + STAND_CABLE_W / 2,
                        to_world_pt(0, BC_BOT, Z_REAR)[1] - 2, 200, STAND_CABLE_FLOOR, 300)
    ch_e = stand.edges().filter_by(Axis.Y).filter_by(
        lambda e: abs(e.center().Z - STAND_CABLE_FLOOR) < 1e-3 and abs(e.center().X - cx) < STAND_CABLE_W)
    try:
        stand = fillet(ch_e, 2.5)
    except Exception:
        pass

    # coin pockets + rubber-foot recesses underneath
    for (x, y) in coin_positions():
        stand = stand - cyl(x, y, -1, COIN_DEPTH, COIN_D)
    for (x, y) in foot_positions():
        stand = stand - cyl(x, y, -1, FOOT_DEPTH, FOOT_D)
    return stand


def coin_positions():
    yb = STAND_D / 2 - 30
    return [(-70, yb), (-25, yb), (25, yb), (70, yb)]


def foot_positions():
    ix = STAND_W / 2 - 22
    iy = STAND_D / 2 - 18
    return [(-ix, -iy), (ix, -iy), (-ix, iy), (ix, iy)]


def make_fit_template():
    """2 mm plate the size of the pocket with corner fences.

    Lay the panel face-down on a towel, drop this on its back: the six
    standoffs must pass through the holes, the module must pass through the
    big window, and the panel edges must sit inside the four corner fences
    (fences = the real pocket size).
    """
    W, H = POCKET_W, POCKET_H
    plate = rrect_prism(W + 2 * TPL_FENCE_T, H + 2 * TPL_FENCE_T, POCKET_R + TPL_FENCE_T, 0, TPL_T)
    # module window (+/- MOD_OFFSET allowance + 1 mm)
    mw = MOD_W + 2 * MOD_OFFSET + 2
    mh = MOD_H + 2 * MOD_OFFSET + 2
    plate = plate - rrect_prism(mw, mh, 4, -1, TPL_T + 1)
    # lightening windows left/right of the module window, stopping short of the standoff rings
    gap = (W - mw) / 2 - 2 * TPL_BAND
    ring = (SO_D + 2 * FIT) / 2 + 4.0
    win_h = 2 * (SO_ROW_PITCH / 2 - ring)
    if gap > 10 and win_h > 20:
        for sx in (-1, 1):
            cx = sx * (mw / 2 + TPL_BAND + gap / 2)
            plate = plate - rrect_prism(gap, win_h, 4, -1, TPL_T + 1, cx=cx)
    # standoff holes (slip over the brass bosses)
    for (x, y) in standoff_positions():
        plate = plate - cyl(x, y, -1, TPL_T + 1, SO_D + 2 * FIT)
    # corner fences (inner faces = pocket)
    fences = None
    L = 30
    for sx in (-1, 1):
        for sy in (-1, 1):
            xo = sx * (W / 2 + TPL_FENCE_T / 2)
            yo = sy * (H / 2 + TPL_FENCE_T / 2)
            fx = box(xo - TPL_FENCE_T / 2, xo + TPL_FENCE_T / 2,
                     min(yo, yo - sy * L), max(yo, yo - sy * L), TPL_T, TPL_T + TPL_FENCE_H)
            fy = box(min(xo, xo - sx * L), max(xo, xo - sx * L),
                     yo - TPL_FENCE_T / 2, yo + TPL_FENCE_T / 2, TPL_T, TPL_T + TPL_FENCE_H)
            f = fx + fy
            fences = f if fences is None else fences + f
    plate = plate + fences
    # ports-side marker (an arrow-ish notch on the -X edge) and a label
    plate = plate - box(-W / 2 - TPL_FENCE_T - 1, -W / 2 + 4, -4, 4, -1, TPL_T + 1)
    try:
        # read from behind (the side you see when it lies on the panel back) -> mirror,
        # and put it on the bed face (Z=0), which is the face you look at
        t = Text("BACK VIEW   USB ports this side  -->", font_size=5, font=WORDMARK_FONT)
        t = mirror(t, Plane.YZ)
        t = Pos(0, -(mh / 2 + 6.5), -0.01) * extrude(t, 0.61)
        plate = plate - t
    except Exception:
        pass
    plate = chamfer(edges_at_z(plate, 0).filter_by(lambda e: e.length > 20), 0.4)
    return plate


# ---------------------------------------------------------------------------
# reference geometry (not printed) used for checks and renders
# ---------------------------------------------------------------------------
def panel_solid(w=PANEL_W, h=PANEL_H, t=PANEL_T):
    return box(-w / 2, w / 2, -h / 2, h / 2, 0, t)


def glass_solid(t=PANEL_T):
    return box(-GLASS_W / 2, GLASS_W / 2, -GLASS_H / 2, GLASS_H / 2, t - 1.0, t)


def module_solid(dx=0.0, dy=0.0):
    return box(-MOD_W / 2 + dx, MOD_W / 2 + dx, -MOD_H / 2 + dy, MOD_H / 2 + dy, -MOD_D, 0)


def plug_envelope(dx=0.0, dy=0.0):
    """Anywhere along the module's left edge (front view), anywhere in its depth."""
    x1 = -MOD_W / 2 + dx
    return box(x1 - PLUG_LEN, x1, -MOD_H / 2 + dy, MOD_H / 2 + dy, -MOD_D, -0.5)


def standoffs_solid():
    s = None
    for (x, y) in standoff_positions():
        c = cyl(x, y, -SO_H, 0, SO_D)
        s = c if s is None else s + c
    return s


def cable_route():
    """6 mm cable from the lowest possible plug, outboard of the standoff boss,
    to the exit slot."""
    zc = Z_REAR - CABLE_SLOT_DEPTH + CABLE_D / 2 + 0.6
    xmid = (CABLE_SLOT_X[0] + CABLE_SLOT_X[1]) / 2
    x_out = -(MOD_W / 2 + MOD_OFFSET + PLUG_LEN) - CABLE_D / 2 - 6   # outboard lane
    pts = [(-(MOD_W / 2 + MOD_OFFSET + PLUG_LEN) + 2, -MOD_H / 2 + 8, zc),
           (x_out, -MOD_H / 2 + 2, zc),
           (x_out, -72, zc),
           (xmid, -90, zc),
           (xmid, BC_BOT - 4, zc)]
    path = Spline(*[Vector(*p) for p in pts])
    prof = Plane(origin=path @ 0, z_dir=path % 0)
    return sweep(prof * Circle(CABLE_D / 2), path)


# =============================================================================
# CHECKS
# =============================================================================
class Checks:
    def __init__(self):
        self.lines = []

    def ok(self, cond, msg):
        self.lines.append(("PASS" if cond else "FAIL", msg))
        print(f"  [{'PASS' if cond else 'FAIL'}] {msg}")
        assert cond, msg


def vol(a, b):
    try:
        r = a & b
        return 0.0 if r is None else abs(r.volume)
    except Exception:
        return 0.0


def bbox_dims(s):
    bb = s.bounding_box()
    return bb.max.X - bb.min.X, bb.max.Y - bb.min.Y, bb.max.Z - bb.min.Z


def run_checks(parts):
    C = Checks()
    bez, env, bc, stand = parts["bezel"], parts["bezel_env"], parts["back_cover"], parts["stand"]
    left, right = parts["bezel_left"], parts["bezel_right"]

    print("\n== window / active area ==")
    # window inside glass with the requested overlap, active area (with tolerance) inside window
    C.ok(abs((GLASS_W - WINDOW_W) / 2 - WINDOW_OVERLAP) < 1e-6, f"window {WINDOW_W:.1f} x {WINDOW_H:.1f} overlaps glass by {WINDOW_OVERLAP} mm")
    ax, ay = ACTIVE_W / 2 + ACTIVE_TOL, ACTIVE_H / 2 + ACTIVE_TOL
    wx, wy = WINDOW_W / 2, WINDOW_H / 2
    cxr, cyr = wx - WINDOW_R, wy - WINDOW_R
    corner_ok = (ax <= cxr or ay <= cyr) or math.hypot(ax - cxr, ay - cyr) <= WINDOW_R
    C.ok(ax < wx and ay < wy and corner_ok,
         f"active area +/-{ACTIVE_TOL} fits the window (margins {wx-ax:.2f} X, {wy-ay:.2f} Y, corner r{WINDOW_R})")
    C.ok(FOAM_SEAT_IN + FOAM_SEAT_W < WINDOW_OVERLAP, "foam channel lies on the glass overlap")
    C.ok(FOAM_SEAT_IN >= WINDOW_CHAMFER, "foam channel clear of the window chamfer (lip stays >= 1.6 mm)")

    print("\n== panel in the bezel ==")
    for (w, h, t, tag) in [(PANEL_W, PANEL_H, PANEL_T, "vendor"), (PANEL_W_MEAS, PANEL_H, PANEL_T_MEAS, "worst-case")]:
        p = panel_solid(w, h, t)
        C.ok(vol(p, bez) < 0.01, f"{tag} panel {w}x{h}x{t} does not hit the bezel (gap to lip {POCKET_D - t:.2f})")
    C.ok(POCKET_W - max(PANEL_W, PANEL_W_MEAS) >= 0.5 and POCKET_H - max(PANEL_H, PANEL_H_MEAS) >= 0.5,
         f"pocket clearance X {POCKET_W - max(PANEL_W, PANEL_W_MEAS):.2f}, Y {POCKET_H - max(PANEL_H, PANEL_H_MEAS):.2f}")

    print("\n== module / standoffs / cable in the back cover ==")
    so = standoffs_solid()
    C.ok(vol(so, bc) < 0.01 and vol(so, bez) < 0.01, "standoffs clear bezel and back cover (tips land on the columns)")
    dmin = 1e9
    for dx in (-MOD_OFFSET, 0, MOD_OFFSET):
        for dy in (-MOD_OFFSET, 0, MOD_OFFSET):
            m = module_solid(dx, dy) + plug_envelope(dx, dy)
            assert vol(m, bc) < 0.01, f"module at offset ({dx},{dy}) hits back cover"
            assert vol(m, bez) < 0.01, f"module at offset ({dx},{dy}) hits bezel"
            dmin = min(dmin, m.distance_to(bc))
    C.ok(dmin >= 1.0, f"module + USB plug envelope clear in all 9 offset corners (+/-{MOD_OFFSET}), min gap {dmin:.2f} mm")
    cable = cable_route()
    C.ok(vol(cable, bc) < 1.0 and vol(cable, so) < 0.5 and vol(cable, bez) < 0.5,
         f"{CABLE_D} mm cable route (outboard of the boss -> bottom slot) is free")
    # panel screw length
    tip = -SO_H - SCREW_FLOOR + PANEL_SCREW_LEN
    C.ok(tip <= -0.8, f"{PANEL_SCREW}x{PANEL_SCREW_LEN:g} tip stops {-tip:.1f} mm short of the panel back")
    C.ok(PANEL_SCREW_LEN - SCREW_FLOOR >= 2.5, f"{PANEL_SCREW} thread engagement {PANEL_SCREW_LEN - SCREW_FLOOR:.1f} mm")
    eng = BEZEL_SCREW_LEN - SCREW_FLOOR
    C.ok(4.0 <= eng <= INSERT_DEPTH - 0.5, f"M3x{BEZEL_SCREW_LEN:g} into insert: engagement {eng:.1f} mm, hole {INSERT_DEPTH}")

    print("\n== bezel <-> back cover ==")
    C.ok(vol(bez, bc) < 0.5, "bezel and back cover do not interfere")
    C.ok(BC_TOP <= BEZ_TOP - REAR_FILLET - 0.3 and BC_BOT >= BEZ_BOT + REAR_FILLET + 0.3
         and BC_W / 2 <= BEZEL_W / 2 - REAR_FILLET - 0.3, "back cover sits on the bezel's flat rear face")
    C.ok(BC_W / 2 >= POCKET_W / 2 + 0.8, f"back cover overlaps the pocket edge ({BC_W / 2 - POCKET_W / 2:.1f} mm/side)")
    for (x, y) in insert_positions():
        assert abs(y) > POCKET_H / 2 + INSERT_HOLE_D / 2 + 1.5, "insert too close to the pocket"
    C.ok(True, "insert holes keep >= 1.5 mm wall to the pocket")

    print("\n== split bezel ==")
    C.ok(vol(left, right) < 0.5, "left/right halves do not interfere (dovetail clearance)")
    v_sum = left.volume + right.volume
    C.ok(abs(v_sum - bez.volume) / bez.volume < 0.01, f"halves add up to the one-piece bezel ({v_sum / bez.volume * 100:.2f} %)")

    print("\n== stand ==")
    case_w = to_world(bez + bc)
    C.ok(vol(case_w, stand) < 1.0, "case drops into the stand groove without interference")
    gap = case_w.distance_to(stand)
    C.ok(STAND_CLEAR - 0.05 <= gap <= STAND_CLEAR + 0.3,
         f"case sits in the groove with {gap:.2f} mm clearance (groove floor carries it)")
    # cheeks stay visible above the stand's front lip
    cheek_low = to_world_pt(CHEEK_X, CHEEK_Y - CHEEK_D / 2, Z_FRONT)[2]
    C.ok(cheek_low >= STAND_H + 1.5, f"cheeks clear the stand lip ({cheek_low - STAND_H:.1f} mm)")
    cable_w = to_world(cable_route())
    C.ok(vol(cable_w, stand) < 1.0, "cable leaves the slot into the stand's cable channel")

    stab = stability(parts)
    C.ok(stab["margin_rear"] > 25 and stab["margin_front"] > 25,
         f"COM inside the footprint: {stab['margin_front']:.0f} mm from front edge, {stab['margin_rear']:.0f} mm from rear edge")
    C.ok(stab["tip_force_N"] >= 2.5, f"push on the top of the screen needed to tip it back: {stab['tip_force_N']:.1f} N (no coins)")

    print("\n== print volumes ==")
    for name, s in parts["print"].items():
        d = bbox_dims(s)
        lim = BED_U1 if name == "bezel_onepiece" else BED_ANY
        C.ok(max(d) <= lim, f"{name:18s} {d[0]:6.1f} x {d[1]:6.1f} x {d[2]:5.1f}  (<= {lim:g})")
    for name, s in parts["print"].items():
        bb = s.bounding_box()
        assert abs(bb.min.Z) < 1e-3, f"{name} not on the bed"
    C.ok(True, "all print parts sit on Z=0")
    return C, stab


def stability(parts):
    g = 9.81
    def m_of(s):
        return s.volume * PLA_DENSITY * PRINT_FILL
    case = to_world(parts["bezel"] + parts["back_cover"])
    m_case = m_of(parts["bezel"]) + m_of(parts["back_cover"])
    c_case = case.center()
    dev = to_world(panel_solid() + module_solid())
    c_dev = dev.center()
    m_dev = DEVICE_MASS_G
    m_st = m_of(parts["stand"])
    c_st = parts["stand"].center()
    M = m_case + m_dev + m_st
    com = (c_case * m_case + c_dev * m_dev + c_st * m_st) * (1.0 / M)
    y_front = -STAND_D / 2 - TOE_OUT
    y_rear = STAND_D / 2
    # push normal to the screen at the top of the window
    p = Vector(*to_world_pt(0, WINDOW_H / 2, PANEL_T))
    n = Vector(0, math.cos(TILT), -math.sin(TILT))      # into the screen
    # tipping about the rear edge line (y = y_rear, z = 0)
    r = p - Vector(0, y_rear, 0)
    torque_per_N = r.Y * n.Z - r.Z * n.Y                # about +X
    restoring = M / 1000 * g * (y_rear - com.Y)       # N*mm
    tip_force = restoring / abs(torque_per_N) if torque_per_N != 0 else float("inf")
    return dict(mass_case_g=m_case, mass_stand_g=m_st, mass_total_g=M, com=(com.X, com.Y, com.Z),
                margin_front=com.Y - y_front, margin_rear=y_rear - com.Y, tip_force_N=tip_force)


# =============================================================================
# BUILD / EXPORT
# =============================================================================
def print_orient_bezel(s):
    """Face-down: front face on the bed."""
    s = Rot(0, 180, 0) * s
    bb = s.bounding_box()
    return Pos(-(bb.min.X + bb.max.X) / 2, -(bb.min.Y + bb.max.Y) / 2, -bb.min.Z) * s


def print_orient_flat(s):
    bb = s.bounding_box()
    return Pos(-(bb.min.X + bb.max.X) / 2, -(bb.min.Y + bb.max.Y) / 2, -bb.min.Z) * s


def build():
    t0 = time.time()
    print("building bezel ...")
    bezel, env = make_bezel()
    print("splitting bezel ...")
    left, right = split_bezel(bezel)
    print("building back cover ...")
    bc, bc_outer = make_back_cover()
    print("building stand ...")
    stand = make_stand()
    print("building cheek insert + fit template ...")
    cheek = make_cheek_insert()
    tpl = make_fit_template()
    print(f"  built in {time.time() - t0:.1f} s")
    parts = dict(bezel=bezel, bezel_env=env, bezel_left=left, bezel_right=right,
                 back_cover=bc, stand=stand, cheek=cheek, template=tpl)
    parts["print"] = {
        "bezel_onepiece": print_orient_bezel(bezel),
        "bezel_left": print_orient_bezel(left),
        "bezel_right": print_orient_bezel(right),
        "back_cover": print_orient_flat(bc),           # back face down
        "stand": stand,                                 # already on its bottom
        "cheek_insert": print_orient_flat(cheek),
        "fit_test_template": print_orient_flat(tpl),
    }
    return parts


def export(parts):
    (HERE / "stl").mkdir(exist_ok=True)
    (HERE / "step").mkdir(exist_ok=True)
    for name, s in parts["print"].items():
        export_stl(s, str(HERE / "stl" / f"{name}.stl"), tolerance=TESS_TOL, angular_tolerance=TESS_ANG)
        export_step(s, str(HERE / "step" / f"{name}.step"))
    asm = assembly_world(parts, exploded=0.0, with_device=False)
    export_step(Compound(children=[s for s, _c, _n in asm]), str(HERE / "step" / "deskling_assembly.step"))
    print("exported stl/ and step/")


def cheek_in_place():
    out = []
    for sx in (-1, 1):
        c = make_cheek_insert()
        out.append(Pos(sx * CHEEK_X, CHEEK_Y, Z_FRONT - CHEEK_DEPTH) * c)
    return out


COL = dict(body=(0.80, 0.76, 0.95), stand=(0.96, 0.92, 0.86), cheek=(0.96, 0.72, 0.62),
           glass=(0.11, 0.10, 0.13), screen=(0.98, 0.96, 0.93), ink=(0.17, 0.14, 0.20),
           module=(0.25, 0.42, 0.33), metal=(0.62, 0.62, 0.64), brass=(0.80, 0.66, 0.30),
           cable=(0.18, 0.18, 0.20))


def screen_face():
    """Render-only: Deskling's face on the screen (two dark oval eyes + blush)."""
    z = PANEL_T + 0.05
    act = box(-ACTIVE_W / 2, ACTIVE_W / 2, -ACTIVE_H / 2, ACTIVE_H / 2, PANEL_T - 0.2, z)
    eyes = []
    for sx in (-1, 1):
        with BuildSketch() as sk:
            from build123d import Ellipse
            with Locations((sx * 30, 8)):
                Ellipse(9, 14)
        eyes.append(Pos(0, 0, z) * extrude(sk.sketch, 0.2))
    blush = []
    for sx in (-1, 1):
        with BuildSketch() as sk:
            from build123d import Ellipse
            with Locations((sx * 54, -12)):
                Ellipse(12, 7)
        blush.append(Pos(0, 0, z) * extrude(sk.sketch, 0.15))
    with BuildSketch() as sk:
        from build123d import Ellipse
        with Locations((0, -14)):
            Ellipse(7, 4.5)
    mouth = [Pos(0, 0, z) * extrude(sk.sketch, 0.2) & box(-20, 20, -40, -14, -1, 20)]
    return act, eyes, blush, mouth


def assembly_device(parts, exploded=0.0, with_device=True):
    """List of (shape, colour, name) in DEVICE coords. exploded spreads parts along Z."""
    e = exploded
    out = [(Pos(0, 0, 1.6 * e) * parts["bezel"], COL["body"], "bezel")]
    for c in cheek_in_place():
        out.append((Pos(0, 0, 2.4 * e) * c, COL["cheek"], "cheek"))
    out.append((Pos(0, 0, -1.6 * e) * parts["back_cover"], COL["body"], "back_cover"))
    if with_device:
        out.append((panel_solid() - glass_solid(), COL["metal"], "panel"))
        out.append((glass_solid(), COL["glass"], "glass"))
        act, eyes, blush, mouth = screen_face()
        out.append((act, COL["screen"], "screen"))
        for s in eyes + mouth:
            out.append((s, COL["ink"], "eye"))
        for s in blush:
            out.append((s, COL["cheek"], "blush"))
        out.append((Pos(0, 0, -0.6 * e) * module_solid(), COL["module"], "module"))
        out.append((Pos(0, 0, -0.3 * e) * standoffs_solid(), COL["brass"], "standoffs"))
    return out


def assembly_world(parts, exploded=0.0, with_device=True, stand_drop=0.0):
    out = [(to_world(s), c, n) for s, c, n in assembly_device(parts, exploded, with_device)]
    out.append((Pos(0, 0, -stand_drop) * parts["stand"], COL["stand"], "stand"))
    return out


# =============================================================================
# HEADLESS RENDERER (numpy z-buffer, no GPU / display needed)
# =============================================================================
def mesh_of(shape, tol=0.12, ang=0.25):
    import numpy as np
    V_all, F_all, N_all = [], [], []
    off = 0
    for f in shape.faces():
        try:
            verts, tris = f.tessellate(tol, ang)
        except Exception:
            continue
        if not tris:
            continue
        V = np.array([[v.X, v.Y, v.Z] for v in verts], dtype=np.float64)
        F = np.array(tris, dtype=np.int64)
        # per-face (B-rep face) smooth vertex normals => sharp edges stay sharp
        fn = np.cross(V[F[:, 1]] - V[F[:, 0]], V[F[:, 2]] - V[F[:, 0]])
        vn = np.zeros_like(V)
        for k in range(3):
            np.add.at(vn, F[:, k], fn)
        vn /= np.linalg.norm(vn, axis=1, keepdims=True) + 1e-12
        V_all.append(V); F_all.append(F + off); N_all.append(vn)
        off += len(V)
    if not V_all:
        return np.zeros((0, 3)), np.zeros((0, 3), int), np.zeros((0, 3))
    return np.vstack(V_all), np.vstack(F_all), np.vstack(N_all)


class Camera:
    def __init__(self, eye, target, up=(0, 0, 1), fov=28.0, W=1600, H=1200, ortho_scale=None):
        import numpy as np
        self.eye = np.array(eye, float); self.target = np.array(target, float)
        f = self.target - self.eye; f /= np.linalg.norm(f)
        r = np.cross(f, np.array(up, float)); r /= np.linalg.norm(r)
        u = np.cross(r, f)
        self.R = np.stack([r, u, -f])          # world -> camera (camera looks down -z)
        self.fov = fov; self.W = W; self.H = H; self.ortho_scale = ortho_scale

    def project(self, P):
        import numpy as np
        c = (P - self.eye) @ self.R.T
        z = -c[:, 2]
        if self.ortho_scale:
            s = self.H / self.ortho_scale
            x = self.W / 2 + c[:, 0] * s
            y = self.H / 2 - c[:, 1] * s
        else:
            fpx = (self.H / 2) / math.tan(math.radians(self.fov) / 2)
            x = self.W / 2 + fpx * c[:, 0] / z
            y = self.H / 2 - fpx * c[:, 1] / z
        return np.stack([x, y, z], axis=1)


def rasterize(cam, meshes, ss=2):
    """meshes: list of (V, F, N, rgb, obj_id). Returns colour/normal/depth/id buffers."""
    import numpy as np
    W, H = cam.W * ss, cam.H * ss
    zbuf = np.full(H * W, np.inf)
    nbuf = np.zeros((H * W, 3))
    cbuf = np.zeros((H * W, 3))
    ibuf = np.full(H * W, -1, dtype=np.int64)
    cam2 = Camera.__new__(Camera)
    cam2.__dict__.update(cam.__dict__); cam2.W, cam2.H = W, H
    for (V, F, N, rgb, oid) in meshes:
        if len(F) == 0:
            continue
        P = cam2.project(V)
        T = P[F]                                  # (n,3,3)
        TN = N[F]
        x0 = np.floor(T[:, :, 0].min(1)).astype(int); x1 = np.ceil(T[:, :, 0].max(1)).astype(int)
        y0 = np.floor(T[:, :, 1].min(1)).astype(int); y1 = np.ceil(T[:, :, 1].max(1)).astype(int)
        keep = (x1 >= 0) & (x0 < W) & (y1 >= 0) & (y0 < H) & (T[:, :, 2].min(1) > 1e-3)
        sz = np.maximum(x1 - x0, y1 - y0) + 1
        for lo, hi in ((0, 2), (2, 4), (4, 8), (8, 16), (16, 32), (32, 64), (64, 10 ** 9)):
            sel = np.nonzero(keep & (sz > lo) & (sz <= hi))[0]
            if len(sel) == 0:
                continue
            if hi > 64:
                for i in sel:
                    _raster_batch(T[i:i + 1], TN[i:i + 1], x0[i:i + 1], y0[i:i + 1], int(sz[i]),
                                  W, H, zbuf, nbuf, cbuf, ibuf, rgb, oid)
            else:
                step = max(1, int(4e6 // (hi * hi)))
                for s0 in range(0, len(sel), step):
                    b = sel[s0:s0 + step]
                    _raster_batch(T[b], TN[b], x0[b], y0[b], hi, W, H, zbuf, nbuf, cbuf, ibuf, rgb, oid)
    return (cbuf.reshape(H, W, 3), nbuf.reshape(H, W, 3), zbuf.reshape(H, W), ibuf.reshape(H, W))


def _raster_batch(T, TN, x0, y0, s, W, H, zbuf, nbuf, cbuf, ibuf, rgb, oid):
    import numpy as np
    if s > 64:  # big triangle: tile it
        for oy in range(0, s, 256):
            for ox in range(0, s, 256):
                _raster_tile(T, TN, x0 + ox, y0 + oy, min(256, s - ox), min(256, s - oy),
                             W, H, zbuf, nbuf, cbuf, ibuf, rgb, oid)
        return
    _raster_tile(T, TN, x0, y0, s, s, W, H, zbuf, nbuf, cbuf, ibuf, rgb, oid)


def _raster_tile(T, TN, x0, y0, sw, sh, W, H, zbuf, nbuf, cbuf, ibuf, rgb, oid):
    import numpy as np
    gx, gy = np.meshgrid(np.arange(sw), np.arange(sh))
    px = x0[:, None] + gx.ravel()[None, :]
    py = y0[:, None] + gy.ravel()[None, :]
    cx = px + 0.5; cy = py + 0.5
    ax, ay = T[:, 0, 0:1], T[:, 0, 1:2]
    bx, by = T[:, 1, 0:1], T[:, 1, 1:2]
    qx, qy = T[:, 2, 0:1], T[:, 2, 1:2]
    den = (by - qy) * (ax - qx) + (qx - bx) * (ay - qy)
    den = np.where(np.abs(den) < 1e-12, 1e-12, den)
    w0 = ((by - qy) * (cx - qx) + (qx - bx) * (cy - qy)) / den
    w1 = ((qy - ay) * (cx - qx) + (ax - qx) * (cy - qy)) / den
    w2 = 1 - w0 - w1
    inside = (w0 >= -1e-6) & (w1 >= -1e-6) & (w2 >= -1e-6) & (px >= 0) & (px < W) & (py >= 0) & (py < H)
    if not inside.any():
        return
    ti, pi = np.nonzero(inside)
    W0, W1, W2 = w0[ti, pi], w1[ti, pi], w2[ti, pi]
    # perspective-correct depth: interpolate 1/z
    iz = W0 / T[ti, 0, 2] + W1 / T[ti, 1, 2] + W2 / T[ti, 2, 2]
    z = 1.0 / iz
    idx = py[ti, pi] * W + px[ti, pi]
    order = np.lexsort((z, idx))
    idx_s = idx[order]
    first = np.ones(len(idx_s), bool); first[1:] = idx_s[1:] != idx_s[:-1]
    o = order[first]
    idx_u = idx[o]; z_u = z[o]
    better = z_u < zbuf[idx_u]
    if not better.any():
        return
    o = o[better]; idx_u = idx_u[better]
    zbuf[idx_u] = z_u[better]
    n = (W0[o, None] * TN[ti[o], 0] + W1[o, None] * TN[ti[o], 1] + W2[o, None] * TN[ti[o], 2])
    nbuf[idx_u] = n / (np.linalg.norm(n, axis=1, keepdims=True) + 1e-12)
    cbuf[idx_u] = rgb
    ibuf[idx_u] = oid


def shade_and_save(cam, buffers, path, ss=2, light_dir=(-0.45, -0.75, 0.85), floor_shadow=None,
                   bg_top=(0.985, 0.972, 0.952), bg_bot=(0.93, 0.915, 0.89), lines=True):
    import numpy as np
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.image as mpimg
    col, nrm, z, ids = buffers
    H, W = z.shape
    hit = np.isfinite(z)
    L = np.array(light_dir, float); L /= np.linalg.norm(L)
    L2 = np.array([0.7, -0.3, 0.35]); L2 /= np.linalg.norm(L2)
    view = -cam.R[2]                                  # camera forward in world
    # make normals face the camera (thin parts / open shells)
    flip = (nrm @ view) > 0
    nrm = np.where(flip[..., None], -nrm, nrm)
    lam = np.clip(nrm @ L, 0, 1)
    lam2 = np.clip(nrm @ L2, 0, 1)
    sky = 0.5 + 0.5 * nrm[..., 2]
    Hv = L - view; Hv /= np.linalg.norm(Hv)
    spec = np.clip(nrm @ Hv, 0, 1) ** 40
    shade = 0.38 + 0.50 * lam + 0.14 * lam2 + 0.12 * sky
    img = col * shade[..., None] + 0.10 * spec[..., None]
    # background gradient
    t = np.linspace(0, 1, H)[:, None, None]
    bg = (1 - t) * np.array(bg_top) + t * np.array(bg_bot)
    bg = np.broadcast_to(bg, (H, W, 3)).copy()
    if floor_shadow is not None:
        bg *= floor_shadow[..., None]
    out = np.where(hit[..., None], img, bg)
    if lines:
        # screen-space outlines: object id changes, depth jumps, normal creases
        e = np.zeros((H, W), bool)
        for dy, dx in ((0, 1), (1, 0), (1, 1)):
            a_id, b_id = ids[:H - dy, :W - dx], ids[dy:, dx:]
            za, zb = z[:H - dy, :W - dx], z[dy:, dx:]
            na, nb = nrm[:H - dy, :W - dx], nrm[dy:, dx:]
            fa, fb = np.isfinite(za), np.isfinite(zb)
            both = fa & fb
            with np.errstate(invalid="ignore", divide="ignore"):
                zz = np.where(both, np.abs(np.where(both, za - zb, 0.0)) / np.where(both, np.minimum(za, zb), 1.0), 0.0)
            crease = both & (np.sum(na * nb, axis=-1) < 0.80)
            m = (fa | fb) & ((a_id != b_id) | (zz > 0.004 * ss) | crease)
            e[:H - dy, :W - dx] |= m
        ink = np.array([0.20, 0.17, 0.24])
        out = np.where(e[..., None], out * 0.35 + ink * 0.65, out)
    # downsample (box filter) + gamma-ish
    out = out.reshape(H // ss, ss, W // ss, ss, 3).mean(axis=(1, 3))
    out = np.clip(out, 0, 1)
    mpimg.imsave(path, out)


def soft_floor_shadow(cam, meshes, ss, extent=420, res=1.0, light=(-0.25, -0.35, 1.0)):
    """Blurred top-down occupancy -> darkening factor for the background/floor pixels."""
    import numpy as np
    try:
        from scipy.ndimage import gaussian_filter
    except Exception:
        return None
    n = int(extent / res)
    occ = np.zeros((n, n))
    Ld = np.array(light, float); Ld /= Ld[2]
    for (V, F, N, rgb, oid) in meshes:
        if len(V) == 0:
            continue
        # project along the light onto z=0
        P = V[:, :2] - V[:, 2:3] * Ld[None, :2]
        ix = ((P[:, 0] + extent / 2) / res).astype(int)
        iy = ((P[:, 1] + extent / 2) / res).astype(int)
        ok = (ix >= 0) & (ix < n) & (iy >= 0) & (iy < n)
        h = np.clip(1.0 - V[ok, 2] / 250.0, 0.2, 1.0)
        np.maximum.at(occ, (iy[ok], ix[ok]), h)
    occ = gaussian_filter(occ, 3.0 / res)
    occ = gaussian_filter(np.clip(occ * 3, 0, 1), 6.0 / res)
    # per screen pixel: intersect view ray with z=0
    W, H = cam.W * ss, cam.H * ss
    fpx = (H / 2) / math.tan(math.radians(cam.fov) / 2)
    xs, ys = np.meshgrid(np.arange(W) + 0.5, np.arange(H) + 0.5)
    dc = np.stack([(xs - W / 2) / fpx, -(ys - H / 2) / fpx, -np.ones_like(xs)], -1)
    dw = dc @ cam.R
    tt = -cam.eye[2] / np.where(np.abs(dw[..., 2]) < 1e-9, -1e-9, dw[..., 2])
    Pw = cam.eye[None, None, :] + tt[..., None] * dw
    ix = ((Pw[..., 0] + extent / 2) / res).astype(int)
    iy = ((Pw[..., 1] + extent / 2) / res).astype(int)
    ok = (tt > 0) & (ix >= 0) & (ix < n) & (iy >= 0) & (iy < n)
    f = np.ones((H, W))
    f[ok] = 1.0 - 0.35 * occ[iy[ok], ix[ok]]
    return f


def render_scene(items, cam, path, ss=2, shadow=True, lines=True, light_dir=(-0.45, -0.75, 0.85)):
    only = os.environ.get("DESKLING_RENDER_ONLY")
    if only and not any(o in os.path.basename(path) for o in only.split(",")):
        return
    meshes = []
    for k, (shape, rgb, name) in enumerate(items):
        V, F, N = mesh_of(shape)
        meshes.append((V, F, N, rgb, k))
    buf = rasterize(cam, meshes, ss=ss)
    fs = soft_floor_shadow(cam, meshes, ss) if shadow else None
    shade_and_save(cam, buf, path, ss=ss, floor_shadow=fs, lines=lines, light_dir=light_dir)
    print(f"  wrote {path}")


def render_all(parts):
    rd = HERE / "renders"
    rd.mkdir(exist_ok=True)
    W, H = 1600, 1200
    sy, lift = case_lift_and_shift()
    tgt = (0, 0, 95)
    # 1. front 3/4 on the stand
    cam = Camera(eye=(-430, -640, 330), target=tgt, fov=26, W=W, H=H)
    render_scene(assembly_world(parts), cam, str(rd / "front_3q.png"))
    # 2. straight front
    cam = Camera(eye=(0, -900, 170), target=(0, 0, 100), fov=22, W=W, H=H)
    render_scene(assembly_world(parts), cam, str(rd / "front.png"))
    # 3. back 3/4
    cam = Camera(eye=(420, 700, 300), target=tgt, fov=26, W=W, H=H)
    render_scene(assembly_world(parts), cam, str(rd / "back_3q.png"), light_dir=(0.5, 0.8, 0.9))
    # 3b. straight back
    cam = Camera(eye=(0, 900, 260), target=(0, 0, 95), fov=22, W=W, H=H)
    render_scene(assembly_world(parts), cam, str(rd / "back.png"), light_dir=(0.35, 0.8, 0.9))
    # 4. exploded
    items = assembly_world(parts, exploded=50.0, stand_drop=90.0)
    cam = Camera(eye=(-930, -330, 300), target=(0, 10, 75), fov=30, W=W, H=H)
    render_scene(items, cam, str(rd / "exploded.png"), shadow=False)
    # 5. side section through the middle (stack-up check)
    cut = box(-400, 0, -400, 400, -400, 400)
    items = [((s & cut), c, n) for s, c, n in assembly_world(parts)]
    items = [(s, c, n) for s, c, n in items if s is not None and s.volume > 1e-3]
    cam = Camera(eye=(900, 0, 90), target=(0, 0, 70), fov=20, W=W, H=H)
    render_scene(items, cam, str(rd / "section_side.png"), shadow=False, light_dir=(0.9, -0.3, 0.5))
    cam = Camera(eye=(900, -20, 40), target=(0, -20, 40), fov=8, W=W, H=H)
    render_scene(items, cam, str(rd / "section_detail_bottom.png"), shadow=False, light_dir=(0.9, -0.3, 0.5))
    # 6. split bezel parts laid out for printing + template
    pl = parts["print"]
    items = [(Pos(78, 0, 0) * pl["bezel_left"], COL["body"], "L"),
             (Pos(-78, 0, 0) * pl["bezel_right"], (0.74, 0.70, 0.92), "R")]
    cam = Camera(eye=(0, -380, 520), target=(0, 0, 0), fov=34, W=W, H=H)
    render_scene(items, cam, str(rd / "bezel_split_print.png"), shadow=False)
    items = [(pl["fit_test_template"], (0.93, 0.80, 0.55), "tpl")]
    cam = Camera(eye=(0, -260, 360), target=(0, 0, 0), fov=40, W=W, H=H)
    render_scene(items, cam, str(rd / "fit_test_template.png"), shadow=False)
    # 7. inside of the back cover (from the front, panel removed)
    items = [(parts["back_cover"], COL["body"], "bc"), (module_solid(), COL["module"], "m"),
             (cable_route(), COL["cable"], "cable"), (standoffs_solid(), COL["brass"], "so")]
    cam = Camera(eye=(0, -120, 520), target=(0, 0, -10), up=(0, 1, 0), fov=34, W=W, H=H)
    render_scene(items, cam, str(rd / "back_cover_inside.png"), shadow=False, light_dir=(-0.3, -0.5, 0.9))
    # 8. stand on its own (top + underside)
    cam = Camera(eye=(-330, -420, 380), target=(0, 0, 10), fov=30, W=W, H=H)
    render_scene([(parts["stand"], COL["stand"], "stand")], cam, str(rd / "stand.png"))
    flip = Rot(180, 0, 0) * parts["stand"]
    cam = Camera(eye=(-250, -380, 420), target=(0, 0, -20), fov=30, W=W, H=H)
    render_scene([(flip, COL["stand"], "stand")], cam, str(rd / "stand_underside.png"), shadow=False)


# =============================================================================
def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[1])
    ap.add_argument("--no-render", action="store_true")
    ap.add_argument("--no-export", action="store_true")
    ap.add_argument("--only-render", default=None, help="comma list of render names (debug)")
    a = ap.parse_args(argv)
    parts = build()
    print("\nrunning fit checks ...")
    C, stab = run_checks(parts)
    print(f"\nmass estimate: case {stab['mass_case_g']:.0f} g, stand {stab['mass_stand_g']:.0f} g, "
          f"total with device {stab['mass_total_g']:.0f} g")
    if not a.no_export:
        export(parts)
    if not a.no_render:
        print("rendering ...")
        render_all(parts)
    print("done.")


if __name__ == "__main__":
    main()

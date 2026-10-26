#!/usr/bin/env python3
"""Compare two folders of benchmark frames (DK_FRAMES dumps) and report which frames differ.
   tools/frame_diff.py <before dir> <after dir> [--save <dir>]
Prints, per scene, how many frames differ, the worst pixel difference and how many pixels
differ by more than a small tolerance (anti-aliasing noise), so motion changes stand out."""
import os, sys
import numpy as np
from PIL import Image

a, b = sys.argv[1], sys.argv[2]
save = sys.argv[sys.argv.index('--save') + 1] if '--save' in sys.argv else None
if save: os.makedirs(save, exist_ok=True)
names = sorted(set(os.listdir(a)) & set(os.listdir(b)))
only = sorted(set(os.listdir(a)) ^ set(os.listdir(b)))
scenes = {}
for n in names:
    x = np.asarray(Image.open(os.path.join(a, n)), dtype=np.int16)
    y = np.asarray(Image.open(os.path.join(b, n)), dtype=np.int16)
    d = np.abs(x - y).max(axis=2)
    scene = n.split('_', 1)[1][:-4]
    s = scenes.setdefault(scene, [0, 0, 0, 0, None])
    s[0] += 1
    big = int((d > 24).sum())
    if d.max() > 0:
        s[1] += 1
    if big > s[3]:
        s[2], s[3], s[4] = int(d.max()), big, n
    if save and big:
        Image.fromarray(np.concatenate([x, y, np.repeat(np.minimum(d * 4, 255)[..., None], 3, 2)], 1).astype(np.uint8)).save(os.path.join(save, n))
print(f"{'scene':34} frames  differ  worst-frame px>24")
for k, (n, diff, mx, big, worst) in scenes.items():
    print(f"{k:34} {n:6} {diff:7}  {big:8}  {worst or ''}")
if only: print('frames only in one folder:', len(only))

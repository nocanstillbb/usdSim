#!/usr/bin/env python3
"""
Test shared USD Stage between Python and C++ viewport.

Uses pybind11 bindings only (no PySide6).
Python modifies the stage periodically, C++ viewport auto-refreshes.
"""

import sys
import os
import time
import random

# Run via: pixi run py-example python/examples/test_shared_stage/main.py
# (build/env.py sets up sys.path and LD_LIBRARY_PATH before this script runs)
from pxr import Usd, UsdUtils, UsdGeom, Gf, Vt
import pyusdSim


def main():
    sample_path = os.path.join(os.path.dirname(__file__), "sample.usda")
    sample_path = os.path.abspath(sample_path)

    # 1. Python opens the stage and inserts into StageCache
    stage = Usd.Stage.Open(sample_path)
    if not stage:
        print(f"Failed to open {sample_path}")
        return 1

    cache = UsdUtils.StageCache.Get()
    cache_id = cache.Insert(stage)
    print(f"Stage inserted into cache with ID: {cache_id.ToLongInt()}")

    # 2. Create and init the Qt app
    app = pyusdSim.UsdSimApp()
    app.register_types()
    app.init(["usdSim"])

    # 3. Find the QML UsdDocument and load from StageCache
    doc = app.find_document()
    if not doc:
        print("ERROR: UsdDocument not found in QML tree")
        return 1

    ok = doc.open_from_stage_cache(cache_id.ToLongInt())
    print(f"open_from_stage_cache: {ok}")

    # 4. Event loop with periodic stage modifications
    cube_prim = stage.GetPrimAtPath("/World/Cube")
    if not cube_prim:
        print("WARNING: /World/Cube not found, skipping animations")

    last_modify_time = time.time()
    modify_interval = 0.5  # seconds

    print("Running event loop. Press Ctrl+C to exit.")
    try:
        while True:
            app.process_events()

            now = time.time()
            if cube_prim and (now - last_modify_time) >= modify_interval:
                last_modify_time = now

                xformable = UsdGeom.Xformable(cube_prim)

                # Random translate
                tx = random.uniform(-100, 100)
                ty = random.uniform(-50, 50)
                tz = random.uniform(-100, 100)
                translate_op = UsdGeom.XformOp(
                    cube_prim.GetAttribute("xformOp:translate")
                )
                translate_op.Set(Gf.Vec3d(tx, ty, tz))

                # Random scale
                s = random.uniform(0.5, 2.0)
                scale_op = UsdGeom.XformOp(
                    cube_prim.GetAttribute("xformOp:scale")
                )
                scale_op.Set(Gf.Vec3d(s, s, s))

                # Random color
                color_attr = cube_prim.GetAttribute("primvars:displayColor")
                if color_attr:
                    r = random.random()
                    g = random.random()
                    b = random.random()
                    color_attr.Set(Vt.Vec3fArray([Gf.Vec3f(r, g, b)]))

                doc.notify_stage_modified(["/World/Cube"])
                print(f"  Modified: translate=({tx:.1f},{ty:.1f},{tz:.1f}) "
                      f"scale={s:.2f} color=({r:.2f},{g:.2f},{b:.2f})")

            time.sleep(0.016)  # ~60fps
    except KeyboardInterrupt:
        print("\nExiting...")

    app.uninit()
    app.unregister_types()
    return 0


if __name__ == "__main__":
    sys.exit(main())

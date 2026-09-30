# Runs apply_phase1.py inside the editor and always closes the editor afterwards.
# MAXI_UI_SAVE=0 in the environment = dry run: restyle in memory, compile, render, save nothing.
import os, runpy, traceback
import unreal

HERE = os.path.dirname(os.path.abspath(__file__))
try:
    runpy.run_path(os.path.join(HERE, "apply_phase1.py"), run_name="__main__")
except BaseException:
    os.makedirs(os.path.join(HERE, "out_phase1"), exist_ok=True)
    open(os.path.join(HERE, "out_phase1", "launcher_error.txt"), "w", encoding="utf-8").write(traceback.format_exc())
finally:
    unreal.SystemLibrary.quit_editor()

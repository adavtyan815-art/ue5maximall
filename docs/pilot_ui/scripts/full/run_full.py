# Runs full_redesign.py inside the editor and always closes the editor afterwards.
# MAXI_UI_SAVE=0: dry run (nothing saved). MAXI_UI_ONLY=WBP_A,WBP_B: restyle only those blueprints.
import os, runpy, traceback
import unreal

HERE = os.path.dirname(os.path.abspath(__file__))
try:
    runpy.run_path(os.path.join(HERE, "full_redesign.py"), run_name="__main__")
except BaseException:
    os.makedirs(os.path.join(HERE, "out"), exist_ok=True)
    open(os.path.join(HERE, "out", "launcher_error.txt"), "w", encoding="utf-8").write(traceback.format_exc())
finally:
    unreal.SystemLibrary.quit_editor()

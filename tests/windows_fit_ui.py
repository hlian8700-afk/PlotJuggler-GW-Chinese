"""Exercise the installed Windows application through Qt's accessibility bridge."""
import ctypes
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time

from PyQt5.QtCore import QByteArray, QSettings
from pywinauto import Desktop
from pywinauto.keyboard import send_keys


def wait_for(read, seconds=40):
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        result = read()
        if result:
            return result
        time.sleep(0.25)
    raise RuntimeError("Timed out waiting for " + read.__name__)


def run(executable):
    settings = QSettings(QSettings.IniFormat, QSettings.UserScope,
                         "PlotJuggler", "io.plotjuggler.PlotJuggler")
    key = "BladeScope/curveAnalysis"
    profile = {"rated": 1500, "rpm": 500, "cutIn": 0.1, "ratedWind": 500,
               "cutOut": 1200, "diameter": 100, "width": 10}
    for role in range(10):
        profile["role" + str(role)] = "color/default"
    existed, previous = settings.contains(key), settings.value(key)
    settings.setValue(key, QByteArray(json.dumps(profile).encode()))
    settings.sync()
    process = subprocess.Popen([str(executable), "--test"], cwd=executable.parent,
                               env=dict(os.environ, QT_ACCESSIBILITY="1", QT_LINUX_ACCESSIBILITY_ALWAYS_ON="1"))
    desktop = Desktop(backend="uia")

    def windows():
        if process.poll() is not None:
            raise RuntimeError(f'BladeScope exited with Windows status {process.returncode:#x}')
        return desktop.windows(process=process.pid, visible_only=True)

    def find(name, control=None):
        for w in windows():
            for child in w.descendants(control_type=control) if control else w.descendants():
                if child.window_text() == name:
                    return child
        return None

    def click(name):
        element = wait_for(lambda: find(name, "Button"))
        element.click_input()
        print(f'Clicked {name}', flush=True)

    def dialog_open(title):
        return any(w.window_text() == title for w in windows())

    def check_dialog(title):
        click(title)
        wait_for(lambda: dialog_open(title))
        send_keys('{ESC}')
        wait_for(lambda: not dialog_open(title))

    try:
        wait_for(lambda: find("曲线分析", "Button"), 80)
        main_window = next(w for w in windows() if 'BladeScope' in w.window_text())
        ctypes.windll.user32.ShowWindow(main_window.handle, 3)
        click("曲线分析")
        wait_for(lambda: find("分箱统计", "Button"))

        def sample_count():
            return any(w.window_text().startswith("有效 ") for top in windows()
                       for w in top.descendants(control_type="Text"))

        wait_for(sample_count)
        check_dialog('分箱统计')
        check_dialog('拟合信息')
        click("时序图")
        wait_for(lambda: find("曲线分析", "Button"))
        print("Installed GUI opened analysis, computed M01, opened statistics and formula tables, and returned to the plot.")
    finally:
        if process.poll() is None:
            process.terminate()
            process.wait(timeout=10)
        if existed:
            settings.setValue(key, previous)
        else:
            settings.remove(key)
        settings.sync()


if __name__ == "__main__":
    run(Path(sys.argv[1]).resolve())

# -*- coding: utf-8 -*-
"""上位机入口。运行：python -m bindings.pyqt.main"""
import sys

from PyQt5.QtWidgets import QApplication

from .main_window import MainWindow


def main():
    app = QApplication(sys.argv)
    win = MainWindow()
    win.show()
    sys.exit(app.exec_())


if __name__ == "__main__":
    main()

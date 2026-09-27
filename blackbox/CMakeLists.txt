# blackbox — 主板容器仓子项目（合宙 ESP32-C3 板）
# 上游：Mi-Bee-Studio/esp32-blackbox（C3 SuperMini / C6 XIAO 双板工程）
# 本移植差异见 README.md「与上游的差异」。
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Git Bash（MSys）下 idf_tools.py 入口直接拒绝执行（MSYSTEM 由 MSYS 运行时
# 注入原生子进程，unset 无效），跳过 Python 依赖预检（同本仓 wifi-csi-sensing）。
idf_build_set_property(__CHECK_PYTHON 0)

project(blackbox)

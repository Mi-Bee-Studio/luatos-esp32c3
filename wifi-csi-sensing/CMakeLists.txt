cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# Git Bash（MSys）下 idf_tools.py 入口直接拒绝执行（MSYSTEM 注入原生子进程，
# unset 无效），跳过 Python 依赖预检（同 s3zero 项目）。
idf_build_set_property(__CHECK_PYTHON 0)

# -Oz：能力全保留（SAE 必须开——路由是 WPA3-SAE）的极限瘦身，比 -Os 再省一点
idf_build_set_property(COMPILE_OPTIONS "-Oz" APPEND)

project(wifi_csi_sensing)

# main 组件 CMakeLists.txt（最精简版）
#
# 本工程不含 Kconfig.projbuild，menuconfig 里没有蜂鸣器专属菜单；
# 参数在 main.c 顶部的 #define 中直接修改。
#
# 想加回自定义菜单：把 Kconfig.projbuild 放到本目录（main/）下即可，
# IDF 会自动扫描并挂到 menuconfig 顶层，这里不需要加任何参数。
# 【坑】绝不要写 KCONFIG_PROJBUILD "Kconfig.projbuild"（相对路径）：
#       v6.0.2 会按「工程根目录」解析，而文件在 main/ 里，
#       从而报 'Kconfig.projbuild' not found。
#       真要显式声明就用绝对路径：
#         KCONFIG_PROJBUILD "${CMAKE_CURRENT_SOURCE_DIR}/Kconfig.projbuild"
idf_component_register(
    SRCS "main.c" "buzzer.c"
    INCLUDE_DIRS "."
)

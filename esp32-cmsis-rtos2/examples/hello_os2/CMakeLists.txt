cmake_minimum_required(VERSION 3.16)

# 把 components/cmsis_os2 加入 EXTRA_COMPONENT_DIRS，
# 这样 idf.py 才能找到 cmsis_os2 组件。
# 注意：CMakeLists 在 examples/hello_os2/ 下，components 在仓库根，
# 需要往上两级（我此前写成一级，是错的）。
set(EXTRA_COMPONENT_DIRS ${CMAKE_CURRENT_LIST_DIR}/../../components)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(hello_os2)

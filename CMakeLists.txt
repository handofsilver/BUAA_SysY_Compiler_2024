# =============================================================================
# BUAA 编译原理 2026 - 词法分析 / 编译器项目
# 符合 requirement_1_lexer.md 提交要求：CMake 3.28.3 / Clang 12 / C++17
# 可执行文件名必须为 Compiler（评测机要求）
# =============================================================================

# -----------------------------------------------------------------------------
# 1. cmake_minimum_required(VERSION 3.10)
# -----------------------------------------------------------------------------
# 作用：声明本项目需要的最低 CMake 版本。
# 必须放在 CMakeLists.txt 的最前面（除注释外），否则 CMake 会警告。
# 为什么必须：不同版本的 CMake 支持的语法和策略不同；写死最低版本可以避免在
# 老版本上用了新语法导致配置失败。评测机使用 3.28.3，我们写 3.10 保证兼容。
cmake_minimum_required(VERSION 3.10)

# -----------------------------------------------------------------------------
# 2. project(Compiler)
# -----------------------------------------------------------------------------
# 作用：定义项目名称。这里必须写 Compiler，因为评测平台要求「CMake 编译后
# 输出的可执行文件名称为 Compiler」；在 CMake 中，默认的可执行文件名会与
# 项目名一致（见下方 add_executable）。
# 副作用：会设置 CMAKE_PROJECT_NAME、并启用 C/CXX 语言支持，便于后续
# 使用 project() 引入的变量（如 PROJECT_NAME）。
project(Compiler)

# -----------------------------------------------------------------------------
# 3. set(CMAKE_CXX_STANDARD 17)
# -----------------------------------------------------------------------------
# 作用：指定 C++ 标准为 C++17。评测环境为「C++17」，必须一致，否则可能
# 出现标准库或语法差异导致编译/运行错误。
# 可选：set(CMAKE_CXX_STANDARD_REQUIRED ON) 表示“必须支持 C++17，不支持就报错”。
# 可选：set(CMAKE_CXX_EXTENSIONS OFF) 关闭编译器扩展，保证可移植性。
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# -----------------------------------------------------------------------------
# 4. 收集源文件（可选写法：也可在 add_executable 里直接写 src/main.cpp）
# -----------------------------------------------------------------------------
# file(GLOB ...) 会在配置阶段把匹配到的文件列表放进变量 SOURCES。
# 优点：新增 .cpp 不用改 CMakeLists.txt。缺点：新文件不会自动被 CMake 发现，
# 需要重新运行 cmake。若你更喜欢显式列出每个 .cpp，可改为：
#   set(SOURCES src/main.cpp src/lexer.cpp)
# 这里用 GLOB 方便后续在 src 下添加词法分析等模块。
# 当前会包含：main.cpp, Lexer.cpp, TokenType.cpp
file(GLOB SOURCES "src/*.cpp" "src/ir/*.cpp" "src/irgen/*.cpp" "src/pass/*.cpp" "src/mips/*.cpp")
# Driver.cpp is included by src/*.cpp GLOB

# -----------------------------------------------------------------------------
# 5. add_executable(Compiler ${SOURCES})
# -----------------------------------------------------------------------------
# 作用：声明要生成一个名为 Compiler 的可执行文件，由 SOURCES 里的源文件编译得到。
# 名字必须为 Compiler：评测机会直接运行 ./Compiler（或 Compiler.exe），
# 与 requirement_1 中「确保 CMake 编译后输出的可执行文件名称为 Compiler」一致。
# ${SOURCES} 是变量展开，把上面 GLOB 得到的 .cpp 列表展开到这里。
add_executable(Compiler ${SOURCES})

# -----------------------------------------------------------------------------
# 6. target_include_directories(Compiler PRIVATE include)
# -----------------------------------------------------------------------------
# 作用：为「目标」Compiler 添加头文件搜索路径 include/。
# 这样在代码里写 #include "lexer.hpp" 时，编译器会在 include/ 下查找。
#
# 与 include_directories(include) 的区别：
#   - include_directories(include)：全局作用，所有后续 target 都会加上 include，
#     容易污染、不利于多目标项目。
#   - target_include_directories(Compiler PRIVATE include)：只对 Compiler 生效，
#     更清晰，符合现代 CMake 推荐。
# PRIVATE 表示：仅编译 Compiler 时需要，不会传递给链接 Compiler 的其他库。
# 若将来有库被 Compiler 使用且也需要 include，可对库用 PUBLIC/INTERFACE。
target_include_directories(Compiler PRIVATE ${CMAKE_SOURCE_DIR}/include)

# -----------------------------------------------------------------------------
# 可选：若希望生成目录在 build/ 下，在项目根目录执行：
#   mkdir build && cd build && cmake .. && cmake --build .
# 则可执行文件在 build/Compiler（或 build/Compiler.exe）。
# 提交时不要提交 build 目录及其内容（见 requirement：不要提交 CMake 构建过程的临时文件）。
# =============================================================================

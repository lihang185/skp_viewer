SKP Viewer 独立项目

运行：双击 run_viewer.bat，默认打开 test2_box.skp。
打开其他模型：将 .skp 文件拖到 run_viewer.bat，或在窗口中 Ctrl+O。
运行示例：run_viewer.bat "data\dining_table.skp"

首次构建：
1. 安装 Visual Studio 2019，选择“使用 C++ 的桌面开发”，并安装 CMake。
2. 执行 build.bat，生成 x64 Release DLL 和示例程序。
3. 安装 64 位 Python（当前验证版本 3.8），执行 install_dependencies.bat。
4. 执行 run_viewer.bat。
Git仓库不包含编译产物，首次下载后请先执行上述构建步骤。

Python 选择顺序：SKP_VIEWER_PYTHON 环境变量、本目录 .venv、
当前电脑 C:\python\Python38\python.exe、系统 py -3-64。
SKP_VIEWER_PYTHON 应为解释器的完整路径，不含外层引号。

目录：
viewer/       PyQt5 + PyOpenGL 界面、ctypes 读取、贴图解码
src/          C++ SKP 解析、模型、三角化、DLL 接口
include/      公共 C 接口和内部数据结构
third_party/  libtess2 源码及许可证（TESSreal 使用 double）
tools/        read_model.cpp 读取示例、inspect_skp.cpp 检查工具
tests/        C++ 和 Python 回归测试、贴图对照样本
data/         38 个 SKP 测试模型
test2_box.skp 默认示例（与 data 内同名文件不是同一个版本）
build_x64/    本项目生成的 VS2019 解决方案及编译产物
analysis/     自动生成的测试报告及验证截图

DLL：build_x64\Release\slapi.dll
VS2019：打开 build_x64\skp_viewer.sln，选择 Release / x64。
此 DLL 为独立 C++ 实现，无需父目录或原版 slapi.dll。
只迁入实际读取和显示代码；逆向原型、IDA 输出和原 DLL 对比工具留在原目录。
以后在本项目修改和构建，原目录源码作为迁移时的备份保留。

数据流程：SKP -> C++ 归档解析 -> 模型/拓扑/材质 -> ctypes -> OpenGL。
公开兼容接口见 include/slapi_compat.h；项目扩展接口见 include/skp_mesh_api.h。
原程序接口仅实现当前读取需求所需子集，不是完整 SketchUp SDK。

操作：左键旋转，右键/中键或 Shift+左键平移，滚轮缩放，F/双击适配。
Faces 显示面；Textures 显示贴图；Edges 显示边；Triangle edges 显示三角网格。
使用透视投影；浅灰背景；空材质白色；边黑色、1 像素。
Hidden 和 Soft 边不显示，但保留面的拓扑。尚未应用图层可见性。
凸单外环使用 fan 三角化；凹多边形/带孔面使用 libtess2。
支持嵌入 PNG/JPEG/TGA、正反面及继承材质、UVQ、颜色调整与透明度。
损坏 UV 使用平面映射并在状态栏提示。支持已验证的 SKP15/17 文件变体，
不保证任意 SketchUp 版本或未知对象类型可读取。

验证：运行 test.bat，执行 C++ 测试、贴图测试及全部 38 个模型测试。
截图验证：python.bat viewer\viewer.py data\dining_table.skp --screenshot analysis\dining_table.png

# SKP Viewer

[English](README.md) | [简体中文](README.zh-CN.md)

An `.skp` file viewer built by reverse-engineering the SKP file format with [REA](https://github.com/morluto/rea). Thanks to the REA project for its open-source contribution.

This project avoids a dependency on the SketchUp SDK and its licensing requirements by providing an independently written C++ SKP reader.

For full modeling capabilities, check out my other project: [DeepDraw](https://github.com/lihang185/deepdraw).

The viewer uses **PyQt5 and PyOpenGL**. C++ handles model, topology, material, and texture parsing and exposes the results through a DLL interface. Python provides the interactive interface and OpenGL rendering. Neither SketchUp nor the original `slapi.dll` is required at runtime.

> Currently supports tested SKP 15 / 17 file variants and a subset of compatible reading APIs. Other versions are not yet supported. See [Known Limitations](#known-limitations) for details.

## Screenshots

**Materials and components**

![Dining table model with materials, textures, and edges](screenshot2.png)

**Basic geometry and texture mapping**

![Test model with faces and textures](screenshot1.png)

## Features

- Read vertices, edges, faces, outer and inner loops, groups, and component instances.
- Apply component transformations recursively and display models in perspective.
- Triangulate convex polygons with a single outer loop using a triangle fan; use libtess2 for concave polygons or polygons with holes.
- Display front and back materials, inherited group and instance materials, and embedded PNG / JPEG / TGA textures.
- Support UVQ texture mapping, HLS color adjustments, image alpha, and material opacity.
- Hide Hidden and Soft edges while retaining them in face topology.
- Toggle faces, textures, original edges, and triangulation edges independently.
- Use a light gray background, white for faces without a material, and black 1-pixel edges by default.

## Requirements

| Component | Requirement |
| --- | --- |
| Operating system | Windows; the current setup targets x64 |
| C++ toolchain | Visual Studio 2019 with the **Desktop development with C++** workload |
| Build tools | CMake 3.15 or later, available on the command line |
| Python | 64-bit; currently tested with Python 3.8 |
| Graphics | A graphics driver supporting an OpenGL 2.1 compatibility context |
| Python packages | PyQt5 5.15.11, PyOpenGL 3.1.7, Pillow 10.4.0 |

Both Python and the DLL must be **64-bit**. Compiled binaries are not included in the repository; build the DLL before first use.

## Getting Started

Run the following commands in Windows **Command Prompt (cmd)**.

### 1. Clone the repository

```bat
git clone https://github.com/lihang185/skp_viewer.git
cd skp_viewer
```

### 2. Build the C++ DLL

```bat
build.bat
```

The script uses the VS2019 generator to build the x64 Release configuration. Outputs include:

- `build_x64\Release\slapi.dll`: the independently implemented reader DLL.
- `build_x64\Release\read_model.exe`: a C++ reader example.
- `build_x64\skp_viewer.sln`: the generated Visual Studio 2019 solution.

### 3. Install Python dependencies

Creating a project virtual environment is recommended:

```bat
py -3.8-64 -m venv .venv
install_dependencies.bat
```

The launch scripts select Python in the following order:

1. The interpreter specified by the `SKP_VIEWER_PYTHON` environment variable.
2. `.venv\Scripts\python.exe` inside the project.
3. The local fallback path `C:\python\Python38\python.exe`.
4. `py -3-64` through the Python Launcher.

You can also specify your own 64-bit interpreter:

```bat
set "SKP_VIEWER_PYTHON=C:\path\to\python.exe"
install_dependencies.bat
```

### 4. Launch the viewer

```bat
run_viewer.bat
```

By default, the viewer opens `test2_box.skp` in the project root. To open another model:

```bat
run_viewer.bat "data\dining_table.skp"
```

You can also double-click `run_viewer.bat`, drag an `.skp` file onto the script, or use **Open SKP / Ctrl+O** in the viewer.

## Controls

| Input | Action |
| --- | --- |
| Left mouse drag | Rotate the model |
| Right / middle mouse drag, or Shift + left drag | Pan the view |
| Mouse wheel | Zoom |
| F / double-click / Fit view | Fit the model to the window |
| Ctrl+O / Open SKP | Open a model |
| Faces | Show or hide filled faces |
| Textures | Toggle textures |
| Edges | Toggle original CEdge outlines |
| Triangle edges | Inspect the triangulated mesh; disabled by default |

## Project Structure

```text
skp_viewer/
├─ viewer/          Python UI, ctypes reader, and texture decoding
├─ src/             C++ archive parsing, model, triangulation, and DLL APIs
├─ include/         Public interfaces and internal data structures
├─ third_party/     libtess2 source and license
├─ tools/           C++ reader example and model inspection tool
├─ tests/           C++ / Python tests and reference texture fixtures
├─ data/            38 SKP test models
├─ analysis/        Generated test reports and screenshots
├─ test2_box.skp    Default test model
├─ build.bat        Build the x64 DLL
├─ run_viewer.bat   Launch the viewer
└─ test.bat         Run regression tests
```

`build_x64/` is generated during the build and is not tracked by Git. The `test2_box.skp` files in the project root and in `data/` are different versions of the test model.

## C++ API and Data Flow

```text
SKP file → Archive parsing → Model / Topology / Materials → ctypes → OpenGL rendering
```

- [slapi_compat.h](include/slapi_compat.h): public compatibility interfaces for reading models.
- [skp_mesh_api.h](include/skp_mesh_api.h): project extensions for triangle meshes, textures, UVQ, and edge attributes.
- [read_model.cpp](tools/read_model.cpp): a C++ client example.

Project extensions use the `SKP` prefix. Refer to the headers for the exact supported interfaces.

## Testing

After building the project and installing its dependencies, run:

```bat
test.bat
```

Tests cover C++ parsing and geometry APIs, triangulation, texture decoding and color processing, and loading all 38 models in `data/`. The current baseline passes 45 C++ tests and texture-loading checks for all 38 models.

To capture an actual OpenGL rendering:

```bat
python.bat viewer\viewer.py data\dining_table.skp --screenshot analysis\dining_table.png
```

## Known Limitations

- Only tested SKP 15 / 17 file variants are supported. Other versions or unknown object types may fail to load.
- The project focuses on reading and viewing models. It does not provide the complete SketchUp SDK or support saving models.
- Layer visibility is not yet applied. Edge rendering filters out Hidden and Soft edges.
- Invalid or degenerate UV mappings fall back to default planar mapping, with a notice in the status bar.
- Transparent materials use sorted rendering. Complex overlapping geometry may render differently from SketchUp.

## Third-Party Dependencies

Polygon triangulation uses [libtess2](third_party/libtess2), with `TESSreal` changed to `double` in this project. See [UPSTREAM.txt](third_party/libtess2/UPSTREAM.txt) for the source revision and modification, and [LICENSE.txt](third_party/libtess2/LICENSE.txt) for its license.

Python dependency versions are listed in [requirements.txt](viewer/requirements.txt).

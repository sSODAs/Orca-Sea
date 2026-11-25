# Orca Sea

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="https://raw.githubusercontent.com/sSODAs/sSODAs/main/assets/orca-dark.gif">
  <source media="(prefers-color-scheme: light)" srcset="https://raw.githubusercontent.com/sSODAs/sSODAs/main/assets/orca-light.gif">
  <img alt="Orca Sea — Real-time procedural ASCII animation" src="https://raw.githubusercontent.com/sSODAs/sSODAs/main/assets/orca-light.gif" width="100%">
</picture>

**Orca Sea** is a real-time procedural animation project that simulates a pod of swimming orcas using C++ and modern OpenGL, transforming a dynamic 3D scene into ASCII art through custom GLSL shaders.

The project explores procedural geometry, skeletal animation, autonomous movement, and non-photorealistic rendering, combining computer graphics with creative programming to bring a small living ocean into a GitHub profile.

## Overview

Rather than animating predefined sprites or using imported 3D models, Orca Sea generates its geometry, movement, and visual effects procedurally.

**What This Project Does**

- Generates 3D orca models with procedural geometry and surface markings.
- Animates swimming motion using skeletal deformation and GPU skinning.
- Generates continuous swimming paths with randomized movement.
- Simulates a pod of five orcas with different sizes, depths, and swimming speeds.
- Reduces visual overlap through predictive movement coordination.
- Converts the rendered 3D scene into ASCII art using GLSL.
- Generates ambient water particles and wake trails.
- Supports multiple camera views, light and dark themes, and real-time parameter adjustment.

## How It Works

### 1. Procedural Geometry

Each orca is constructed from a series of interpolated cross-sections that define the body, dorsal fin, pectoral fins, and tail flukes.

The characteristic black-and-white markings are generated procedurally through fragment shaders, without requiring external textures or imported models.

### 2. Skeletal Animation

The swimming motion is driven by a nine-joint skeletal system.

Forward kinematics and GPU skinning deform the body, while procedural oscillation generates vertical tail movement. Joint rotation constraints help maintain a natural body shape during turns.

### 3. Autonomous Movement

Each orca follows a continuous, procedurally generated swimming path.

The skeletal system samples the previously travelled path to determine body curvature, allowing the body and tail to follow the direction of movement naturally rather than rotating as a single rigid object.

### 4. Pod Simulation

Five autonomous agents swim within a shared environment.

Each agent has its own size, swimming speed, depth, and opacity. A predictive separation algorithm evaluates potential overlaps along upcoming paths and adjusts swimming speeds to reduce visual interference between agents.

### 5. ASCII Rendering

The animated 3D scene is first rendered into an off-screen framebuffer.

A custom GLSL rendering pass divides the image into cells and evaluates surface coverage, luminance, material information, and depth-related opacity to generate ASCII glyphs.

Procedural water particles, wake trails, and ambient glyphs are combined with the final result to create the appearance of a moving ocean.

## Technologies Used

![C++](https://img.shields.io/badge/C++-00599C?style=flat&logo=c%2B%2B&logoColor=white)
![OpenGL](https://img.shields.io/badge/OpenGL-5586A4?style=flat&logo=opengl&logoColor=white)
![GLSL](https://img.shields.io/badge/GLSL-5586A4?style=flat)
![GLFW](https://img.shields.io/badge/GLFW-333333?style=flat)
![GLEW](https://img.shields.io/badge/GLEW-333333?style=flat)
![GLM](https://img.shields.io/badge/GLM-DA8B25?style=flat)
![CMake](https://img.shields.io/badge/CMake-064F8C?style=flat&logo=cmake&logoColor=white)
![vcpkg](https://img.shields.io/badge/vcpkg-512BD4?style=flat)

## Setup & Installation

**Requirements**

- Windows x64
- Visual Studio 2022 with Desktop Development with C++
- CMake 3.20 or later
- vcpkg
- OpenGL 3.3 compatible GPU

**1. Clone the repository**

```powershell
git clone https://github.com/sSODAs/Orca-Sea.git
cd Orca-Sea
```

**2. Configure vcpkg**

Set the environment variable to your vcpkg installation directory.

```powershell
$env:VCPKG_ROOT = "C:\dev\vcpkg"
```

**3. Configure and build**

```powershell
cmake --preset windows-x64
cmake --build --preset release
```

**4. Run the simulation**

```powershell
.\build\Release\OrcaSea.exe
```

## Controls

| Key | Action |
|---|---|
| 1 | Perspective view |
| 2 | Top-down view |
| 3 | Capture view |
| 4 | Mesh inspection |
| 5 | Side inspection |
| 7 | Switch light / dark theme |
| Tab | Toggle live parameter panel |
| Arrow keys | Select and adjust parameters |
| F | Reset parameters while the panel is open |
| H | Toggle ambient ASCII background |
| J | Toggle water particles |
| K | Toggle predictive separation |
| B | Toggle skeleton visualization |
| P | Toggle swimming path visualization |
| W | Toggle wireframe |
| G | Toggle reference grid |
| M | Toggle autonomous movement |
| T | Toggle rest pose |
| N | Generate a new random seed |
| E | Save current parameter settings |
| Space | Pause / resume |
| R | Reset simulation |
| Esc | Exit |

## Project Structure

```text
Orca-Sea/
├── src/
│   ├── geometry/       # Procedural mesh and materials
│   ├── simulation/     # Skeleton, movement, and pod simulation
│   ├── rendering/      # ASCII rendering and frame capture
│   ├── ui/             # Live parameter panel
│   └── main.cpp        # Application entry point
├── CMakeLists.txt
├── CMakePresets.json
├── vcpkg.json
└── README.md
```

## Limitations & Notes

- Swimming behaviour is kinematic and visually driven rather than a physically accurate hydrodynamic simulation.
- Predictive separation reduces potential visual overlap but does not implement full mesh-based collision detection.
- Orca geometry and movement are stylized rather than anatomically exact.
- The provided build configuration targets Windows.
- Generated animations and local export utilities are not included in this repository.

The animated preview was generated from the procedural simulation and is also displayed on my [GitHub profile](https://github.com/sSODAs).
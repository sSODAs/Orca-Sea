cmake_minimum_required(VERSION 3.20)

project(OrcaSea VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(OpenGL REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(GLEW REQUIRED)
find_package(glm CONFIG REQUIRED)

add_executable(OrcaSea
    src/main.cpp

    src/geometry/OrcaMesh.cpp
    src/geometry/OrcaMesh.h

    src/simulation/SwimRig.h
    src/simulation/SwimPath.h
    src/simulation/PodMotion.h

    src/rendering/AsciiCapture.h

    src/ui/HelpOverlay.h
)

target_include_directories(OrcaSea PRIVATE
    src
    src/geometry
    src/simulation
    src/rendering
    src/ui
)

target_link_libraries(OrcaSea PRIVATE
    OpenGL::GL
    glfw
    GLEW::GLEW
    glm::glm
)

if(WIN32)
    target_link_libraries(OrcaSea PRIVATE
        gdi32
        user32
    )
endif()

if(MSVC)
    target_compile_options(OrcaSea PRIVATE /W4)
endif()

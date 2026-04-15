cmake_minimum_required(VERSION 3.20)                                  # refuse to configure with an ancient CMake; FetchContent features we use need >=3.20
project(ParametricDesignTool VERSION 0.1.0 LANGUAGES CXX)             # declare the project name, version, and that it's a C++ project (enables CXX toolchain detection)

set(CMAKE_CXX_STANDARD 17)                                            # target C++17 -- the minimum the spec calls for
set(CMAKE_CXX_STANDARD_REQUIRED ON)                                   # fail hard if the compiler can't do C++17 (instead of silently downgrading)
set(CMAKE_CXX_EXTENSIONS OFF)                                         # disable GNU extensions so we get -std=c++17 instead of -std=gnu++17 (portability)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)            # only set a default build type for single-config generators (Makefiles, Ninja)...
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)     # ...defaulting to Release so hobby builds get optimization by default
endif()                                                               # end of single-config default guard

include(FetchContent)                                                 # pull in the FetchContent module used to declare third-party deps below

# ---------------------------------------------------------------------------
# GLFW - windowing and OpenGL context
# ---------------------------------------------------------------------------
set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)                      # disable GLFW's documentation build (we don't need it)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)                      # disable GLFW's test suite
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)                      # disable GLFW's example programs
set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)                      # disable GLFW's install rules since we're embedding it, not installing system-wide
FetchContent_Declare(                                                 # declare GLFW as a fetched dependency
    glfw                                                              # local name used to refer to the dep
    GIT_REPOSITORY https://github.com/glfw/glfw.git                   # upstream git URL to clone from
    GIT_TAG        3.4                                                # specific tag so the build is reproducible
)                                                                     # end FetchContent_Declare(glfw)

# ---------------------------------------------------------------------------
# Dear ImGui - docking branch for DockSpace support
# ---------------------------------------------------------------------------
FetchContent_Declare(                                                 # declare ImGui
    imgui                                                             # local name
    GIT_REPOSITORY https://github.com/ocornut/imgui.git               # upstream git URL
    GIT_TAG        docking                                            # use the docking branch; main branch lacks DockSpace API
)                                                                     # end FetchContent_Declare(imgui)

FetchContent_MakeAvailable(glfw imgui)                                # actually clone + add_subdirectory both deps; populates ${glfw_SOURCE_DIR} and ${imgui_SOURCE_DIR}

# ImGui has no CMakeLists of its own -> build a static lib ourselves.
add_library(imgui_lib STATIC                                          # create a static library target named imgui_lib
    ${imgui_SOURCE_DIR}/imgui.cpp                                     # core ImGui source
    ${imgui_SOURCE_DIR}/imgui_demo.cpp                                # the demo window source (useful for reference, small overhead)
    ${imgui_SOURCE_DIR}/imgui_draw.cpp                                # draw-list implementation (AddText, AddLine, AddRect etc.)
    ${imgui_SOURCE_DIR}/imgui_tables.cpp                              # table widget implementation
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp                             # all the standard widgets (buttons, sliders, combos, etc.)
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp                  # GLFW platform backend (input + window events)
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp               # OpenGL 3 renderer backend (draws ImGui via GL)
)                                                                     # end add_library(imgui_lib)
target_include_directories(imgui_lib PUBLIC                           # expose include paths; PUBLIC means anything linking imgui_lib also sees them
    ${imgui_SOURCE_DIR}                                               # root ImGui headers (imgui.h, imgui_internal.h, ...)
    ${imgui_SOURCE_DIR}/backends                                      # backend headers (imgui_impl_glfw.h, imgui_impl_opengl3.h)
)                                                                     # end target_include_directories
target_link_libraries(imgui_lib PUBLIC glfw)                          # imgui_lib needs GLFW symbols because its glfw backend uses them

find_package(OpenGL REQUIRED)                                         # locate the platform's OpenGL library; REQUIRED means fail if not found
target_link_libraries(imgui_lib PUBLIC OpenGL::GL)                    # link against the OpenGL library (provides glClear, glViewport, etc.)

# ---------------------------------------------------------------------------
# Application
# ---------------------------------------------------------------------------
add_executable(${PROJECT_NAME}                                        # create the main executable target; name comes from project() above
    src/main.cpp                                                      # entry point
    src/application.cpp                                               # Application class implementation
    src/canvas.cpp                                                    # Canvas class implementation
)                                                                     # end add_executable
target_include_directories(${PROJECT_NAME} PRIVATE src)               # let sources include headers with "application.h" instead of "src/application.h"
target_link_libraries(${PROJECT_NAME} PRIVATE imgui_lib)              # link our exe against imgui_lib (which transitively pulls in GLFW and OpenGL)

if(MSVC)                                                              # MSVC-specific block: we're being built by Microsoft's compiler
    target_compile_options(${PROJECT_NAME} PRIVATE /W3)               # warning level 3 -- reasonable default, not as noisy as /W4
    # Use the regular console subsystem so printf/stderr are visible during dev
    set_target_properties(${PROJECT_NAME} PROPERTIES                  # set some VS-specific properties on our target
        VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")          # when F5-debugging from VS, run the exe from the project root (so relative paths work)
else()                                                                # non-MSVC (GCC/Clang)
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wpedantic)  # turn on the standard set of useful warnings for GCC/Clang
endif()                                                               # end MSVC-vs-GCC/Clang branch

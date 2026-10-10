cmake_minimum_required(VERSION 3.30...4.4)
project(GMap)
set(CMAKE_CXX_EXTENSIONS OFF)
add_compile_options("-fno-fast-math" "-ffp-contract=off")

add_executable(${PROJECT_NAME}
	"GMap.cpp"
)

target_include_directories(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/libs")  # (for header-only libs)

add_library(GMAP_glad
        "libs/glad/glad.c"
)

set(GWINDOWER_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
add_subdirectory("libs/libGWindower")

set(GRENDERER3D_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
add_subdirectory("libs/libGRenderer3D")

set(GRENDERER2D_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
add_subdirectory("libs/libGRenderer2D")

set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
add_subdirectory("libs/json")

add_library(dear_imgui
        "libs/imgui/imgui.cpp"
        "libs/imgui/imgui_draw.cpp"
        "libs/imgui/imgui_tables.cpp"
        "libs/imgui/imgui_widgets.cpp"
        "libs/imgui/imgui_demo.cpp"
        "libs/imgui/backends/imgui_impl_glfw.cpp"
        "libs/imgui/backends/imgui_impl_opengl3.cpp"
        "libs/imgui/misc/cpp/imgui_stdlib.cpp"
)
target_include_directories(dear_imgui PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/libs/imgui")
target_compile_definitions(dear_imgui PRIVATE "IMGUI_IMPL_GLFW_DISABLE_X11")

set(NFD_PORTAL ON CACHE BOOL "" FORCE)
set(NFD_X11 OFF CACHE BOOL "" FORCE)
add_subdirectory("libs/nativefiledialog-extended")

target_link_libraries(${PROJECT_NAME} PRIVATE
        GMAP_glad
        GWindower
        GRenderer3D
        GRenderer2D
        nlohmann_json
        dear_imgui
        nfd
)
include(FetchContent)

# Pinned tags, not branches: a portable build that silently changes what it
# fetched is not reproducible. imgui and imgui_test_engine must stay on
# matching versions -- the test engine reaches into imgui_internal.h.
set(DAVESHOT_SDL_TAG        "release-3.4.14")
set(DAVESHOT_IMGUI_TAG      "v1.92.9b-docking")
set(DAVESHOT_IMGUI_TE_TAG   "v1.92.9b")
set(DAVESHOT_STB_COMMIT     "2c980bb59875b0d32144a71867fbdebb2f77cd20")

# --- SDL3 ------------------------------------------------------------------
set(SDL_STATIC       ON  CACHE BOOL "" FORCE)
set(SDL_SHARED       OFF CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL      OFF CACHE BOOL "" FORCE)

FetchContent_Declare(SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG        ${DAVESHOT_SDL_TAG}
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE)

# --- Dear ImGui (docking branch) -------------------------------------------
# Ships no CMakeLists, so we compile it ourselves below.
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        ${DAVESHOT_IMGUI_TAG}
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE)

FetchContent_Declare(imgui_test_engine
    GIT_REPOSITORY https://github.com/ocornut/imgui_test_engine.git
    GIT_TAG        ${DAVESHOT_IMGUI_TE_TAG}
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE)

# --- stb (Linux only) ----------------------------------------------------
# PNG and JPEG codecs for the platforms without a system one. Header-only and
# untagged upstream, so pinned to a commit.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    FetchContent_Declare(stb
        GIT_REPOSITORY https://github.com/nothings/stb.git
        GIT_TAG        ${DAVESHOT_STB_COMMIT}
        GIT_PROGRESS   TRUE)
    FetchContent_MakeAvailable(stb)
    add_library(daveshot_stb INTERFACE)
    target_include_directories(daveshot_stb SYSTEM INTERFACE ${stb_SOURCE_DIR})
endif()

FetchContent_MakeAvailable(SDL3 imgui imgui_test_engine)

# --- daveshot_imgui: imgui, the SDL3 backends, and the test engine ---------
#
# The test engine is compiled into the same library rather than sitting beside
# it. IMGUI_ENABLE_TEST_ENGINE turns hook calls on inside imgui.cpp itself, and
# those hooks are defined by the engine -- so anything that links imgui needs
# the engine's objects too, and splitting them into two static libraries only
# produces a dependency cycle. The hooks are inert (a single branch on
# g.TestEngineHookItems) until a test binary actually starts an engine.
set(DAVESHOT_TE_DIR ${imgui_test_engine_SOURCE_DIR}/imgui_test_engine)
add_library(daveshot_imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer3.cpp

    ${DAVESHOT_TE_DIR}/imgui_te_context.cpp
    ${DAVESHOT_TE_DIR}/imgui_te_coroutine.cpp
    ${DAVESHOT_TE_DIR}/imgui_te_engine.cpp
    ${DAVESHOT_TE_DIR}/imgui_te_exporters.cpp
    ${DAVESHOT_TE_DIR}/imgui_te_perftool.cpp
    ${DAVESHOT_TE_DIR}/imgui_te_ui.cpp
    ${DAVESHOT_TE_DIR}/imgui_te_utils.cpp
    ${DAVESHOT_TE_DIR}/imgui_capture_tool.cpp
)
target_include_directories(daveshot_imgui SYSTEM PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    ${imgui_test_engine_SOURCE_DIR})
target_link_libraries(daveshot_imgui PUBLIC SDL3::SDL3-static)

# The test engine hooks itself into imgui through this define, which has to be
# set for every translation unit that includes imgui.h -- application code
# included, since the hooks live in imgui's own headers. Leaving it PUBLIC on
# the imgui target is what keeps the ODR intact between the app and the tests.
target_compile_definitions(daveshot_imgui PUBLIC IMGUI_ENABLE_TEST_ENGINE)

# These are the knobs imgui_te_imconfig.h would set. They appear in the test
# engine's own headers as well as its sources, so both sides of the link have
# to agree on them -- hence PUBLIC rather than PRIVATE.
target_compile_definitions(daveshot_imgui PUBLIC
    IMGUI_TEST_ENGINE_ENABLE_CAPTURE=1
    IMGUI_TEST_ENGINE_ENABLE_IMPLOT=0
    IMGUI_TEST_ENGINE_ENABLE_STD_FUNCTION=0
    # Gives us the std::thread coroutine backend the engine needs; without it
    # every test binary has to supply its own coroutine implementation.
    IMGUI_TEST_ENGINE_ENABLE_COROUTINE_STDTHREAD_IMPL=1)

cmake_minimum_required(VERSION 3.20)
project(spout_stream LANGUAGES CXX)

# Version affichee par --version (la CI de release passe le tag, ex. -DAPP_VERSION=1.2.0)
set(APP_VERSION "dev" CACHE STRING "Version de spout_stream")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Runtime C statique : l'exe ne depend d'aucune DLL VC++
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# SDK Spout2 : dossier local, sinon telechargement de la version figee
set(SPOUT2_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/Spout2" CACHE PATH "Racine du depot Spout2")
if(NOT EXISTS "${SPOUT2_DIR}/SPOUTSDK/SpoutDirectX/SpoutDX/SpoutDX.cpp")
    include(FetchContent)
    FetchContent_Declare(spout2
        URL      https://github.com/leadedge/Spout2/archive/c2bcc12147711d12ace7d5f08e869d774d840f8a.zip
        URL_HASH SHA256=075f7def6686f35f087db6956819092ac5c19b08dfcc6fcc1fe8896e32531ffa
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        SOURCE_SUBDIR _no_cmake_)  # recupere seulement les sources
    FetchContent_MakeAvailable(spout2)
    set(SPOUT2_DIR "${spout2_SOURCE_DIR}")
endif()

set(SPOUT_GL "${SPOUT2_DIR}/SPOUTSDK/SpoutGL")
set(SPOUT_DX "${SPOUT2_DIR}/SPOUTSDK/SpoutDirectX/SpoutDX")

# SpoutDX et ses dependances, compiles en bibliotheque statique
add_library(spoutdx STATIC
    ${SPOUT_DX}/SpoutDX.cpp
    ${SPOUT_GL}/SpoutCopy.cpp
    ${SPOUT_GL}/SpoutDirectX.cpp
    ${SPOUT_GL}/SpoutFrameCount.cpp
    ${SPOUT_GL}/SpoutSenderNames.cpp
    ${SPOUT_GL}/SpoutSharedMemory.cpp
    ${SPOUT_GL}/SpoutUtils.cpp
)
target_include_directories(spoutdx PUBLIC ${SPOUT_DX} ${SPOUT_GL})
target_compile_definitions(spoutdx PUBLIC NOMINMAX _CRT_SECURE_NO_WARNINGS)
target_link_libraries(spoutdx PUBLIC d3d11 dxgi winmm)

add_executable(spout_stream src/main.cpp)
target_compile_definitions(spout_stream PRIVATE UNICODE _UNICODE APP_VERSION="${APP_VERSION}")
target_compile_options(spout_stream PRIVATE /EHsc /permissive- /utf-8 /W4)
target_link_libraries(spout_stream PRIVATE spoutdx dwmapi windowsapp)

# Dossier portable : cmake --install build --prefix install
install(TARGETS spout_stream RUNTIME DESTINATION .)
install(FILES README.md DESTINATION .)
# Licence BSD de Spout2, a redistribuer avec le binaire
install(FILES ${SPOUT2_DIR}/SPOUTSDK/licence.txt DESTINATION . RENAME LICENSE-Spout2.txt)

# Tests : ctest --test-dir build -C Release
include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

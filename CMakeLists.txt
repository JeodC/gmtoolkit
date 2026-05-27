cmake_minimum_required(VERSION 3.18)
project(gmtoolkit LANGUAGES C CXX)

if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 99)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# ----------------------------------------------------------------------
# Dependencies
# ----------------------------------------------------------------------
include(FetchContent)

if(CMAKE_SYSTEM_PROCESSOR MATCHES "(aarch64|arm64|ARM64)")
    set(ASTCENC_ISA_NEON ON CACHE BOOL "" FORCE)
    set(GMTOOLKIT_ASTCENC_LIB astcenc-neon-static)
else()
    set(ASTCENC_ISA_AVX2 ON CACHE BOOL "" FORCE)
    set(GMTOOLKIT_ASTCENC_LIB astcenc-avx2-static)
endif()
set(ASTCENC_CLI OFF CACHE BOOL "" FORCE)
set(ASTCENC_SHAREDLIB OFF CACHE BOOL "" FORCE)
set(ASTCENC_UNITTEST OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    astcenc
    GIT_REPOSITORY https://github.com/ARM-software/astc-encoder.git
    GIT_TAG 4.8.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(astcenc)

FetchContent_Declare(
    bzip2_src
    URL https://sourceware.org/pub/bzip2/bzip2-1.0.8.tar.gz
    URL_HASH SHA256=ab5a03176ee106d3f0fa90e381da478ddae405918153cca248e682cd0c4a2269
    SOURCE_SUBDIR _no_subdir_
)
FetchContent_MakeAvailable(bzip2_src)

# libogg + libvorbis -- linked statically so audio compression works
# without requiring oggenc/oggdec on PATH. Replaces the old shell-out path.
set(BUILD_SHARED_LIBS_PREV ${BUILD_SHARED_LIBS})
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(INSTALL_DOCS OFF CACHE BOOL "" FORCE)
set(INSTALL_PKG_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
set(INSTALL_CMAKE_PACKAGE_MODULE OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    ogg
    GIT_REPOSITORY https://github.com/xiph/ogg.git
    GIT_TAG        v1.3.5
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(ogg)

# Point vorbis's find_package(Ogg) at the fetched libogg before
# configuring vorbis. Without these vars vorbis fails to configure
# because Ogg has no install-tree pkg-config to find.
set(OGG_INCLUDE_DIR "${ogg_SOURCE_DIR}/include" CACHE PATH "" FORCE)
set(OGG_LIBRARY ogg CACHE STRING "" FORCE)

FetchContent_Declare(
    vorbis
    GIT_REPOSITORY https://github.com/xiph/vorbis.git
    GIT_TAG        v1.3.7
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(vorbis)

set(BUILD_SHARED_LIBS ${BUILD_SHARED_LIBS_PREV})

# stb_image.h is single-header and not in a release archive, so we grab the
# one file we need directly from nothings/stb at a pinned commit (avoids
# cloning the whole 5MB stb repo and sidesteps git-protocol limits on
# shallow-cloning by SHA).
set(STB_COMMIT f0569113c93ad095470c54bf34a17b36646bbbb5)
set(STB_DIR "${CMAKE_BINARY_DIR}/_deps/stb")
file(MAKE_DIRECTORY "${STB_DIR}")
foreach(_stb_hdr IN ITEMS stb_image.h stb_image_write.h)
    if(NOT EXISTS "${STB_DIR}/${_stb_hdr}")
        file(DOWNLOAD
            "https://raw.githubusercontent.com/nothings/stb/${STB_COMMIT}/${_stb_hdr}"
            "${STB_DIR}/${_stb_hdr}"
            TLS_VERIFY ON
            STATUS _stb_dl_status)
        list(GET _stb_dl_status 0 _stb_dl_code)
        if(NOT _stb_dl_code EQUAL 0)
            list(GET _stb_dl_status 1 _stb_dl_err)
            message(FATAL_ERROR "Failed to download ${_stb_hdr}: ${_stb_dl_err}")
        endif()
    endif()
endforeach()
set(stb_SOURCE_DIR "${STB_DIR}")

add_library(gmtoolkit_bzip2 STATIC
    ${bzip2_src_SOURCE_DIR}/blocksort.c
    ${bzip2_src_SOURCE_DIR}/bzlib.c
    ${bzip2_src_SOURCE_DIR}/compress.c
    ${bzip2_src_SOURCE_DIR}/crctable.c
    ${bzip2_src_SOURCE_DIR}/decompress.c
    ${bzip2_src_SOURCE_DIR}/huffman.c
    ${bzip2_src_SOURCE_DIR}/randtable.c
)
target_include_directories(gmtoolkit_bzip2 PUBLIC ${bzip2_src_SOURCE_DIR})
if(MSVC)
    target_compile_definitions(gmtoolkit_bzip2 PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(gmtoolkit_bzip2 PRIVATE
        /wd4018 /wd4146 /wd4244 /wd4267 /wd4996)
else()
    target_compile_options(gmtoolkit_bzip2 PRIVATE
        -Wno-implicit-function-declaration
        -Wno-unused-but-set-variable
        -Wno-unused-parameter
        -Wno-unused-variable
        -O2)
endif()

# ----------------------------------------------------------------------
# Main executable
# ----------------------------------------------------------------------
add_executable(gmtoolkit
    src/Toolkit/Main.cpp

    # Low-level codec / format helpers.
    src/Toolkit/Codec/Astc.cpp
    src/Toolkit/Codec/Png.cpp
    src/Toolkit/Codec/Qoif.cpp
    src/Toolkit/Codec/TxtrFormat.cpp

    # High-level chunk-mutation operations.
    src/Toolkit/CompressAudio.cpp
    src/Toolkit/DragDropTee.cpp
    src/Toolkit/ExtractTextures.cpp
    src/Toolkit/PatchShaders.cpp
    src/Toolkit/RepackTextures.cpp
    src/Toolkit/RetargetBytecode.cpp
    src/Toolkit/ToggleFlags.cpp
    src/Toolkit/Verify.cpp

    # CLI entry support (argv-mode dispatchers + JSON config loader).
    src/Toolkit/DebugOps.cpp
    src/Toolkit/Options.cpp

    # Underanalyzer — 1:1 port of github.com/UnderminersTeam/Underanalyzer @ 4be8217.
    # MPL-2.0. Compiler pipeline: Lexer -> Parser -> AST nodes -> BytecodeContext.
    src/Underanalyzer/GMEnum.cpp
    src/Underanalyzer/VMConstants.cpp
    src/Underanalyzer/VMData.cpp
    src/Underanalyzer/Compiler/CompileContext.cpp
    src/Underanalyzer/Compiler/FunctionScope.cpp
    src/Underanalyzer/Compiler/NodeArena.cpp
    src/Underanalyzer/Compiler/Errors/LexerError.cpp
    src/Underanalyzer/Compiler/Errors/ParserError.cpp
    src/Underanalyzer/Compiler/Lexer/ContiguousTextReader.cpp
    src/Underanalyzer/Compiler/Lexer/Identifiers.cpp
    src/Underanalyzer/Compiler/Lexer/LexContext.cpp
    src/Underanalyzer/Compiler/Lexer/Macro.cpp
    src/Underanalyzer/Compiler/Lexer/Numbers.cpp
    src/Underanalyzer/Compiler/Lexer/Strings.cpp
    src/Underanalyzer/Compiler/Lexer/Symbols.cpp
    src/Underanalyzer/Compiler/Lexer/Tags.cpp
    src/Underanalyzer/Compiler/Lexer/Token.cpp
    src/Underanalyzer/Compiler/Lexer/Whitespace.cpp
    src/Underanalyzer/Compiler/Parser/Expressions.cpp
    src/Underanalyzer/Compiler/Parser/Functions.cpp
    src/Underanalyzer/Compiler/Parser/ParseContext.cpp
    src/Underanalyzer/Compiler/Parser/Statements.cpp
    src/Underanalyzer/Compiler/Parser/StaticDeclarations.cpp
    src/Underanalyzer/Compiler/Bytecode/ArrayOwners.cpp
    src/Underanalyzer/Compiler/Bytecode/BranchPatch.cpp
    src/Underanalyzer/Compiler/Bytecode/BytecodeContext.cpp
    src/Underanalyzer/Compiler/Bytecode/ControlFlowContext.cpp
    src/Underanalyzer/Compiler/Bytecode/FunctionEntry.cpp
    src/Underanalyzer/Compiler/Bytecode/InstructionPatch.cpp
    src/Underanalyzer/Compiler/Nodes/AccessorNode.cpp
    src/Underanalyzer/Compiler/Nodes/AssetReferenceNode.cpp
    src/Underanalyzer/Compiler/Nodes/AssignNode.cpp
    src/Underanalyzer/Compiler/Nodes/BinaryChainNode.cpp
    src/Underanalyzer/Compiler/Nodes/BlockNode.cpp
    src/Underanalyzer/Compiler/Nodes/BooleanNode.cpp
    src/Underanalyzer/Compiler/Nodes/BreakNode.cpp
    src/Underanalyzer/Compiler/Nodes/ConditionalNode.cpp
    src/Underanalyzer/Compiler/Nodes/ContinueNode.cpp
    src/Underanalyzer/Compiler/Nodes/DoUntilLoopNode.cpp
    src/Underanalyzer/Compiler/Nodes/DotVariableNode.cpp
    src/Underanalyzer/Compiler/Nodes/EmptyNode.cpp
    src/Underanalyzer/Compiler/Nodes/EnumDeclaration.cpp
    src/Underanalyzer/Compiler/Nodes/ExitNode.cpp
    src/Underanalyzer/Compiler/Nodes/ForLoopNode.cpp
    src/Underanalyzer/Compiler/Nodes/FunctionCallNode.cpp
    src/Underanalyzer/Compiler/Nodes/FunctionDeclNode.cpp
    src/Underanalyzer/Compiler/Nodes/IfNode.cpp
    src/Underanalyzer/Compiler/Nodes/Int64Node.cpp
    src/Underanalyzer/Compiler/Nodes/LocalVarDeclNode.cpp
    src/Underanalyzer/Compiler/Nodes/NewObjectNode.cpp
    src/Underanalyzer/Compiler/Nodes/NullishCoalesceNode.cpp
    src/Underanalyzer/Compiler/Nodes/NumberNode.cpp
    src/Underanalyzer/Compiler/Nodes/PostfixNode.cpp
    src/Underanalyzer/Compiler/Nodes/PrefixNode.cpp
    src/Underanalyzer/Compiler/Nodes/RepeatLoopNode.cpp
    src/Underanalyzer/Compiler/Nodes/ReturnNode.cpp
    src/Underanalyzer/Compiler/Nodes/SimpleFunctionCallNode.cpp
    src/Underanalyzer/Compiler/Nodes/SimpleVariableNode.cpp
    src/Underanalyzer/Compiler/Nodes/StringNode.cpp
    src/Underanalyzer/Compiler/Nodes/SwitchCaseNode.cpp
    src/Underanalyzer/Compiler/Nodes/SwitchNode.cpp
    src/Underanalyzer/Compiler/Nodes/ThrowNode.cpp
    src/Underanalyzer/Compiler/Nodes/TryCatchNode.cpp
    src/Underanalyzer/Compiler/Nodes/UnaryNode.cpp
    src/Underanalyzer/Compiler/Nodes/WhileLoopNode.cpp
    src/Underanalyzer/Compiler/Nodes/WithLoopNode.cpp

    # GMSLib — 1:1 port of github.com/UnderminersTeam/UndertaleModTool @ c4e5c2c3.
    # GPL-3.0. data file container, model classes, compile bridge.
    src/GMSLib/GMSChunks.cpp
    src/GMSLib/GMSData.cpp
    src/GMSLib/GMSGameContext.cpp
    src/GMSLib/GMSIO.cpp
    src/GMSLib/GMSIO_Save.cpp
    src/GMSLib/GlobalFunctions.cpp
    src/GMSLib/Compiler/BuiltinList.cpp
    src/GMSLib/Compiler/Builtins.cpp
    src/GMSLib/Compiler/BytecodeSerializer.cpp
    src/GMSLib/Compiler/CodeBuilder.cpp
    src/GMSLib/Compiler/CompileGroup.cpp
    src/GMSLib/Compiler/CompileResult.cpp

    # SaveBackend
    src/GMSLib/SaveBackend/ChunkShift.cpp
    src/GMSLib/SaveBackend/CodeChunk.cpp
    src/GMSLib/SaveBackend/Pools.cpp
    src/GMSLib/SaveBackend/PoolsCommit.cpp
    src/GMSLib/SaveBackend/ScptGrow.cpp
    src/GMSLib/SaveBackend/VersionDetect.cpp
)
target_link_libraries(gmtoolkit PRIVATE
    ${GMTOOLKIT_ASTCENC_LIB}
    gmtoolkit_bzip2
    vorbisenc
    vorbisfile
    vorbis
    ogg
)
target_include_directories(gmtoolkit PRIVATE include ${stb_SOURCE_DIR})

if(MSVC)
    target_compile_definitions(gmtoolkit PRIVATE
        _CRT_SECURE_NO_WARNINGS
        NOMINMAX)
    target_compile_options(gmtoolkit PRIVATE /W3 /O2)
    target_link_libraries(gmtoolkit PRIVATE psapi)
else()
    target_compile_options(gmtoolkit PRIVATE -O2 -Wall -Wextra)
    find_package(Threads REQUIRED)
    target_link_libraries(gmtoolkit PRIVATE Threads::Threads)
    if(NOT APPLE)
        target_link_options(gmtoolkit PRIVATE
            -static-libstdc++ -static-libgcc)
    endif()
endif()

install(TARGETS gmtoolkit RUNTIME DESTINATION bin)

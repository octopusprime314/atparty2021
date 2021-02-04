cmake_minimum_required(VERSION 3.15)
project(atparty2021)
set(VS_STARTUP_VERSION atparty2021)
set(TLS_VERIFY OFF)

include(InstallRequiredSystemLibraries)
include(FetchContent)


set(CMAKE_INSTALL_PREFIX "${CMAKE_SOURCE_DIR}/atparty2021-dist")

include_directories("${CMAKE_SOURCE_DIR}/libs/dx/include")
include_directories("${CMAKE_SOURCE_DIR}/libs/fmod/inc")
include_directories("${CMAKE_SOURCE_DIR}/libs/gltf/include")
include_directories("${CMAKE_SOURCE_DIR}/libs/json")
include_directories("${CMAKE_SOURCE_DIR}/model/include")
include_directories("${CMAKE_SOURCE_DIR}/glLayer/include")
include_directories("${CMAKE_SOURCE_DIR}/shading/include")
include_directories("${CMAKE_SOURCE_DIR}/audio/include")
include_directories("${CMAKE_SOURCE_DIR}/drawbuffers/include")
include_directories("${CMAKE_SOURCE_DIR}/engine/include")
include_directories("${CMAKE_SOURCE_DIR}/math/include")
include_directories("${CMAKE_SOURCE_DIR}/effects/include")
include_directories("${CMAKE_SOURCE_DIR}/texture/include")
include_directories("${CMAKE_SOURCE_DIR}/io/include")
include_directories("${CMAKE_SOURCE_DIR}/lights/include")
include_directories("${CMAKE_SOURCE_DIR}/view/include")
include_directories("${CMAKE_SOURCE_DIR}/dxLayer/include")
include_directories("${CMAKE_SOURCE_DIR}/imgui")
include_directories("${CMAKE_SOURCE_DIR}/compaction-lib/include")

FILE(GLOB MODEL_HEADER_FILES         ${CMAKE_SOURCE_DIR}/model/include/*.h)
FILE(GLOB SHADING_HEADER_FILES       ${CMAKE_SOURCE_DIR}/shading/include/*.h)
FILE(GLOB AUDIO_HEADER_FILES         ${CMAKE_SOURCE_DIR}/audio/include/*.h)
FILE(GLOB DRAWBUFFERS_HEADER_FILES   ${CMAKE_SOURCE_DIR}/drawbuffers/include/*.h)
FILE(GLOB ENGINE_HEADER_FILES        ${CMAKE_SOURCE_DIR}/engine/include/*.h)
FILE(GLOB MATH_HEADER_FILES          ${CMAKE_SOURCE_DIR}/math/include/*.h)
FILE(GLOB EFFECTS_HEADER_FILES       ${CMAKE_SOURCE_DIR}/effects/include/*.h)
FILE(GLOB TEXTURE_HEADER_FILES       ${CMAKE_SOURCE_DIR}/texture/include/*.h)
FILE(GLOB IO_HEADER_FILES            ${CMAKE_SOURCE_DIR}/io/include/*.h)
FILE(GLOB LIGHTS_HEADER_FILES        ${CMAKE_SOURCE_DIR}/lights/include/*.h)
FILE(GLOB HLSL_VS_SHADER_FILES       ${CMAKE_SOURCE_DIR}/shading/shaders/hlsl/vs/*.hlsl)
FILE(GLOB HLSL_PS_SHADER_FILES       ${CMAKE_SOURCE_DIR}/shading/shaders/hlsl/ps/*.hlsl)
FILE(GLOB HLSL_CS_SHADER_FILES       ${CMAKE_SOURCE_DIR}/shading/shaders/hlsl/cs/*.hlsl)
FILE(GLOB HLSL_CS_DXR1_1SHADER_FILES ${CMAKE_SOURCE_DIR}/shading/shaders/hlsl/cs/dxr1_1/*.hlsl)
FILE(GLOB HLSL_DXR_SHADER_FILES      ${CMAKE_SOURCE_DIR}/shading/shaders/hlsl/dxr/*.hlsl)
FILE(GLOB HLSL_INCLUDE_SHADER_FILES  ${CMAKE_SOURCE_DIR}/shading/shaders/hlsl/include/*.hlsl)
FILE(GLOB VIEW_HEADER_FILES          ${CMAKE_SOURCE_DIR}/view/include/*.h)
FILE(GLOB DX_HEADER_FILES            ${CMAKE_SOURCE_DIR}/dxLayer/include/*.h)
FILE(GLOB IMGUI_HEADER_FILES         ${CMAKE_SOURCE_DIR}/imgui/*)
FILE(GLOB COMPACTION_HEADER_FILES    ${CMAKE_SOURCE_DIR}/compaction-lib/include/*.h)

FILE(GLOB MODEL_SRC_FILES            ${CMAKE_SOURCE_DIR}/model/src/*.cpp)
FILE(GLOB SHADING_SRC_FILES          ${CMAKE_SOURCE_DIR}/shading/src/*.cpp)
FILE(GLOB AUDIO_SRC_FILES            ${CMAKE_SOURCE_DIR}/audio/src/*.cpp)
FILE(GLOB DRAWBUFFERS_SRC_FILES      ${CMAKE_SOURCE_DIR}/drawbuffers/src/*.cpp)
FILE(GLOB ENGINE_SRC_FILES           ${CMAKE_SOURCE_DIR}/engine/src/*.cpp)
FILE(GLOB MATH_SRC_FILES             ${CMAKE_SOURCE_DIR}/math/src/*.cpp)
FILE(GLOB EFFECTS_SRC_FILES          ${CMAKE_SOURCE_DIR}/effects/src/*.cpp)
FILE(GLOB TEXTURE_SRC_FILES          ${CMAKE_SOURCE_DIR}/texture/src/*.cpp)
FILE(GLOB IO_SRC_FILES               ${CMAKE_SOURCE_DIR}/io/src/*.cpp)
FILE(GLOB LIGHTS_SRC_FILES           ${CMAKE_SOURCE_DIR}/lights/src/*.cpp)
FILE(GLOB VIEW_SRC_FILES             ${CMAKE_SOURCE_DIR}/view/src/*.cpp)
FILE(GLOB DX_SRC_FILES               ${CMAKE_SOURCE_DIR}/dxLayer/src/*.cpp)
FILE(GLOB COMPACTION_SRC_FILES       ${CMAKE_SOURCE_DIR}/compaction-lib/src/*.cpp)

source_group("model"                  FILES  ${MODEL_HEADER_FILES} ${MODEL_SRC_FILES})
source_group("shading"                FILES  ${SHADING_HEADER_FILES} ${SHADING_SRC_FILES})
source_group("audio"                  FILES  ${AUDIO_HEADER_FILES} ${AUDIO_SRC_FILES})
source_group("drawbuffers"            FILES  ${DRAWBUFFERS_HEADER_FILES} ${DRAWBUFFERS_SRC_FILES})
source_group("engine"                 FILES  ${ENGINE_HEADER_FILES} ${ENGINE_SRC_FILES})
source_group("math"                   FILES  ${MATH_HEADER_FILES} ${MATH_SRC_FILES})
source_group("effects"                FILES  ${EFFECTS_HEADER_FILES} ${EFFECTS_SRC_FILES})
source_group("texture"                FILES  ${TEXTURE_HEADER_FILES} ${TEXTURE_SRC_FILES})
source_group("io"                     FILES  ${IO_HEADER_FILES} ${IO_SRC_FILES})
source_group("lights"                 FILES  ${LIGHTS_HEADER_FILES} ${LIGHTS_SRC_FILES})
source_group("shaders/hlsl/include"   FILES  ${HLSL_INCLUDE_SHADER_FILES})
source_group("shaders/hlsl/vs"        FILES  ${HLSL_VS_SHADER_FILES})
source_group("shaders/hlsl/ps"        FILES  ${HLSL_PS_SHADER_FILES})
source_group("shaders/hlsl/cs"        FILES  ${HLSL_CS_SHADER_FILES})
source_group("shaders/hlsl/cs/dxr1_1" FILES  ${HLSL_CS_DXR1_1SHADER_FILES})
source_group("shaders/hlsl/dxr"       FILES  ${HLSL_DXR_SHADER_FILES})
source_group("view"                   FILES  ${VIEW_HEADER_FILES} ${VIEW_SRC_FILES})
source_group("dxLayer"                FILES  ${DX_HEADER_FILES} ${DX_SRC_FILES})
source_group("imgui"                  FILES  ${IMGUI_HEADER_FILES})
source_group("compaction-lib"         FILES  ${COMPACTION_HEADER_FILES} ${COMPACTION_SRC_FILES})

add_executable(atparty2021
               ${MODEL_SRC_FILES}
               ${SHADING_SRC_FILES}
               ${AUDIO_SRC_FILES}
               ${MODEL_HEADER_FILES}
               ${SHADING_HEADER_FILES}
               ${AUDIO_HEADER_FILES}
               ${DRAWBUFFERS_HEADER_FILES} 
               ${DRAWBUFFERS_SRC_FILES}
               ${ENGINE_HEADER_FILES}
               ${ENGINE_SRC_FILES}
               ${MATH_HEADER_FILES}
               ${MATH_SRC_FILES}
               ${RENDERER_HEADER_FILES} 
               ${RENDERER_SRC_FILES}
               ${EFFECTS_HEADER_FILES} 
               ${EFFECTS_SRC_FILES}
               ${TEXTURE_HEADER_FILES}
               ${TEXTURE_SRC_FILES}
               ${IO_HEADER_FILES}
               ${IO_SRC_FILES}
               ${LIGHTS_HEADER_FILES}
               ${LIGHTS_SRC_FILES}
               ${HLSL_INCLUDE_SHADER_FILES}
               ${HLSL_VS_SHADER_FILES}
               ${HLSL_PS_SHADER_FILES}
               ${HLSL_CS_SHADER_FILES}
               ${HLSL_CS_DXR1_1SHADER_FILES}
               ${HLSL_DXR_SHADER_FILES}
               ${VIEW_SRC_FILES}
               ${DX_HEADER_FILES}
               ${DX_SRC_FILES}
               ${IMGUI_HEADER_FILES}
               ${COMPACTION_HEADER_FILES}
               ${COMPACTION_SRC_FILES})

target_compile_features(atparty2021 PRIVATE cxx_range_for)

if (MSVC)
    set(VS_STARTUP_Project atparty2021)
    add_definitions(-D_CRT_SECURE_NO_WARNINGS)
    add_definitions("/MP /std:c++17")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /SUBSYSTEM:WINDOWS")
    # Disable individual compilation of hlsl include files
    set_source_files_properties(${HLSL_INCLUDE_SHADER_FILES}   PROPERTIES VS_TOOL_OVERRIDE "None")
    set_source_files_properties(${HLSL_VS_SHADER_FILES}        PROPERTIES VS_SHADER_FLAGS "/Fo \"${CMAKE_SOURCE_DIR}/shading/shaders/hlsl/vs/%(Filename).cso\"         /Fh \"${CMAKE_SOURCE_DIR}/shading/shaders/hlsl/vs/%(Filename)_compiled.h\"         -Od /Zi -WX -Qembed_debug /I ${CMAKE_SOURCE_DIR}/shading/shaders/hlsl/include/" VS_SHADER_TYPE Vertex  VS_SHADER_MODEL 6.2 VS_SHADER_ENTRYPOINT main)
    set_source_files_properties(${HLSL_PS_SHADER_FILES}        PROPERTIES VS_SHADER_FLAGS "/Fo \"${CMAKE_SOURCE_DIR}/shading/shaders/hlsl/ps/%(Filename).cso\"         /Fh \"${CMAKE_SOURCE_DIR}/shading/shaders/hlsl/ps/%(Filename)_compiled.h\"         -Od /Zi -WX -Qembed_debug /I ${CMAKE_SOURCE_DIR}/shading/shaders/hlsl/include/" VS_SHADER_TYPE Pixel   VS_SHADER_MODEL 6.2 VS_SHADER_ENTRYPOINT main)
    set_source_files_properties(${HLSL_CS_SHADER_FILES}        PROPERTIES VS_SHADER_FLAGS "/Fo \"${CMAKE_SOURCE_DIR}/shading/shaders/hlsl/cs/%(Filename).cso\"         /Fh \"${CMAKE_SOURCE_DIR}/shading/shaders/hlsl/cs/%(Filename)_compiled.h\"         -Od /Zi -WX -Qembed_debug /I ${CMAKE_SOURCE_DIR}/shading/shaders/hlsl/include/" VS_SHADER_TYPE Compute VS_SHADER_MODEL 6.2 VS_SHADER_ENTRYPOINT main)
    set_source_files_properties(${HLSL_DXR_SHADER_FILES}       PROPERTIES VS_SHADER_FLAGS "/Fo \"${CMAKE_SOURCE_DIR}/shading/shaders/hlsl/dxr/%(Filename).cso\"        /Fh \"${CMAKE_SOURCE_DIR}/shading/shaders/hlsl/dxr/%(Filename)_compiled.h\"        -Od /Zi -WX -Qembed_debug /I ${CMAKE_SOURCE_DIR}/shading/shaders/hlsl/include/" VS_SHADER_TYPE Library VS_SHADER_MODEL 6.5 VS_SHADER_ENTRYPOINT "")
    set_source_files_properties(${HLSL_CS_DXR1_1SHADER_FILES}  PROPERTIES VS_SHADER_FLAGS "/Fo \"${CMAKE_SOURCE_DIR}/shading/shaders/hlsl/cs/dxr1_1/%(Filename).cso\"  /Fh \"${CMAKE_SOURCE_DIR}/shading/shaders/hlsl/cs/dxr1_1/%(Filename)_compiled.h\"  -Od /Zi -WX -Qembed_debug /I ${CMAKE_SOURCE_DIR}/shading/shaders/hlsl/include/" VS_SHADER_TYPE Compute VS_SHADER_MODEL 6.5 VS_SHADER_ENTRYPOINT main)
endif()

target_link_libraries(atparty2021           legacy_stdio_definitions.lib)
target_link_libraries(atparty2021           ${CMAKE_SOURCE_DIR}/libs/fmod/lib/fmod64_vc.lib)
target_link_libraries(atparty2021 debug     ${CMAKE_SOURCE_DIR}/libs/gltf/lib/debug/GLTFSDK.lib)
target_link_libraries(atparty2021 optimized ${CMAKE_SOURCE_DIR}/libs/gltf/lib/release/GLTFSDK.lib)

#pre fetches assets during cmake -G configuration
FetchContent_Populate(assets
                      URL        https://www.dropbox.com/s/eqms9g5o39c4oxz/assets.zip?dl=0
                      URL_HASH   MD5=6c8c21c8fb8f4924518da1ea50074c4c
                      SOURCE_DIR assets)

#pre fetches lib dependencies during cmake -G configuration
FetchContent_Populate(libs
                      URL        https://www.dropbox.com/s/k6uatfwg2ixlvio/compaction-demo-libs.zip?dl=0
                      URL_HASH   MD5=57bf31cf16d6f54c233f80d599003c19
                      SOURCE_DIR libs)

#Copy over the contents of the unzipped folder
file(COPY ${CMAKE_SOURCE_DIR}/build/assets/ DESTINATION ${CMAKE_SOURCE_DIR}/assets)
#delete the contents after moving
file(REMOVE_RECURSE ${CMAKE_SOURCE_DIR}/build/assets/)

#Copy over the contents of the unzipped folder
file(COPY ${CMAKE_SOURCE_DIR}/build/libs/ DESTINATION ${CMAKE_SOURCE_DIR}/libs)
#delete the contents after moving
file(REMOVE_RECURSE ${CMAKE_SOURCE_DIR}/build/libs/)

#dxil and dxcompiler dlls are needed for dxr
add_custom_command(TARGET atparty2021 POST_BUILD
                   COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/libs/fmod/lib/fmod64.dll   $<TARGET_FILE_DIR:atparty2021>
                   COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/libs/dx/lib/dxcompiler.dll $<TARGET_FILE_DIR:atparty2021>
                   COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/libs/dx/lib/dxil.dll       $<TARGET_FILE_DIR:atparty2021>)

install(TARGETS atparty2021 RUNTIME DESTINATION bin)
install(FILES "${CMAKE_SOURCE_DIR}/libs/dx/lib/dxil.dll"
              "${CMAKE_SOURCE_DIR}/libs/fmod/lib/fmod64.dll"
              "${CMAKE_SOURCE_DIR}/libs/dx/lib/dxcompiler.dll"
        DESTINATION bin)
# FYI: Keep the trailing '/'s here.
install(DIRECTORY "${CMAKE_SOURCE_DIR}/shading/shaders/"
        DESTINATION shading/shaders)
install(DIRECTORY "${CMAKE_SOURCE_DIR}/assets/"
        DESTINATION assets)
install(DIRECTORY "${CMAKE_SOURCE_DIR}/config/"
        DESTINATION config)

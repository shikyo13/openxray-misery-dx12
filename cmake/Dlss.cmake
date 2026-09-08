# Official NVIDIA DLSS 310.7.0 SDK. Download only the SR components, never
# ray reconstruction or NVIDIA frame generation. Every input is hash pinned.
option(XRAY_ENABLE_DLSS "Build native NVIDIA DLSS/DLAA support" ON)
if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8 OR NOT XRAY_ENABLE_DLSS)
    return()
endif()
set(XRAY_DLSS_SDK_ROOT "${CMAKE_BINARY_DIR}/_deps/dlss-sdk-src" CACHE PATH "DLSS SDK cache")
function(xray_dlss_file path hash)
    set(destination "${XRAY_DLSS_SDK_ROOT}/${path}")
    if(EXISTS "${destination}")
        file(SHA256 "${destination}" actual_hash)
        if(actual_hash STREQUAL hash)
            return()
        endif()
    endif()
    get_filename_component(directory "${destination}" DIRECTORY)
    file(MAKE_DIRECTORY "${directory}")
    file(DOWNLOAD "https://raw.githubusercontent.com/NVIDIA/DLSS/a291cc7d2cc642a51566f3dfd5376f635cd1b284/${path}"
        "${destination}" EXPECTED_HASH "SHA256=${hash}" TLS_VERIFY ON STATUS result)
    list(GET result 0 error_code)
    if(NOT error_code EQUAL 0)
        message(FATAL_ERROR "DLSS SDK download failed: ${path}: ${result}")
    endif()
endfunction()
xray_dlss_file(include/nvsdk_ngx.h f6014a256f9d75ccec1278ac6e23d596b398a76cc3960048ca1a274b378b1989)
xray_dlss_file(include/nvsdk_ngx_defs.h ea23f33497cd274860d1c25a97644fce807dcb0037c594547203343103fad03e)
xray_dlss_file(include/nvsdk_ngx_params.h 943bc8cc5cdae03b6303016fbad3183636f2335ae27a2d18776798c3b4efabbc)
xray_dlss_file(include/nvsdk_ngx_helpers.h 2d5661f8b5ab55e1223e485f24146274d48077e09051873826b653d4384fe7d8)
xray_dlss_file(lib/Windows_x86_64/x64/nvsdk_ngx_d.lib 31e82b4ec3242ec6e5b42c73e1f5f5e260338a6ee213bd64444f6c2fa364aa84)
xray_dlss_file(lib/Windows_x86_64/x64/nvsdk_ngx_d_dbg.lib c6b22796b54820eb19e38db740ff339befadac6ef12d53a2502a8c3365558ac1)
xray_dlss_file(lib/Windows_x86_64/rel/nvngx_dlss.dll be6e434a94ca32499515eb62ca0e6c274526055d568d0426e4c652dcdfb6ee6e)
xray_dlss_file(LICENSE.txt 21b5daec892b12bea692e66bc8fe45cf5902ccaf3a7b831e78050d8859881c37)
add_library(xray_ngx STATIC IMPORTED GLOBAL)
set_target_properties(xray_ngx PROPERTIES
    IMPORTED_LOCATION "${XRAY_DLSS_SDK_ROOT}/lib/Windows_x86_64/x64/nvsdk_ngx_d.lib"
    IMPORTED_LOCATION_DEBUG "${XRAY_DLSS_SDK_ROOT}/lib/Windows_x86_64/x64/nvsdk_ngx_d_dbg.lib"
    INTERFACE_INCLUDE_DIRECTORIES "${XRAY_DLSS_SDK_ROOT}/include"
    XRAY_DLSS_RUNTIME "${XRAY_DLSS_SDK_ROOT}/lib/Windows_x86_64/rel/nvngx_dlss.dll"
    XRAY_DLSS_LICENSE "${XRAY_DLSS_SDK_ROOT}/LICENSE.txt")
target_link_libraries(xrRender PRIVATE xray_ngx)
target_compile_definitions(xrRender PRIVATE XRAY_HAVE_DLSS=1)

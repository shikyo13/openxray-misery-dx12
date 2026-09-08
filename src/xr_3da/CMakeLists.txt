add_executable(xr_3da WIN32)

if(TARGET xray_fidelityfx)
    foreach(runtime amd_fidelityfx_loader_dx12.dll amd_fidelityfx_framegeneration_dx12.dll)
        add_custom_command(TARGET xr_3da POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_PROPERTY:xray_fidelityfx,XRAY_FIDELITYFX_BIN>/${runtime}" "$<TARGET_FILE_DIR:xr_3da>/${runtime}"
            VERBATIM)
        install(FILES "$<TARGET_PROPERTY:xray_fidelityfx,XRAY_FIDELITYFX_BIN>/${runtime}"
            DESTINATION "${CMAKE_INSTALL_BINDIR}")
    endforeach()
    add_custom_command(TARGET xr_3da POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_PROPERTY:xray_fidelityfx,XRAY_FIDELITYFX_LICENSE>" "$<TARGET_FILE_DIR:xr_3da>/AMD_FIDELITYFX_LICENSE.md"
        VERBATIM)
    install(FILES "$<TARGET_PROPERTY:xray_fidelityfx,XRAY_FIDELITYFX_LICENSE>"
        DESTINATION "${CMAKE_INSTALL_BINDIR}" RENAME AMD_FIDELITYFX_LICENSE.md)
endif()

if(TARGET xray_ngx)
    add_custom_command(TARGET xr_3da POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_PROPERTY:xray_ngx,XRAY_DLSS_RUNTIME>" "$<TARGET_FILE_DIR:xr_3da>/nvngx_dlss.dll"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_PROPERTY:xray_ngx,XRAY_DLSS_LICENSE>" "$<TARGET_FILE_DIR:xr_3da>/NVIDIA_DLSS_LICENSE.txt"
        VERBATIM)
    install(FILES "$<TARGET_PROPERTY:xray_ngx,XRAY_DLSS_RUNTIME>"
        DESTINATION "${CMAKE_INSTALL_BINDIR}")
    install(FILES "$<TARGET_PROPERTY:xray_ngx,XRAY_DLSS_LICENSE>"
        DESTINATION "${CMAKE_INSTALL_BINDIR}" RENAME NVIDIA_DLSS_LICENSE.txt)
endif()

target_sources(xr_3da PRIVATE
    entry_point.cpp
    memory_capacity_check.cpp
    stdafx.h

    resource.h
    resource.rc

    splash_oxr.bmp
    stalker_cop.ico
    stalker_cs.ico
    stalker_soc.ico
)

set_property(
    DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    PROPERTY
    VS_STARTUP_PROJECT xr_3da
)

target_include_directories(xr_3da
    PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)

target_link_libraries(xr_3da
    PRIVATE
    xrCore
    xrAPI
    xrEngine
    xrGame
    xrRender
)

if (WIN32)
    target_link_options(xr_3da PRIVATE "LINKER:/ERRORLIMIT:0")
endif()

if (MSVC)
    # Release objects contain /Zi information; link it into a usable crash PDB.
    target_link_options(xr_3da PRIVATE "$<$<CONFIG:Release>:/DEBUG:FULL>")
endif()

set_target_properties(xr_3da PROPERTIES
    PREFIX ""
)

install(TARGETS xr_3da RUNTIME
    DESTINATION "${CMAKE_INSTALL_BINDIR}"
)

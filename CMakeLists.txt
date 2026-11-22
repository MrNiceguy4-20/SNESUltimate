cmake_minimum_required(VERSION 3.16)
project(SNESUltimate LANGUAGES C CXX)
if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
option(SNESULTIMATE_ENABLE_IPO "Enable IPO/LTO for optimized builds" ON)
if(SNESULTIMATE_ENABLE_IPO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT SNESULTIMATE_IPO_SUPPORTED OUTPUT SNESULTIMATE_IPO_ERROR)
    if(SNESULTIMATE_IPO_SUPPORTED)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO ON)
    endif()
endif()
file(GLOB SNESULTIMATE_ASMJIT_SOURCES
    third_party/asmjit/asmjit/axl/*.cpp
    third_party/asmjit/asmjit/core/*.cpp
    third_party/asmjit/asmjit/x86/*.cpp)
add_library(snesultimate_asmjit STATIC ${SNESULTIMATE_ASMJIT_SOURCES})
target_include_directories(snesultimate_asmjit PUBLIC third_party/asmjit)
target_compile_definitions(snesultimate_asmjit PUBLIC ASMJIT_STATIC ASMJIT_NO_AARCH64 ASMJIT_NO_FOREIGN)
add_library(snesultimate_core STATIC
    src/core/apu.cpp
    src/core/archive_loader.cpp
    src/core/embedded_firmware.cpp
    src/core/bus.cpp
    src/core/cartridge.cpp
    src/core/cartridge_device.cpp
    src/core/controller.cpp
    src/core/cpu.cpp
    src/core/dma.cpp
    src/core/nec_dsp.cpp
    src/core/arm6.cpp
    src/core/dsp1.cpp
    src/core/dsp2.cpp
    src/core/dsp3.cpp
    src/core/dsp4.cpp
    src/core/sdd1.cpp
    src/core/spc7110.cpp
    src/core/spc7110_decomp.cpp
    src/core/srtc.cpp
    src/core/seta.cpp
    src/core/st018_lle.cpp
    src/core/cx4.cpp
    src/core/hg51b.cpp
    src/core/ppu.cpp
    src/core/scheduler.cpp
    src/core/snes.cpp
    src/core/cheats.cpp
    src/core/superfx.cpp
    src/core/superfx2.cpp
    src/core/sa1.cpp
    third_party/miniz/miniz.c
)
target_include_directories(snesultimate_core PUBLIC include)
target_link_libraries(snesultimate_core PUBLIC snesultimate_asmjit)
add_executable(snesultimate_headless src/headless/main.cpp)
target_link_libraries(snesultimate_headless PRIVATE snesultimate_core)
if (MSVC)
    target_link_options(snesultimate_headless PRIVATE /STACK:8388608)
endif()
option(SNESULTIMATE_BUILD_FRONTEND "Build the SDL3 graphics frontend (Vulkan, with D3D12/OpenGL fallback)" ${WIN32})
set(SNESULTIMATE_SDL3_ROOT "" CACHE PATH "SDL3 VC development package root (include/SDL3 and lib/x64)")
set(SNESULTIMATE_VULKAN_SDK_ROOT "" CACHE PATH "Vulkan SDK root; defaults to VULKAN_SDK or C:/VulkanSDK/1.4.357.0 on Windows")
if(SNESULTIMATE_BUILD_FRONTEND)
    if(NOT WIN32)
        message(FATAL_ERROR "SNESULTIMATE_BUILD_FRONTEND currently supports the Windows frontend only")
    endif()
    if(NOT SNESULTIMATE_VULKAN_SDK_ROOT)
        if(DEFINED ENV{VULKAN_SDK} AND EXISTS "$ENV{VULKAN_SDK}/Include/vulkan/vulkan.h")
            file(TO_CMAKE_PATH "$ENV{VULKAN_SDK}" SNESULTIMATE_VULKAN_SDK_ROOT)
        elseif(EXISTS "C:/VulkanSDK/1.4.357.0/Include/vulkan/vulkan.h")
            set(SNESULTIMATE_VULKAN_SDK_ROOT "C:/VulkanSDK/1.4.357.0")
        endif()
    endif()
    if(NOT SNESULTIMATE_VULKAN_SDK_ROOT OR
       NOT EXISTS "${SNESULTIMATE_VULKAN_SDK_ROOT}/Include/vulkan/vulkan.h" OR
       NOT EXISTS "${SNESULTIMATE_VULKAN_SDK_ROOT}/Lib/vulkan-1.lib")
        message(FATAL_ERROR
            "Vulkan SDK not found. Set VULKAN_SDK or SNESULTIMATE_VULKAN_SDK_ROOT; "
            "the requested fallback is C:/VulkanSDK/1.4.357.0")
    endif()
    if(NOT SNESULTIMATE_SDL3_ROOT AND DEFINED ENV{SDL3_ROOT})
        file(TO_CMAKE_PATH "$ENV{SDL3_ROOT}" SNESULTIMATE_SDL3_ROOT)
    endif()
    find_package(SDL3 CONFIG QUIET)
    if(NOT SDL3_FOUND AND NOT SNESULTIMATE_SDL3_ROOT)
        foreach(_sdl_candidate IN ITEMS
            "${CMAKE_SOURCE_DIR}/SDL3-devel-3.4.20-VC"
            "${CMAKE_SOURCE_DIR}/SDL3-devel-3.4.20-VC/SDL3-3.4.20")
            if(EXISTS "${_sdl_candidate}/include/SDL3/SDL.h")
                set(SNESULTIMATE_SDL3_ROOT "${_sdl_candidate}")
                break()
            endif()
        endforeach()
    endif()
    if(SDL3_FOUND)
        set(SNESULTIMATE_SDL3_TARGET SDL3::SDL3)
    else()
        if(NOT EXISTS "${SNESULTIMATE_SDL3_ROOT}/include/SDL3/SDL.h" OR
           NOT EXISTS "${SNESULTIMATE_SDL3_ROOT}/lib/x64/SDL3.lib")
            message(FATAL_ERROR
                "SDL3 x64 development files not found. Set SNESULTIMATE_SDL3_ROOT to a package "
                "containing include/SDL3/SDL.h and lib/x64/SDL3.lib")
        endif()
        add_library(snesultimate_sdl3 UNKNOWN IMPORTED)
        set_target_properties(snesultimate_sdl3 PROPERTIES
            IMPORTED_LOCATION "${SNESULTIMATE_SDL3_ROOT}/lib/x64/SDL3.lib"
            INTERFACE_INCLUDE_DIRECTORIES "${SNESULTIMATE_SDL3_ROOT}/include")
        set(SNESULTIMATE_SDL3_TARGET snesultimate_sdl3)
    endif()
    set(SNESULTIMATE_IMGUI_ROOT "${CMAKE_SOURCE_DIR}/imgui-1.93.0")
    add_executable(snesultimate
        src/frontend/main.cpp
        src/frontend/frontend_settings.cpp
        src/frontend/video_capture.cpp
        src/frontend/vulkan_renderer.cpp
        src/frontend/embedded_shaders.cpp
        src/frontend/sdl_fallback_renderer.cpp
        ${SNESULTIMATE_IMGUI_ROOT}/imgui.cpp
        ${SNESULTIMATE_IMGUI_ROOT}/imgui_draw.cpp
        ${SNESULTIMATE_IMGUI_ROOT}/imgui_tables.cpp
        ${SNESULTIMATE_IMGUI_ROOT}/imgui_widgets.cpp
        ${SNESULTIMATE_IMGUI_ROOT}/backends/imgui_impl_sdl3.cpp
        ${SNESULTIMATE_IMGUI_ROOT}/backends/imgui_impl_sdlrenderer3.cpp
        ${SNESULTIMATE_IMGUI_ROOT}/backends/imgui_impl_vulkan.cpp
    )
    target_include_directories(snesultimate PRIVATE
        include
        ${SNESULTIMATE_IMGUI_ROOT}
        ${SNESULTIMATE_IMGUI_ROOT}/backends
        "${SNESULTIMATE_VULKAN_SDK_ROOT}/Include")
    target_link_libraries(snesultimate PRIVATE
        snesultimate_core
        ${SNESULTIMATE_SDL3_TARGET}
        "${SNESULTIMATE_VULKAN_SDK_ROOT}/Lib/vulkan-1.lib"
        mfplat mfreadwrite mfuuid ole32)
    if(MSVC)
        target_link_libraries(snesultimate PRIVATE delayimp)
        target_link_options(snesultimate PRIVATE /DELAYLOAD:vulkan-1.dll /STACK:8388608)
    endif()
    if(NOT SDL3_FOUND AND EXISTS "${SNESULTIMATE_SDL3_ROOT}/lib/x64/SDL3.dll")
        add_custom_command(TARGET snesultimate POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${SNESULTIMATE_SDL3_ROOT}/lib/x64/SDL3.dll"
                "$<TARGET_FILE_DIR:snesultimate>/SDL3.dll")
    endif()
endif()
if(MSVC)
    target_compile_options(snesultimate_core PRIVATE /W3 $<$<CONFIG:Release>:/Ob3> $<$<CONFIG:RelWithDebInfo>:/Ob3>)
    target_compile_options(snesultimate_headless PRIVATE /W3 $<$<CONFIG:Release>:/Ob3> $<$<CONFIG:RelWithDebInfo>:/Ob3>)
    if(TARGET snesultimate)
        target_compile_options(snesultimate PRIVATE /W3 $<$<CONFIG:Release>:/Ob3> $<$<CONFIG:RelWithDebInfo>:/Ob3>)
    endif()
else()
    target_compile_options(snesultimate_core PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(snesultimate_headless PRIVATE -Wall -Wextra -Wpedantic)
    if(TARGET snesultimate)
        target_compile_options(snesultimate PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()
enable_testing()
set(SNESULTIMATE_REGRESSION_MANIFEST "" CACHE FILEPATH "Path to a headless external ROM regression manifest")
set(SNESULTIMATE_HARDWARE_SUITE_ROOT "" CACHE PATH "Root containing gilyon/undisbeliever SNES hardware-test assets; discovered automatically")
set(SNESULTIMATE_ENHANCEMENT_ROM_ROOT "" CACHE PATH "Private root containing legally obtained commercial enhancement-chip SNES ROMs")
set(SNESULTIMATE_ENHANCEMENT_GOLDENS "" CACHE FILEPATH "Optional trusted golden hashes/checkpoints keyed by enhancement ROM SHA-256")
set(SNESULTIMATE_TEST_ROM "" CACHE FILEPATH "ROM used by the deterministic headless smoke test")
set(SNESULTIMATE_EXTERNAL_SAMPLE 100 CACHE STRING "Vectors sampled per opcode JSON file for normal external CTest runs; 0 runs the full corpus")
set(SNESULTIMATE_EXTERNAL_REPORT_DIR "" CACHE PATH "Optional directory for machine-readable external oracle reports")
set(SNESULTIMATE_ACCURACY_QUEUE_DIR "" CACHE PATH "Optional directory for merged Phase-1 accuracy defect queue")
function(add_core_regression name)
    add_executable(${name} tests/${name}.cpp)
    target_link_libraries(${name} PRIVATE snesultimate_core)
    if(MSVC)
        target_compile_options(${name} PRIVATE /UNDEBUG)
        target_link_options(${name} PRIVATE /STACK:8388608)
    else()
        target_compile_options(${name} PRIVATE -UNDEBUG)
    endif()
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "SNESULTIMATE_DISABLE_EMBEDDED_FIRMWARE=1")
endfunction()
function(add_private_access_regression name)
    add_executable(${name} tests/${name}.cpp src/core/apu.cpp)
    target_include_directories(${name} PRIVATE include)
    target_link_libraries(${name} PRIVATE snesultimate_core)
    if(MSVC)
        target_compile_definitions(${name} PRIVATE private=public _ALLOW_KEYWORD_MACROS)
        target_compile_options(${name} PRIVATE /UNDEBUG)
        target_link_options(${name} PRIVATE /STACK:8388608)
    else()
        target_compile_definitions(${name} PRIVATE private=public)
        target_compile_options(${name} PRIVATE -UNDEBUG)
    endif()
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "SNESULTIMATE_DISABLE_EMBEDDED_FIRMWARE=1")
endfunction()
add_core_regression(apu_dsp_accuracy)
add_private_access_regression(apu_dsp_brr_end_timing)
add_private_access_regression(apu_dsp_brr_fetch_timing)
add_private_access_regression(apu_dsp_directory_pipeline)
add_private_access_regression(apu_dsp_echo_pipeline_timing)
add_private_access_regression(apu_dsp_endx_v4_timing)
add_private_access_regression(apu_dsp_envelope_latch)
add_core_regression(apu_dsp_key_pipeline)
add_core_regression(apu_dsp_phase_latches)
add_private_access_regression(apu_dsp_pitch_latch_timing)
add_private_access_regression(apu_dsp_pitch_timing)
add_core_regression(apu_dsp_register_arbitration)
add_private_access_regression(apu_dsp_reset_cadence)
add_core_regression(apu_dsp_source_fetch_timing)
add_private_access_regression(apu_dsp_v3c_timing)
add_core_regression(apu_dsp_writeback_timing)
add_core_regression(apu_dspaddr_readonly)
add_core_regression(apu_register_area_echo)
add_core_regression(apu_reset_power_accuracy)
add_core_regression(apu_test_register)
add_core_regression(apu_test_waitstate_accuracy)
add_core_regression(apu_port_write_timing_accuracy)
add_core_regression(apu_port_read_timing_accuracy)
add_core_regression(apu_timer_accuracy)
add_core_regression(base_open_bus_accuracy)
add_core_regression(cartridge_hardware_accuracy)
add_core_regression(cartridge_mapping_accuracy)
add_core_regression(controller_autojoy_hdma_edges)
add_core_regression(controller_serial)
add_core_regression(controller_peripherals)
add_core_regression(cpu_addressing_cycle_order)
add_core_regression(cpu_alu_accuracy)
add_core_regression(cpu_cycle_order)
add_core_regression(cpu_direct_state_path)
add_core_regression(cpu_dp_indirect_y_bank_carry)
add_core_regression(cpu_edge_case_accuracy)
add_core_regression(cpu_dma_interrupt_boundary)
add_core_regression(cpu_irq_timer_register_reschedule)
add_core_regression(cpu_nmi_recognition_accuracy)
add_core_regression(cpu_irq_hdma_phase_accuracy)
add_core_regression(cpu_math_timing)
add_core_regression(cpu_revision_accuracy)
add_core_regression(cpu_memsel_timing)
add_core_regression(cpu_microcode_coverage)
add_core_regression(cx4_accuracy)
add_core_regression(hg51b_accuracy)
add_core_regression(cx4_lle_accuracy)
add_core_regression(cx4_controller_timing)
add_core_regression(cx4_dma_address_space_accuracy)
add_core_regression(dma_wram_port_conflict)
add_core_regression(dma_abus_restrictions)
add_core_regression(dma_sync_end_accuracy)
add_core_regression(dsp1_accuracy)
add_core_regression(dsp1_timing_accuracy)
add_core_regression(dsp1_raster_timing_accuracy)
add_core_regression(dsp1_extended_timing_accuracy)
add_core_regression(dsp2_accuracy)
add_core_regression(dsp3_accuracy)
add_core_regression(dsp4_accuracy)
add_core_regression(nec_dsp_firmware_accuracy)
add_core_regression(ppu_auto_joypad_timing)
add_core_regression(ppu_clip_to_black)
add_core_regression(ppu_dram_refresh)
add_core_regression(ppu_hblank_status_accuracy)
add_core_regression(ppu_hdma_timing)
add_core_regression(ppu_long_dot_timing)
add_core_regression(ppu_counter_field_timing)
add_core_regression(ppu_mode1_row_cache)
add_core_regression(ppu_mode56_hires)
add_core_regression(ppu_mode7_extbg)
add_core_regression(ppu_mode7_multiplier)
add_core_regression(ppu_mode7_scroll_latch)
add_core_regression(ppu_mosaic_mode7)
add_core_regression(ppu_mosaic_vertical)
add_core_regression(ppu_oam_status_accuracy)
add_core_regression(ppu_oam_contention)
add_core_regression(ppu_obj_fetch_pipeline)
add_core_regression(ppu_mode1_priority_order)
add_core_regression(ppu_direct_color_accuracy)
add_core_regression(ppu_obj_interlace)
add_core_regression(ppu_offset_per_tile)
add_core_regression(ppu_overscan)
add_core_regression(ppu_pipeline_cgram_accuracy)
add_core_regression(ppu_pipeline_cgram_halfdot)
add_core_regression(ppu_pipeline_per_dot)
add_core_regression(ppu_port_accuracy)
add_core_regression(ppu_open_bus_domains)
add_core_regression(cpu_open_bus_decay)
add_core_regression(ppu_inidisp_early_read)
add_core_regression(ppu_setini_early_read)
add_core_regression(ppu_vblank_oam_reset_timing)
add_core_regression(power_reset_state)
add_core_regression(ppu_pseudo_hires)
add_core_regression(ppu_scroll_latch)
add_core_regression(ppu_stat78_accuracy)
add_core_regression(ppu_tilemap_screens)
add_core_regression(ppu_vram_remap)
add_core_regression(ppu_window_edges)
add_core_regression(sa1_accuracy)
add_core_regression(sa1_bus_arbitration_accuracy)
add_core_regression(sa1_arbitration_queue_accuracy)
add_core_regression(sa1_control_io_accuracy)
add_core_regression(sdd1_accuracy)
add_core_regression(sdd1_streaming_accuracy)
add_core_regression(seta_accuracy)
add_core_regression(st010_table_accuracy)
add_core_regression(st010_compass_accuracy)
add_core_regression(arm6_accuracy)
add_core_regression(st018_firmware_accuracy)
add_core_regression(spc7110_decomp_reference)
add_core_regression(cheats_database)
add_core_regression(ppu_sprite_limit_option)
add_core_regression(ppu_mode7_presentation)
add_core_regression(ppu_fast_scanline)
add_core_regression(ppu_jit_scanline)
add_executable(asmjit_smoke tests/asmjit_smoke.cpp)
target_link_libraries(asmjit_smoke PRIVATE snesultimate_asmjit)
add_test(NAME asmjit_smoke COMMAND asmjit_smoke)
add_core_regression(spc7110_accuracy)
add_core_regression(spc7110_timing_accuracy)
add_core_regression(spc7110_rtc_interface_accuracy)
add_core_regression(srtc_accuracy)
add_core_regression(superfx_instruction_conformance)
add_core_regression(superfx_timing_accuracy)
add_core_regression(superfx_pixel_transaction_accuracy)
add_core_regression(superfx_wait_resume_accuracy)
add_core_regression(cartridge_rtc_clock_accuracy)
add_core_regression(cartridge_state_accuracy)
add_core_regression(console_region_accuracy)
add_core_regression(whole_system_state_accuracy)
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND AND SNESULTIMATE_TEST_ROM)
    add_test(NAME headless_determinism_smoke
             COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/headless_runner_smoke.py"
                     $<TARGET_FILE:snesultimate_headless> "${SNESULTIMATE_TEST_ROM}")
    set_tests_properties(headless_determinism_smoke PROPERTIES LABELS "external;system;determinism")
endif()
if(Python3_Interpreter_FOUND AND SNESULTIMATE_REGRESSION_MANIFEST)
    set(_rom_external_args
        --runner $<TARGET_FILE:snesultimate_headless>
        --manifest "${SNESULTIMATE_REGRESSION_MANIFEST}")
    if(SNESULTIMATE_EXTERNAL_REPORT_DIR)
        file(MAKE_DIRECTORY "${SNESULTIMATE_EXTERNAL_REPORT_DIR}")
        list(APPEND _rom_external_args --report-json "${SNESULTIMATE_EXTERNAL_REPORT_DIR}/rom-report.json")
    endif()
    add_test(NAME external_rom_regressions
             COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/external_regression.py" ${_rom_external_args})
    set_tests_properties(external_rom_regressions PROPERTIES LABELS "external;system;accuracy")
endif()
if(Python3_Interpreter_FOUND)
    add_test(NAME external_regression_checkpoint_smoke
             COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/external_regression_checkpoint_smoke.py"
                     "${CMAKE_CURRENT_SOURCE_DIR}/tests/external_regression.py")
    set_tests_properties(external_regression_checkpoint_smoke PROPERTIES LABELS "external;accuracy;harness;phase12")
    add_test(NAME enhancement_chip_manifest_smoke
             COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/enhancement_chip_manifest_smoke.py"
                     "${CMAKE_CURRENT_SOURCE_DIR}/tests/prepare_enhancement_chip_manifest.py")
    set_tests_properties(enhancement_chip_manifest_smoke PROPERTIES LABELS "external;cartridge;enhancement;accuracy;harness;phase2")
    if(SNESULTIMATE_ENHANCEMENT_ROM_ROOT)
        set(_enhancement_suite_args
            --root "${SNESULTIMATE_ENHANCEMENT_ROM_ROOT}"
            --runner $<TARGET_FILE:snesultimate_headless>)
        if(SNESULTIMATE_ENHANCEMENT_GOLDENS)
            list(APPEND _enhancement_suite_args --goldens "${SNESULTIMATE_ENHANCEMENT_GOLDENS}")
        endif()
        if(SNESULTIMATE_EXTERNAL_REPORT_DIR)
            file(MAKE_DIRECTORY "${SNESULTIMATE_EXTERNAL_REPORT_DIR}")
            list(APPEND _enhancement_suite_args --report-json "${SNESULTIMATE_EXTERNAL_REPORT_DIR}/enhancement-chip-report.json")
        endif()
        add_test(NAME external_enhancement_chip_roms
                 COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_enhancement_chip_suite.py" ${_enhancement_suite_args})
        set_tests_properties(external_enhancement_chip_roms PROPERTIES LABELS "external;cartridge;enhancement;accuracy;phase2")
    endif()
    if(SNESULTIMATE_HARDWARE_SUITE_ROOT)
        set(_hardware_suite_args
            --root "${SNESULTIMATE_HARDWARE_SUITE_ROOT}"
            --runner $<TARGET_FILE:snesultimate_headless>)
        if(SNESULTIMATE_EXTERNAL_REPORT_DIR)
            file(MAKE_DIRECTORY "${SNESULTIMATE_EXTERNAL_REPORT_DIR}")
            list(APPEND _hardware_suite_args --report-json "${SNESULTIMATE_EXTERNAL_REPORT_DIR}/hardware-suite-report.json")
        endif()
        add_test(NAME external_hardware_suites
                 COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_hardware_suites.py" ${_hardware_suite_args})
        set_tests_properties(external_hardware_suites PROPERTIES LABELS "external;hardware;cpu;ppu;dma;apu;accuracy")
    endif()
    add_test(NAME accuracy_defect_queue_smoke
             COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/accuracy_defect_queue_smoke.py"
                     "${CMAKE_CURRENT_SOURCE_DIR}/tests/accuracy_defect_queue.py")
    set_tests_properties(accuracy_defect_queue_smoke PROPERTIES LABELS "external;accuracy;harness")
    add_test(NAME final_conformance_gate_smoke
             COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/final_conformance_gate_smoke.py"
                     "${CMAKE_CURRENT_SOURCE_DIR}/tests/final_conformance_gate.py")
    set_tests_properties(final_conformance_gate_smoke PROPERTIES LABELS "external;accuracy;harness;phase3")
endif()

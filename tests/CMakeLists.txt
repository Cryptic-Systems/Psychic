# Unit tests and benchmarks for Psychic

add_executable(unit_tests
    unit_tests.cpp
)
target_include_directories(unit_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(unit_tests PRIVATE dsp_engine)
if(NOT MSVC)
    target_compile_options(unit_tests PRIVATE -fexceptions -frtti)
endif()

if(WIN32 AND TARGET dsp_engine)
    add_custom_command(TARGET unit_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:dsp_engine>
            $<TARGET_FILE_DIR:unit_tests>
        COMMENT "Copy dsp_engine.dll next to unit_tests"
    )
endif()

add_test(NAME unit_tests COMMAND unit_tests)

add_executable(adaptive_controller_tests adaptive_controller_tests.cpp)
target_include_directories(adaptive_controller_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
if(NOT MSVC)
    target_compile_options(adaptive_controller_tests PRIVATE -fexceptions -frtti)
endif()
add_test(NAME adaptive_controller_tests COMMAND adaptive_controller_tests)

add_executable(adaptive_signal_scenarios adaptive_signal_scenarios.cpp)
target_include_directories(adaptive_signal_scenarios PRIVATE ${CMAKE_SOURCE_DIR}/src)
if(NOT MSVC)
    target_compile_options(adaptive_signal_scenarios PRIVATE -fexceptions -frtti)
endif()
add_test(NAME adaptive_signal_scenarios COMMAND adaptive_signal_scenarios)

add_executable(track_analysis_tests track_analysis_tests.cpp)
target_include_directories(track_analysis_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
if(NOT MSVC)
    target_compile_options(track_analysis_tests PRIVATE -fexceptions -frtti -fno-fast-math)
endif()
add_test(NAME track_analysis_tests COMMAND track_analysis_tests)
set_tests_properties(unit_tests PROPERTIES TIMEOUT 30)

add_executable(benchmarks benchmarks.cpp)
target_include_directories(benchmarks PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(benchmarks PRIVATE dsp_engine)

if(WIN32 AND TARGET dsp_engine)
    add_custom_command(TARGET benchmarks POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:dsp_engine>
            $<TARGET_FILE_DIR:benchmarks>
        COMMENT "Copy dsp_engine.dll next to benchmarks"
    )
endif()

add_executable(adaptive_audio_evaluation adaptive_audio_evaluation.cpp)
target_include_directories(adaptive_audio_evaluation PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(adaptive_audio_evaluation PRIVATE dsp_engine)

add_executable(track_intelligence_tests track_intelligence_tests.cpp)
target_include_directories(track_intelligence_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
if(NOT MSVC)
    target_compile_options(track_intelligence_tests PRIVATE -fexceptions -frtti -fno-fast-math)
endif()
add_test(NAME track_intelligence_tests COMMAND track_intelligence_tests)

add_executable(track_intelligence_calibration_tests track_intelligence_calibration_tests.cpp)
target_include_directories(track_intelligence_calibration_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
if(NOT MSVC)
    target_compile_options(track_intelligence_calibration_tests PRIVATE -fexceptions -frtti -fno-fast-math)
endif()
add_test(NAME track_intelligence_calibration_tests COMMAND track_intelligence_calibration_tests)

# Host-side incremental track scan lifecycle tests.
add_executable(track_scan_session_tests track_scan_session_tests.cpp)
target_include_directories(track_scan_session_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
if(MSVC)
    target_compile_options(track_scan_session_tests PRIVATE /fp:precise)
else()
    target_compile_options(track_scan_session_tests PRIVATE -fexceptions -frtti -fno-fast-math)
endif()
add_test(NAME track_scan_session_tests COMMAND track_scan_session_tests)

# Host-side bounded analysis cache identity and eviction tests.
add_executable(track_analysis_cache_tests track_analysis_cache_tests.cpp)
target_include_directories(track_analysis_cache_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
if(MSVC)
    target_compile_options(track_analysis_cache_tests PRIVATE /fp:precise)
else()
    target_compile_options(track_analysis_cache_tests PRIVATE -fexceptions -frtti -fno-fast-math)
endif()
add_test(NAME track_analysis_cache_tests COMMAND track_analysis_cache_tests)

# Completed whole-track report to existing playback-hint adapter.
add_executable(track_playback_hint_tests track_playback_hint_tests.cpp)
target_include_directories(track_playback_hint_tests PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(track_playback_hint_tests PRIVATE dsp_engine)
if(MSVC)
    target_compile_options(track_playback_hint_tests PRIVATE /fp:precise)
else()
    target_compile_options(track_playback_hint_tests PRIVATE -fexceptions -frtti -fno-fast-math)
endif()
if(WIN32 AND TARGET dsp_engine)
    add_custom_command(TARGET track_playback_hint_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:dsp_engine>
            $<TARGET_FILE_DIR:track_playback_hint_tests>
        COMMENT "Copy dsp_engine.dll next to track_playback_hint_tests"
    )
endif()
add_test(NAME track_playback_hint_tests COMMAND track_playback_hint_tests)

# Manual offline player: scans a WAV, applies selected hints, processes audio,
# and writes a float32 output WAV plus per-block CSV. Not an automated test.
add_executable(psychic_playback_simulator psychic_playback_simulator.cpp)
target_include_directories(psychic_playback_simulator PRIVATE
    ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(psychic_playback_simulator PRIVATE dsp_engine)
if(MSVC)
    target_compile_options(psychic_playback_simulator PRIVATE /fp:precise)
else()
    target_compile_options(psychic_playback_simulator PRIVATE -fexceptions -frtti -fno-fast-math)
endif()
if(WIN32 AND TARGET dsp_engine)
    add_custom_command(TARGET psychic_playback_simulator POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:dsp_engine>
            $<TARGET_FILE_DIR:psychic_playback_simulator>
        COMMENT "Copy dsp_engine.dll next to psychic_playback_simulator"
    )
endif()


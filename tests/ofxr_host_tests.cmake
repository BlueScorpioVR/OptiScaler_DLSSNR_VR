# The three retired pre-Wilsjo host harnesses referenced absent headers and
# have no implementation in this source line. NR contract/DRED tests remain.

add_executable(xrfg_embedded_menu_tests "${CMAKE_CURRENT_LIST_DIR}/../third_party/ofxr/tests/layer_manual_disarm_tests.cpp")
target_compile_features(xrfg_embedded_menu_tests PRIVATE cxx_std_20)
target_include_directories(xrfg_embedded_menu_tests PRIVATE "${XRFG_OPENXR_SDK_ROOT}/include")
target_compile_definitions(xrfg_embedded_menu_tests PRIVATE XRFG_EMBEDDED_MENU_TEST NOMINMAX WIN32_LEAN_AND_MEAN
    XR_USE_GRAPHICS_API_D3D11 XR_USE_GRAPHICS_API_D3D12 XR_USE_PLATFORM_WIN32)
target_link_libraries(xrfg_embedded_menu_tests PRIVATE xrfg_implicit_layer d3d11 d3d12 dxgi)
foreach(mode inline flight steamvr d3d11)
    add_test(NAME xrfg_embedded_menu_${mode} COMMAND xrfg_embedded_menu_tests "${XRFG_TEST_LAYER_DLL}" ${mode})
endforeach()

add_executable(xrfg_embedded_reconfigure_gpu_tests "${CMAKE_CURRENT_LIST_DIR}/../third_party/ofxr/tests/embedded_reconfigure_gpu_tests.cpp")
target_include_directories(xrfg_embedded_reconfigure_gpu_tests PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated-shaders")
target_link_libraries(xrfg_embedded_reconfigure_gpu_tests PRIVATE xrfg_d3d12_history xrfg_bridge_flight_logger)
target_compile_definitions(xrfg_embedded_reconfigure_gpu_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
# Deliberately manual: requires an NVIDIA OFA-capable GPU. Not portable WARP CI.

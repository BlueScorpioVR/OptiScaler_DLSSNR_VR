$ErrorActionPreference = 'Stop'
$cmake = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$sdk = Join-Path $PSScriptRoot 'external/FidelityFX-SDK/sdk'
$ffxBin = Join-Path $sdk 'bin/ffx_sdk'
$ffxBackend = Join-Path $ffxBin 'ofxr_ffx_backend_dx12_x64.lib'
$ffxOpticalFlow = Join-Path $ffxBin 'ofxr_ffx_opticalflow_x64.lib'
$openXrSdk = Join-Path $PSScriptRoot 'third_party/ofxr/external/OpenXR-SDK'
$nvidiaOpticalFlowSdk = Join-Path $PSScriptRoot 'third_party/ofxr/external/NVIDIA-Optical-Flow-SDK-5.0.7/Optical_Flow_SDK_5.0.7'
if (-not (Test-Path -LiteralPath $ffxBackend) -or
    -not (Test-Path -LiteralPath $ffxOpticalFlow)) {
    & $cmake --fresh -S $sdk -B "$PSScriptRoot/build-ofxr-ffx" -G 'Visual Studio 17 2022' -A x64 -DFFX_API_BACKEND=DX12_X64 -DFFX_ALL=OFF -DFFX_OF=ON -DFFX_BUILD_AS_DLL=OFF "-DCMAKE_PROJECT_INCLUDE=$PSScriptRoot/third_party/ofxr/ffx_private_build.cmake"
    if ($LASTEXITCODE) { throw 'FidelityFX configuration failed' }
    & $cmake --build "$PSScriptRoot/build-ofxr-ffx" --config Release --parallel 8
    if ($LASTEXITCODE) { throw 'FidelityFX build failed' }
}
& $cmake --fresh -S "$PSScriptRoot/third_party/ofxr" -B "$PSScriptRoot/build-ofxr" -G 'Visual Studio 17 2022' -A x64 -DXRFG_BUILD_STANDALONE=OFF -DXRFG_BUILD_TESTS=OFF "-DXRFG_FIDELITYFX_SDK_ROOT=$sdk" "-DXRFG_OPENXR_SDK_ROOT=$openXrSdk" "-DXRFG_NVIDIA_OPTICAL_FLOW_SDK_ROOT=$nvidiaOpticalFlowSdk" "-DXRFG_TEST_LAYER_DLL=$PSScriptRoot/x64/Release/OptiScaler.dll"
if ($LASTEXITCODE) { throw 'OFXR configuration failed' }
& $cmake --build "$PSScriptRoot/build-ofxr" --config Release --parallel 8
if ($LASTEXITCODE) { throw 'OFXR build failed' }

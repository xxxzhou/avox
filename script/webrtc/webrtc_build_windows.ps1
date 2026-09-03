# WebRTC Windows build script
# Usage: .\webrtc_build_windows.ps1 [debug|release]

param(
    [Parameter(Mandatory=$true)]
    [ValidateSet("debug", "release")]
    [string]$BuildType
)

$WEBRTC_DIR = "D:\Work\webrtc"
$BUILD_DIR = "$WEBRTC_DIR\build\windows\$BuildType"

Write-Host "Starting WebRTC Windows $BuildType build..."
Write-Host "WebRTC directory: $WEBRTC_DIR"
Write-Host "Build directory: $BUILD_DIR"

# Check if src directory exists
if (-not (Test-Path "$WEBRTC_DIR\src")) {
    Write-Host "Error: src directory not found: $WEBRTC_DIR\src"
    exit 1
}

# Set environment variables
$env:DEPOT_TOOLS_WIN_TOOLCHAIN = "0"

$IS_DEBUG = $false
if ($BuildType -eq "debug") {
    $IS_DEBUG = $true    
}
$GN_ARGS = "is_debug=$IS_DEBUG enable_iterator_debugging=$IS_DEBUG use_custom_libcxx=false use_rtti=true rtc_include_tests=false rtc_enable_protobuf=false rtc_build_tools=false rtc_build_examples=true rtc_use_h264=false rtc_use_h265=true proprietary_codecs=true"
$IDE_ARGS = "--ide=vs2022"

Set-Location "$WEBRTC_DIR\src"

# Generate GN configuration
Write-Host "Generating GN configuration..."
$gnResult = gn gen --target=x64 $IDE_ARGS "..\build\windows\$BuildType" --args="$GN_ARGS"

if ($LASTEXITCODE -ne 0) {
    Write-Host "GN configuration generation failed"
    exit 1
}

# Build with Ninja
Write-Host "Starting Ninja build..."
ninja -C "..\build\windows\$BuildType"

if ($LASTEXITCODE -eq 0) {
    Write-Host "WebRTC Windows $BuildType build successful!"
    Write-Host "Build directory: $BUILD_DIR"
} else {
    Write-Host "Build failed, error code: $LASTEXITCODE"
    exit $LASTEXITCODE
}

Write-Host "Build completed!"
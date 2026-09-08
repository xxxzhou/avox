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

if ($LASTEXITCODE -ne 0) {
    Write-Host "Build failed, error code: $LASTEXITCODE"
    exit $LASTEXITCODE
}

# 无符号轻量版: llvm-strip剥CodeView调试信息(带符号版约313MB, 无符号约84MB)
# 坑: llvm-strip/objcopy对boringssl_asm成员(GNU as产出COFF)会剥掉整个符号表而非仅调试信息,
# 导致链接报ChaCha20_ctr32_*/vpaes_*等73个LNK2019; 需剥完后删坏成员并回插原始asm成员
$llvmAr = Get-ChildItem "C:\Program Files\Microsoft Visual Studio\2022\*\VC\Tools\Llvm\x64\bin\llvm-ar.exe" -ErrorAction SilentlyContinue | Select-Object -First 1
$llvmStrip = Get-ChildItem "C:\Program Files\Microsoft Visual Studio\2022\*\VC\Tools\Llvm\x64\bin\llvm-strip.exe" -ErrorAction SilentlyContinue | Select-Object -First 1
if ($llvmStrip -and $llvmAr) {
  $libIn = "$BUILD_DIR\obj\webrtc.lib"
  $libOut = "$BUILD_DIR\obj\webrtc_nosym.lib"
  $tmpDir = Join-Path $env:TEMP "webrtc_nosym_asm"
  # 半成品防护: 任一步失败删掉libOut再退出, 防坏库被FindWebRTC的nosym优先链入
  function Fail-Nosym([string]$msg) {
    Write-Host "Error: $msg"
    if (Test-Path $tmpDir) { Pop-Location -ErrorAction SilentlyContinue; Remove-Item -Recurse -Force $tmpDir }
    if (Test-Path $libOut) { Remove-Item -Force $libOut }
    exit 1
  }
  & $llvmStrip.FullName --strip-debug $libIn -o $libOut
  if ($LASTEXITCODE -ne 0) { Fail-Nosym "llvm-strip failed, error code: $LASTEXITCODE" }
  $asmMembers = @(& $llvmAr.FullName t $libIn | Select-String "boringssl_asm" | ForEach-Object { $_.Line })
  if ($LASTEXITCODE -ne 0) { Fail-Nosym "llvm-ar t failed, error code: $LASTEXITCODE" }
  if ($asmMembers.Count -eq 0) {
    # 无回插对象 = 剥坏的asm符号留在库里必然LNK2019, 宁可不产出nosym
    Fail-Nosym "no boringssl_asm members matched, nosym lib would be broken, skip"
  }
  if (Test-Path $tmpDir) { Remove-Item -Recurse -Force $tmpDir }
  New-Item -ItemType Directory -Path $tmpDir | Out-Null
  Push-Location $tmpDir
  foreach ($m in $asmMembers) {
    & $llvmAr.FullName x $libIn (Split-Path $m -Leaf)
    if ($LASTEXITCODE -ne 0) { Fail-Nosym "llvm-ar x $m failed, error code: $LASTEXITCODE" }
  }
  & $llvmAr.FullName d $libOut $asmMembers
  if ($LASTEXITCODE -ne 0) { Fail-Nosym "llvm-ar d failed, error code: $LASTEXITCODE" }
  & $llvmAr.FullName rc $libOut (Get-ChildItem *.o | ForEach-Object { $_.Name })
  if ($LASTEXITCODE -ne 0) { Fail-Nosym "llvm-ar rc failed, error code: $LASTEXITCODE" }
  & $llvmAr.FullName s $libOut
  if ($LASTEXITCODE -ne 0) { Fail-Nosym "llvm-ar s failed, error code: $LASTEXITCODE" }
  Pop-Location
  Remove-Item -Recurse -Force $tmpDir
  Write-Host "Static library(带符号): $BUILD_DIR\obj\webrtc.lib"
  Write-Host "Static library(无符号): $BUILD_DIR\obj\webrtc_nosym.lib"
  Write-Host "拷贝到SDK依赖目录: cp obj\webrtc*.lib <avc_library>\build\windows\release\"
} else {
  Write-Host "Warning: llvm-strip/llvm-ar not found, skip nosym lib"
  Write-Host "Static library(带符号): $BUILD_DIR\obj\webrtc.lib"
}

Write-Host "Build completed!"
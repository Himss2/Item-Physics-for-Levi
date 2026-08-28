param(
    [string]$Ndk = "",
    [string]$BuildType = "Release"
)
$ErrorActionPreference = "Stop"
$Root = $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($Ndk)) { $Ndk = $env:ANDROID_NDK_HOME }
if ([string]::IsNullOrWhiteSpace($Ndk)) { $Ndk = $env:ANDROID_NDK_ROOT }
if ([string]::IsNullOrWhiteSpace($Ndk) -or -not (Test-Path (Join-Path $Ndk "build\cmake\android.toolchain.cmake"))) {
    throw "Android NDK not found. Pass -Ndk or set ANDROID_NDK_HOME. Recommended: NDK r28c (28.2.13676358)."
}
$Build = Join-Path $Root "build\android-arm64-v8a-$BuildType"
$Dist = Join-Path $Root "dist\arm64-v8a"
$Pkg = Join-Path $Dist "levi-item-physics"
if (Test-Path $Build) { Remove-Item -Recurse -Force $Build }
if (Test-Path $Dist) { Remove-Item -Recurse -Force $Dist }
cmake -S $Root -B $Build -G Ninja `
  "-DCMAKE_TOOLCHAIN_FILE=$(Join-Path $Ndk 'build\cmake\android.toolchain.cmake')" `
  "-DANDROID_ABI=arm64-v8a" `
  "-DANDROID_PLATFORM=android-24" `
  "-DCMAKE_BUILD_TYPE=$BuildType"
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }
cmake --build $Build --target levi_item_physics
if ($LASTEXITCODE -ne 0) { throw "Build failed" }
New-Item -ItemType Directory -Force -Path (Join-Path $Pkg "config") | Out-Null
Copy-Item (Join-Path $Root "manifest.json") (Join-Path $Pkg "manifest.json")
Copy-Item (Join-Path $Root "config\config.json") (Join-Path $Pkg "config\config.json")
Copy-Item (Join-Path $Root "config\config.schema.json") (Join-Path $Pkg "config\config.schema.json")
Copy-Item (Join-Path $Build "out\arm64-v8a\liblevi_item_physics.so") (Join-Path $Pkg "liblevi_item_physics.so")
$TempZip = Join-Path $Dist "levi-item-physics.zip"
$Levipack = Join-Path $Dist "levi-item-physics.levipack"
Compress-Archive -Path (Join-Path $Pkg "*") -DestinationPath $TempZip -Force
Move-Item $TempZip $Levipack -Force
Write-Host "Built: $Levipack"

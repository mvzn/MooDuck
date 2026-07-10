# Installs an already-built MooDuck VST3 to the standard Windows plugin folder.
# CMake's COPY_PLUGIN_AFTER_BUILD already does this as part of `cmake --build`;
# this script is for reinstalling a build without recompiling.
param(
    [string]$Config = "Release",
    [string]$BuildDir = "build-cmake"
)

$Source = Join-Path $BuildDir "MooDuck_artefacts\$Config\VST3\MooDuck.vst3"
if (-not (Test-Path $Source)) {
    Write-Error "$Source not found - build first: cmake --build $BuildDir --config $Config"
    exit 1
}

$Dest = Join-Path $env:COMMONPROGRAMFILES "VST3"
New-Item -ItemType Directory -Force -Path $Dest | Out-Null
Copy-Item -Recurse -Force $Source $Dest

Write-Host "Installed VST3 to $Dest"

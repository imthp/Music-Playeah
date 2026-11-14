# One-time setup: installs vcpkg (the C++ package manager) outside the repo.
# Run from PowerShell:  .\scripts\setup-vcpkg.ps1
# Optional: .\scripts\setup-vcpkg.ps1 -Path "E:\tools\vcpkg"  (then update CMakeUserPresets.json)
param([string]$Path = "D:\dev\vcpkg")

$ErrorActionPreference = "Stop"

if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
    throw "git is not on PATH. Install Git for Windows first."
}

if (-not (Test-Path "$Path\.git")) {
    git clone https://github.com/microsoft/vcpkg.git $Path
} else {
    Write-Host "vcpkg already cloned at $Path"
}

if (-not (Test-Path "$Path\vcpkg.exe")) {
    & "$Path\bootstrap-vcpkg.bat" -disableMetrics
}

if (-not (Test-Path "$Path\scripts\buildsystems\vcpkg.cmake")) {
    throw "vcpkg.cmake not found under $Path. The clone looks incomplete."
}

Write-Host ""
Write-Host "vcpkg is ready at $Path"
Write-Host "Toolchain file: $($Path -replace '\\','/')/scripts/buildsystems/vcpkg.cmake"

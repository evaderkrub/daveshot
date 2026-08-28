<#
.SYNOPSIS
    Configures, builds and tests daveshot with MSVC + Ninja.

.DESCRIPTION
    Ninja does not set up the MSVC environment the way the Visual Studio
    generator does, so cl.exe has to be on PATH before CMake runs. Without
    that, CMake happily finds some other compiler on the machine -- a MinGW
    g++ from an msys2 install, say -- and produces a build that is neither
    /W4-clean nor statically linked against the MSVC runtime. This script
    imports the Visual Studio environment first so that cannot happen.

    Build output goes to C:/buildfiles/daveshot/<preset>, never into the
    source tree.

.EXAMPLE
    ./scripts/build.ps1                  # debug: configure, build, test
    ./scripts/build.ps1 -Preset release
    ./scripts/build.ps1 -SkipTests
#>
[CmdletBinding()]
param(
    [ValidateSet('debug', 'release')]
    [string] $Preset = 'debug',
    [switch] $SkipTests
)

$ErrorActionPreference = 'Stop'

function Import-VisualStudioEnvironment {
    if (Get-Command cl.exe -ErrorAction SilentlyContinue) {
        Write-Host "cl.exe already on PATH; using the current environment."
        return
    }

    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) {
        throw "vswhere.exe not found. Install Visual Studio with the C++ workload."
    }

    $install = & $vswhere -latest -products * `
                          -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
                          -property installationPath
    if (-not $install) {
        throw "No Visual Studio installation with the C++ toolset was found."
    }

    $vcvars = Join-Path $install 'VC\Auxiliary\Build\vcvars64.bat'
    if (-not (Test-Path $vcvars)) {
        throw "vcvars64.bat not found under $install."
    }

    # vcvars64.bat only exports into its own cmd session, so run it there and
    # copy the resulting environment back into this one.
    Write-Host "Importing MSVC environment from $install"
    $output = & cmd.exe /c "`"$vcvars`" >nul 2>&1 && set"
    foreach ($line in $output) {
        if ($line -match '^([^=]+)=(.*)$') {
            Set-Item -Path ("Env:" + $matches[1]) -Value $matches[2]
        }
    }
}

Import-VisualStudioEnvironment

$root = Split-Path -Parent $PSScriptRoot
Push-Location $root
try {
    cmake --preset $Preset
    if ($LASTEXITCODE -ne 0) { throw "configure failed" }

    cmake --build --preset $Preset
    if ($LASTEXITCODE -ne 0) { throw "build failed" }

    if (-not $SkipTests) {
        ctest --preset $Preset
        if ($LASTEXITCODE -ne 0) { throw "tests failed" }
    }

    Write-Host ""
    Write-Host "Staged build: C:/buildfiles/daveshot/$Preset/stage"
}
finally {
    Pop-Location
}

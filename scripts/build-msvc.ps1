param(
    [ValidateSet("App", "SmokeTests")]
    [string]$Target = "App",
    [string]$Configuration = "Release",
    [string]$Platform = "x64"
)

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"

function Find-MSBuild {
    $pathCommand = Get-Command msbuild -ErrorAction SilentlyContinue
    if ($pathCommand -and $pathCommand.Source) {
        return $pathCommand.Source
    }

    if (Test-Path -LiteralPath $vswhere) {
        $found = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -find "MSBuild\**\Bin\MSBuild.exe" | Select-Object -First 1
        if ($found -and (Test-Path -LiteralPath $found)) {
            return $found
        }

        $installPath = & $vswhere -latest -products * -property installationPath
        if ($installPath) {
            $candidate = Join-Path $installPath "MSBuild\Current\Bin\amd64\MSBuild.exe"
            if (Test-Path -LiteralPath $candidate) {
                return $candidate
            }

            $candidate = Join-Path $installPath "MSBuild\Current\Bin\MSBuild.exe"
            if (Test-Path -LiteralPath $candidate) {
                return $candidate
            }
        }
    }

    $fallbacks = @(
        "${env:ProgramFiles}\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\amd64\MSBuild.exe",
        "${env:ProgramFiles}\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe",
        "${env:ProgramFiles}\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\amd64\MSBuild.exe",
        "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\amd64\MSBuild.exe"
    )

    foreach ($candidate in $fallbacks) {
        if ($candidate -and (Test-Path -LiteralPath $candidate)) {
            return $candidate
        }
    }

    return $null
}

function Get-BuildPlan {
    if ($Target -eq "SmokeTests") {
        return [pscustomobject]@{
            Project = Join-Path $root "tests\SmokeTests.vcxproj"
            OutDir = Join-Path $root "bin\$Platform\$Configuration\tests"
            IntDir = Join-Path $root "obj\SmokeTests\$Platform\$Configuration"
            ExpectedOutput = Join-Path $root "bin\$Platform\$Configuration\tests\settings_smoke.exe"
            Label = "Smoke tests"
        }
    }

    return [pscustomobject]@{
        Project = Join-Path $root "src\vrec.vcxproj"
        OutDir = Join-Path $root "bin\$Platform\$Configuration"
        IntDir = Join-Path $root "obj\vrec\$Platform\$Configuration"
        ExpectedOutput = Join-Path $root "bin\$Platform\$Configuration\vrec.exe"
        Label = "VRec"
    }
}

$plan = Get-BuildPlan
if (!(Test-Path -LiteralPath $plan.Project -PathType Leaf)) {
    throw "Project file was not found: $($plan.Project)"
}

$msbuild = Find-MSBuild
if (!$msbuild) {
    throw "MSBuild was not found. Run this script from Developer PowerShell or install Visual Studio / Build Tools."
}

Write-Host "Using MSBuild: $msbuild"
Write-Host "Target: $($plan.Label)"
Write-Host "Configuration: $Configuration"
Write-Host "Platform: $Platform"
Write-Host "Expected output: $($plan.ExpectedOutput)"

$buildStarted = Get-Date
& $msbuild `
    $plan.Project `
    "/m" `
    "/t:Rebuild" `
    "/p:Configuration=$Configuration" `
    "/p:Platform=$Platform" `
    "/p:OutDir=$($plan.OutDir)\" `
    "/p:IntDir=$($plan.IntDir)\"

if ($LASTEXITCODE -ne 0) {
    throw "MSBuild failed with exit code $LASTEXITCODE."
}

if (!(Test-Path -LiteralPath $plan.ExpectedOutput -PathType Leaf)) {
    throw "Build succeeded, but expected output was not found: $($plan.ExpectedOutput)"
}

$outputInfo = Get-Item -LiteralPath $plan.ExpectedOutput
if ($outputInfo.LastWriteTime -lt $buildStarted.AddSeconds(-2)) {
    throw "Build succeeded, but expected output was not updated after build start: $($plan.ExpectedOutput)"
}

Write-Host "Build output:"
Write-Host "  File: $($outputInfo.FullName)"
Write-Host "  LastWriteTime: $($outputInfo.LastWriteTime.ToString('yyyy-MM-dd HH:mm:ss'))"
Write-Host "  Size bytes: $($outputInfo.Length)"

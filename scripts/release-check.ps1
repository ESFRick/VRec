param(
    [string]$Configuration = "Release",
    [string]$Platform = "x64",
    [switch]$SkipKill,
    [switch]$SkipTests,
    [switch]$SkipPackage
)

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$buildScript = Join-Path $PSScriptRoot "build-msvc.ps1"
$appExe = Join-Path $root "bin\$Platform\$Configuration\vrec.exe"
$smokeExe = Join-Path $root "bin\$Platform\$Configuration\tests\settings_smoke.exe"
$distRoot = Join-Path $root "dist"
$distDir = Join-Path $distRoot "VRec"
$archivePath = Join-Path $distRoot "VRec-1.0.0-win-x64.zip"
$assetsDir = Join-Path $root "assets"
$openVrDll = Join-Path $root "third_party\openvr_sdk\bin\win64\openvr_api.dll"
$sourceIcon = Join-Path $assetsDir "app.ico"

function Write-Step {
    param([string]$Text)
    Write-Host ""
    Write-Host "== $Text =="
}

function Assert-FileExists {
    param(
        [string]$Path,
        [string]$Name
    )

    if (!(Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "$Name was not found: $Path"
    }
}

function Assert-DirectoryExists {
    param(
        [string]$Path,
        [string]$Name
    )

    if (!(Test-Path -LiteralPath $Path -PathType Container)) {
        throw "$Name was not found: $Path"
    }
}

function Run-Native {
    param(
        [string]$FilePath,
        [string[]]$Arguments,
        [string]$WorkingDirectory = $root,
        [string]$FailureMessage = "Command failed."
    )

    $oldLocation = Get-Location
    try {
        Set-Location -LiteralPath $WorkingDirectory
        & $FilePath @Arguments
        if ($LASTEXITCODE -ne 0) {
            throw "$FailureMessage Exit code: $LASTEXITCODE"
        }
    }
    finally {
        Set-Location $oldLocation
    }
}

function Stop-VRecIfRunning {
    $processes = @(Get-Process -Name "vrec" -ErrorAction SilentlyContinue)
    if ($processes.Count -eq 0) {
        Write-Host "vrec.exe is not running."
        return
    }

    foreach ($process in $processes) {
        Write-Host "Stopping vrec.exe PID $($process.Id)..."
        Stop-Process -Id $process.Id -Force -ErrorAction Stop
    }

    $deadline = (Get-Date).AddSeconds(5)
    do {
        Start-Sleep -Milliseconds 200
        $remaining = @(Get-Process -Name "vrec" -ErrorAction SilentlyContinue)
    } while ($remaining.Count -gt 0 -and (Get-Date) -lt $deadline)

    if ($remaining.Count -gt 0) {
        throw "vrec.exe is still running. Close it from the tray or Task Manager and run release-check again."
    }
}

function Check-Repository {
    Assert-FileExists (Join-Path $root "README.md") "README"
    Assert-FileExists (Join-Path $root "LICENSE") "Project license"
    Assert-FileExists (Join-Path $root "vrec.sln") "Solution"
    Assert-FileExists (Join-Path $root "src\vrec.vcxproj") "Application project"
    Assert-FileExists (Join-Path $root "tests\SmokeTests.vcxproj") "Smoke tests project"
    Assert-FileExists $buildScript "Build script"

    $requiredLicenses = @(
        (Join-Path $root "third_party\openvr_sdk\LICENSE"),
        (Join-Path $root "third_party\webview2_sdk\LICENSE.txt"),
        (Join-Path $root "third_party\nlohmann_json\LICENSE.MIT")
    )
    foreach ($license in $requiredLicenses) {
        Assert-FileExists $license "Third-party license"
    }

    $authoredFiles = @(
        Get-ChildItem -LiteralPath (Join-Path $root "src") -File | Where-Object { $_.Extension -in @(".cpp", ".h", ".rc", ".vcxproj", ".filters") }
        Get-ChildItem -LiteralPath (Join-Path $root "web") -File
        Get-ChildItem -LiteralPath (Join-Path $root "assets") -File | Where-Object { $_.Extension -in @(".json", ".vrmanifest") }
        Get-Item -LiteralPath (Join-Path $root "README.md")
    )

    $forbiddenReferences = @(
        "VRCapture",
        "VRcapture",
        "VrCapture"
    )
    foreach ($pattern in $forbiddenReferences) {
        $matches = @($authoredFiles | Select-String -SimpleMatch $pattern)
        if ($matches.Count -gt 0) {
            throw "Stale project reference '$pattern' found in $($matches[0].Path):$($matches[0].LineNumber)"
        }
    }

    $runtimeFiles = @(
        Get-ChildItem -LiteralPath (Join-Path $root "src") -File
        Get-ChildItem -LiteralPath (Join-Path $root "web") -File
    )
    foreach ($legacyKey in @("outputFolder", "overlayScale", "overlayOffset", "obsAutoConnect")) {
        $matches = @($runtimeFiles | Select-String -SimpleMatch $legacyKey)
        if ($matches.Count -gt 0) {
            throw "Legacy settings key '$legacyKey' remains in runtime code: $($matches[0].Path):$($matches[0].LineNumber)"
        }
    }

    foreach ($legacyKey in @("autoLaunch")) {
        $patterns = @(
            '"' + $legacyKey + '"',
            "'" + $legacyKey + "'"
        )
        foreach ($pattern in $patterns) {
            $matches = @($runtimeFiles | Select-String -SimpleMatch $pattern)
            if ($matches.Count -gt 0) {
                throw "Legacy settings key '$legacyKey' remains in runtime code: $($matches[0].Path):$($matches[0].LineNumber)"
            }
        }
    }

    $readme = Join-Path $root "README.md"
    $readmeText = Get-Content -LiteralPath $readme -Raw
    $localTargets = @()
    foreach ($match in [regex]::Matches($readmeText, '\[[^\]]+\]\((?!https?://|#)([^)]+)\)')) {
        $localTargets += $match.Groups[1].Value
    }
    foreach ($match in [regex]::Matches($readmeText, '<(?:img|a)[^>]+(?:src|href)="(?!https?://|#)([^"]+)"')) {
        $localTargets += $match.Groups[1].Value
    }
    foreach ($target in $localTargets | Select-Object -Unique) {
        $pathPart = ($target -split "#", 2)[0]
        if ($pathPart -and !(Test-Path -LiteralPath (Join-Path $root $pathPart))) {
            throw "README target does not exist: $target"
        }
    }

    $git = Get-Command git -ErrorAction SilentlyContinue
    if ($git -and (Test-Path -LiteralPath (Join-Path $root ".git") -PathType Container)) {
        $trackedArtifacts = @(
            & $git.Source -C $root ls-files |
                Where-Object {
                    $_ -notlike "third_party/*" -and
                    [IO.Path]::GetExtension($_) -in @(".exe", ".dll", ".obj", ".pdb", ".ilk", ".lib")
                }
        )
        if ($trackedArtifacts.Count -gt 0) {
            throw "Tracked build artifact found: $($trackedArtifacts[0])"
        }
    }

    Write-Host "Repository checks passed."
}

function Package-Dist {
    Assert-FileExists $appExe "Application executable"
    Assert-FileExists $openVrDll "OpenVR runtime DLL"
    Assert-FileExists (Join-Path $assetsDir "app.ico") "App icon"
    Assert-FileExists (Join-Path $assetsDir "app.vrmanifest") "OpenVR manifest"
    Assert-FileExists (Join-Path $assetsDir "actions.json") "OpenVR action manifest"

    $bindingFiles = @(Get-ChildItem -LiteralPath $assetsDir -Filter "bindings_*.json" -File)
    if ($bindingFiles.Count -eq 0) {
        throw "No SteamVR input binding files were found in $assetsDir"
    }

    $releaseDir = Split-Path -Parent $appExe
    $webSrc = Join-Path $releaseDir "web"
    Assert-DirectoryExists $webSrc "Build output web directory"

    $resolvedRoot = (Resolve-Path -LiteralPath $root).Path
    New-Item -ItemType Directory -Force $distRoot | Out-Null
    $resolvedDistRoot = (Resolve-Path -LiteralPath $distRoot).Path
    if ($resolvedDistRoot -notlike "$resolvedRoot*") {
        throw "Refusing to clean dist outside the project root: $resolvedDistRoot"
    }

    if (Test-Path -LiteralPath $distDir) {
        $resolvedDistDir = (Resolve-Path -LiteralPath $distDir).Path
        if ($resolvedDistDir -notlike "$resolvedDistRoot\*" -or (Split-Path -Leaf $resolvedDistDir) -ne "VRec") {
            throw "Refusing to clean unexpected directory: $resolvedDistDir"
        }
        Remove-Item -LiteralPath $resolvedDistDir -Recurse -Force
    }

    New-Item -ItemType Directory -Force $distDir | Out-Null

    Copy-Item -LiteralPath $appExe -Destination $distDir -Force
    Copy-Item -LiteralPath $openVrDll -Destination $distDir -Force
    Copy-Item -LiteralPath $webSrc -Destination $distDir -Recurse -Force
    Copy-Item -LiteralPath (Join-Path $assetsDir "app.ico") -Destination $distDir -Force
    Copy-Item -LiteralPath (Join-Path $assetsDir "app.vrmanifest") -Destination $distDir -Force
    Copy-Item -LiteralPath (Join-Path $assetsDir "actions.json") -Destination $distDir -Force
    foreach ($file in $bindingFiles) {
        Copy-Item -LiteralPath $file.FullName -Destination $distDir -Force
    }

    $distExe = Join-Path $distDir "vrec.exe"
    $sourceHash = (Get-FileHash -LiteralPath $appExe -Algorithm SHA256).Hash
    $distHash = (Get-FileHash -LiteralPath $distExe -Algorithm SHA256).Hash
    if ($sourceHash -ne $distHash) {
        throw "Release exe hash does not match packaged exe hash."
    }

    Write-Host "Packaged release to: $distDir"
    Write-Host "Release exe SHA256: $sourceHash"
}

function Check-DistContents {
    Assert-DirectoryExists $distDir "Dist directory"

    foreach ($fileName in @("vrec.exe", "openvr_api.dll", "app.ico", "app.vrmanifest", "actions.json")) {
        Assert-FileExists (Join-Path $distDir $fileName) "Dist file $fileName"
    }

    foreach ($fileName in @("index.html", "app.js", "styles.css")) {
        Assert-FileExists (Join-Path $distDir "web\$fileName") "Dist web file $fileName"
    }

    $sourceIconHash = (Get-FileHash -LiteralPath $sourceIcon -Algorithm SHA256).Hash
    $distIconHash = (Get-FileHash -LiteralPath (Join-Path $distDir "app.ico") -Algorithm SHA256).Hash
    if ($sourceIconHash -ne $distIconHash) {
        throw "Packaged icon hash does not match assets\app.ico."
    }

    $bindingFiles = @(Get-ChildItem -LiteralPath $distDir -Filter "bindings_*.json" -File)
    if ($bindingFiles.Count -eq 0) {
        throw "No SteamVR binding files were found in dist: $distDir"
    }

    $forbiddenFiles = @(Get-ChildItem -LiteralPath $distDir -Recurse -Force -File | Where-Object {
        $_.Extension -in @(".pdb", ".cpp", ".h", ".hpp", ".vcxproj", ".sln", ".obj", ".lib", ".ilk")
    })
    if ($forbiddenFiles.Count -gt 0) {
        Write-Host "Forbidden files found in dist:"
        foreach ($file in $forbiddenFiles) {
            Write-Host "  $($file.FullName)"
        }
        throw "Dist contains developer/build files."
    }

    $forbiddenDirectories = @(Get-ChildItem -LiteralPath $distDir -Recurse -Force -Directory | Where-Object {
        $_.Name -in @(".git", ".vs", "src", "obj", "build", "tests")
    })
    if ($forbiddenDirectories.Count -gt 0) {
        Write-Host "Forbidden directories found in dist:"
        foreach ($dir in $forbiddenDirectories) {
            Write-Host "  $($dir.FullName)"
        }
        throw "Dist contains developer/build directories."
    }

    Write-Host "Dist files:"
    Get-ChildItem -LiteralPath $distDir -File | Sort-Object Name | ForEach-Object {
        Write-Host ("  {0} ({1} bytes)" -f $_.Name, $_.Length)
    }
}

function Package-Archive {
    if (Test-Path -LiteralPath $archivePath) {
        Remove-Item -LiteralPath $archivePath -Force
    }

    Compress-Archive -Path (Join-Path $distDir "*") -DestinationPath $archivePath -CompressionLevel Optimal
    Assert-FileExists $archivePath "Release archive"

    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $archive = [IO.Compression.ZipFile]::OpenRead($archivePath)
    try {
        $entries = @($archive.Entries | ForEach-Object { $_.FullName.Replace("\", "/") })
        $requiredEntries = @(
            "vrec.exe",
            "openvr_api.dll",
            "app.ico",
            "app.vrmanifest",
            "actions.json",
            "web/index.html",
            "web/app.js",
            "web/styles.css"
        )

        foreach ($entry in $requiredEntries) {
            if ($entry -notin $entries) {
                throw "Release archive is missing: $entry"
            }
        }

        $developerFiles = @($entries | Where-Object {
            [IO.Path]::GetExtension($_) -in @(".pdb", ".cpp", ".h", ".hpp", ".vcxproj", ".sln", ".obj", ".lib", ".ilk")
        })
        if ($developerFiles.Count -gt 0) {
            throw "Release archive contains a developer file: $($developerFiles[0])"
        }
    }
    finally {
        $archive.Dispose()
    }

    $archiveHash = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash
    Write-Host "Release archive: $archivePath"
    Write-Host "Release archive SHA256: $archiveHash"
}

Write-Host "Release check"
Write-Host "  Configuration: $Configuration"
Write-Host "  Platform: $Platform"
Write-Host "  Project root: $root"

if (!$SkipKill) {
    Write-Step "Stop running app"
    Stop-VRecIfRunning
}

Write-Step "Check repository"
Check-Repository

Write-Step "Build application"
& $buildScript -Target App -Configuration $Configuration -Platform $Platform
Assert-FileExists $appExe "Application executable"

if (!$SkipTests) {
    Write-Step "Build smoke tests"
    & $buildScript -Target SmokeTests -Configuration $Configuration -Platform $Platform
    Assert-FileExists $smokeExe "Settings smoke test executable"

    Write-Step "Run smoke tests"
    Run-Native -FilePath $smokeExe -Arguments @() -FailureMessage "Smoke tests failed."
}

if (!$SkipPackage) {
    Write-Step "Package dist"
    Package-Dist

    Write-Step "Check dist contents"
    Check-DistContents

    Write-Step "Package release archive"
    Package-Archive
}

Write-Host ""
Write-Host "Release check passed."
if (!$SkipPackage) {
    Write-Host "Dist path: $distDir"
    Write-Host "Archive path: $archivePath"
}

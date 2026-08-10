# Copyright (C) 2026 Flyandance JZ
# SPDX-License-Identifier: GPL-3.0-or-later

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$ProjectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
Set-Location -LiteralPath $ProjectRoot

$Version = 'V3.2.28'
$BuildDir = Join-Path $ProjectRoot 'build-ucrt64-release'
$DistDir = Join-Path $ProjectRoot "dist\FlyingBytesPro_$Version"
$LogFile = Join-Path $ProjectRoot 'BUILD_LOG.txt'
$ResultFile = Join-Path $ProjectRoot 'BUILD_RESULT.txt'

Remove-Item -LiteralPath $LogFile -Force -ErrorAction SilentlyContinue
Remove-Item -LiteralPath $ResultFile -Force -ErrorAction SilentlyContinue

function Write-Banner([string]$Text) {
    Write-Host ''
    Write-Host '============================================================'
    Write-Host " $Text"
    Write-Host '============================================================'
}

function Find-Msys2Root {
    $Candidates = @()
    if ($env:MSYS2_HOME) { $Candidates += $env:MSYS2_HOME }
    $Candidates += 'C:\msys64', 'D:\msys64', 'E:\msys64'

    foreach ($Candidate in $Candidates) {
        if ($Candidate -and (Test-Path -LiteralPath (Join-Path $Candidate 'ucrt64\bin\g++.exe'))) {
            return [System.IO.Path]::GetFullPath($Candidate)
        }
    }
    throw 'MSYS2 UCRT64 was not found. Expected C:\msys64 or the MSYS2_HOME environment variable.'
}

function Require-File([string]$Path, [string]$Description) {
    if (-not (Test-Path -LiteralPath $Path)) {
        throw "$Description was not found: $Path"
    }
}

function Invoke-NativeStep([string]$Name, [string]$Program, [string[]]$Arguments) {
    Write-Banner $Name
    Write-Host "Program: $Program"
    if ($Arguments.Count -gt 0) {
        Write-Host ('Arguments: ' + ($Arguments -join ' '))
    }
    Write-Host ''

    & $Program @Arguments
    $Code = $LASTEXITCODE
    if ($Code -ne 0) {
        throw "$Name failed with exit code $Code."
    }
}

$TranscriptStarted = $false
$Succeeded = $false

try {
    Start-Transcript -LiteralPath $LogFile -Force | Out-Null
    $TranscriptStarted = $true

    Write-Banner "FlyingBytesPro $Version - Build and Run"
    Write-Host "Project folder:          $ProjectRoot"
    Write-Host "Temporary build files:   $BuildDir"
    Write-Host "Portable application:    $DistDir"
    Write-Host "Complete build log:      $LogFile"
    Write-Host ''
    Write-Host 'This window will remain open after success or failure.'

    $MsysRoot = Find-Msys2Root
    $UcrtBin = Join-Path $MsysRoot 'ucrt64\bin'
    $UsrBin = Join-Path $MsysRoot 'usr\bin'

    $CMake = Join-Path $UcrtBin 'cmake.exe'
    $CTest = Join-Path $UcrtBin 'ctest.exe'
    $Python = Join-Path $UcrtBin 'python.exe'
    $Ninja = Join-Path $UcrtBin 'ninja.exe'
    $Compiler = Join-Path $UcrtBin 'g++.exe'
    $PkgConfig = Join-Path $UcrtBin 'pkg-config.exe'
    $ObjDump = Join-Path $UcrtBin 'objdump.exe'
    $WinDeployQt = Join-Path $UcrtBin 'windeployqt6.exe'
    if (-not (Test-Path -LiteralPath $WinDeployQt)) {
        $WinDeployQt = Join-Path $UcrtBin 'windeployqt.exe'
    }
    $LibUsbDll = Join-Path $UcrtBin 'libusb-1.0.dll'

    Require-File $CMake 'CMake'
    Require-File $CTest 'CTest'
    Require-File $Python 'Python'
    Require-File $Ninja 'Ninja'
    Require-File $Compiler 'G++'
    Require-File $PkgConfig 'pkg-config'
    Require-File $ObjDump 'objdump'
    Require-File $WinDeployQt 'windeployqt'
    Require-File $LibUsbDll 'libusb-1.0.dll'

    $env:PATH = "$UcrtBin;$UsrBin;$env:PATH"
    $env:PKG_CONFIG_PATH = Join-Path $UcrtBin '..\lib\pkgconfig'

    Write-Banner 'Repairing extracted file timestamps'
    $Now = Get-Date
    $SafeTime = $Now.AddMinutes(-2)
    $FutureLimit = $Now.AddSeconds(5)
    $TimestampRoots = @(
        (Join-Path $ProjectRoot 'CMakeLists.txt'),
        (Join-Path $ProjectRoot 'CMakePresets.json'),
        (Join-Path $ProjectRoot 'src'),
        (Join-Path $ProjectRoot 'tests'),
        (Join-Path $ProjectRoot 'resources')
    )
    $RepairedCount = 0
    foreach ($TimestampRoot in $TimestampRoots) {
        if (-not (Test-Path -LiteralPath $TimestampRoot)) { continue }
        $Items = if ((Get-Item -LiteralPath $TimestampRoot).PSIsContainer) {
            Get-ChildItem -LiteralPath $TimestampRoot -File -Recurse
        } else {
            @(Get-Item -LiteralPath $TimestampRoot)
        }
        foreach ($Item in $Items) {
            if ($Item.LastWriteTime -gt $FutureLimit) {
                $Item.LastWriteTime = $SafeTime
                $RepairedCount++
            }
        }
    }
    Write-Host "Future-dated build inputs repaired: $RepairedCount"

    Write-Banner 'Detected build tools'
    Write-Host "MSYS2:     $MsysRoot"
    & $Compiler --version | Select-Object -First 1
    & $CMake --version | Select-Object -First 1
    & $Python --version
    & $PkgConfig --modversion Qt6Core
    if ($LASTEXITCODE -ne 0) { throw 'Qt 6 was not detected by pkg-config.' }
    & $PkgConfig --modversion libusb-1.0
    if ($LASTEXITCODE -ne 0) { throw 'libusb 1.0 was not detected by pkg-config.' }

    Invoke-NativeStep 'Checking Windows-safe filenames' $Python @('scripts\validate_windows_filenames.py')
    Invoke-NativeStep 'Running static project audit' $Python @('scripts\static_project_audit.py')
    Invoke-NativeStep 'Checking the AVR device database' $Python @('scripts\validate_device_database.py')

    Write-Banner 'Preparing a clean build directory'
    if (Test-Path -LiteralPath $BuildDir) {
        Remove-Item -LiteralPath $BuildDir -Recurse -Force
    }

    $PrefixPath = Join-Path $MsysRoot 'ucrt64'
    Invoke-NativeStep 'Configuring the project' $CMake @(
        '-S', $ProjectRoot,
        '-B', $BuildDir,
        '-G', 'Ninja',
        '-DCMAKE_BUILD_TYPE=Release',
        "-DCMAKE_PREFIX_PATH=$PrefixPath",
        '-DBUILD_TESTING=ON'
    )

    Invoke-NativeStep 'Compiling the program' $CMake @('--build', $BuildDir, '--parallel')
    Invoke-NativeStep 'Running automated tests' $CTest @('--test-dir', $BuildDir, '--output-on-failure')

    Write-Banner 'Creating the portable Windows application folder'
    if (Test-Path -LiteralPath $DistDir) {
        Remove-Item -LiteralPath $DistDir -Recurse -Force
    }
    New-Item -ItemType Directory -Path $DistDir -Force | Out-Null

    $GuiExe = Join-Path $BuildDir 'FlyingBytesPro.exe'
    $ProbeExe = Join-Path $BuildDir 'usbasp_probe.exe'
    $CliExe = Join-Path $BuildDir 'FlyingBytesProCLI.exe'
    Require-File $GuiExe 'Compiled GUI executable'
    Require-File $ProbeExe 'Compiled USBasp probe executable'
    Require-File $CliExe 'Compiled FlyingBytesPro command-line executable'

    Copy-Item -LiteralPath $GuiExe -Destination $DistDir -Force
    Copy-Item -LiteralPath $ProbeExe -Destination $DistDir -Force
    Copy-Item -LiteralPath $CliExe -Destination $DistDir -Force

    $Documents = @(
        'README.md',
        'TECHNICAL_README.md',
        'CLI_GUIDE.md',
        'BUILD_WINDOWS_MSYS2.md',
        'LICENSE',
        'THIRD_PARTY_NOTICES.md',
        'SOURCE_MANIFEST_SHA256.txt'
    )
    foreach ($Document in $Documents) {
        $Source = Join-Path $ProjectRoot $Document
        if (Test-Path -LiteralPath $Source) {
            Copy-Item -LiteralPath $Source -Destination $DistDir -Force
        }
    }

    $PackagedGui = Join-Path $DistDir 'FlyingBytesPro.exe'
    Invoke-NativeStep 'Copying Qt runtime files' $WinDeployQt @(
        '--release',
        '--compiler-runtime',
        '--no-translations',
        $PackagedGui
    )
    Copy-Item -LiteralPath $LibUsbDll -Destination $DistDir -Force

    # MSYS2's windeployqt may leave GCC runtime DLLs behind even when
    # --compiler-runtime is requested. Do not trust the option blindly.
    # Recursively inspect every packaged EXE/DLL and copy all non-system
    # dependencies found in UCRT64/bin, then fail if anything is unresolved.
    $DependencyReport = Join-Path $DistDir 'DEPENDENCY_REPORT.txt'
    Invoke-NativeStep 'Auditing and copying all portable runtime dependencies' $Python @(
        'scripts\deploy_runtime_dependencies.py',
        '--dist', $DistDir,
        '--ucrt-bin', $UcrtBin,
        '--objdump', $ObjDump,
        '--windows-dir', $env:WINDIR,
        '--report', $DependencyReport
    )

    Write-Banner 'Testing the packaged application with MSYS2 removed from PATH'
    $SavedPath = $env:PATH
    try {
        $env:PATH = "$DistDir;$env:WINDIR\System32;$env:WINDIR"
        $CheckProcess = Start-Process -FilePath $PackagedGui -ArgumentList @('--deployment-check') -Wait -PassThru
        if ($CheckProcess.ExitCode -ne 0) {
            throw "Portable application startup test failed with exit code $($CheckProcess.ExitCode)."
        }
        Write-Host 'PASS: the packaged GUI starts without MSYS2 on PATH.'

        $PackagedCli = Join-Path $DistDir 'FlyingBytesProCLI.exe'
        $CliCheck = Start-Process -FilePath $PackagedCli -ArgumentList @('--version') -Wait -PassThru -NoNewWindow
        if ($CliCheck.ExitCode -ne 0) {
            throw "Portable CLI startup test failed with exit code $($CliCheck.ExitCode)."
        }
        Write-Host 'PASS: the packaged CLI starts without MSYS2 on PATH.'

        & $PackagedCli --list-parts | Out-Null
        if ($LASTEXITCODE -ne 0) {
            throw "Portable CLI embedded-database test failed with exit code $LASTEXITCODE."
        }
        Write-Host 'PASS: the packaged CLI can load the embedded 175-device database.'
    }
    finally {
        $env:PATH = $SavedPath
    }

    $ResultText = @"
BUILD PASSED

Portable application:
$PackagedGui

Command-line programmer:
$(Join-Path $DistDir 'FlyingBytesProCLI.exe')

USBasp probe:
$(Join-Path $DistDir 'usbasp_probe.exe')

Temporary compiler files:
$BuildDir

Complete log:
$LogFile
"@
    Set-Content -LiteralPath $ResultFile -Value $ResultText -Encoding UTF8

    Write-Banner 'BUILD PASSED'
    Write-Host 'The compiled files are located here:' -ForegroundColor Green
    Write-Host $DistDir -ForegroundColor Green
    Write-Host ''
    Write-Host 'FlyingBytesPro will now start in real hardware mode.'
    Write-Host 'This PowerShell window intentionally remains open.'

    Start-Process -FilePath $PackagedGui
    $Succeeded = $true
}
catch {
    $Message = $_.Exception.Message
    Write-Banner 'BUILD FAILED'
    Write-Host $Message -ForegroundColor Red
    Write-Host ''
    Write-Host "Complete log: $LogFile"
    Write-Host "Short result: $ResultFile"
    Write-Host ''
    Write-Host 'This PowerShell window intentionally remains open.'

    $ResultText = @"
BUILD FAILED

Reason:
$Message

Temporary compiler files:
$BuildDir

Complete log:
$LogFile
"@
    Set-Content -LiteralPath $ResultFile -Value $ResultText -Encoding UTF8
}
finally {
    if ($TranscriptStarted) {
        try { Stop-Transcript | Out-Null } catch { }
    }
}

if ($Succeeded) {
    Write-Host ''
    Write-Host 'SUCCESS. Close this window after reviewing the result.' -ForegroundColor Green
} else {
    Write-Host ''
    Write-Host 'FAILED. The error and log locations are shown above.' -ForegroundColor Red
}

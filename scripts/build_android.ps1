[CmdletBinding()]
param(
    [ValidateSet("configure", "build", "all", "rebuild")]
    [string]$Action = "all",

    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$BuildType = "Release",

    [string]$BuildDir = "Build/android_ninja",
    [string]$Target = "rflow_client",
    [int]$Jobs = 8,

    [switch]$UseNinjaPlanFallback,

    [string]$CMakeExe = "D:\Qt\Tools\CMake_64\bin\cmake.exe",
    [string]$NinjaExe = "C:\Qt\android-sdk-windows\platform-tools\ninja.exe",
    [string]$SdkRoot = "D:\Qt\android-sdk",
    [string]$NdkRoot = "D:\Qt\android-sdk\ndk\26.1.10909125",
    [string]$QtRoot = "D:\Qt\6.8.3\android_arm64_v8a",
    [string]$QtHostPath = "D:\Qt\6.8.3\mingw_64",
    [string]$AndroidAbi = "arm64-v8a",
    [string]$AndroidPlatform = "android-28"
)

$ErrorActionPreference = "Stop"

function Assert-File([string]$Path, [string]$Label) {
    if (-not (Test-Path -LiteralPath $Path)) {
        throw "$Label not found: $Path"
    }
}

function Invoke-Checked {
    param(
        [string]$Exe,
        [string[]]$Arguments
    )

    Write-Host ""
    Write-Host "> $Exe $($Arguments -join ' ')"
    & $Exe @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed with exit code $LASTEXITCODE"
    }
}

function Invoke-NinjaPlanBuild {
    param(
        [string]$BuildDirectory,
        [string]$NinjaPath,
        [string]$BuildTarget
    )

    $planLines = & $NinjaPath -C $BuildDirectory -n -v $BuildTarget
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to generate Ninja plan"
    }

    $commands = @(
        $planLines |
        Where-Object { $_ -match '^\[\d+/\d+\]\s+' } |
        ForEach-Object { $_ -replace '^\[\d+/\d+\]\s+', '' }
    )

    if ($commands.Count -eq 0) {
        throw "No Ninja commands were generated for target '$BuildTarget'"
    }

    foreach ($command in $commands) {
        Write-Host ""
        Write-Host "> $command"
        & cmd.exe /d /c "cd /d `"$BuildDirectory`" && $command"
        if ($LASTEXITCODE -ne 0) {
            throw "Planned Ninja command failed with exit code $LASTEXITCODE"
        }
    }
}

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$buildPath = Join-Path $repoRoot $BuildDir
$toolchainFile = Join-Path $NdkRoot "build\cmake\android.toolchain.cmake"
$cxxCompiler = Join-Path $NdkRoot "toolchains\llvm\prebuilt\windows-x86_64\bin\clang++.exe"

Assert-File $CMakeExe "CMake"
Assert-File $NinjaExe "Ninja"
Assert-File $toolchainFile "Android toolchain file"
Assert-File $cxxCompiler "Android clang++"

if ($Action -eq "rebuild") {
    if (Test-Path -LiteralPath $buildPath) {
        Write-Host ""
        Write-Host "> Remove-Item -Recurse -Force $buildPath"
        Remove-Item -Recurse -Force $buildPath
    }
    $Action = "all"
}

if ($Action -in @("configure", "all")) {
    $configureArgs = @(
        "-S", $repoRoot,
        "-B", $buildPath,
        "-G", "Ninja",
        "-DCMAKE_MAKE_PROGRAM=$NinjaExe",
        "-DCMAKE_TOOLCHAIN_FILE=$toolchainFile",
        "-DANDROID_NDK=$NdkRoot",
        "-DANDROID_SDK_ROOT=$SdkRoot",
        "-DANDROID_ABI=$AndroidAbi",
        "-DANDROID_PLATFORM=$AndroidPlatform",
        "-DANDROID_STL=c++_shared",
        "-DANDROID_USE_LEGACY_TOOLCHAIN_FILE=OFF",
        "-DCMAKE_PREFIX_PATH=$QtRoot",
        "-DCMAKE_FIND_ROOT_PATH=$QtRoot",
        "-DCMAKE_CXX_COMPILER=$cxxCompiler",
        "-DCMAKE_CXX_COMPILER_FORCED=TRUE",
        "-DCMAKE_CXX_COMPILER_WORKS=TRUE",
        "-DCMAKE_CXX_ABI_COMPILED=TRUE",
        "-DCMAKE_CXX_FLAGS=-DANDROID -fdata-sections -ffunction-sections -funwind-tables -fstack-protector-strong -no-canonical-prefixes -D_FORTIFY_SOURCE=2 -Wformat -Werror=format-security -fexceptions -frtti -stdlib=libc++",
        "-DCMAKE_EXE_LINKER_FLAGS=-Wl,--build-id=sha1 -Wl,--no-rosegment -Wl,--no-undefined-version -Wl,--fatal-warnings -Wl,--gc-sections -Qunused-arguments -Wl,--no-undefined -Wl,--gc-sections",
        "-DQT_QMAKE_EXECUTABLE=$QtRoot\bin\qmake.bat",
        "-DQT_HOST_PATH=$QtHostPath",
        "-DRFLOW_BUILD_CLIENT=ON",
        "-DRFLOW_BUILD_SERVICE=OFF",
        "-DRFLOW_CLIENT_ENABLE_WEBRTC_IMPL=ON",
        "-DCMAKE_BUILD_TYPE=$BuildType"
    )
    Invoke-Checked -Exe $CMakeExe -Arguments $configureArgs
}

if ($Action -in @("build", "all")) {
    if ($UseNinjaPlanFallback) {
        Invoke-NinjaPlanBuild -BuildDirectory $buildPath -NinjaPath $NinjaExe -BuildTarget $Target
    } else {
        $buildArgs = @(
            "--build", $buildPath,
            "--target", $Target,
            "--parallel", $Jobs
        )
        Invoke-Checked -Exe $CMakeExe -Arguments $buildArgs
    }
}

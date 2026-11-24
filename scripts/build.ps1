# SPDX-License-Identifier: BSD-3-Clause
param(
    [ValidateSet('All','Native','Addon','Test')][string]$Target='All',
    [switch]$RunGpuTests,
    [string]$SdkVersion='10.0.26100.0',
    [ValidateRange(1,32)][int]$Jobs=1
)
$ErrorActionPreference='Stop'
$repo=(Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if(!(Test-Path -LiteralPath $vswhere)){throw 'Install Visual Studio 2022 or Build Tools with Desktop development with C++.'}
$vsInstall=& $vswhere -latest -products '*' -version '[17.0,18.0)' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(!$vsInstall){throw 'VS 2022 with the x64 C++ toolchain was not found.'}
& (Join-Path $vsInstall 'Common7\Tools\Launch-VsDevShell.ps1') -VsInstallationPath $vsInstall -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
$cl=(Get-Command cl.exe -ErrorAction Stop).Source
$msbuild=Join-Path $vsInstall 'MSBuild\Current\Bin\MSBuild.exe'
$build=Join-Path $repo 'build'
$bin=Join-Path $build 'bin'
$tests=Join-Path $build 'tests'
$null=New-Item -ItemType Directory -Force -Path $bin,$tests
$upstream=Join-Path $repo 'external\reshade'
function Invoke-Logged([string]$Executable,[string[]]$Arguments,[string]$Log) {
    & $Executable @Arguments *> $Log
    if($LASTEXITCODE -ne 0){
        Get-Content -LiteralPath $Log -Tail 50
        throw "Build/test command failed. See $Log"
    }
}
if($Target -in 'All','Native','Addon'){
    if(!(Test-Path -LiteralPath (Join-Path $upstream 'source\d3d12\er_batch.cpp'))){throw 'Run python scripts/prepare.py first.'}
    foreach($name in @('er_batch.cpp','er_batch.hpp')){
        if((Get-FileHash -LiteralPath (Join-Path $repo ('src\native\'+$name))).Hash -ne
           (Get-FileHash -LiteralPath (Join-Path $upstream ('source\d3d12\'+$name))).Hash){
            throw 'Native dependency copy is stale. Rerun scripts/prepare.py.'
        }
    }
}
if($Target -in 'All','Native'){
    $pythonDir=Join-Path $repo '.venv\Scripts'
    if(!(Test-Path -LiteralPath (Join-Path $pythonDir 'python.exe'))){throw 'Create .venv and install requirements-build.txt first.'}
    $savedPath=$env:PATH
    $env:PATH=$pythonDir+';'+$env:PATH
    Push-Location $upstream
    try {
        Invoke-Logged $msbuild @('ReShade.vcxproj','/m:1','/nr:false',"/p:CL_MPCount=$Jobs",
            '/p:Configuration=Release','/p:Platform=x64','/p:PlatformToolset=v143',
            "/p:WindowsTargetPlatformVersion=$SdkVersion",'/p:GladReproducible=true',
            "/p:SolutionDir=$upstream\",'/verbosity:minimal') (Join-Path $build 'native-build.log')
        Copy-Item -LiteralPath (Join-Path $upstream 'bin\x64\Release\ReShade64.dll') -Destination (Join-Path $bin 'EldenRingRT.dll')
    } finally {Pop-Location; $env:PATH=$savedPath}
}
if($Target -in 'All','Addon'){
    $obj=Join-Path $build 'addon-obj'
    $null=New-Item -ItemType Directory -Force -Path $obj
    Push-Location $obj
    try {
        Invoke-Logged $cl @('/nologo','/std:c++20','/EHsc','/W4','/MT','/LD','/O2','/DUNICODE','/D_UNICODE',
            "/I$upstream\include",(Join-Path $repo 'src\addon\rt_pass_probe.cpp'),
            (Join-Path $repo 'src\addon\gpu_profiler.cpp'),"/Fe$bin\RT-Pass-Probe.addon64",
            '/link','user32.lib','d3d12.lib') (Join-Path $build 'addon-build.log')
    } finally {Pop-Location}
}
if($Target -in 'All','Test'){
    Push-Location $tests
    try {
        Invoke-Logged $cl @('/nologo','/std:c++20','/EHsc','/W4','/MT','/O2',
            '/DER_BATCH_STANDALONE','/DNOMINMAX','/DWIN32_LEAN_AND_MEAN',
            "/I$repo\src\native","/I$repo\tests",
            (Join-Path $repo 'tests\native_batch_test.cpp'),
            (Join-Path $repo 'src\native\er_batch.cpp'),'/Fenative_batch_test.exe',
            '/link','user32.lib','d3d12.lib','dxgi.lib') (Join-Path $build 'test-build.log')
        Invoke-Logged $cl @('/nologo','/std:c++20','/EHsc','/W4','/MT','/O2',
            "/I$repo\src\addon",(Join-Path $repo 'tests\vegetation_policy_test.cpp'),
            '/Fevegetation_policy_test.exe') (Join-Path $build 'policy-build.log')
        Invoke-Logged (Join-Path $tests 'vegetation_policy_test.exe') @() (Join-Path $build 'policy-test-results.txt')
        Get-Content -LiteralPath (Join-Path $build 'policy-test-results.txt')
        Invoke-Logged $cl @('/nologo','/std:c++20','/EHsc','/W4','/MT','/O2',
            (Join-Path $repo 'tests\runtime_load_test.cpp'),'/Feruntime_load_test.exe',
            '/link','user32.lib','d3d12.lib','dxgi.lib') (Join-Path $build 'runtime-load-build.log')
        if($RunGpuTests){
            Invoke-Logged (Join-Path $tests 'native_batch_test.exe') @() (Join-Path $build 'gpu-test-results.txt')
            Get-Content -LiteralPath (Join-Path $build 'gpu-test-results.txt')
        }
    } finally {Pop-Location}
}
if($Target -eq 'All'){
    $smoke=Join-Path $build 'runtime-load-smoke'
    $null=New-Item -ItemType Directory -Force -Path $smoke
    foreach($name in @('EldenRingRT.dll','RT-Pass-Probe.addon64')){
        Copy-Item -LiteralPath (Join-Path $bin $name) -Destination (Join-Path $smoke $name)
    }
    foreach($name in @('ReShade.ini','RTVegetation.ini')){
        Copy-Item -LiteralPath (Join-Path $repo ('package\'+$name)) -Destination (Join-Path $smoke $name)
    }
    Invoke-Logged (Join-Path $tests 'runtime_load_test.exe') @((Join-Path $smoke 'EldenRingRT.dll')) (Join-Path $build 'runtime-load-test-results.txt')
    Get-Content -LiteralPath (Join-Path $build 'runtime-load-test-results.txt')
}
$sourceCommit=(& git -C $repo rev-parse HEAD).Trim()
if($LASTEXITCODE -ne 0){throw 'Cannot identify source commit.'}
[ordered]@{
    source_commit=$sourceCommit
    target=$Target
    visual_studio_version=(& $vswhere -latest -products '*' -version '[17.0,18.0)' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationVersion)
    addon_msvc_toolset=$env:VCToolsVersion
    native_sdk=$SdkVersion
    addon_sdk=$env:WindowsSDKVersion
    compiler_workers=$Jobs
    cpu_policy_test_run=($Target -in 'All','Test')
    runtime_load_test_run=($Target -eq 'All')
    gpu_test_run=([bool]$RunGpuTests -and $Target -in 'All','Test')
} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $build 'build-info.json') -Encoding UTF8
Write-Output "Built $Target. Outputs are under build/. No game files were changed."

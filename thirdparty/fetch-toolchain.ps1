# ---------------------------------------------------------------------------
#  fetch-toolchain.ps1 - stage the bundled Clang C++ toolchain
#
#  Downloads an llvm-mingw release and trims it into thirdparty\toolchain, which
#  the installer packs and the installed editor compiles with. That is what lets
#  a fresh Windows machine build C++ with no Visual Studio, no MinGW and nothing
#  on PATH.
#
#  llvm-mingw rather than the official LLVM Windows build on purpose: the latter
#  targets the MSVC ABI and needs Visual Studio's headers and libraries to be
#  installed, so it is not self-contained. llvm-mingw ships clang, lld, the
#  mingw-w64 CRT headers and import libraries, libc++ and the runtime DLLs in
#  one redistributable tree (Apache-2.0 with LLVM exception; the mingw-w64
#  runtime is permissively licensed).
#
#  The binaries are NOT committed - like sdl2\ and thirdparty\llvm\ they are
#  fetched per machine. Re-run this script after a clean checkout.
#
#    .\thirdparty\fetch-toolchain.ps1                 # stage the default release
#    .\thirdparty\fetch-toolchain.ps1 -KeepLldb       # also keep the LLDB debugger
#    .\thirdparty\fetch-toolchain.ps1 -Force          # restage over an existing tree
# ---------------------------------------------------------------------------
[CmdletBinding()]
param(
    [string] $Version = '20260922',
    [string] $Variant = 'ucrt-x86_64',
    [string] $Dest    = (Join-Path $PSScriptRoot 'toolchain'),
    [string] $Tools   = (Join-Path $PSScriptRoot 'buildtools'),
    [string] $Cache   = (Join-Path $env:TEMP 'gedi-toolchain-cache'),

    # Build-system versions staged alongside the compiler.
    [string] $CMakeVersion  = '4.4.3',
    [string] $NinjaVersion  = 'v1.13.2',
    [string] $MesonVersion  = '1.12.1',
    [string] $PythonVersion = '3.13.7',

    # LLDB is the debugger on Windows: llvm-mingw ships no gdb, and lldb-mi is a
    # GDB/MI front end the editor's existing MI backend drives unchanged. It
    # costs liblldb plus a CPython, so -NoLldb drops it for a compile-only build.
    [switch] $NoLldb,
    [switch] $SkipCompiler,
    [switch] $SkipBuildTools,
    [switch] $Force
)

$ErrorActionPreference = 'Stop'
$ProgressPreference    = 'SilentlyContinue'

$name    = "llvm-mingw-$Version-$Variant"
$url     = "https://github.com/mstorsjo/llvm-mingw/releases/download/$Version/$name.zip"
$zip     = Join-Path $Cache "$name.zip"
$unpack  = Join-Path $Cache 'unpacked'
$srcRoot = Join-Path $unpack $name

New-Item -ItemType Directory -Force -Path $Cache | Out-Null

# Download to the cache once; every re-stage reuses it.
function Get-Cached([string]$Url, [string]$File) {
    $path = Join-Path $Cache $File
    if (-not (Test-Path $path)) {
        Write-Host "downloading $File ..."
        Invoke-WebRequest -Uri $Url -OutFile $path -UseBasicParsing
    }
    return $path
}

function Expand-Cached([string]$Zip, [string]$Into) {
    if (-not (Test-Path $Into)) {
        Add-Type -AssemblyName System.IO.Compression.FileSystem
        [System.IO.Compression.ZipFile]::ExtractToDirectory($Zip, $Into)
    }
    return $Into
}

if ($SkipCompiler) { Write-Host "skipping the compiler" }
elseif ((Test-Path $Dest) -and -not $Force) {
    Write-Host "compiler already staged at $Dest (use -Force to restage)"
    $SkipCompiler = $true
}

if (-not $SkipCompiler) {
    # ---- download -------------------------------------------------------------
    if (-not (Test-Path $zip)) {
        Write-Host "downloading $name.zip ..."
        Invoke-WebRequest -Uri $url -OutFile $zip -UseBasicParsing
    }
    Write-Host ("archive : {0:N1} MB" -f ((Get-Item $zip).Length / 1MB))

    # ---- extract --------------------------------------------------------------
    if (-not (Test-Path $srcRoot)) {
        Write-Host "extracting ..."
        Add-Type -AssemblyName System.IO.Compression.FileSystem
        [System.IO.Compression.ZipFile]::ExtractToDirectory($zip, $unpack)
    }

    # ---- stage ----------------------------------------------------------------
    # Everything here targets x86_64 Windows only. The release also carries aarch64,
    # armv7 and i686 sysroots, which together are about 200 MB of payload nobody
    # installing a Windows editor is going to use.
    if (Test-Path $Dest) { Remove-Item $Dest -Recurse -Force }
    New-Item -ItemType Directory -Force -Path $Dest | Out-Null

    Write-Host "staging into $Dest ..."

    # bin\ : the drivers, the linker, the binutils equivalents and the runtime DLLs
    # those need. Dropped: other-architecture driver wrappers, the clangd language
    # server and clang-tidy (gedi does its own analysis through libclang), and -
    # unless asked for - everything LLDB.
    $binDst = Join-Path $Dest 'bin'
    New-Item -ItemType Directory -Force -Path $binDst | Out-Null

    $dropPrefixes = @('aarch64-', 'armv7-', 'arm64ec-', 'i686-')
    $dropNames    = @('clangd.exe', 'clang-tidy.exe', 'clang-repl.exe')

    Get-ChildItem (Join-Path $srcRoot 'bin') -File | ForEach-Object {
        $n = $_.Name
        $isLldb = $n -like 'lldb*' -or $n -like 'liblldb*' -or $n -like 'libpython*'
        if ($dropPrefixes | Where-Object { $n.StartsWith($_) }) { return }
        if ($dropNames -contains $n)                           { return }
        if ($isLldb -and $NoLldb)                              { return }
        Copy-Item $_.FullName -Destination $binDst
    }

    # The release only ships GNU make as mingw32-make.exe, but the editor (and
    # most Makefiles) invoke plain "make". Give it both names.
    $mk = Join-Path $binDst 'mingw32-make.exe'
    if (Test-Path $mk) { Copy-Item $mk (Join-Path $binDst 'make.exe') }

    # include\ : the mingw-w64 Windows headers plus libc++. Architecture neutral,
    # so it comes across whole.
    Copy-Item (Join-Path $srcRoot 'include') -Destination $Dest -Recurse

    # lib\ : the clang resource directory (builtin headers, compiler-rt) and the
    # libc++/libunwind import libraries, minus the other architectures' runtimes.
    Copy-Item (Join-Path $srcRoot 'lib') -Destination $Dest -Recurse
    Get-ChildItem (Join-Path $Dest 'lib') -Recurse -File |
        Where-Object { $_.Name -match '(aarch64|armv7|arm64ec|i386|i686)' } |
        Remove-Item -Force -ErrorAction SilentlyContinue

    # The x86_64 sysroot, and busybox for the shell utilities a Makefile expects.
    Copy-Item (Join-Path $srcRoot 'x86_64-w64-mingw32') -Destination $Dest -Recurse
    if (Test-Path (Join-Path $srcRoot 'busybox')) {
        Copy-Item (Join-Path $srcRoot 'busybox') -Destination $Dest -Recurse
    }
    if (Test-Path (Join-Path $srcRoot 'share')) {
        Copy-Item (Join-Path $srcRoot 'share') -Destination $Dest -Recurse
    }
    Copy-Item (Join-Path $srcRoot 'LICENSE.TXT') -Destination $Dest

    # liblldb links against CPython and needs its standard library at runtime.
    if (-not $NoLldb -and (Test-Path (Join-Path $srcRoot 'python'))) {
        Copy-Item (Join-Path $srcRoot 'python') -Destination $Dest -Recurse
    }

    # ---- record what this is --------------------------------------------------
    @{
        source  = $url
        release = $Version
        variant = $Variant
        lldb    = (-not $NoLldb)
        staged  = (Get-Date -Format 'yyyy-MM-dd')
    } | ConvertTo-Json | Set-Content (Join-Path $Dest 'TOOLCHAIN.json')

    $mb = (Get-ChildItem $Dest -Recurse -File | Measure-Object Length -Sum).Sum / 1MB
    Write-Host ("staged  : {0:N1} MB in {1}" -f $mb, $Dest)

    $clang = Join-Path $Dest 'bin\clang++.exe'
    if (-not (Test-Path $clang)) { throw "staging failed: $clang is missing" }
    & $clang --version | Select-Object -First 2
}

# ===========================================================================
#  Build systems: CMake, Ninja and Meson
#
#  The compiler alone only covers single-file compiles and Makefiles. gedi also
#  drives cmake, meson and ninja, so a machine with none of them installed still
#  could not build most projects it generates. These are staged next to the
#  compiler and put on PATH by the editor the same way.
#
#  Meson is pure Python with no official standalone Windows binary, so it rides
#  on the embeddable CPython distribution - about 10 MB, and the only piece of
#  this that is assembled rather than merely unpacked.
# ===========================================================================
if ($SkipBuildTools) { Write-Host "skipping the build tools"; exit 0 }
if ((Test-Path $Tools) -and -not $Force) {
    Write-Host "build tools already staged at $Tools (use -Force to restage)"
    exit 0
}
if (Test-Path $Tools) { Remove-Item $Tools -Recurse -Force }
New-Item -ItemType Directory -Force -Path $Tools | Out-Null

# ---- CMake ----------------------------------------------------------------
$cmZip = Get-Cached "https://github.com/Kitware/CMake/releases/download/v$CMakeVersion/cmake-$CMakeVersion-windows-x86_64.zip" "cmake-$CMakeVersion.zip"
$cmOut = Expand-Cached $cmZip (Join-Path $Cache "cmake-$CMakeVersion")
$cmSrc = Get-ChildItem $cmOut -Directory | Select-Object -First 1
Copy-Item $cmSrc.FullName (Join-Path $Tools 'cmake') -Recurse
# doc\ is a full HTML manual - a third of the download, and the editor never
# opens it.
Remove-Item (Join-Path $Tools 'cmake\doc') -Recurse -Force -ErrorAction SilentlyContinue

# ---- Ninja ----------------------------------------------------------------
$njZip = Get-Cached "https://github.com/ninja-build/ninja/releases/download/$NinjaVersion/ninja-win.zip" "ninja-$NinjaVersion.zip"
Expand-Cached $njZip (Join-Path $Tools 'ninja') | Out-Null

# ---- Meson (+ embeddable Python) ------------------------------------------
$mesonDir = Join-Path $Tools 'meson'
New-Item -ItemType Directory -Force -Path $mesonDir | Out-Null

$pyZip = Get-Cached "https://www.python.org/ftp/python/$PythonVersion/python-$PythonVersion-embed-amd64.zip" "python-$PythonVersion-embed.zip"
Expand-Cached $pyZip (Join-Path $mesonDir 'python') | Out-Null

$msTgz = Get-Cached "https://github.com/mesonbuild/meson/releases/download/$MesonVersion/meson-$MesonVersion.tar.gz" "meson-$MesonVersion.tar.gz"
$msOut = Join-Path $Cache "meson-$MesonVersion"
if (-not (Test-Path $msOut)) {
    New-Item -ItemType Directory -Force -Path $msOut | Out-Null
    tar -xzf $msTgz -C $msOut
}
$msSrc = Get-ChildItem $msOut -Directory | Select-Object -First 1
Copy-Item (Join-Path $msSrc.FullName 'mesonbuild') $mesonDir -Recurse
Copy-Item (Join-Path $msSrc.FullName 'meson.py')   $mesonDir
foreach ($extra in @('COPYING', 'data')) {
    $p = Join-Path $msSrc.FullName $extra
    if (Test-Path $p) { Copy-Item $p $mesonDir -Recurse }
}

# The embeddable distribution takes its entire sys.path from python3xx._pth and
# does NOT add the script's own directory, so "import mesonbuild" would fail.
# Point it one level up, at the directory holding mesonbuild\.
Get-ChildItem (Join-Path $mesonDir 'python') -Filter 'python*._pth' | ForEach-Object {
    Add-Content $_.FullName "`n.."
}

# A .cmd shim so "meson" works as a plain command on PATH.
@'
@echo off
rem Meson is pure Python; run it with the embeddable interpreter staged beside it.
"%~dp0python\python.exe" "%~dp0meson.py" %*
'@ | Set-Content (Join-Path $mesonDir 'meson.cmd') -Encoding ASCII

# ---- record + verify ------------------------------------------------------
@{
    cmake  = $CMakeVersion
    ninja  = $NinjaVersion
    meson  = $MesonVersion
    python = $PythonVersion
    staged = (Get-Date -Format 'yyyy-MM-dd')
} | ConvertTo-Json | Set-Content (Join-Path $Tools 'BUILDTOOLS.json')

$mb = (Get-ChildItem $Tools -Recurse -File | Measure-Object Length -Sum).Sum / 1MB
Write-Host ("build tools: {0:N1} MB in {1}" -f $mb, $Tools)

& (Join-Path $Tools 'cmake\bin\cmake.exe') --version | Select-Object -First 1
& (Join-Path $Tools 'ninja\ninja.exe')     --version
& (Join-Path $Tools 'meson\meson.cmd')     --version

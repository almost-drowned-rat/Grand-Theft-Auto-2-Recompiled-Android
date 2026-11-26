# release_windows.ps1 - GTA 2 Recompiled's Windows release: GTA2-windows-x86_64.zip with gta2_launcher.exe (and
# recomplauncher.json) on top. The Windows twin of tools/linux/release_linux.sh: no game code and no game data - the
# launcher (launcher/, GPL-3.0, the Windows 95 window game_setup_ui_win95.c) carries a payload appended to the exe:
# the port and the runtime prebuilt as static libraries (GTA2_PORT_ONLY), the game's resources (gta2_res.o: icon,
# manifest, VERSIONINFO), SDL3.dll + its import library, unshield.exe, the winrecomp lifter with the embeddable
# Python 3.12 (+ capstone, pefile), tools/setup_build.py, config/, and the compiler (llvm-mingw cut to x86-64,
# bundle/llvm-mingw.zip). The player's first start builds data\gta2_recomp.exe from his own GTA 2 freeware installer
# (GTA2.exe) or an installed GTA 2 folder. Nothing is downloaded.
#
#   powershell -ExecutionPolicy Bypass -File tools\windows\release_windows.ps1 -Release v0.7.0 -Out <folder>
#       -SdlDev <SDL3-devel-3.4.x-mingw\x86_64-w64-mingw32> -SdlSrc <SDL3-3.4.x source folder>
#       -Unshield <unshield.exe, static> -UnshieldLicence <its LICENSE> -ZlibLicence <zlib's LICENSE>
#       -LlvmZip <llvm-mingw-...-ucrt-x86_64-slim.zip (the toolkit's prune_llvm_mingw.py)>
#       -PyZip <python-3.12.x-embed-amd64.zip> -SitePackages <a Python 3.12 site-packages with capstone 5 + pefile>
#
# Builds with the llvm-mingw clang on PATH (the same toolchain the payload ships), CMake and Ninja. All paths absolute,
# without spaces. Out: <out>\release\{GTA2-windows-x86_64.zip, SHA256SUMS-windows.txt}.
param(
    [Parameter(Mandatory)][string]$Release,
    [Parameter(Mandatory)][string]$Out,
    [Parameter(Mandatory)][string]$SdlDev,
    [Parameter(Mandatory)][string]$SdlSrc,
    [Parameter(Mandatory)][string]$Unshield,
    [Parameter(Mandatory)][string]$UnshieldLicence,
    [Parameter(Mandatory)][string]$ZlibLicence,
    [Parameter(Mandatory)][string]$LlvmZip,
    [Parameter(Mandatory)][string]$PyZip,
    [Parameter(Mandatory)][string]$SitePackages,
    [int]$Jobs = 4
)
$ErrorActionPreference = 'Stop'
if ($Release.Length -gt 32) { throw "-Release: at most 32 bytes" }
$Version = $Release.TrimStart('v')
if ($Version -notmatch '^\d+\.\d+\.\d+$') { throw "-Release: vA.B.C" }
$utf8 = New-Object Text.UTF8Encoding($false)
function Fwd([string]$p) { return ($p -replace '\\', '/') }
function Need([string]$p) { if (-not (Test-Path -LiteralPath $p)) { throw "missing: $p" } }
function Run([string]$what, [string]$log, [scriptblock]$cmd) {
    Write-Host "== $what"
    & $cmd *> $log
    if ($LASTEXITCODE) { Get-Content $log -Tail 30; throw "$what failed (exit $LASTEXITCODE) - see $log" }
}
foreach ($p in @($SdlDev, $SdlSrc, $Unshield, $UnshieldLicence, $ZlibLicence, $LlvmZip, $PyZip, $SitePackages)) { Need $p }
foreach ($p in @($Out, $SdlDev, $SdlSrc, $LlvmZip)) { if ($p -match ' ') { throw "path with a space: $p" } }
$Port = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$Toolkit = (Resolve-Path (Join-Path $Port '..\winrecomp')).Path
$clang = (Get-Command clang -ErrorAction Stop).Source
$llvmBin = Split-Path $clang
$windres = Join-Path $llvmBin 'llvm-windres.exe'
$python = (Get-Command python -ErrorAction Stop).Source
New-Item -ItemType Directory -Force $Out | Out-Null
$Out = (Resolve-Path $Out).Path
foreach ($n in 'capstone', 'pefile.py', 'peutils.py', 'ordlookup') { Need (Join-Path $SitePackages $n) }
$gitPort = (git -C $Port rev-parse --short HEAD).Trim()
$gitTk = (git -C $Toolkit rev-parse --short HEAD).Trim()
if ((git -C $Port status --porcelain) -or (git -C $Toolkit status --porcelain)) { Write-Warning "uncommitted changes: the build is -dirty" }
Write-Host "release ${Release}: port $gitPort, winrecomp $gitTk"

# -- the port and the runtime: static libraries (no generated code) ------------------------------------------------
$bp = Join-Path $Out 'build-port'
if (Test-Path $bp) { Remove-Item -Recurse -Force $bp }
Run 'the port (GTA2_PORT_ONLY)' (Join-Path $Out 'build-port.log') {
    cmake -S $Port -B $bp -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang "-DCMAKE_PREFIX_PATH=$(Fwd $SdlDev)" -DGTA2_PORT_ONLY=ON
    if (-not $LASTEXITCODE) { ninja -C $bp -j $Jobs }
}

# -- the launcher (SDL3 linked in from source, the Windows 95 window) ----------------------------------------------
$bl = Join-Path $Out 'build-launcher'
if (Test-Path $bl) { Remove-Item -Recurse -Force $bl }
Run 'the launcher' (Join-Path $Out 'build-launcher.log') {
    cmake -S (Join-Path $Port 'launcher') -B $bl -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang `
        "-DCMAKE_RC_COMPILER=$(Fwd $windres)" "-DGTA2_LAUNCHER_SDL3=$(Fwd $SdlSrc)" `
        "-DGTA2_LAUNCHER_ICON=$(Fwd (Join-Path $Port 'assets\icon_64.bmp'))" `
        "-DGTA2_LAUNCHER_ICO=$(Fwd (Join-Path $Port 'assets\icon.ico'))" "-DGTA2_LAUNCHER_VERSION=$Version"
    if (-not $LASTEXITCODE) { ninja -C $bl -j $Jobs gta2_launcher }
}
$launcherExe = Join-Path $bl 'gta2_launcher.exe'
Need $launcherExe

# -- the payload ---------------------------------------------------------------------------------------------------
$S = Join-Path $Out 'payload'
if (Test-Path $S) { Remove-Item -Recurse -Force $S }
foreach ($d in 'lib', 'bin', 'bundle', 'LICENSES', 'winrecomp', 'src') { New-Item -ItemType Directory -Force (Join-Path $S $d) | Out-Null }
Copy-Item (Join-Path $bp 'libgta2_port.a'), (Join-Path $bp 'winrecomp_rt\libwinrecomp_rt.a'),
          (Join-Path $bp 'winrecomp_rt\shaderpack\libxbox_shaderpack.a') (Join-Path $S 'lib')
Copy-Item (Join-Path $SdlDev 'lib\libSDL3.dll.a') (Join-Path $S 'lib')
Copy-Item (Join-Path $SdlDev 'bin\SDL3.dll') (Join-Path $S 'lib')
# the game's resources: icon, manifest, VERSIONINFO -> lib\gta2_res.o
$rcIn = [IO.File]::ReadAllText((Join-Path $PSScriptRoot 'gta2.rc.in'))
$rc = $rcIn.Replace('@ICO@', (Fwd (Join-Path $Port 'assets\icon.ico'))).Replace('@MANIFEST@', (Fwd (Join-Path $PSScriptRoot 'gta2_recomp.manifest'))).Replace('@VER_COMMAS@', $Version.Replace('.', ',')).Replace('@VERSION@', $Version)
$rcFile = Join-Path $Out 'gta2.rc'
[IO.File]::WriteAllText($rcFile, $rc, $utf8)
& $windres --target=pe-x86-64 -i $rcFile -o (Join-Path $S 'lib\gta2_res.o')
if ($LASTEXITCODE) { throw "llvm-windres failed" }
Copy-Item $Unshield (Join-Path $S 'bin\unshield.exe')
Copy-Item (Join-Path $Port 'tools\setup_build.py') (Join-Path $S 'setup_build.py')
Copy-Item -Recurse (Join-Path $Port 'config') (Join-Path $S 'config')
# config\game.toml's exclude_manual = "../src/gta2_manual.c" (without it: duplicate symbols at the link)
Copy-Item (Join-Path $Port 'src\gta2_manual.c') (Join-Path $S 'src\gta2_manual.c')
# the lifter: the toolkit's tools\ (without tests and caches) and templates\
foreach ($top in 'tools', 'templates') {
    Get-ChildItem -Recurse -File (Join-Path $Toolkit $top) | Where-Object {
        $_.FullName -notmatch '\\__pycache__\\' -and $_.Extension -ne '.pyc' -and $_.Name -notlike 'test_*.py'
    } | ForEach-Object {
        $rel = $_.FullName.Substring($Toolkit.Length + 1)
        $dst = Join-Path (Join-Path $S 'winrecomp') $rel
        New-Item -ItemType Directory -Force (Split-Path $dst) | Out-Null
        Copy-Item $_.FullName $dst
    }
}
Copy-Item (Join-Path $Toolkit 'LICENSE') (Join-Path $S 'LICENSES\winrecomp-LICENSE.txt')
if (Test-Path (Join-Path $Toolkit 'NOTICE')) { Copy-Item (Join-Path $Toolkit 'NOTICE') (Join-Path $S 'LICENSES\winrecomp-NOTICE.txt') }
# Python: the embeddable 3.12 with capstone and pefile (site-packages on its ._pth)
$py = Join-Path $S 'python'
Expand-Archive -LiteralPath $PyZip -DestinationPath $py
$sp = Join-Path $py 'Lib\site-packages'
New-Item -ItemType Directory -Force $sp | Out-Null
foreach ($n in 'capstone', 'pefile.py', 'peutils.py', 'ordlookup') { Copy-Item -Recurse (Join-Path $SitePackages $n) $sp }
Get-ChildItem -Recurse -Directory $sp -Filter '__pycache__' | Remove-Item -Recurse -Force
$pth = Get-ChildItem $py -Filter 'python*._pth' | Select-Object -First 1
$zipName = (Get-ChildItem $py -Filter 'python3*.zip' | Select-Object -First 1).Name
[IO.File]::WriteAllLines($pth.FullName, @($zipName, '.', 'Lib\site-packages'))
& (Join-Path $py 'python.exe') -I -c "import capstone, pefile, tomllib; print('python ok', capstone.__version__)"
if ($LASTEXITCODE) { throw "the payload's Python cannot import capstone / pefile" }
Copy-Item (Join-Path $py 'LICENSE.txt') (Join-Path $S 'LICENSES\Python-LICENSE.txt')
$capLic = Get-ChildItem $SitePackages -Directory -Filter 'capstone-*.dist-info' | Select-Object -First 1
$peLic = Get-ChildItem $SitePackages -Directory -Filter 'pefile-*.dist-info' | Select-Object -First 1
Copy-Item (Get-ChildItem $capLic.FullName -Filter 'LICENSE*' | Select-Object -First 1).FullName (Join-Path $S 'LICENSES\capstone-LICENSE.txt')
$peL = Get-ChildItem $peLic.FullName -Recurse -Filter 'LICENSE*' | Select-Object -First 1
if ($peL) { Copy-Item $peL.FullName (Join-Path $S 'LICENSES\pefile-LICENSE.txt') }
# the compiler: llvm-mingw cut to x86-64, unpacked once at the first start
$bz = Join-Path $S 'bundle\llvm-mingw.zip'
Copy-Item $LlvmZip $bz
$top = & $python -I -c "import sys, zipfile; print({n.split('/')[0] for n in zipfile.ZipFile(sys.argv[1]).namelist() if n}.pop())" $bz
$zsha = (Get-FileHash $bz -Algorithm SHA256).Hash.ToLower()
$zsize = (Get-Item $bz).Length
& $python -I -c "import sys, zipfile; z = zipfile.ZipFile(sys.argv[1]); open(sys.argv[2], 'wb').write(z.read(sys.argv[3] + '/LICENSE.TXT'))" $bz (Join-Path $S 'LICENSES\llvm-mingw-LICENSE.txt') $top
Copy-Item (Join-Path $Port 'launcher\LICENSE') (Join-Path $S 'LICENSES\launcher-GPL-3.0.txt')
Copy-Item (Join-Path $Port 'launcher\third_party\miniz\LICENSE') (Join-Path $S 'LICENSES\miniz-MIT.txt')
Copy-Item (Join-Path $Port 'LICENSE') (Join-Path $S 'LICENSES\gta2-recompiled-MIT.txt')
Copy-Item (Join-Path $SdlSrc 'LICENSE.txt') (Join-Path $S 'LICENSES\SDL3-LICENSE.txt')
Copy-Item $UnshieldLicence (Join-Path $S 'LICENSES\unshield-LICENSE.txt')
Copy-Item $ZlibLicence (Join-Path $S 'LICENSES\zlib-LICENSE.txt')
$T = "`t"
[IO.File]::WriteAllText((Join-Path $S 'toolchain.txt'),
    "format${T}2`nclang${T}llvm/bin/clang.exe`npython${T}python/python.exe`nbundle${T}bundle/llvm-mingw.zip${T}$zsha${T}$zsize${T}$top${T}llvm`n", $utf8)
# the steps (launcher.c): check / extract / extract-folder by themselves, then the lifts in order
$lift = @(
    "# the first-start build (tools/setup_build.py): check / extract run by themselves, then the rest in order",
    "check${T}{TOOLS}/setup_build.py${T}check${T}--game${T}{GAME}",
    "extract${T}{TOOLS}/setup_build.py${T}extract${T}--installer${T}{INPUT}${T}--unshield${T}{TOOLS}/bin/unshield.exe${T}--work${T}{WORK}${T}--game${T}{GAME}",
    "extract-folder${T}{TOOLS}/setup_build.py${T}extract${T}--folder${T}{INPUT}${T}--work${T}{WORK}${T}--game${T}{GAME}",
    "lift-gta2${T}{TOOLS}/setup_build.py${T}lift${T}--game${T}{GAME}${T}--toolkit${T}{TOOLS}/winrecomp${T}--config${T}{TOOLS}/config${T}--work${T}{WORK}${T}--module${T}gta2",
    "lift-bink${T}{TOOLS}/setup_build.py${T}lift${T}--game${T}{GAME}${T}--toolkit${T}{TOOLS}/winrecomp${T}--config${T}{TOOLS}/config${T}--work${T}{WORK}${T}--module${T}bink")
[IO.File]::WriteAllText((Join-Path $S 'lift.txt'), ($lift -join "`n") + "`n", $utf8)
# the compile (CMakeLists.txt's gta2_gen / bink_gen: C11 + GNU, -O2 -w -fwrapv -fno-strict-aliasing) and the link
# (the port whole, the runtime, SDL3's import library, the resources; static C runtime + winpthreads; GUI program)
$cc = @('cc', '--target=x86_64-w64-mingw32', '-x', 'c', '-std=gnu11', '-O2', '-DNDEBUG', '-w', '-fwrapv', '-fno-strict-aliasing')
$link = @('link', '--target=x86_64-w64-mingw32', '-fuse-ld=lld', '-static', '-mwindows', '{OBJECTS}',
          '-Wl,--whole-archive', '{TOOLS}/lib/libgta2_port.a', '-Wl,--no-whole-archive',
          '{TOOLS}/lib/libwinrecomp_rt.a', '{TOOLS}/lib/libxbox_shaderpack.a', '{TOOLS}/lib/gta2_res.o',
          '{TOOLS}/lib/libSDL3.dll.a', '-lm', '-lpthread', '-lkernel32', '-luser32', '-lgdi32', '-lwinspool',
          '-lshell32', '-lole32', '-loleaut32', '-luuid', '-lcomdlg32', '-ladvapi32', '-o', '{OUT}')
$build = @('# the game''s compile and link (tools/windows/release_windows.ps1)', ($cc -join $T),
           "glob${T}{WORK}/gen/gta2/src${T}.c", "glob${T}{WORK}/gen/bink/src${T}.c", ($link -join $T),
           "copy${T}{TOOLS}/lib/SDL3.dll${T}{DATA}/SDL3.dll")
[IO.File]::WriteAllText((Join-Path $S 'build.txt'), ($build -join "`n") + "`n", $utf8)
[IO.File]::WriteAllText((Join-Path $S 'release.txt'), $Release, $utf8)

# -- the launcher with its payload (zip appended + trailer: magic, offset, release) ----------------------------------
Write-Host "== the launcher with its payload"
$lp = Join-Path $Out 'gta2_launcher.exe'
& $python -I (Join-Path $PSScriptRoot 'append_payload.py') $S $launcherExe $lp $Release
if ($LASTEXITCODE) { throw "append_payload.py failed" }

# -- the zip: only the launcher (and RecompLauncher's manifest) on top -----------------------------------------------
$rel = Join-Path $Out 'release'
if (Test-Path $rel) { Remove-Item -Recurse -Force $rel }
New-Item -ItemType Directory $rel | Out-Null
$zip = Join-Path $rel 'GTA2-windows-x86_64.zip'
& $python -I -c "import sys, zipfile; z = zipfile.ZipFile(sys.argv[1], 'w', zipfile.ZIP_DEFLATED, compresslevel=6); z.write(sys.argv[2], 'gta2_launcher.exe'); z.write(sys.argv[3], 'recomplauncher.json'); z.close()" $zip $lp (Join-Path $Port 'recomplauncher.json')
if ($LASTEXITCODE) { throw "zip failed" }
$h = (Get-FileHash $zip -Algorithm SHA256).Hash.ToLower()
[IO.File]::WriteAllText((Join-Path $rel 'SHA256SUMS-windows.txt'), "$h  GTA2-windows-x86_64.zip`n", $utf8)
Write-Host ("done: {0} ({1:N1} MB) sha256 {2}; port {3}, winrecomp {4}" -f $zip, ((Get-Item $zip).Length / 1MB), $h, $gitPort, $gitTk)

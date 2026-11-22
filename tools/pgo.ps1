param(
    [string[]]$Roms = @(),
    [int]$SecondsPerRom = 30,
    [switch]$SkipTraining
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root
$msbuild = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -prerelease -find "MSBuild\**\Bin\MSBuild.exe" | Select-Object -First 1
if (-not $msbuild) { throw 'MSBuild.exe not found' }
if ($Roms.Count -eq 0) {
    $Roms = Get-ChildItem (Join-Path $root 'rom') -Include *.smc,*.sfc -Recurse | ForEach-Object { $_.FullName }
}
if ($Roms.Count -eq 0 -and -not $SkipTraining) { throw 'No ROMs found. Pass -Roms or put ROMs in the rom folder.' }
$pgoDir = Join-Path $root 'pgo'
New-Item -ItemType Directory -Force $pgoDir | Out-Null
if (-not $SkipTraining) {
    Get-ChildItem $pgoDir -Filter *.pgc -ErrorAction SilentlyContinue | Remove-Item -Force
    Remove-Item (Join-Path $pgoDir 'SNESUltimate.pgd') -Force -ErrorAction SilentlyContinue
    & $msbuild SNESUltimate.vcxproj /p:Configuration=Release /p:Platform=x64 /p:PgoMode=Instrument "/p:OutDir=$root\x64\PGI\" /m /v:minimal
    if ($LASTEXITCODE -ne 0) { throw 'Instrumented build failed' }
    $instDir = Join-Path $root 'x64\PGI'
    Get-ChildItem $instDir -Filter *.pgc -ErrorAction SilentlyContinue | Remove-Item -Force
    foreach ($rom in $Roms) {
        Write-Host "Training on $rom for $SecondsPerRom s"
        $p = Start-Process -FilePath (Join-Path $instDir 'SNESUltimate.exe') -ArgumentList "`"$rom`"" -WorkingDirectory $instDir -PassThru
        Start-Sleep -Seconds $SecondsPerRom
        if (-not $p.HasExited) { [void]$p.CloseMainWindow() }
        if (-not $p.WaitForExit(60000)) { Write-Warning 'Emulator did not exit cleanly; its profile data may be lost.'; $p.Kill() }
    }
    $pgcs = Get-ChildItem $instDir -Filter *.pgc
    if ($pgcs.Count -eq 0) { throw 'No .pgc profile data was written.' }
    $pgcs | Copy-Item -Destination $pgoDir -Force
}
& $msbuild SNESUltimate.vcxproj /p:Configuration=Release /p:Platform=x64 /p:PgoMode=Optimize "/p:OutDir=$root\x64\PGO\" /m /v:minimal
if ($LASTEXITCODE -ne 0) { throw 'Optimized build failed' }
Write-Host "Done: $(Join-Path $root 'x64\PGO\SNESUltimate.exe')"

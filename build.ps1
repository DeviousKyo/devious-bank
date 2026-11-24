# Builds DeviousBank.3dsx with devkitARM.
# GNU make can't handle the space in "Devious Bank", so this maps the project to a
# temporary drive letter, builds there, and removes the mapping afterwards.
# Usage:  .\build.ps1          (build)
#         .\build.ps1 clean    (clean)
param([string]$Target = "")

$bash = "C:\devkitPro\msys2\usr\bin\bash.exe"
$used = (Get-PSDrive -PSProvider FileSystem).Name
$letter = @('W','V','U','T','S','R','Q','P') | Where-Object { $used -notcontains $_ } | Select-Object -First 1
if (-not $letter) { Write-Error "No free drive letter for subst"; exit 1 }

subst "${letter}:" $PSScriptRoot
try {
    & $bash -lc "export DEVKITPRO=/opt/devkitpro DEVKITARM=/opt/devkitpro/devkitARM; cd /$($letter.ToLower()) && make -j8 $Target"
    $code = $LASTEXITCODE
}
finally {
    subst "${letter}:" /D
}
exit $code

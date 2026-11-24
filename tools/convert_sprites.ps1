# Converts PKHeX "Big Pokemon Sprites" box icons (68x56 PNG, b_<species>.png and
# b_egg.png) into one small texture per species for the 3DS:
#
#   1. crop to 64x56 (columns 2..65; every icon's pixels sit in columns 4..64)
#      -> assets/pkm_icons/<species>.png   (kept in the repo so builds are reproducible)
#   2. tex3ds, RGBA5551 (the icons use only fully opaque/transparent pixels), compressed
#      -> romfs/gfx/pkm/<species>.t3x       (64x64 texture, 64x56 subtexture)
#
# One file per species lets the app load just the 30 icons of the box on screen.
# Usage: .\tools\convert_sprites.ps1 -Source <folder with b_*.png>
param([Parameter(Mandatory = $true)][string]$Source)

Add-Type -AssemblyName System.Drawing
$root   = Split-Path $PSScriptRoot -Parent
$pngOut = Join-Path $root "assets\pkm_icons"
$t3xOut = Join-Path $root "romfs\gfx\pkm"
$tex3ds = "C:\devkitPro\tools\bin\tex3ds.exe"
New-Item -ItemType Directory -Force $pngOut, $t3xOut | Out-Null

$count = 0
foreach ($f in Get-ChildItem $Source -Filter "b_*.png") {
    if ($f.BaseName -notmatch '^b_(\d+|egg)$') { continue }   # base forms only
    $name = $Matches[1]
    if ($name -ne 'egg' -and [int]$name -gt 809) { continue }  # Gen 1-7 only

    $src = New-Object System.Drawing.Bitmap $f.FullName
    try {
        if ($src.Width -ne 68 -or $src.Height -ne 56) { throw "$($f.Name): expected 68x56" }
        $rect = New-Object System.Drawing.Rectangle 2, 0, 64, 56
        $crop = $src.Clone($rect, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
        $png  = Join-Path $pngOut "$name.png"
        $crop.Save($png, [System.Drawing.Imaging.ImageFormat]::Png)
        $crop.Dispose()
    } finally { $src.Dispose() }

    & $tex3ds -f rgba5551 -z auto -o (Join-Path $t3xOut "$name.t3x") $png | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "tex3ds failed on $png" }
    $count++
}
"converted $count icons"

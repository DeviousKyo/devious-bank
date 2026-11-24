# Converts game icon art (any size PNG) into one small opaque texture per game for the
# 3DS game-select tiles:
#
#   1. fit inside 84x84 (aspect preserved, high-quality bicubic), centered on an opaque
#      84x84 canvas filled with the tile colour (0x1E,0x1F,0x29)
#      -> assets/gameart/<key>.png   (kept in the repo so builds are reproducible)
#   2. tex3ds, RGB565 (opaque after step 1), compressed
#      -> romfs/gfx/gameart/<key>.t3x  (128x128 texture, 84x84 subtexture)
#
# One file per game lets the app load just the icons of the tiles on screen.
# -Map is a CSV with header "key,file": the icon key (e.g. D, HG) and the PNG's file
# name inside -Source.
# Usage: .\tools\convert_gameart.ps1 -Source <folder with icon PNGs> -Map <gameart.csv>
param(
    [Parameter(Mandatory = $true)][string]$Source,
    [Parameter(Mandatory = $true)][string]$Map
)

Add-Type -AssemblyName System.Drawing
$root   = Split-Path $PSScriptRoot -Parent
$pngOut = Join-Path $root "assets\gameart"
$t3xOut = Join-Path $root "romfs\gfx\gameart"
$tex3ds = "C:\devkitPro\tools\bin\tex3ds.exe"
$size   = 84
New-Item -ItemType Directory -Force $pngOut, $t3xOut | Out-Null

if (-not (Test-Path -LiteralPath $Map)) { throw "map file not found: $Map" }

$count = 0
foreach ($row in Import-Csv -LiteralPath $Map) {
    $key  = "$($row.key)".Trim()
    $file = "$($row.file)".Trim()
    if (-not $key) { continue }
    $srcPath = Join-Path $Source $file
    if (-not $file -or -not (Test-Path -LiteralPath $srcPath)) {
        throw "icon '$key': file '$file' not found in $Source"
    }

    $src = New-Object System.Drawing.Bitmap $srcPath
    try {
        $scale = [Math]::Min($size / $src.Width, $size / $src.Height)
        $w = [Math]::Max(1, [int][Math]::Round($src.Width * $scale))
        $h = [Math]::Max(1, [int][Math]::Round($src.Height * $scale))
        $x = [int][Math]::Floor(($size - $w) / 2)
        $y = [int][Math]::Floor(($size - $h) / 2)

        $dst = New-Object System.Drawing.Bitmap $size, $size, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
        $g   = [System.Drawing.Graphics]::FromImage($dst)
        try {
            $g.Clear([System.Drawing.Color]::FromArgb(255, 0x1E, 0x1F, 0x29))
            $g.InterpolationMode  = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
            $g.SmoothingMode      = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
            $g.PixelOffsetMode    = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
            $g.CompositingQuality = [System.Drawing.Drawing2D.CompositingQuality]::HighQuality
            # TileFlipXY stops bicubic sampling from bleeding transparent edges into the image.
            $attr = New-Object System.Drawing.Imaging.ImageAttributes
            $attr.SetWrapMode([System.Drawing.Drawing2D.WrapMode]::TileFlipXY)
            $destRect = New-Object System.Drawing.Rectangle $x, $y, $w, $h
            $g.DrawImage($src, $destRect, 0, 0, $src.Width, $src.Height, [System.Drawing.GraphicsUnit]::Pixel, $attr)
            $attr.Dispose()
        } finally { $g.Dispose() }

        $png = Join-Path $pngOut "$key.png"
        $dst.Save($png, [System.Drawing.Imaging.ImageFormat]::Png)
        $dst.Dispose()
    } finally { $src.Dispose() }

    & $tex3ds -f rgb565 -z auto -o (Join-Path $t3xOut "$key.t3x") $png | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "tex3ds failed on $png" }
    $count++
}
"converted $count icons"

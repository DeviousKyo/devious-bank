# Prepares the HOME Menu art used by the CIA (and the .3dsx icon):
#   assets/cia/icon_source.png  your icon at any size (square)  -> resized to icon.png (48x48)
#   assets/cia/banner.png       256x128 top-screen banner; used as-is, never touched here
#   assets/cia/banner.wav       short chime played with the banner (generated here)
# Run after replacing icon_source.png:
#   powershell -ExecutionPolicy Bypass -File tools\make_cia_art.ps1
Add-Type -AssemblyName System.Drawing
$out = Join-Path $PSScriptRoot "..\assets\cia"
New-Item -ItemType Directory -Force $out | Out-Null

# ---- icon: high-quality downscale of icon_source.png to 48x48
$srcPath = Join-Path $out "icon_source.png"
if (Test-Path $srcPath) {
    $img = [System.Drawing.Image]::FromFile((Resolve-Path $srcPath))
    $bmp = New-Object System.Drawing.Bitmap 48, 48, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.InterpolationMode = 'HighQualityBicubic'; $g.PixelOffsetMode = 'HighQuality'; $g.SmoothingMode = 'HighQuality'
    $attr = New-Object System.Drawing.Imaging.ImageAttributes; $attr.SetWrapMode('TileFlipXY')
    $g.DrawImage($img, (New-Object System.Drawing.Rectangle 0, 0, 48, 48), 0, 0, $img.Width, $img.Height, 'Pixel', $attr)
    $g.Dispose(); $img.Dispose()
    $bmp.Save((Join-Path (Resolve-Path $out) "icon.png"), [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
    "Wrote icon.png (48x48) from icon_source.png"
} else {
    "No icon_source.png; keeping the existing icon.png"
}

# ---- banner check
$bannerPath = Join-Path $out "banner.png"
if (Test-Path $bannerPath) {
    $b = [System.Drawing.Image]::FromFile((Resolve-Path $bannerPath))
    if ($b.Width -ne 256 -or $b.Height -ne 128) { Write-Warning "banner.png is $($b.Width)x$($b.Height); it should be 256x128" }
    $b.Dispose()
}

# ---- chime: two soft sine notes (E6 then B6), 16-bit mono 22050 Hz, ~0.9 s
$rate = 22050
$notes = @(@(1318.5, 0.0, 0.45), @(1975.5, 0.30, 0.60))
$len = [int]($rate * 0.9)
$samples = New-Object double[] $len
foreach ($n in $notes) {
    $start = [int]($n[1] * $rate); $dur = [int]($n[2] * $rate)
    for ($i = 0; $i -lt $dur -and $start + $i -lt $len; $i++) {
        $t = $i / $rate
        $env = [math]::Min(1.0, $i / 200.0) * [math]::Exp(-5.0 * $t)
        $samples[$start + $i] += 0.35 * $env * [math]::Sin(2 * [math]::PI * $n[0] * $t)
    }
}
$ms = New-Object System.IO.MemoryStream
$bw = New-Object System.IO.BinaryWriter $ms
$bw.Write([Text.Encoding]::ASCII.GetBytes("RIFF")); $bw.Write([int](36 + $len * 2))
$bw.Write([Text.Encoding]::ASCII.GetBytes("WAVEfmt ")); $bw.Write([int]16)
$bw.Write([int16]1); $bw.Write([int16]1); $bw.Write([int]$rate); $bw.Write([int]($rate * 2))
$bw.Write([int16]2); $bw.Write([int16]16)
$bw.Write([Text.Encoding]::ASCII.GetBytes("data")); $bw.Write([int]($len * 2))
foreach ($s in $samples) { $bw.Write([int16][math]::Max(-32767, [math]::Min(32767, [math]::Round($s * 32767)))) }
$bw.Flush(); [System.IO.File]::WriteAllBytes((Join-Path $out "banner.wav"), $ms.ToArray())
"Wrote banner.wav"

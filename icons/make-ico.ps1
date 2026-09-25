# ---------------------------------------------------------------------------
#  make-ico.ps1 - build a multi-resolution Windows .ico from a PNG
#
#  Windows takes an executable's icon from an embedded resource, not from a PNG
#  on disk, so the two artwork files have to be turned into .ico containers the
#  .rc files reference.
#
#  Sizes up to 128 are written as 32bpp DIBs, which every shell surface has
#  understood forever; 256 is written as an embedded PNG, which is how large
#  icons are stored (a 256x256 DIB would be a megabyte on its own). Source
#  artwork that is not square is letterboxed rather than stretched.
#
#    .\icons\make-ico.ps1 -Png icons\gt_gui.png -Ico icons\gt_gui.ico
# ---------------------------------------------------------------------------
[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string] $Png,
    [Parameter(Mandatory)] [string] $Ico,
    [int[]] $Sizes = @(16, 24, 32, 48, 64, 128, 256)
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$src = [System.Drawing.Image]::FromFile((Resolve-Path $Png))

# Square canvas, aspect preserved, centred - stretching artwork to square is the
# one thing that always looks wrong at 16x16.
function Render([int]$n) {
    $bmp = New-Object System.Drawing.Bitmap $n, $n, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g   = [System.Drawing.Graphics]::FromImage($bmp)
    $g.InterpolationMode  = 'HighQualityBicubic'
    $g.PixelOffsetMode    = 'HighQuality'
    $g.SmoothingMode      = 'HighQuality'
    $g.CompositingQuality = 'HighQuality'
    $g.Clear([System.Drawing.Color]::Transparent)

    $scale = [Math]::Min($n / $src.Width, $n / $src.Height)
    $w = [int][Math]::Round($src.Width  * $scale)
    $h = [int][Math]::Round($src.Height * $scale)
    $g.DrawImage($src, [int](($n - $w) / 2), [int](($n - $h) / 2), $w, $h)
    $g.Dispose()
    return $bmp
}

# A 32bpp bottom-up DIB: BITMAPINFOHEADER, BGRA pixels, then the 1bpp AND mask
# (all zero - the alpha channel carries the transparency).
function DibBytes([System.Drawing.Bitmap]$bmp) {
    $n  = $bmp.Width
    $ms = New-Object System.IO.MemoryStream
    $bw = New-Object System.IO.BinaryWriter $ms

    $maskStride = [int](([Math]::Floor(($n + 31) / 32)) * 4)
    $bw.Write([uint32]40)
    $bw.Write([int32]$n)
    $bw.Write([int32]($n * 2))      # height counts XOR + AND
    $bw.Write([uint16]1)
    $bw.Write([uint16]32)
    $bw.Write([uint32]0)            # BI_RGB
    $bw.Write([uint32]($n * $n * 4 + $maskStride * $n))
    $bw.Write([int32]0); $bw.Write([int32]0)
    $bw.Write([uint32]0); $bw.Write([uint32]0)

    $rect = New-Object System.Drawing.Rectangle 0, 0, $n, $n
    $data = $bmp.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly,
                          [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $row = New-Object byte[] ($n * 4)
    for ($y = $n - 1; $y -ge 0; $y--) {     # bottom-up
        $ptr = [IntPtr]::Add($data.Scan0, $y * $data.Stride)
        [System.Runtime.InteropServices.Marshal]::Copy($ptr, $row, 0, $n * 4)
        $bw.Write($row, 0, $n * 4)
    }
    $bmp.UnlockBits($data)

    $bw.Write((New-Object byte[] ($maskStride * $n)), 0, $maskStride * $n)
    $bw.Flush()
    return $ms.ToArray()
}

function PngBytes([System.Drawing.Bitmap]$bmp) {
    $ms = New-Object System.IO.MemoryStream
    $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
    return $ms.ToArray()
}

$images = @()
foreach ($n in ($Sizes | Sort-Object)) {
    $bmp = Render $n
    $images += [pscustomobject]@{
        Size  = $n
        Bytes = if ($n -ge 256) { PngBytes $bmp } else { DibBytes $bmp }
    }
    $bmp.Dispose()
}
$src.Dispose()

# ICONDIR, then one 16-byte ICONDIRENTRY each, then the payloads.
$out = New-Object System.IO.MemoryStream
$w   = New-Object System.IO.BinaryWriter $out
$w.Write([uint16]0); $w.Write([uint16]1); $w.Write([uint16]$images.Count)

$offset = 6 + 16 * $images.Count
foreach ($img in $images) {
    $dim = if ($img.Size -ge 256) { 0 } else { $img.Size }   # 0 encodes 256
    $w.Write([byte]$dim); $w.Write([byte]$dim)
    $w.Write([byte]0); $w.Write([byte]0)
    $w.Write([uint16]1); $w.Write([uint16]32)
    $w.Write([uint32]$img.Bytes.Length)
    $w.Write([uint32]$offset)
    $offset += $img.Bytes.Length
}
foreach ($img in $images) { $w.Write($img.Bytes, 0, $img.Bytes.Length) }
$w.Flush()

[System.IO.File]::WriteAllBytes((Join-Path (Get-Location) $Ico), $out.ToArray())
$out.Dispose()

"{0}: {1} sizes ({2}), {3:N1} KB" -f $Ico, $images.Count,
    (($images | ForEach-Object { $_.Size }) -join ','),
    ((Get-Item $Ico).Length / 1KB)

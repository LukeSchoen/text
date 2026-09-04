param(
    [Parameter(Mandatory = $true)]
    [string]$ExePath,

    [Parameter(Mandatory = $true)]
    [string]$PngPath
)

$ErrorActionPreference = 'Stop'

$IconResourceId = 101
$IconImageId = 1
$LanguageEnUs = 1033
$RtIcon = [IntPtr]3
$RtGroupIcon = [IntPtr]14

Add-Type -AssemblyName System.Drawing

if (-not ([System.Management.Automation.PSTypeName]'NativeResourceApi').Type) {
    Add-Type @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;

public static class NativeResourceApi {
    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    public static extern IntPtr BeginUpdateResource(string fileName, bool deleteExistingResources);

    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern bool UpdateResource(IntPtr update, IntPtr type, IntPtr name, ushort language, byte[] data, uint dataSize);

    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern bool EndUpdateResource(IntPtr update, bool discard);

    public static void ThrowLastError(string action) {
        throw new Win32Exception(Marshal.GetLastWin32Error(), action);
    }
}
'@
}

function Write-UInt16LE {
    param([System.IO.BinaryWriter]$Writer, [int]$Value)
    $Writer.Write([UInt16]$Value)
}

function Write-UInt32LE {
    param([System.IO.BinaryWriter]$Writer, [long]$Value)
    $Writer.Write([UInt32]$Value)
}

function New-IconPngBytes {
    param([string]$Path)

    $source = [System.Drawing.Image]::FromFile((Resolve-Path $Path))
    try {
        $bitmap = New-Object System.Drawing.Bitmap 256, 256
        try {
            $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
            try {
                $graphics.Clear([System.Drawing.Color]::Transparent)
                $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
                $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
                $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
                $graphics.DrawImage($source, 0, 0, 256, 256)
            } finally {
                $graphics.Dispose()
            }

            $stream = New-Object System.IO.MemoryStream
            try {
                $bitmap.Save($stream, [System.Drawing.Imaging.ImageFormat]::Png)
                return $stream.ToArray()
            } finally {
                $stream.Dispose()
            }
        } finally {
            $bitmap.Dispose()
        }
    } finally {
        $source.Dispose()
    }
}

function New-GroupIconResource {
    param([byte[]]$PngBytes)

    $stream = New-Object System.IO.MemoryStream
    $writer = New-Object System.IO.BinaryWriter $stream
    try {
        Write-UInt16LE $writer 0
        Write-UInt16LE $writer 1
        Write-UInt16LE $writer 1
        $writer.Write([byte]0)
        $writer.Write([byte]0)
        $writer.Write([byte]0)
        $writer.Write([byte]0)
        Write-UInt16LE $writer 1
        Write-UInt16LE $writer 32
        Write-UInt32LE $writer $PngBytes.Length
        Write-UInt16LE $writer $IconImageId
        return $stream.ToArray()
    } finally {
        $writer.Dispose()
        $stream.Dispose()
    }
}

$resolvedExe = (Resolve-Path $ExePath).Path
$pngBytes = New-IconPngBytes $PngPath
$groupBytes = New-GroupIconResource $pngBytes

$update = [NativeResourceApi]::BeginUpdateResource($resolvedExe, $false)
if ($update -eq [IntPtr]::Zero) {
    [NativeResourceApi]::ThrowLastError('BeginUpdateResource failed')
}

$committed = $false
try {
    if (-not [NativeResourceApi]::UpdateResource($update, $RtIcon, [IntPtr]$IconImageId, $LanguageEnUs, $pngBytes, $pngBytes.Length)) {
        [NativeResourceApi]::ThrowLastError('UpdateResource RT_ICON failed')
    }
    if (-not [NativeResourceApi]::UpdateResource($update, $RtGroupIcon, [IntPtr]$IconResourceId, $LanguageEnUs, $groupBytes, $groupBytes.Length)) {
        [NativeResourceApi]::ThrowLastError('UpdateResource RT_GROUP_ICON failed')
    }
    if (-not [NativeResourceApi]::EndUpdateResource($update, $false)) {
        [NativeResourceApi]::ThrowLastError('EndUpdateResource failed')
    }
    $committed = $true
} finally {
    if (-not $committed) {
        [void][NativeResourceApi]::EndUpdateResource($update, $true)
    }
}

Write-Host "Embedded icon resource $IconResourceId from $PngPath"

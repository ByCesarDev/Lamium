#Requires -Version 5.1
<#
.SYNOPSIS
Exports PNG copies of the Lamium icon from its SVG master.

.DESCRIPTION
Renders assets/icon/lamium-icon.svg with a Chromium browser in headless mode
(Microsoft Edge or Google Chrome, whichever is installed) and writes
assets/icon/lamium-icon-<size>.png for each requested size. Nothing is
downloaded. Re-run after editing the SVG and commit the PNGs with it.
#>
[CmdletBinding()]
param(
    [int[]]$Size = @(512),
    [string]$Source = (Join-Path $PSScriptRoot '..\assets\icon\lamium-icon.svg'),
    [string]$OutputDirectory = (Join-Path $PSScriptRoot '..\assets\icon')
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$browser = @(
    "${env:ProgramFiles(x86)}\Microsoft\Edge\Application\msedge.exe",
    "$env:ProgramFiles\Microsoft\Edge\Application\msedge.exe",
    "$env:ProgramFiles\Google\Chrome\Application\chrome.exe",
    "$env:LOCALAPPDATA\Google\Chrome\Application\chrome.exe"
) | Where-Object { $_ -and (Test-Path -LiteralPath $_) } | Select-Object -First 1
if (-not $browser) { throw 'Microsoft Edge or Google Chrome is required to render the icon.' }

$svg = Get-Content -LiteralPath (Resolve-Path -LiteralPath $Source) -Raw
$output = (Resolve-Path -LiteralPath $OutputDirectory).Path

foreach ($pixels in $Size) {
    if ($pixels -lt 16 -or $pixels -gt 2048) { throw "Unsupported size: $pixels" }
    # The SVG is embedded inline so no file URL or relative path is involved.
    $markup = $svg -replace '<svg ', "<svg style=`"display:block;width:${pixels}px;height:${pixels}px`" "
    $page = Join-Path ([IO.Path]::GetTempPath()) "lamium-icon-$pixels.html"
    Set-Content -LiteralPath $page -Encoding UTF8 -Value (
        "<!doctype html><html><head><meta charset=`"utf-8`"></head>" +
        "<body style=`"margin:0;overflow:hidden`">$markup</body></html>")
    $target = Join-Path $output "lamium-icon-$pixels.png"
    if (Test-Path -LiteralPath $target) { Remove-Item -LiteralPath $target }
    $arguments = @('--headless=new', '--disable-gpu', '--hide-scrollbars', '--force-device-scale-factor=1',
        "--window-size=$pixels,$pixels", "--screenshot=$target", ([Uri]$page).AbsoluteUri)
    & $browser @arguments 2>$null | Out-Null
    for ($wait = 0; $wait -lt 50 -and -not (Test-Path -LiteralPath $target); $wait++) { Start-Sleep -Milliseconds 100 }
    Remove-Item -LiteralPath $page -ErrorAction SilentlyContinue
    if (-not (Test-Path -LiteralPath $target)) { throw "The browser did not write $target" }
    Write-Output $target
}

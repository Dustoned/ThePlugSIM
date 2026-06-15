# upload-build.ps1 - package ThePlugSIM (Win64), zip it, en upload als GitHub Release.
# Gebruik:  powershell -ExecutionPolicy Bypass -File "<pad>\Tools\upload-build.ps1" [-Quality 2K|1K|4K] [-Notes "wat is er nieuw"]
#   -Quality 2K (default) = aanbevolen balans; 1K = kleinste download; 4K = beste kwaliteit.
#   Voor twee losse releases: draai 'm twee keer, met -Quality 2K en met -Quality 1K.
param(
    [string]$Notes = "Nieuwe test-build.",
    [string]$Config = "Shipping",
    [ValidateSet("2K","1K","4K")][string]$Quality = "2K"
)
$ErrorActionPreference = "Stop"
$Proj   = "C:\Users\Dustoned\Documents\Unreal Projects\ThePlugSIM - Claude"
$UProj  = Join-Path $Proj "ThePlugSIM.uproject"
$UAT    = "E:\UE\UE_5.7\Engine\Build\BatchFiles\RunUAT.bat"
$Archive = Join-Path $Proj "Build\Archive"
$Repo   = "Dustoned/ThePlugSIM"
New-Item -ItemType Directory -Force (Join-Path $Proj "Build") | Out-Null

# Texture-resolutie-cap voor deze build (Config\DefaultDeviceProfiles.ini). Bron-assets blijven onaangetast.
$MaxLOD = @{ "4K" = 4096; "2K" = 2048; "1K" = 1024 }[$Quality]
$Ini = Join-Path $Proj "Config\DefaultDeviceProfiles.ini"
if (Test-Path $Ini) {
    $IniText = [System.IO.File]::ReadAllText($Ini)
    $IniText = [regex]::Replace($IniText, "MaxLODSize=\d+", "MaxLODSize=$MaxLOD")
    [System.IO.File]::WriteAllText($Ini, $IniText, (New-Object System.Text.UTF8Encoding($false)))
    Write-Host "== Texture-cap gezet op $Quality ($MaxLOD px) =="
}

# Geen -Notes meegegeven? Lees dan automatisch de patch notes uit Docs\PATCHNOTES.md (UTF-8).
$NotesPath = Join-Path $Proj "Docs\PATCHNOTES.md"
if ($Notes -eq "Nieuwe test-build." -and (Test-Path $NotesPath)) {
    $Notes = [System.IO.File]::ReadAllText($NotesPath, [System.Text.Encoding]::UTF8)
    Write-Host "== Patch notes geladen uit Docs\PATCHNOTES.md =="
}

# Automatische changelog uit git-commits sinds de vorige build.
$ShaFile = Join-Path $Proj "Build\last-build-sha.txt"
$LastSha = ""
if (Test-Path $ShaFile) { $LastSha = (Get-Content $ShaFile -Raw).Trim() }
$Changelog = ""
if ($LastSha -ne "") { $Changelog = (git -C "$Proj" log "$LastSha..HEAD" --pretty=format:"- %s") -join "`n" }
if ([string]::IsNullOrWhiteSpace($Changelog)) { $Changelog = "- (eerste build / geen nieuwe commits)" }

Write-Host "== Editor sluiten (DLL-lock voorkomen) =="
Get-Process UnrealEditor -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Seconds 3

Write-Host "== Packagen ($Config) - dit duurt even... =="
& $UAT BuildCookRun "-project=$UProj" -noP4 -platform=Win64 "-clientconfig=$Config" `
    -cook -build -stage -pak -archive "-archivedirectory=$Archive" -nocompileeditor -utf8output
if ($LASTEXITCODE -ne 0) { Write-Error "Packagen mislukt (UAT exit $LASTEXITCODE)"; exit 1 }

# Gestagede build vinden (de map met ThePlugSIM.exe).
$WinDir = Join-Path $Archive "Windows"
if (-not (Test-Path $WinDir)) { $WinDir = Join-Path $Archive "WindowsNoEditor" }
if (-not (Test-Path $WinDir)) { Write-Error "Geen gestagede build gevonden in $Archive"; exit 1 }

# Losse runtime-bestanden mee-kopieren: de game laadt iconen + menu-art rechtstreeks van schijf
# (Content/_Project/UI), maar die zitten niet in de gecookte .pak. Zonder dit valt de build terug
# op procedurele iconen en mist 't menu z'n achtergrond/logo.
$SrcUI = Join-Path $Proj "Content\_Project\UI"
$DstUI = Join-Path $WinDir "ThePlugSIM\Content\_Project\UI"
if (Test-Path $SrcUI) {
    New-Item -ItemType Directory -Force $DstUI | Out-Null
    Copy-Item -Recurse -Force (Join-Path $SrcUI "*") $DstUI
    Write-Host "== Losse UI-bestanden (iconen + menu-art) meegekopieerd naar de build =="
}

# Versie/tag op datum-tijd + kwaliteit (zo botsen 2K- en 1K-release niet).
$Stamp = Get-Date -Format "yyyyMMdd-HHmm"
$Tag   = "build-$Stamp-$Quality"
$Zip   = Join-Path $Proj "Build\ThePlugSIM-$Stamp-$Quality.zip"

Write-Host "== Zippen -> $Zip =="
if (Test-Path $Zip) { Remove-Item $Zip -Force }
Compress-Archive -Path (Join-Path $WinDir "*") -DestinationPath $Zip -CompressionLevel Optimal

$SizeMB = [math]::Round((Get-Item $Zip).Length / 1MB, 1)
Write-Host "== Zip klaar: $SizeMB MB =="

Write-Host "== Uploaden naar GitHub Release ($Tag) =="
$QLabel = @{ "4K" = "4K textures - beste kwaliteit, grootste download"; "2K" = "2K textures - aanbevolen balans"; "1K" = "1K textures - kleinste download" }[$Quality]
$Title = "ThePlugSIM test-build $Stamp ($Quality)"
$Body  = "$Notes`n`n## Deze download`n$QLabel`n`n## Wijzigingen sinds de vorige build`n$Changelog`n`n---`nWindows $Config build. Download de zip, pak het uit en start ThePlugSIM.exe.`nCo-op: host start een LAN/IP-spel; anderen verbinden via het IP van de host (zelfde netwerk, of port-forward 7777, of een VPN zoals Radmin of ZeroTier)."
# Notes via een UTF-8 bestand (--notes-file) zodat symbolen (pijlen/vinkjes/lijnen in de patch notes) correct renderen.
$NotesFile = Join-Path $Proj "Build\release-notes.md"
[System.IO.File]::WriteAllText($NotesFile, $Body, (New-Object System.Text.UTF8Encoding($false)))
# 2K is de aanbevolen 'latest'; andere kwaliteiten staan als losse release in de lijst.
$LatestArg = if ($Quality -eq "2K") { @("--latest") } else { @("--latest=false") }
gh release create $Tag "$Zip" --repo $Repo --title "$Title" --notes-file "$NotesFile" @LatestArg
if ($LASTEXITCODE -ne 0) { Write-Error "GitHub release upload mislukt"; exit 1 }

# Onthoud welke commit deze build was, voor de changelog van de volgende keer.
(git -C "$Proj" rev-parse HEAD).Trim() | Set-Content $ShaFile -Encoding ascii

# Reset de texture-cap terug naar de 2K-default zodat de repo-config schoon blijft.
if (Test-Path $Ini) {
    $IniText = [System.IO.File]::ReadAllText($Ini)
    $IniText = [regex]::Replace($IniText, "MaxLODSize=\d+", "MaxLODSize=2048")
    [System.IO.File]::WriteAllText($Ini, $IniText, (New-Object System.Text.UTF8Encoding($false)))
}

Write-Host "== KLAAR! Vrienden kunnen downloaden op: https://github.com/$Repo/releases/latest =="

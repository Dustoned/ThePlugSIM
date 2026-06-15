# upload-build.ps1 - package ThePlugSIM (Win64) in een of meer texture-kwaliteiten, zip elk,
# en upload ze samen als ASSETS onder EEN GitHub Release.
# Gebruik:  powershell -ExecutionPolicy Bypass -File "<pad>\Tools\upload-build.ps1" [-Qualities 2K] [-Notes "wat is er nieuw"]
#   -Qualities 2K (default) = een build onder een release. UE's Shipping-compressie (IoStore+Oodle)
#   drukt de build sowieso naar ~700 MB, dus een aparte 1K scheelt maar ~1% - daarom niet standaard.
#   Wil je toch meerdere als assets onder EEN release:  -Qualities 2K,1K   (4K kan ook).
param(
    [string]$Notes = "Nieuwe test-build.",
    [string]$Config = "Shipping",
    [ValidateSet("4K","2K","1K")][string[]]$Qualities = @("2K")
)
$ErrorActionPreference = "Stop"
$Proj    = "C:\Users\Dustoned\Documents\Unreal Projects\ThePlugSIM - Claude"
$UProj   = Join-Path $Proj "ThePlugSIM.uproject"
$UAT     = "E:\UE\UE_5.7\Engine\Build\BatchFiles\RunUAT.bat"
$Archive = Join-Path $Proj "Build\Archive"
$Repo    = "Dustoned/ThePlugSIM"
$Ini     = Join-Path $Proj "Config\DefaultDeviceProfiles.ini"
$CapMap  = @{ "4K" = 4096; "2K" = 2048; "1K" = 1024 }
$QLabel  = @{ "4K" = "4K textures - beste kwaliteit, grootste download"; "2K" = "2K textures - aanbevolen balans"; "1K" = "1K textures - kleinste download" }
New-Item -ItemType Directory -Force (Join-Path $Proj "Build") | Out-Null

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

# Een tijdstempel/tag voor de hele build-sessie - alle kwaliteiten komen onder DEZE ene release.
$Stamp = Get-Date -Format "yyyyMMdd-HHmm"
$Tag   = "build-$Stamp"

Write-Host "== Editor sluiten (DLL-lock voorkomen) =="
Get-Process UnrealEditor -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Seconds 3

# Per kwaliteit: cap zetten -> packagen -> losse UI mee -> zippen. Zips verzamelen voor de release.
$Zips = @()
$AssetLines = @()
foreach ($q in $Qualities) {
    $MaxLOD = $CapMap[$q]
    if (Test-Path $Ini) {
        $IniText = [System.IO.File]::ReadAllText($Ini)
        $IniText = [regex]::Replace($IniText, "MaxLODSize=\d+", "MaxLODSize=$MaxLOD")
        [System.IO.File]::WriteAllText($Ini, $IniText, (New-Object System.Text.UTF8Encoding($false)))
    }
    Write-Host "== [$q] Texture-cap gezet op $MaxLOD px =="

    Write-Host "== [$q] Packagen ($Config) - dit duurt even... =="
    & $UAT BuildCookRun "-project=$UProj" -noP4 -platform=Win64 "-clientconfig=$Config" `
        -cook -build -stage -pak -archive "-archivedirectory=$Archive" -nocompileeditor -utf8output
    if ($LASTEXITCODE -ne 0) { Write-Error "Packagen ($q) mislukt (UAT exit $LASTEXITCODE)"; exit 1 }

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
        Write-Host "== [$q] Losse UI-bestanden (iconen + menu-art) meegekopieerd naar de build =="
    }

    $Zip = Join-Path $Proj "Build\ThePlugSIM-$Stamp-$q.zip"
    Write-Host "== [$q] Zippen -> $Zip =="
    if (Test-Path $Zip) { Remove-Item $Zip -Force }
    Compress-Archive -Path (Join-Path $WinDir "*") -DestinationPath $Zip -CompressionLevel Optimal
    $SizeMB = [math]::Round((Get-Item $Zip).Length / 1MB, 1)
    Write-Host "== [$q] Zip klaar: $SizeMB MB =="

    $Zips += $Zip
    $AssetLines += "- **ThePlugSIM-$Stamp-$q.zip** - $($QLabel[$q]) ($SizeMB MB)"
}

# Release-tekst (een keer), met een Downloads-sectie die beide assets benoemt.
$Title = "ThePlugSIM test-build $Stamp"
$Downloads = ($AssetLines -join "`n")
$DlHeader = if ($Zips.Count -gt 1) { "## Downloads (kies een kwaliteit)" } else { "## Download" }
$Body = "$Notes`n`n$DlHeader`n$Downloads`n`n## Wijzigingen sinds de vorige build`n$Changelog`n`n---`nWindows $Config build. Download de zip, pak het uit en start ThePlugSIM.exe.`nCo-op: host start een LAN/IP-spel; anderen verbinden via het IP van de host (zelfde netwerk, of port-forward 7777, of een VPN zoals Radmin of ZeroTier)."
$NotesFile = Join-Path $Proj "Build\release-notes.md"
[System.IO.File]::WriteAllText($NotesFile, $Body, (New-Object System.Text.UTF8Encoding($false)))

# Een release met ALLE zips als assets. $Zips is een array; een native command rolt 'm uit tot
# losse argumenten (GEEN @-splat - dat zou een 1-element-array als string char-voor-char splatten).
Write-Host "== Uploaden naar GitHub Release ($Tag) met $($Zips.Count) asset(s) =="
gh release create $Tag $Zips --repo $Repo --title "$Title" --notes-file "$NotesFile" --latest
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

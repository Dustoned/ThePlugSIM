# upload-build.ps1 - package ThePlugSIM (Win64) in een of meer texture-kwaliteiten, zip elk,
# en upload ze samen als ASSETS onder EEN GitHub Release.
# Gebruik:  powershell -ExecutionPolicy Bypass -File "<pad>\Tools\upload-build.ps1" [-Qualities 1K] [-Notes "wat is er nieuw"]
#   -Qualities 1K (default) = textures op 1K zodat de COMPLETE build (strand-map + characters)
#   onder GitHub's 2GB-asset-limiet past. 2K/4K geven een grotere build (>2GB) die je extern
#   moet hosten (Drive/WeTransfer). Meerdere als losse assets onder EEN release:  -Qualities 1K,2K
param(
    [string]$Notes = "Nieuwe test-build.",
    [string]$Config = "Shipping",
    [ValidateSet("4K","2K","1K")][string[]]$Qualities = @("1K")
)
$ErrorActionPreference = "Stop"
$Proj    = "C:\Users\Dustoned\Documents\Unreal Projects\ThePlugSIM - Claude"
$UProj   = Join-Path $Proj "ThePlugSIM.uproject"
$UAT     = "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\RunUAT.bat"
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

# In-game versie (hoofdmenu, linksonder) synchroon houden met de bovenste "Version X.Y.Z" uit de patch
# notes. Zo klopt de versie in de game ALTIJD met de release zonder dat ik 'm met de hand hoef te bumpen.
$VerHeader = Join-Path $Proj "Source\WeedShopCore\Public\WeedShopVersion.h"
if ((Test-Path $NotesPath) -and (Test-Path $VerHeader)) {
    $notesTxt = [System.IO.File]::ReadAllText($NotesPath, [System.Text.Encoding]::UTF8)
    $vm = [regex]::Match($notesTxt, 'Version\s+(\d+\.\d+\.\d+)')
    if ($vm.Success) {
        $ver = $vm.Groups[1].Value
        $vh = [System.IO.File]::ReadAllText($VerHeader)
        $vh2 = [regex]::Replace($vh, 'WEEDSHOP_VERSION_STRING\s+TEXT\("[^"]*"\)', "WEEDSHOP_VERSION_STRING TEXT(`"$ver`")")
        if ($vh2 -ne $vh) {
            [System.IO.File]::WriteAllText($VerHeader, $vh2, (New-Object System.Text.UTF8Encoding($false)))
            Write-Host "== In-game versie gezet op v$ver (uit PATCHNOTES.md) =="
        } else {
            Write-Host "== In-game versie al v$ver =="
        }
    }
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

# BakedData-resync (VOOR de cook): de wereld-geometrie (starter-meubels, competitive-kamers, home-spawns,
# build-area, no-build-zones, meet/mark-spots, delivery-punt, licht-config, kamer-templates) wordt runtime
# string-geladen uit Saved/*.txt. WeedData::RestoreAll kopieert een gebakken file ALLEEN als de Saved-kopie
# ONTBREEKT (nooit bijwerken), en niks resync't de andere kant op. Een editor-fix aan de layout landt dus
# NIET in de packaged build tenzij we de actuele Saved-versie eerst OVER de gebakken snapshot zetten.
# Daarom hier: kopieer de bekende geometrie-datafiles van Saved/ -> Content/BakedData/ zodat de cook de
# ACTUELE layout bakt. Absolute paden (.NET-copy gebruikt de PS-cwd NIET). Overige BakedData-files blijven.
Write-Host "== BakedData-resync: actuele wereld-geometrie van Saved/ -> Content/BakedData/ =="
$SavedRoot = Join-Path $Proj "Saved"
$BakedRoot = Join-Path $Proj "Content\BakedData"
New-Item -ItemType Directory -Force $BakedRoot | Out-Null
# Platte geometrie-datafiles: alleen deze bekende files overschrijven (rest van BakedData ongemoeid laten).
$GeomFiles = @(
    "StarterFurniture.txt", "CompSpawns.txt", "CompDoors.txt", "HomeSpawn.txt",
    "BuildArea.txt", "NoBuildZones.txt", "MeetSpots.txt", "MarkedSpots.txt",
    "DeliveryPoint.txt", "LightConfig.txt", "RoomStamps.txt"
)
$Resynced = 0
foreach ($gf in $GeomFiles) {
    $src = Join-Path $SavedRoot $gf
    if (Test-Path $src) {
        $dst = Join-Path $BakedRoot $gf
        [System.IO.File]::Copy($src, $dst, $true)
        Write-Host ("   resync {0}" -f $gf)
        $Resynced++
    }
}
# Map-datafiles (RoomTemplates: per-kamer .txt-templates die RoomStamper string-laadt). Hele map spiegelen
# van Saved/ -> BakedData/ (nieuwe templates mee, gewijzigde overschreven). Ontbreekt de Saved-map: niets te doen.
$SavedTpl = Join-Path $SavedRoot "RoomTemplates"
if (Test-Path $SavedTpl) {
    $BakedTpl = Join-Path $BakedRoot "RoomTemplates"
    New-Item -ItemType Directory -Force $BakedTpl | Out-Null
    foreach ($tpl in (Get-ChildItem -Path $SavedTpl -Filter *.txt -File -ErrorAction SilentlyContinue)) {
        [System.IO.File]::Copy($tpl.FullName, (Join-Path $BakedTpl $tpl.Name), $true)
        Write-Host ("   resync RoomTemplates\{0}" -f $tpl.Name)
        $Resynced++
    }
}
Write-Host ("== BakedData-resync klaar: {0} bestand(en) bijgewerkt =="  -f $Resynced)

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

    # GATE 1 - Cook-parity: alles wat de code runtime via een string-pad laadt MOET in deze build
    # zitten (LoadObject/LoadClass cookt NIET vanzelf mee). Vangt stille misses zoals de settings-
    # toggles/pauze-menu/Quinn-skin die in dev wel werkten maar in de download ontbraken (v1.19.0).
    & (Join-Path $Proj "Tools\check-cook-parity.ps1")
    if ($LASTEXITCODE -ne 0) { Write-Error "Cook-parity FAILED ($q) - string-geladen content mist in de build; NIET geupload. Fix DirectoriesToAlwaysCook en package opnieuw."; exit 1 }

    # Gestagede build vinden (de map met ThePlugSIM.exe).
    $WinDir = Join-Path $Archive "Windows"
    if (-not (Test-Path $WinDir)) { $WinDir = Join-Path $Archive "WindowsNoEditor" }
    if (-not (Test-Path $WinDir)) { Write-Error "Geen gestagede build gevonden in $Archive"; exit 1 }

    # GATE 2 - Smoke-test: er gaat NOOIT een ongeteste build naar GitHub. Shipping logt niet ->
    # start de exe, wacht 100s, proces moet nog leven en er mag geen crash-dialog staan.
    $Exe = Join-Path $WinDir "ThePlugSIM.exe"
    if (-not (Test-Path $Exe)) { Write-Error "Smoke-test: $Exe niet gevonden"; exit 1 }
    Write-Host "== [$q] Smoke-test: exe starten, 100s wachten... =="
    $Smoke = Start-Process -FilePath $Exe -PassThru
    Start-Sleep -Seconds 100
    $Alive = Get-Process ThePlugSIM-Win64-Shipping -ErrorAction SilentlyContinue
    $Crash = Get-Process CrashReportClient -ErrorAction SilentlyContinue
    Get-Process ThePlugSIM,ThePlugSIM-Win64-Shipping,CrashReportClient -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
    if (-not $Alive -or $Crash) { Write-Error "Smoke-test FAILED ($q) - build crasht binnen 100s; NIET geupload."; exit 1 }
    Write-Host "== [$q] Smoke-test PASS (proces leefde na 100s, geen crash-dialog) =="

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

    # Visual C++ runtime-installer meebundelen: zonder die runtime start een UE-game niet
    # (VCRUNTIME140-fout). Vrienden draaien 'm een keer als de game niet opstart.
    $Redist = "C:\Program Files\Epic Games\UE_5.8\Engine\Extras\Redist\en-us\vc_redist.x64.exe"
    if (Test-Path $Redist) {
        Copy-Item $Redist (Join-Path $WinDir "vc_redist.x64.exe") -Force
        $ReadmeTxt = "ThePlugSIM`r`n`r`nStarten: dubbelklik ThePlugSIM.exe`r`n`r`nStart de game niet (foutmelding over VCRUNTIME140 of een ontbrekende .dll)?`r`nVoer dan eerst vc_redist.x64.exe uit (Microsoft Visual C++ runtime) en start de game opnieuw.`r`n"
        [System.IO.File]::WriteAllText((Join-Path $WinDir "LEES MIJ EERST.txt"), $ReadmeTxt, (New-Object System.Text.UTF8Encoding($false)))
        Write-Host "== [$q] vc_redist.x64.exe + leesmij meegebundeld in de build =="
    } else {
        Write-Host "== [$q] WAARSCHUWING: vc_redist niet gevonden op $Redist - niet meegebundeld =="
    }

    # Debug-symbolen (.pdb, ~224MB) niet meeleveren - niet nodig voor spelers, scheelt grootte.
    Get-ChildItem $WinDir -Recurse -Filter *.pdb -File -ErrorAction SilentlyContinue | Remove-Item -Force -ErrorAction SilentlyContinue

    $Zip = Join-Path $Proj "Build\ThePlugSIM-$Stamp-$q.zip"
    Write-Host "== [$q] Zippen -> $Zip =="
    if (Test-Path $Zip) { Remove-Item $Zip -Force }
    # Compress-Archive faalt boven ~2GB ("stream te lang", buffert in geheugen).
    # ZipFile.CreateFromDirectory streamt naar disk en ondersteunt Zip64 (grote builds).
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [System.IO.Compression.ZipFile]::CreateFromDirectory($WinDir, $Zip, [System.IO.Compression.CompressionLevel]::Optimal, $false)
    $ZipInfo = Get-Item $Zip
    $SizeMB = [math]::Round($ZipInfo.Length / 1MB, 1)
    if ($ZipInfo.Length -ge 2GB) {
        Write-Error ("Zip is {0} MB en past niet onder GitHub's 2GB release-asset limiet. Upload gestopt; gebruik 1K/lager of verwijder grote losse assets." -f $SizeMB)
        exit 1
    }
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

# Reset de texture-cap terug naar de 1K-default zodat de repo-config schoon blijft.
if (Test-Path $Ini) {
    $IniText = [System.IO.File]::ReadAllText($Ini)
    $IniText = [regex]::Replace($IniText, "MaxLODSize=\d+", "MaxLODSize=1024")
    [System.IO.File]::WriteAllText($Ini, $IniText, (New-Object System.Text.UTF8Encoding($false)))
}

Write-Host "== KLAAR! Vrienden kunnen downloaden op: https://github.com/$Repo/releases/latest =="

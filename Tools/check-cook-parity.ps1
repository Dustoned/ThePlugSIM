# check-cook-parity.ps1 - Release-gate: zit alle string-geladen content ECHT in de packaged build?
# Scant de C++-source op TEXT("/Game/...")-literals, de Data/*.csv EN de baked .txt-datafiles
# (Content/BakedData/**, o.a. RoomTemplates die deur/kamer-meshes string-laden via RoomStamper)
# en checkt elk /Game-pad tegen de staging-manifest van de laatste UAT-package-run. Vangt de
# klassieke stille cook-miss (LoadObject/LoadClass-pad dat niet onder DirectoriesToAlwaysCook valt
# -> asset ontbreekt stilletjes in Shipping). De BakedData-.txt-scan is toegevoegd omdat die tekst
# wel als los UFS-bestand gestaged wordt (DefaultGame.ini) maar de uassets waar 'ie naar wijst NIET
# automatisch mee-cooken - een blinde vlek waardoor een template-mesh ongemerkt kon wegvallen.
# Gebruik: powershell -File Tools\check-cook-parity.ps1   (exit 1 = MISSING gevonden, release blokkeren)

$ErrorActionPreference = 'Stop'
$Proj = Split-Path -Parent $PSScriptRoot
$SourceDir = Join-Path $Proj 'Source'
$DataDir = Join-Path $Proj 'Data'

# --- 1. Nieuwste staging-manifest vinden (per engine-installatie een eigen log-map) ---
$LogRoot = Join-Path $env:APPDATA 'Unreal Engine\AutomationTool\Logs'
$Manifest = Get-ChildItem -Path $LogRoot -Recurse -Filter 'FinalCopyWin64_UFSFiles.txt' -ErrorAction SilentlyContinue |
	Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (-not $Manifest) { Write-Host 'FOUT: geen staging-manifest gevonden - eerst een UAT-package draaien.' -ForegroundColor Red; exit 1 }
Write-Host ("Manifest: {0} ({1})" -f $Manifest.FullName, $Manifest.LastWriteTime)
$ManifestText = [System.IO.File]::ReadAllText($Manifest.FullName)

# --- 2. Alle /Game-paden uit de source + Data-CSV's halen ---
$Paths = New-Object System.Collections.Generic.HashSet[string]
$rx = [regex]'TEXT\("(/Game/[A-Za-z0-9_\-\./ ]+)"\)'
Get-ChildItem -Path $SourceDir -Recurse -Include *.cpp,*.h | ForEach-Object {
	$txt = [System.IO.File]::ReadAllText($_.FullName)
	foreach ($m in $rx.Matches($txt)) { [void]$Paths.Add($m.Groups[1].Value) }
}
$rxCsv = [regex]'(/Game/[A-Za-z0-9_\-\./ ]+)'
if (Test-Path $DataDir) {
	Get-ChildItem -Path $DataDir -Filter *.csv | ForEach-Object {
		$txt = [System.IO.File]::ReadAllText($_.FullName)
		# .TrimEnd() - de regex staat spaties toe (paden als "new models") en pikt zo een naloop-spatie
		# aan het regel-eind mee; die zou de manifest-match ("... .uasset") onterecht laten falen.
		foreach ($m in $rxCsv.Matches($txt)) { [void]$Paths.Add($m.Groups[1].Value.TrimEnd()) }
	}
}
# Baked .txt-datafiles (RoomTemplates/RoomStamps/RoomJobs e.d.): string-geladen mesh/material-paden.
# Deze tekst wordt als los UFS-bestand gestaged, maar de gerefereerde uassets cooken NIET vanzelf mee.
$BakedDir = Join-Path $Proj 'Content\BakedData'
if (Test-Path $BakedDir) {
	Get-ChildItem -Path $BakedDir -Recurse -Filter *.txt -ErrorAction SilentlyContinue | ForEach-Object {
		$txt = [System.IO.File]::ReadAllText($_.FullName)
		foreach ($m in $rxCsv.Matches($txt)) { [void]$Paths.Add($m.Groups[1].Value.TrimEnd()) }
	}
}
Write-Host ("{0} unieke /Game-paden gevonden in Source + Data + BakedData" -f $Paths.Count)

# --- 3. Elk pad checken: als asset (.uasset/.umap) OF als map-prefix (dan is 't geen miss) ---
$Missing = @()
foreach ($p in ($Paths | Sort-Object)) {
	$rel = ($p -replace '^/Game/', '').Split('.')[0].TrimEnd('/')
	# expliciet ThePlugSIM\Content\ - anders kan een project-miss per ongeluk tegen Engine\Content\ matchen
	$win = 'ThePlugSIM\Content\' + ($rel -replace '/', '\')
	$isAsset = $ManifestText.Contains($win + '.uasset') -or $ManifestText.Contains($win + '.umap')
	$isDir = $ManifestText.Contains($win + '\')
	# dynamische naam-basis (pad eindigt op '_', code plakt er runtime een naam achter) -> prefix-match volstaat
	$isDynBase = $rel.EndsWith('_') -and $ManifestText.Contains($win)
	if (-not ($isAsset -or $isDir -or $isDynBase)) { $Missing += $p }
}

# --- 4. Loose-file check: runtime-PNG-iconen moeten als losse files in de staged build staan ---
$IconSrc = Join-Path $Proj 'Content\_Project\UI\Icons'
$IconStaged = Join-Path $Proj 'Saved\StagedBuilds\Windows\ThePlugSIM\Content\_Project\UI\Icons'
if (Test-Path $IconSrc) {
	$srcN = (Get-ChildItem $IconSrc -Filter *.png -ErrorAction SilentlyContinue | Measure-Object).Count
	$stgN = 0
	if (Test-Path $IconStaged) { $stgN = (Get-ChildItem $IconStaged -Filter *.png -ErrorAction SilentlyContinue | Measure-Object).Count }
	if ($stgN -lt $srcN) { Write-Host ("LET OP: loose icons {0}/{1} in staged build - upload-build.ps1 kopieert ze pas bij het zippen; alleen een probleem als dit NA upload-build.ps1 nog zo is." -f $stgN, $srcN) -ForegroundColor Yellow }
	else { Write-Host ("Loose icons OK ({0}/{1} in staged build)" -f $stgN, $srcN) }
}

# --- 4b. BakedData-staleness: wereld-geometrie MOET byte-identiek zijn aan Saved/ ---
# De wereld-layout (starter-meubels, competitive-kamers, home-spawns, build-area, no-build-zones,
# meet/mark-spots, delivery-punt, licht-config, kamer-templates) wordt runtime string-geladen uit
# Saved/*.txt. Content/BakedData/ is de snapshot die de packaged build cookt. RestoreAll kopieert
# alleen ONTBREKENDE files (Baked -> Saved), nooit terug -> een editor-fix aan de layout kan stil in
# een STALE BakedData-snapshot blijven hangen en dus niet in de download landen. upload-build.ps1
# resync't Saved -> BakedData vlak voor de cook; deze gate is de vangnet: wijkt een gebakken file af
# van z'n Saved-tegenhanger (beide aanwezig), dan HARD FAIL zodat een stale snapshot niet stil passeert.
# Ontbreekt de Saved-file, dan is er niks te resyncen (die geometrie is niet gezet) -> geen fail.
$GeomFiles = @(
	'StarterFurniture.txt', 'CompSpawns.txt', 'CompDoors.txt', 'HomeSpawn.txt',
	'BuildArea.txt', 'NoBuildZones.txt', 'MeetSpots.txt', 'MarkedSpots.txt',
	'DeliveryPoint.txt', 'LightConfig.txt', 'RoomStamps.txt'
)
$SavedRoot = Join-Path $Proj 'Saved'
$BakedRoot = Join-Path $Proj 'Content\BakedData'
$Stale = @()
# Byte-identiek = zelfde SHA256 van de file-inhoud (los van regeleindes/encoding-nuances: pure bytes).
function Get-FileSha([string]$Path) { return (Get-FileHash -Path $Path -Algorithm SHA256).Hash }
foreach ($gf in $GeomFiles) {
	$sv = Join-Path $SavedRoot $gf
	$bk = Join-Path $BakedRoot $gf
	# Alleen vergelijken als BEIDE bestaan: geen Saved -> niks te resyncen; geen Baked -> valt onder de
	# cook-miss-scan hierboven, niet onder staleness.
	if ((Test-Path $sv) -and (Test-Path $bk)) {
		if ((Get-FileSha $sv) -ne (Get-FileSha $bk)) { $Stale += $gf }
	}
}
# RoomTemplates-map: elke Saved-template moet byte-identiek in BakedData staan (anders stale layout).
$SavedTpl = Join-Path $SavedRoot 'RoomTemplates'
$BakedTpl = Join-Path $BakedRoot 'RoomTemplates'
if (Test-Path $SavedTpl) {
	Get-ChildItem -Path $SavedTpl -Filter *.txt -File -ErrorAction SilentlyContinue | ForEach-Object {
		$bk = Join-Path $BakedTpl $_.Name
		if (Test-Path $bk) {
			if ((Get-FileSha $_.FullName) -ne (Get-FileSha $bk)) { $Stale += ('RoomTemplates\' + $_.Name) }
		} else {
			# Saved-template die (nog) niet gebakken is -> ook stale: de packaged build mist deze kamer.
			$Stale += ('RoomTemplates\' + $_.Name + ' (ontbreekt in BakedData)')
		}
	}
}
if ($Stale.Count -gt 0) {
	Write-Host ''
	Write-Host ('BAKEDDATA STALE - {0} geometrie-file(s) wijken af van Saved/ (packaged build zou de OUDE layout bakken):' -f $Stale.Count) -ForegroundColor Red
	$Stale | ForEach-Object { Write-Host ('  STALE  BakedData stale: {0} wijkt af van Saved -> resync nodig' -f $_) -ForegroundColor Red }
	Write-Host ''
	Write-Host 'Fix: draai de BakedData-resync (zit in upload-build.ps1 vlak voor de cook) en package opnieuw.' -ForegroundColor Yellow
	exit 1
}
Write-Host 'BakedData-parity OK - wereld-geometrie in BakedData is in sync met Saved.' -ForegroundColor Green

# --- 5. Uitslag ---
if ($Missing.Count -gt 0) {
	Write-Host ''
	Write-Host ("COOK-PARITY FAILED - {0} pad(en) NIET in de packaged build:" -f $Missing.Count) -ForegroundColor Red
	$Missing | ForEach-Object { Write-Host ("  MISSING  {0}" -f $_) -ForegroundColor Red }
	Write-Host ''
	Write-Host 'Fix: map toevoegen aan +DirectoriesToAlwaysCook in Config/DefaultGame.ini en opnieuw packagen.' -ForegroundColor Yellow
	exit 1
}
Write-Host ''
Write-Host 'COOK-PARITY OK - alle string-geladen content zit in de packaged build.' -ForegroundColor Green
exit 0

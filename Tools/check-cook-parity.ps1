# check-cook-parity.ps1 - Release-gate: zit alle string-geladen content ECHT in de packaged build?
# Scant de C++-source op TEXT("/Game/...")-literals en checkt elk pad tegen de staging-manifest
# van de laatste UAT-package-run. Vangt de klassieke stille cook-miss (LoadObject/LoadClass-pad
# dat niet onder DirectoriesToAlwaysCook valt -> asset ontbreekt stilletjes in Shipping).
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
		foreach ($m in $rxCsv.Matches($txt)) { [void]$Paths.Add($m.Groups[1].Value) }
	}
}
Write-Host ("{0} unieke /Game-paden gevonden in Source + Data" -f $Paths.Count)

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

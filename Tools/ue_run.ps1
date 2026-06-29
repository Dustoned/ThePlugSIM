param([Parameter(Mandatory=$true)][string]$PyFile)
# Draai een Python-script in de OPEN UnrealClaude-editor (HTTP localhost:3000) en print de output.
$ProgressPreference='SilentlyContinue'
function UC($n,$j){ try{ Invoke-RestMethod -Uri "http://localhost:3000/mcp/tool/$n" -Method Post -Body $j -ContentType 'application/json' -TimeoutSec 90 } catch { [pscustomobject]@{success=$false;data=$null;err=$_.Exception.Message} } }
if (-not (Test-Path $PyFile)) { "PY FILE NOT FOUND: $PyFile"; exit 1 }
$code = Get-Content $PyFile -Raw
$body = @{ script_type='python'; script_content=$code; description=(Split-Path $PyFile -Leaf) } | ConvertTo-Json
$sub = UC execute_script $body
$tid = $sub.data.task_id
if (-not $tid) { "SUBMIT FAIL: " + ($sub | ConvertTo-Json -Depth 6 -Compress); exit 1 }
$status='pending'
for ($i=0; $i -lt 80 -and @('completed','failed','cancelled','timed_out') -notcontains $status; $i++){
  Start-Sleep -Milliseconds 700
  $st = UC task_status "{""task_id"":""$tid""}"
  $status = if ($st.data.status) { $st.data.status } else { $st.status }
}
$res = UC task_result "{""task_id"":""$tid""}"
"STATUS=$status"
$out = $res.data.data.output
if ($out) { "OUTPUT:`n$out" } else { "RESULT:`n" + ($res | ConvertTo-Json -Depth 7 -Compress) }
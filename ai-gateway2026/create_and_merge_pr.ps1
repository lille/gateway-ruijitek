$token = $env:GITHUB_TOKEN
# If token not in current process env (setx was used), read from user environment
if (-not $token) { $token = [Environment]::GetEnvironmentVariable('GITHUB_TOKEN','User') }
if (-not $token) { Write-Output "NO_TOKEN"; exit 1 }
$repo = "lille/gateway-ruijitek"
$body = @{
  title = "Merge feature/ai-gateway2026"
  head = "feature/ai-gateway2026"
  base = "main"
  body = "Automated PR created by assistant."
} | ConvertTo-Json
$headers = @{
  Authorization = "token $token"
  'User-Agent' = "api-client"
}
try {
  $pr = Invoke-RestMethod -Method Post -Uri "https://api.github.com/repos/$repo/pulls" -Headers $headers -Body $body -ContentType "application/json"
  Write-Output ("PR_CREATED: {0} {1}" -f $pr.number, $pr.html_url)
} catch {
  Write-Output ("PR_ERROR: {0}" -f $_.Exception.Message)
  exit 2
}
Start-Sleep -Seconds 1
$mergeBody = @{
  commit_title = "Merge feature/ai-gateway2026"
  commit_message = "Merged by automation"
  merge_method = "merge"
} | ConvertTo-Json
try {
  $merge = Invoke-RestMethod -Method Put -Uri "https://api.github.com/repos/$repo/pulls/$($pr.number)/merge" -Headers $headers -Body $mergeBody -ContentType "application/json"
  Write-Output ("MERGE_RESULT: {0} {1}" -f $merge.merged, $merge.message)
} catch {
  Write-Output ("MERGE_ERROR: {0}" -f $_.Exception.Message)
  exit 3
}

# Ends the demo: deletes the proxy deployment, so every token in every installer
# stops working at once. Afterwards, also revoke the two provider keys in their
# dashboards (Anthropic Console, AssemblyAI) -- the proxy held them.
param([string]$Project = "zephyrus-demo-proxy")
$ErrorActionPreference = "Stop"
& vercel remove $Project --yes
if ($LASTEXITCODE -ne 0) { throw "vercel remove failed" }
Write-Host "Proxy removed. Now revoke the provider keys in their dashboards." -ForegroundColor Green

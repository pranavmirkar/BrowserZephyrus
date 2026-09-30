# One command to make the hackathon build work with nothing entered.
#
#   powershell -ExecutionPolicy Bypass -File setup_demo.ps1            # 14 days
#   powershell -ExecutionPolicy Bypass -File setup_demo.ps1 -Days 7
#
# It asks for your two provider keys in THIS window (typed hidden, never written
# to a file or the command line), puts them in the deployment's environment,
# deploys the proxy, and writes the build's config. The keys leave this machine
# only to Vercel's encrypted environment; the browser never contains them.
param(
  [int]$Days = 14,
  [string]$Project = "zephyrus-demo-proxy",
  [string]$KeysDir = "D:\zephyrus-keys",
  [string]$ArgsGn = "D:\chromium\src\out\Release\args.gn"
)
# NOT "Stop": Windows PowerShell 5.1 turns any text a native program writes to
# stderr (the Vercel CLI prints its progress there) into a terminating error.
# Every native call below checks its own exit code instead.
$ErrorActionPreference = "Continue"
[Console]::OutputEncoding = [Text.Encoding]::UTF8
$proxyDir = Split-Path $PSScriptRoot -Parent

function Step($m) { Write-Host "`n== $m" -ForegroundColor Cyan }
function Need($cmd) {
  if (-not (Get-Command $cmd -ErrorAction SilentlyContinue)) {
    throw "$cmd is not installed or not on PATH."
  }
}
function Plain([System.Security.SecureString]$s) {
  $b = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($s)
  try { [Runtime.InteropServices.Marshal]::PtrToStringBSTR($b) }
  finally { [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($b) }
}
# Sends the value on stdin with NO trailing newline, so a key never gains one.
function Set-VercelEnv($name, $value) {
  $psi = New-Object Diagnostics.ProcessStartInfo
  $psi.FileName = "cmd.exe"
  $psi.Arguments = "/c vercel env add $name production --force --sensitive --yes"
  $psi.WorkingDirectory = $proxyDir
  $psi.RedirectStandardInput = $true
  $psi.RedirectStandardOutput = $true
  $psi.RedirectStandardError = $true
  $psi.UseShellExecute = $false
  $p = [Diagnostics.Process]::Start($psi)
  $p.StandardInput.Write($value)
  $p.StandardInput.Close()
  $p.WaitForExit()
  if ($p.ExitCode -ne 0) { throw "could not set $name : $($p.StandardError.ReadToEnd())" }
}

Step "Checking tools"
Need node
Need vercel
Push-Location $proxyDir
try {
  & vercel whoami
  if ($LASTEXITCODE -ne 0) { throw "Not logged in to Vercel. Run: vercel login   (then run this again)" }

  Step "Provider keys (typed hidden; not stored anywhere on this machine)"
  Write-Host "Use DEDICATED keys for the hackathon, with a monthly spend cap set in each dashboard."
  $anthropic = Plain (Read-Host "Anthropic API key" -AsSecureString)
  $assembly = Plain (Read-Host "AssemblyAI API key" -AsSecureString)
  if ($anthropic.Length -lt 20 -or $assembly.Length -lt 16) { throw "Those do not look like keys." }
  $anthropic = $anthropic.Trim(); $assembly = $assembly.Trim()

  $bytes = New-Object byte[] 32
  [Security.Cryptography.RandomNumberGenerator]::Create().GetBytes($bytes)
  $secret = ($bytes | ForEach-Object { $_.ToString("x2") }) -join ""

  Step "Creating the Vercel project"
  & vercel link --yes --project $Project
  if ($LASTEXITCODE -ne 0) { throw "vercel link failed" }

  Step "Storing keys in the deployment's environment"
  Set-VercelEnv "ANTHROPIC_API_KEY" $anthropic
  Set-VercelEnv "ASSEMBLYAI_API_KEY" $assembly
  Set-VercelEnv "DEMO_TOKEN_SECRET" $secret
  $anthropic = $null; $assembly = $null

  Step "Deploying"
  # Through cmd so its stderr is merged before PowerShell sees it.
  $out = cmd /c "vercel deploy --prod --yes 2>&1" | Out-String
  $deployExit = $LASTEXITCODE
  Write-Host $out
  if ($deployExit -ne 0) { throw "deploy failed (see the output above)" }
  $url = $null
  if ($out -match "Aliased:\s*(https://\S+)") { $url = $Matches[1].TrimEnd("/") }
  if (-not $url) { $url = "https://$Project.vercel.app" }
  Write-Host "Proxy address: $url"

  Step "Minting a token that expires in $Days days"
  $token = (& node "$PSScriptRoot\mint_token.js" $secret $Days | Out-String).Trim()
  if ($token -notmatch '^zd1\.\d+\.[0-9a-f]{40}$') { throw "token was not minted" }

  Step "Checking the deployment answers to that token"
  $ok = $false
  for ($i = 0; $i -lt 6 -and -not $ok; $i++) {
    try {
      $r = Invoke-RestMethod -Uri "$url/api/health" -Headers @{ "x-api-key" = $token } -TimeoutSec 20
      $ok = ($r.ok -eq $true -and $r.anthropic -and $r.assemblyai)
    } catch { Start-Sleep -Seconds 5 }
  }
  if (-not $ok) {
    throw ("The proxy did not answer as expected at $url/api/health. If the page asks " +
           "you to sign in to Vercel, turn OFF 'Vercel Authentication' for this project " +
           "(Project Settings > Deployment Protection), then run this script again.")
  }
  Write-Host "Proxy is live and both provider keys are set." -ForegroundColor Green

  Step "Writing the build config"
  New-Item -ItemType Directory -Force $KeysDir -ErrorAction Stop | Out-Null
  $cfg = Join-Path $KeysDir "demo_keys.json"
  $json = @{ proxy_url = $url; demo_token = $token } | ConvertTo-Json
  # No byte-order mark: Set-Content -Encoding utf8 adds one in Windows PowerShell 5.1.
  [IO.File]::WriteAllText($cfg, $json, (New-Object Text.UTF8Encoding($false)))
  if (Test-Path $ArgsGn) {
    $lines = Get-Content $ArgsGn | Where-Object { $_ -notmatch '^\s*zephyrus_bundled_keys_file' }
    $lines += ('zephyrus_bundled_keys_file = "' + ($cfg -replace '\\', '/') + '"')
    Set-Content -Encoding ascii $ArgsGn $lines -ErrorAction Stop
    Write-Host "Updated $ArgsGn"
  }
  Write-Host "`nDONE. Now rebuild:  autoninja -C out/Release chrome mini_installer" -ForegroundColor Green
  Write-Host "The token expires in $Days days. To end the demo early: scripts\kill_demo.ps1"
} finally {
  Pop-Location
}

param([Parameter(Mandatory=$true)][string]$RequestFile, [string]$OutputFile)
$ErrorActionPreference = 'Stop'
$uri = 'http://127.0.0.1:8000/mcp'
$headers = @{Accept='application/json, text/event-stream'}
$init = '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-11-25","capabilities":{},"clientInfo":{"name":"HQ379","version":"1"}}}'
$response = Invoke-WebRequest -Uri $uri -Method Post -ContentType 'application/json' -Headers $headers -Body $init
$headers['Mcp-Session-Id'] = $response.Headers['Mcp-Session-Id'][0]
$body = Get-Content -LiteralPath $RequestFile -Raw
$response = Invoke-WebRequest -Uri $uri -Method Post -ContentType 'application/json' -Headers $headers -Body ([System.Text.Encoding]::UTF8.GetBytes($body))
if ($OutputFile) { [System.IO.File]::WriteAllText($OutputFile, $response.Content) }
$response.Content

# Regenerate the pinned MIT-licensed SingleStepTests subset. No network at build time.
param(
    [ValidateRange(1,10000)][int]$CasesPerOpcode = 64,
    [ValidateSet('nes6502','6502')][string]$Processor = 'nes6502'
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Net.Http
$revision = '2f6980a2d95757486c7bee24355c360e40e2a224'
$opcodes = @'
00 01 05 06 08 09 0a 0d 0e 10 11 15 16 18 19 1d 1e
20 21 24 25 26 28 29 2a 2c 2d 2e 30 31 35 36 38 39 3d 3e
40 41 45 46 48 49 4a 4c 4d 4e 50 51 55 56 58 59 5d 5e
60 61 65 66 68 69 6a 6c 6d 6e 70 71 75 76 78 79 7d 7e
81 84 85 86 88 8a 8c 8d 8e 90 91 94 95 96 98 99 9a 9d
a0 a1 a2 a4 a5 a6 a8 a9 aa ac ad ae b0 b1 b4 b5 b6 b8 b9 ba bc bd be
c0 c1 c4 c5 c6 c8 c9 ca cc cd ce d0 d1 d5 d6 d8 d9 dd de
e0 e1 e4 e5 e6 e8 e9 ea ec ed ee f0 f1 f5 f6 f8 f9 fd fe
'@ -split '\s+' | Where-Object { $_ }
$fixture = [System.Text.StringBuilder]::new()
[void]$fixture.AppendLine("RSE_NES6502_V1 $($opcodes.Count * $CasesPerOpcode)")
$client = [System.Net.Http.HttpClient]::new()
try {
    foreach ($opcode in $opcodes) {
        $url = "https://raw.githubusercontent.com/SingleStepTests/65x02/$revision/$Processor/v1/$opcode.json"
        $response = $client.GetAsync($url, [System.Net.Http.HttpCompletionOption]::ResponseHeadersRead).GetAwaiter().GetResult()
        try {
            [void]$response.EnsureSuccessStatusCode()
            $reader = [System.IO.StreamReader]::new($response.Content.ReadAsStreamAsync().GetAwaiter().GetResult())
            try {
                $count = 0
                while ($count -lt $CasesPerOpcode) {
                    $line = $reader.ReadLine()
                    if ($null -eq $line) { throw "Too few cases for $opcode" }
                    if (-not $line.TrimStart().StartsWith('{')) { continue }
                    $case = $line.Trim().TrimEnd(',') | ConvertFrom-Json
                    $record = [System.Text.StringBuilder]::new()
                    [void]$record.Append("$opcode-$count ")
                    foreach ($state in @($case.initial, $case.final)) {
                        [void]$record.Append("$($state.pc) $($state.s) $($state.a) $($state.x) $($state.y) $($state.p) $($state.ram.Count) ")
                        foreach ($pair in $state.ram) {
                            [void]$record.Append("$($pair[0]) $($pair[1]) ")
                        }
                    }
                    [void]$record.Append("$($case.cycles.Count) ")
                    foreach ($cycle in $case.cycles) {
                        $writing = if ($cycle[2] -eq 'write') { 1 } else { 0 }
                        [void]$record.Append("$($cycle[0]) $($cycle[1]) $writing ")
                    }
                    [void]$fixture.AppendLine($record.ToString().TrimEnd())
                    ++$count
                }
            } finally { $reader.Dispose() }
        } finally { $response.Dispose() }
        Write-Output "Imported $opcode ($count cases)"
    }
} finally { $client.Dispose() }
$destination = Join-Path $PSScriptRoot "../tests/data/$Processor/vectors.txt"
[System.IO.Directory]::CreateDirectory([System.IO.Path]::GetDirectoryName($destination)) | Out-Null
[System.IO.File]::WriteAllText($destination, $fixture.ToString().Replace("`r`n", "`n"), [System.Text.UTF8Encoding]::new($false))

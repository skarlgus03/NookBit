$ErrorActionPreference = 'Stop'

# PowerShell의 -I경로/-std:값 인자 재해석을 피하고 원래 Windows 명령줄을 보존한다.
$raw = [regex]::Match([Environment]::CommandLine, '(?is)\s-File\s+(?:"[^"]+"|\S+)\s+(.*)$')
$command = [regex]::Match($raw.Groups[1].Value, '^(?:"([^"]+)"|(\S+))\s*(.*)$')
if (!$raw.Success -or !$command.Success) { throw 'Missing compiler command' }
$utf8 = New-Object System.Text.UTF8Encoding($false)
[Console]::OutputEncoding = $utf8
$env:VSLANG = '1033'
$info = New-Object System.Diagnostics.ProcessStartInfo
$info.FileName = if ($command.Groups[1].Success) { $command.Groups[1].Value } else { $command.Groups[2].Value }
$info.Arguments = $command.Groups[3].Value
$info.UseShellExecute = $false
$info.RedirectStandardOutput = $true
$info.StandardOutputEncoding = $utf8
$process = New-Object System.Diagnostics.Process
$process.StartInfo = $info
$koreanPrefix = -join ([char[]](0xCC38, 0xACE0, 0x3A, 0x20, 0xD3EC, 0xD568, 0x20, 0xD30C, 0xC77C, 0x3A))
try {
    [void]$process.Start()
    while ($null -ne ($line = $process.StandardOutput.ReadLine())) {
        # 영어 언어팩이 없으면 MSVC가 한국어로 돌아가므로 ASCII 접두사로 통일한다.
        if ($line.StartsWith($koreanPrefix)) { $line = 'Note: including file:' + $line.Substring($koreanPrefix.Length) }
        [Console]::WriteLine($line)
    }
    $process.WaitForExit()
    $code = $process.ExitCode
} finally { $process.Dispose() }
exit $code

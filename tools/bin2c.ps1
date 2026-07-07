# Convert binary files to C header arrays for embedding
param(
    [string]$InputFile,
    [string]$OutputFile,
    [string]$ArrayName
)

$bytes = [System.IO.File]::ReadAllBytes($InputFile)
$sb = New-Object System.Text.StringBuilder

[void]$sb.AppendLine("#pragma once")
[void]$sb.AppendLine("// Auto-generated from $([System.IO.Path]::GetFileName($InputFile))")
[void]$sb.AppendLine("static const unsigned int ${ArrayName}_len = $($bytes.Length);")
[void]$sb.Append("static const unsigned char ${ArrayName}[] = {")

$lineLength = 0
for ($i = 0; $i -lt $bytes.Length; $i++) {
    if ($i % 16 -eq 0) {
        [void]$sb.Append("`n    ")
        $lineLength = 4
    }
    $byteStr = "0x{0:x2}" -f $bytes[$i]
    if ($i -lt $bytes.Length - 1) { $byteStr += "," }
    [void]$sb.Append($byteStr)
    $lineLength += $byteStr.Length
    if ($lineLength -gt 100 -and $i -lt $bytes.Length - 1) {
        [void]$sb.Append("`n    ")
        $lineLength = 4
    }
}

[void]$sb.AppendLine("`n};")

[System.IO.File]::WriteAllText($OutputFile, $sb.ToString())
Write-Host "Generated: $OutputFile ($($bytes.Length) bytes)"

# Runs on the Windows PC: runs the engine self test, prints its log, then cleans up after it.
param([string]$Slot = 'engine', [int]$TimeoutSeconds = 420)
$base = "C:\dev\slots\$Slot"
$folder = "$base\engine-test"
$exe = "$base\build\nagare.exe"

$process = Start-Process -FilePath $exe -ArgumentList "--engine-test $folder" -PassThru
$handle = $process.Handle
if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
    "TIMEOUT after $TimeoutSeconds s"
    $process.Kill()
    $process.WaitForExit()
}
"exit code: $($process.ExitCode)"
Get-Content "$folder\engine-test.log" -ErrorAction SilentlyContinue

# Remove with data sends files to the Recycle Bin, so take back only what this test put there.
$shell = New-Object -ComObject Shell.Application
$bin = $shell.Namespace(10)
$recycled = 0
foreach ($item in @($bin.Items())) {
    $origin = $bin.GetDetailsOf($item, 1)
    if ($origin -like "$folder*") {
        $recycled++
        Remove-Item -LiteralPath $item.Path -Recurse -Force -ErrorAction SilentlyContinue
    }
}
"recycle bin items from this run: $recycled"
Remove-Item -LiteralPath $folder -Recurse -Force -ErrorAction SilentlyContinue
"left in test folder: $(Test-Path $folder)"

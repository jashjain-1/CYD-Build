param([string]$target)
if (-not $target) {
    $target = ".\.pio\build\native\program.exe"
}
& $target
$exitCode = $LASTEXITCODE
exit $exitCode

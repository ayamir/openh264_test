$debugDir = Join-Path -Path $PSScriptRoot -ChildPath "Debug"
$executeOutCommand = ".\openh264_test.exe 0"
$executeOutdiffCommand = ".\openh264_test.exe 1"
$testBitratesMbps = @(
    "1.5",
    "2.5",
    "5",
    "8",
    "12"
)


Set-Location -Path $debugDir

foreach ($testBitrateMbps in $testBitratesMbps) {
    $outCommand = $executeOutCommand + " " + $testBitrateMbps
    $outdiffCommand = $executeOutdiffCommand + " " + $testBitrateMbps
    Invoke-Expression -Command $outCommand
    Invoke-Expression -Command $outdiffCommand
}
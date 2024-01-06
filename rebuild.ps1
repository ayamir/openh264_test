# Sample PowerShell 7 script

$openh264Folder = "C:\PersonalFiles\Code\MM\webrtc-checkout\src\third_party\openh264\src"
$openh264BuildCommand = "ninja -C builddir"
$openh264InstallCommand = "ninja -C builddir install"
$cmakeBuildCommand = "cmake --build ."
$sourceFolders = @(
    "C:\bin",
    "C:\include",
    "C:\lib"
)
$destFolder = Join-Path -Path $PSScriptRoot -ChildPath "openh264-win64"

# cd to openh264
Set-Location -Path $openh264Folder
# build
Invoke-Expression -Command $openh264BuildCommand
# install
Invoke-Expression -Command $openh264InstallCommand
# copy bin&include&lib
foreach ($sourceFolder in $sourceFolders) {
    Copy-Item -Path $sourceFolder -Destination $destFolder -Recurse -Force -Verbose
}
# cd to pwd
Set-Location $PSScriptRoot
# cmake build .
Invoke-Expression -Command $cmakeBuildCommand
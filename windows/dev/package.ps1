# Runs on the Windows PC, from dev/package.sh: turns a finished build into the zip and the installer.
param(
    [string]$Source = 'C:\dev\nagare\windows',
    [string]$Build = 'C:\dev\nagare-build',
    [string]$Legal = 'C:\dev\nagare',
    [string]$Out = 'C:\dev\nagare\windows\dist',
    [string]$Compiler = 'C:\dev\tools\inno\ISCC.exe',
    # A folder of search sources to seed on a new install, for a build shared privately. Public builds pass nothing.
    [string]$Sources = '')
$ErrorActionPreference = 'Stop'

$header = Get-Content "$Source\resources\version.h" -Raw
if ($header -notmatch 'NAGARE_VERSION_TEXT "([^"]+)"') { throw 'No version in resources/version.h' }
$version = $Matches[1]
$name = "Nagare-$version-win-x64"

# The payload is everything a user gets: the executable and what the licences require to travel with it.
$payload = Join-Path $Out $name
Remove-Item -Recurse -Force $Out -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force "$payload\licenses" | Out-Null
Copy-Item "$Build\nagare.exe" $payload
Copy-Item "$Legal\COPYING" $payload
Copy-Item "$Legal\licenses\*" "$payload\licenses"
Copy-Item "$Source\assets\fonts\Outfit-OFL.txt" "$payload\licenses"

# bsdtar ships with Windows, and unlike Compress-Archive it writes portable paths.
& "$env:SystemRoot\System32\tar.exe" -a -c -f "$Out\$name.zip" -C $Out $name
if ($LASTEXITCODE -ne 0) { throw 'tar failed' }

$defines = @("/DAppVersion=$version", "/DPayloadDir=$payload", "/DOutputDir=$Out")
if ($Sources) { $defines += "/DSourcesDir=$Sources" }
& $Compiler /Q $defines "$Source\installer\nagare.iss"
if ($LASTEXITCODE -ne 0) { throw 'ISCC failed' }

Remove-Item -Recurse -Force $payload
Get-ChildItem $Out | ForEach-Object {
    '{0}  {1:N2} MB  {2}' -f (Get-FileHash $_.FullName -Algorithm SHA256).Hash.ToLower(), ($_.Length / 1MB), $_.Name
}

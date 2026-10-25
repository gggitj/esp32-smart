param(
    [string]$IdfPath = 'S:\esp\esp-idf',
    [string]$ToolsPath = 'S:\esp\.espressif',
    [string]$StageDirectory = ''
)

$ErrorActionPreference = 'Stop'
$projectDirectory = $PSScriptRoot
if (!$StageDirectory) {
    $StageDirectory = Join-Path ([IO.Path]::GetTempPath()) ('pibridge-nfc-' + [guid]::NewGuid().ToString('N').Substring(0, 12))
}
$stagePath = [IO.Path]::GetFullPath($StageDirectory)
if ($stagePath -match '[^\x00-\x7f]') { throw 'ESP-IDF build staging path must contain ASCII characters only.' }
New-Item -ItemType Directory -Path $stagePath -Force | Out-Null
function Copy-StagedFile([string]$relativePath) {
    $sourceFile = Join-Path $projectDirectory $relativePath
    $stagedFile = Join-Path $stagePath $relativePath
    if ((Test-Path -LiteralPath $stagedFile) -and
        (Get-FileHash -LiteralPath $sourceFile).Hash -eq (Get-FileHash -LiteralPath $stagedFile).Hash) { return }
    New-Item -ItemType Directory -Path (Split-Path -Parent $stagedFile) -Force | Out-Null
    Copy-Item -LiteralPath $sourceFile -Destination $stagedFile -Force
    # Copy-Item preserves old timestamps, which could hide changes from Ninja.
    (Get-Item -LiteralPath $stagedFile).LastWriteTimeUtc = [DateTime]::UtcNow
}
foreach ($entry in @('CMakeLists.txt', 'sdkconfig.defaults')) { Copy-StagedFile $entry }
# sdkconfig is git-ignored, so a fresh checkout does not have it; idf.py then
# generates the configuration from sdkconfig.defaults on first build. Copy and
# touch it only when it exists, so a locally tuned config is still honored.
if (Test-Path -LiteralPath (Join-Path $projectDirectory 'sdkconfig')) {
    Copy-StagedFile 'sdkconfig'
    # Regenerate the full config so the exported sdkconfig matches this build.
    (Get-Item -LiteralPath (Join-Path $stagePath 'sdkconfig')).LastWriteTimeUtc = [DateTime]::UtcNow
}
foreach ($directory in @('main', 'components')) {
    Get-ChildItem -LiteralPath (Join-Path $projectDirectory $directory) -Recurse -File | ForEach-Object {
        # [IO.Path]::GetRelativePath needs .NET Core 2.0+; stay compatible with
        # both Windows PowerShell 5.1 and PowerShell 7 by trimming the prefix.
        Copy-StagedFile ($_.FullName.Substring($projectDirectory.Length + 1))
    }
}

$pythonEnv = Join-Path $ToolsPath 'python_env\idf5.5_py3.10_env'
$pythonExe = Join-Path $pythonEnv 'Scripts\python.exe'
$ninjaDirectory = Join-Path $ToolsPath 'tools\ninja\1.12.1'
$compilerDirectory = Join-Path $ToolsPath 'tools\xtensa-esp-elf\esp-14.2.0_20260121\xtensa-esp-elf\bin'
$cmakeDirectory = Join-Path $ToolsPath 'tools\cmake\3.30.2\bin'
$env:IDF_PATH = $IdfPath
$env:IDF_TOOLS_PATH = $ToolsPath
$env:IDF_PYTHON_ENV_PATH = $pythonEnv
$env:IDF_CCACHE_ENABLE = '0'
$env:IDF_COMPONENT_MANAGER = '0'
$env:PYTHONUTF8 = '1'
$romElfDirectory = Join-Path $ToolsPath 'tools\esp-rom-elfs\20241011'
if (Test-Path -LiteralPath $romElfDirectory) { $env:ESP_ROM_ELF_DIR = $romElfDirectory }
$env:CMAKE_PROGRAM_PATH = "$ninjaDirectory;$compilerDirectory"
$env:PATH = "$pythonEnv\Scripts;$cmakeDirectory;$ninjaDirectory;$compilerDirectory;" + $env:PATH
# Git Bash / MSYS shells leak MSYSTEM into child processes; idf.py refuses or
# warns under MSYS even though this build uses the Windows toolchain.
if (Test-Path Env:MSYSTEM) { Remove-Item Env:MSYSTEM }

$verificationDirectory = Join-Path $projectDirectory 'verification'
New-Item -ItemType Directory -Path $verificationDirectory -Force | Out-Null
Set-Content -LiteralPath (Join-Path $verificationDirectory 'build-stage.txt') -Value $stagePath -Encoding utf8
Write-Output "Build staging directory: $stagePath"
Push-Location -LiteralPath $stagePath
try {
    # Windows PowerShell 5.1 turns native stderr lines into terminating errors
    # when $ErrorActionPreference is 'Stop'; idf.py writes benign warnings to
    # stderr, so judge success by the exit code instead.
    $ErrorActionPreference = 'Continue'
    & $pythonExe (Join-Path $IdfPath 'tools\idf.py') "-DCMAKE_MAKE_PROGRAM=$ninjaDirectory\ninja.exe" build 2>&1 |
        Tee-Object -FilePath (Join-Path $verificationDirectory 'build.log')
    $ErrorActionPreference = 'Stop'
    if ($LASTEXITCODE -ne 0) { throw "ESP-IDF build failed: exit $LASTEXITCODE; staging retained at $stagePath" }
} finally {
    Pop-Location
}

$finalConfig = Get-Content -LiteralPath (Join-Path $stagePath 'sdkconfig') -Raw
if ($finalConfig -notmatch '(?m)^(CONFIG_BRIDGE_RFID_ENABLED=y|# CONFIG_BRIDGE_RFID_ENABLED is not set)\r?$') {
    throw 'Final sdkconfig is missing the reader options: artifacts were not exported.'
}
if ($finalConfig -notmatch '(?m)^CONFIG_BOOT_ROM_LOG_ALWAYS_ON=y\r?$' -or
    $finalConfig -match '(?m)^CONFIG_BOOT_ROM_LOG_(ALWAYS_OFF|ON_GPIO_HIGH|ON_GPIO_LOW)=y\r?$') {
    throw 'Unsafe ROM log scheme in final sdkconfig: artifacts were not exported.'
}
if ($finalConfig -notmatch '(?m)^CONFIG_ESP_CONSOLE_NONE=y\r?$' -or
    $finalConfig -notmatch '(?m)^CONFIG_ESP_CONSOLE_SECONDARY_NONE=y\r?$') {
    throw 'Console would share the binary bridge channel: artifacts were not exported.'
}

$artifactDirectory = Join-Path $projectDirectory 'artifacts'
New-Item -ItemType Directory -Path $artifactDirectory -Force | Out-Null
$exports = @{
    'build\s3_pogo_host.bin' = 's3_pogo_host.bin'
    'build\s3_pogo_host.elf' = 's3_pogo_host.elf'
    'build\bootloader\bootloader.bin' = 'bootloader.bin'
    'build\partition_table\partition-table.bin' = 'partition-table.bin'
    'sdkconfig' = 'sdkconfig.verified'
    'build\flasher_args.json' = 'flasher_args.json'
}
foreach ($entry in $exports.GetEnumerator()) {
    Copy-Item -LiteralPath (Join-Path $stagePath $entry.Key) -Destination (Join-Path $artifactDirectory $entry.Value) -Force
}
Copy-Item -LiteralPath (Join-Path $stagePath 'sdkconfig') -Destination (Join-Path $projectDirectory 'sdkconfig') -Force
$flashArgsPath = Join-Path $artifactDirectory 'flasher_args.json'
$flashArgs = Get-Content -LiteralPath $flashArgsPath -Raw | ConvertFrom-Json
foreach ($file in $flashArgs.flash_files.PSObject.Properties) { $file.Value = Split-Path -Leaf $file.Value }
foreach ($section in @('bootloader', 'app', 'partition-table')) {
    $flashArgs.$section.file = Split-Path -Leaf $flashArgs.$section.file
}
foreach ($file in $flashArgs.flash_files.PSObject.Properties) {
    if (!(Test-Path -LiteralPath (Join-Path $artifactDirectory $file.Value))) {
        throw "Exported flash file missing: $($file.Value)"
    }
}
$flashArgs | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $flashArgsPath -Encoding utf8
Get-ChildItem -LiteralPath $artifactDirectory -Filter '*.bin' |
    Get-FileHash -Algorithm SHA256 |
    Select-Object Hash, @{Name='File'; Expression={Split-Path -Leaf $_.Path}} |
    ConvertTo-Json | Set-Content -LiteralPath (Join-Path $artifactDirectory 'sha256.json') -Encoding utf8
Write-Output "Verified build exported to: $artifactDirectory"

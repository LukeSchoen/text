param(
    [ValidateSet('Tests', 'Pedantic', 'UserSim', 'UserSimPedantic', 'Core')]
    [string]$Suite = 'Tests',
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$TestArgs
)
$ErrorActionPreference = 'Stop'
$repo = $PSScriptRoot
$build = Join-Path $repo '.build'
New-Item -ItemType Directory -Force $build | Out-Null
$buildOnly = $TestArgs -contains '--build-only'
$TestArgs = @($TestArgs | Where-Object { $_ -ne '--build-only' })
$libs = @('-luser32', '-lgdi32', '-lcomdlg32', '-lshell32', '-luxtheme', '-ldwmapi', '-lmsimg32')

function Build-Test([string]$Name, [string[]]$Sources, [string[]]$Libraries, [string[]]$Dependencies) {
    $output = Join-Path $build "$Name.exe"
    $inputs = @($Sources) + @($Dependencies) + @('cpc.exe', 'test_runner.ps1')
    $rebuild = !(Test-Path -LiteralPath $output)
    if (!$rebuild) {
        $stamp = (Get-Item -LiteralPath $output).LastWriteTimeUtc
        foreach ($inputFile in $inputs) {
            if ((Get-Item -LiteralPath (Join-Path $repo $inputFile)).LastWriteTimeUtc -gt $stamp) {
                $rebuild = $true
                break
            }
        }
    }
    if ($rebuild) {
        $timer = [Diagnostics.Stopwatch]::StartNew()
        $sourcePaths = @($Sources | ForEach-Object { Join-Path $repo $_ })
        & (Join-Path $repo 'cpc.exe') -o $output @sourcePaths @Libraries | Out-Host
        if ($LASTEXITCODE) { throw "Build failed: $Name ($LASTEXITCODE)" }
        Write-Host ('BUILD {0} ({1:N1} ms)' -f $Name, $timer.Elapsed.TotalMilliseconds)
    }
    return $output
}

Push-Location $repo
try {
    $coreDeps = @('core/core.h')
    $appDeps = $coreDeps + @('main.c', 'repro_logger.c', 'repro_logger.h')
    if ($Suite -eq 'Core') {
        $exe = Build-Test 'core_tests' @('core/core_tests.c', 'core/core.c') @() $coreDeps
    } elseif ($Suite -like 'UserSim*') {
        $app = Build-Test 'text_under_test' @('main.c', 'core/core.c') $libs $appDeps
        $exe = Build-Test 'text_user_sim_tests' @('text_blackbox_tests.c') @('-luser32', '-lgdi32') @()
        Copy-Item -LiteralPath (Join-Path $repo 'doCommands.txt') -Destination $build -Force
        $look = Join-Path $build 'look'
        New-Item -ItemType Directory -Force $look | Out-Null
        $env:TEXT_LOOK_DIR = $look
        $TestArgs = @($app) + @($TestArgs)
    } else {
        $exe = Build-Test 'text_tests' @('text_tests.c', 'core/core.c') $libs $appDeps
    }
    if ($Suite -in @('Pedantic', 'UserSimPedantic')) { $TestArgs += '--pedantic' }
    if ($buildOnly) { exit 0 }
    & $exe @TestArgs
    exit $LASTEXITCODE
} finally {
    Pop-Location
}

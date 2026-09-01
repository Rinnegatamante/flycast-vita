[CmdletBinding()]
param(
    [string]$Target = $env:PSP2_TARGET,
    [string]$BuildDir = 'build-vita',
    [string]$ProcessName = 'FLYCASTDC',
    [string]$RunName = 'game-loop',
    [ValidateRange(1, 1000)]
    [int]$Iterations = 1,
    [ValidateRange(10, 1800)]
    [int]$Timeout = 120,
    [ValidateRange(0, 1800)]
    [int]$ObservationSeconds = 0,
    [string]$InputRecording,
    [string]$InputStartPattern,
    [ValidateRange(0, 600)]
    [int]$InputDelaySeconds = 0,
    [string]$CompletionPattern,
    [ValidateRange(0, 120)]
    [int]$PostCompletionSeconds = 5,
    [switch]$Screenshot,
    [switch]$NoDebug
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Invoke-Ctrl {
    param([string[]]$Arguments)
    & $script:psp2ctrl @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "psp2ctrl failed ($LASTEXITCODE): $($Arguments -join ' ')"
    }
}

function Save-Screenshot {
    param([string]$Path, [string]$TargetId)
    for ($attempt = 1; $attempt -le 3; ++$attempt) {
        & $script:psp2ctrl screenshot app $Path $TargetId
        if ($LASTEXITCODE -eq 0) {
            return $true
        }
        Start-Sleep -Milliseconds 250
    }
    # Target Manager occasionally returns a transient GDI+ error here.  A
    # missing diagnostic image must not skip the subsequent target cleanup.
    Write-Warning "Target screenshot failed after 3 attempts: $Path"
    return $false
}

function Resolve-FromRoot {
    param([string]$Path, [string]$Root)
    if ([IO.Path]::IsPathRooted($Path)) {
        return [IO.Path]::GetFullPath($Path)
    }
    return [IO.Path]::GetFullPath((Join-Path $Root $Path))
}

if ([string]::IsNullOrWhiteSpace($Target)) {
    throw 'Specify -Target or set PSP2_TARGET to the DevKit id.'
}
if ($RunName -notmatch '^[A-Za-z0-9._-]+$') {
    throw 'RunName may contain only letters, digits, dot, underscore and dash.'
}

$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$resolvedBuildDir = Resolve-FromRoot -Path $BuildDir -Root $root
$eboot = Join-Path $resolvedBuildDir 'eboot.bin'
if (-not (Test-Path -LiteralPath $eboot -PathType Leaf)) {
    throw "Missing $eboot. Build the Vita target first."
}

$resolvedInput = $null
if (-not [string]::IsNullOrWhiteSpace($InputRecording)) {
    $resolvedInput = Resolve-FromRoot -Path $InputRecording -Root $root
    if (-not (Test-Path -LiteralPath $resolvedInput -PathType Leaf)) {
        throw "Missing Target Manager input recording: $resolvedInput"
    }
}

$sceRoot = $env:SCE_ROOT_DIR
if (-not $sceRoot) {
    $sceRoot = 'C:\Program Files (x86)\SCE'
}
$tmBin = Join-Path $sceRoot 'PSP2\Tools\Target Manager Server\bin'
$script:psp2ctrl = Join-Path $tmBin 'psp2ctrl.exe'
$psp2run = Join-Path $tmBin 'psp2run.exe'
if (-not (Test-Path -LiteralPath $script:psp2ctrl) -or
    -not (Test-Path -LiteralPath $psp2run)) {
    throw "Target Manager tools not found under $tmBin"
}

$logRoot = Join-Path $resolvedBuildDir "devkit-logs\$RunName"
New-Item -ItemType Directory -Path $logRoot -Force | Out-Null
$sessionRoot = Join-Path $logRoot ([DateTime]::Now.ToString('yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $sessionRoot -Force | Out-Null

& $script:psp2ctrl force-disconnect $Target | Out-Null
Invoke-Ctrl -Arguments @('connect', $Target)

try {
    for ($iteration = 1; $iteration -le $Iterations; ++$iteration) {
        $runDir = Join-Path $sessionRoot ('run-{0:D3}' -f $iteration)
        New-Item -ItemType Directory -Path $runDir -Force | Out-Null
        $xmlLog = Join-Path $runDir 'run.xml'
        $consoleLog = Join-Path $runDir 'console.txt'
        $errorLog = Join-Path $runDir 'psp2run.stderr.txt'

        $runArgs = @(
            "/t:$Target",
            '/console:all',
            "/log:$xmlLog",
            '/logFormat:xml'
        )
        if (-not $NoDebug) {
            $runArgs += '/debug'
            $runArgs += '/kill'
        }
        $runArgs += @(
            "/y:$Timeout",
            '/elf',
            $eboot
        )

        Write-Host "[$RunName] Run $iteration/$Iterations"
        & $script:psp2ctrl pkill $ProcessName $Target 2>$null | Out-Null
        $run = Start-Process -FilePath $psp2run -ArgumentList $runArgs `
            -RedirectStandardOutput $consoleLog -RedirectStandardError $errorLog `
            -PassThru -WindowStyle Hidden

        $started = [DateTime]::UtcNow
        $deadline = $started.AddSeconds($Timeout + 15)
        $inputStarted = $false
        $inputProcess = $null
        $completionObserved = $false
        $fixedWindowComplete = $false
        $observationDeadline = $null

        while (-not $run.HasExited -and [DateTime]::UtcNow -lt $deadline) {
            $console = ''
            if (Test-Path -LiteralPath $consoleLog) {
                $console = Get-Content -LiteralPath $consoleLog -Raw -ErrorAction SilentlyContinue
            }

            if ($null -ne $resolvedInput -and -not $inputStarted) {
                $delayReached = [DateTime]::UtcNow -ge $started.AddSeconds($InputDelaySeconds)
                $triggerReached = [string]::IsNullOrWhiteSpace($InputStartPattern) -or
                    $console -match $InputStartPattern
                if ($delayReached -and $triggerReached) {
                    Write-Host "[$RunName] Starting Target Manager input playback."
                    $inputStdout = Join-Path $runDir 'input-playback.txt'
                    $inputStderr = Join-Path $runDir 'input-playback.stderr.txt'
                    $inputProcess = Start-Process -FilePath $script:psp2ctrl `
                        -ArgumentList @('playback', $Target) `
                        -RedirectStandardInput $resolvedInput `
                        -RedirectStandardOutput $inputStdout `
                        -RedirectStandardError $inputStderr `
                        -PassThru -WindowStyle Hidden
                    $inputStarted = $true
                }
            }

            if (-not $completionObserved -and
                -not [string]::IsNullOrWhiteSpace($CompletionPattern) -and
                $console -match $CompletionPattern) {
                $completionObserved = $true
                $observationDeadline = [DateTime]::UtcNow.AddSeconds($PostCompletionSeconds)
                Write-Host "[$RunName] Completion marker observed."
            }

            if (-not $fixedWindowComplete -and $ObservationSeconds -gt 0 -and
                [DateTime]::UtcNow -ge $started.AddSeconds($ObservationSeconds)) {
                $fixedWindowComplete = $true
                if ($Screenshot) {
                    Save-Screenshot -Path (Join-Path $runDir 'final.bmp') -TargetId $Target | Out-Null
                }
                Invoke-Ctrl -Arguments @('pkill', $ProcessName, $Target)
                Write-Host "[$RunName] Fixed observation window completed."
            }

            if ($null -ne $observationDeadline -and
                [DateTime]::UtcNow -ge $observationDeadline) {
                if ($Screenshot) {
                    Save-Screenshot -Path (Join-Path $runDir 'final.bmp') -TargetId $Target | Out-Null
                }
                Invoke-Ctrl -Arguments @('pkill', $ProcessName, $Target)
                $observationDeadline = $null
            }

            Start-Sleep -Milliseconds 100
            $run.Refresh()
        }

        if (-not $run.HasExited) {
            if ($Screenshot) {
                Save-Screenshot -Path (Join-Path $runDir 'timeout.bmp') -TargetId $Target | Out-Null
            }
            & $script:psp2ctrl pkill $ProcessName $Target 2>$null | Out-Null
            Stop-Process -Id $run.Id -ErrorAction SilentlyContinue
            throw "psp2run exceeded the ${Timeout}s target timeout."
        }
        $run.WaitForExit()
        if ($null -ne $inputProcess -and -not $inputProcess.HasExited) {
            Stop-Process -Id $inputProcess.Id -ErrorAction SilentlyContinue
        }

        $reason = 'unknown'
        if (Test-Path -LiteralPath $xmlLog) {
            # Failed target loads can leave an empty XML file. Keep the loader
            # error visible instead of replacing it with a null regex error.
            $rawXml = [string](Get-Content -LiteralPath $xmlLog -Raw -ErrorAction SilentlyContinue)
            $matches = [regex]::Matches(
                $rawXml,
                'ProcessExit|ProcessKilled|StopNotification|AbortString|TargetError|Timeout')
            if ($matches.Count -gt 0) {
                $reason = $matches[$matches.Count - 1].Value
            }
        }
        Write-Host ("[$RunName] Completed: psp2run={0}, reason={1}, input={2}, marker={3}, window={4}" -f `
                $run.ExitCode, $reason, $inputStarted, $completionObserved, $fixedWindowComplete)
        Write-Host "[$RunName] Artifacts: $runDir"
        if ($reason -in @('StopNotification', 'AbortString', 'TargetError') -and -not $fixedWindowComplete) {
            throw "Target stopped before the observation completed ($reason); inspect $xmlLog"
        }
        if (Test-Path -LiteralPath $errorLog) {
            $loaderError = [string](Get-Content -LiteralPath $errorLog -Raw)
            if ($loaderError -match 'Failed to load|Invalid system file') {
                throw "DevKit load failed; inspect $errorLog"
            }
        }
    }
} finally {
    & $script:psp2ctrl disconnect $Target | Out-Null
}

# Test de bout en bout du mode fenetre :
#   1. spout_stream attend une fenetre qui n'existe pas encore
#   2. la fenetre (rouge, zone client 640x360) s'ouvre -> le flux Spout la diffuse (--client)
#   3. la fenetre se ferme -> le flux passe au noir
#   4. la fenetre se rouvre -> le flux reprend
# Code de sortie : 0 = succes, 1 = echec, 77 = test ignore (capture indisponible sur cette machine)
param(
    [Parameter(Mandatory)][string]$Exe,
    [Parameter(Mandatory)][string]$Probe
)

$ErrorActionPreference = 'Stop'
$id = Get-Random
$title = "SpoutE2E_$id"
$sender = "E2E_$id"
$log = Join-Path $env:TEMP "spout_stream_e2e_$id.log"
$formScript = Join-Path $env:TEMP "spout_stream_e2e_form_$id.ps1"
$stream = $null
$form = $null

# Fenetre de test DPI-aware : la zone client fait 640x360 pixels physiques quel que soit le zoom
@"
Add-Type -AssemblyName System.Windows.Forms
Add-Type -Namespace Native -Name Dpi -MemberDefinition '[DllImport("user32.dll")] public static extern bool SetProcessDPIAware();'
[Native.Dpi]::SetProcessDPIAware() | Out-Null
`$f = New-Object System.Windows.Forms.Form
`$f.Text = '$title'
`$f.ClientSize = New-Object System.Drawing.Size(640, 360)
`$f.BackColor = [System.Drawing.Color]::Red
`$f.StartPosition = 'Manual'
`$f.Location = New-Object System.Drawing.Point(50, 50)
`$f.TopMost = `$true
[System.Windows.Forms.Application]::Run(`$f)
"@ | Set-Content -Encoding ascii $formScript

function Open-TestWindow {
    Start-Process powershell -ArgumentList '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $formScript -PassThru
}

function Wait-Log([string]$pattern, [int]$count = 1, [int]$timeoutSec = 15) {
    $deadline = (Get-Date).AddSeconds($timeoutSec)
    while ((Get-Date) -lt $deadline) {
        $content = if (Test-Path $log) { Get-Content $log -Raw } else { '' }
        if (([regex]::Matches("$content", $pattern)).Count -ge $count) { return }
        if ($stream.HasExited) { throw "spout_stream s'est arrete :`n$content" }
        Start-Sleep -Milliseconds 200
    }
    throw "Delai depasse en attendant '$pattern' (x$count). Journal :`n$(Get-Content $log -Raw)"
}

function Test-Stream([string]$step, [string]$expectedSize, [string]$expectedBgra) {
    $out = & $Probe $sender 2
    Write-Host "[$step] $out"
    if ($out -notmatch 'connected=1') { throw "[$step] recepteur non connecte" }
    if ($out -notmatch "size=$expectedSize ") { throw "[$step] taille attendue $expectedSize" }
    if ($out -notmatch "center_bgra=$expectedBgra") { throw "[$step] couleur attendue $expectedBgra" }
}

try {
    $stream = Start-Process $Exe -ArgumentList '--window', $title, '--exact', '--client', '--name', $sender `
        -PassThru -NoNewWindow -RedirectStandardOutput $log
    Start-Sleep -Milliseconds 1500
    if ($stream.HasExited) {
        $content = Get-Content $log -Raw
        if ($content -match "n'est pas disponible") {
            Write-Host "Windows.Graphics.Capture indisponible : test ignore."
            exit 77
        }
        throw "spout_stream s'est arrete :`n$content"
    }
    Wait-Log 'En attente de la fenetre'

    $form = Open-TestWindow
    Wait-Log 'Capture de la fenetre'
    Start-Sleep -Milliseconds 500
    Test-Stream 'ouverture' '640x360' '0,0,255,255'

    Stop-Process $form -Force
    Wait-Log 'Source perdue'
    Test-Stream 'fermeture' '640x360' '0,0,0,255'

    $form = Open-TestWindow
    Wait-Log 'Capture de la fenetre' 2
    Start-Sleep -Milliseconds 500
    Test-Stream 'reouverture' '640x360' '0,0,255,255'

    Write-Host 'OK'
    exit 0
}
catch {
    Write-Host "ECHEC : $_"
    exit 1
}
finally {
    if ($form -and -not $form.HasExited) { Stop-Process $form -Force -ErrorAction SilentlyContinue }
    if ($stream -and -not $stream.HasExited) { Stop-Process $stream -Force -ErrorAction SilentlyContinue }
    Remove-Item $formScript, $log -ErrorAction SilentlyContinue
}

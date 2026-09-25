# Test de bout en bout du mode ecran : le flux Spout de l'ecran principal est recu
# avec une taille non nulle.
# Code de sortie : 0 = succes, 1 = echec, 77 = test ignore (capture indisponible sur cette machine)
param(
    [Parameter(Mandatory)][string]$Exe,
    [Parameter(Mandatory)][string]$Probe
)

$ErrorActionPreference = 'Stop'
$id = Get-Random
$sender = "E2E_screen_$id"
$log = Join-Path $env:TEMP "spout_stream_e2e_screen_$id.log"
$stream = $null

try {
    $stream = Start-Process $Exe -ArgumentList '--screen', '--name', $sender -PassThru -NoNewWindow -RedirectStandardOutput $log
    Start-Sleep -Milliseconds 1500
    $content = Get-Content $log -Raw
    if ($stream.HasExited) {
        if ($content -match "n'est pas disponible") {
            Write-Host "Windows.Graphics.Capture indisponible : test ignore."
            exit 77
        }
        throw "spout_stream s'est arrete :`n$content"
    }
    if ($content -notmatch "Capture de l'ecran (\d+)x(\d+)") { throw "Capture non demarree :`n$content" }
    $expected = "$($Matches[1])x$($Matches[2])"

    $out = & $Probe $sender 2
    Write-Host $out
    if ($out -notmatch 'connected=1') { throw 'recepteur non connecte' }
    if ($out -notmatch "size=$expected ") { throw "taille attendue $expected" }

    Write-Host 'OK'
    exit 0
}
catch {
    Write-Host "ECHEC : $_"
    exit 1
}
finally {
    if ($stream -and -not $stream.HasExited) { Stop-Process $stream -Force -ErrorAction SilentlyContinue }
    Remove-Item $log -ErrorAction SilentlyContinue
}

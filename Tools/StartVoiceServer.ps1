<#
Local/LAN development launcher. Never put the Vivox signing key in Git, Unity,
command-line arguments, chat, screenshots, or a client build.

Supply the four values from YOUR Vivox project (do not enable client Test Mode).
The secure prompt passes the key only to the server process environment.
This script does not activate a service, register billing, or change account data.

Example (use the directory containing your existing server CSV files):
  ./Tools/StartVoiceServer.ps1 -ServerExe 'C:/build/ProjectVectorServer.exe' -DataDirectory 'C:/server-data'

Protocol: CS 380 (73 bytes), SC 381 (2730 bytes); existing packets unchanged.
Without valid configuration the optional voice feature stays unavailable; game
login/matching continue. Restart the server after changing these four variables.

Policy: lobby party; normal/ranked own team (including dead teammates); no private
room/observer channel. Match results leave team voice; lobby re-joins party voice.
Membership polls every second; stale local authorization mutes after four seconds.
Roster changes rotate channel names. Tokens expire after 60 seconds, but token
expiry alone DOES NOT evict already joined sessions. Normal clients migrate to
the new channel; this is not a Vivox moderator-kick implementation.

The existing game TCP connection is NOT TLS. Bearer token delivery requires a
trusted LAN/VPN for development. Before public Internet deployment, add an
authenticated TLS/HTTPS token transport (do not send these tokens over plaintext).

Verification completed by Tests/VoiceIntegration.py is TCP/policy/crypto only.
Two real PCs/headsets must still verify PTT press/release, automatic VAD, mute,
devices/hotplug, denied microphone access, enemy isolation, reconnect and all
scene transitions. SDK compilation/initialization does not prove working audio.
#>
param(
    [Parameter(Mandatory=$true)][string]$ServerExe,
    [Parameter(Mandatory=$true)][string]$DataDirectory
)
$ErrorActionPreference = 'Stop'
$taskServer = (Resolve-Path -LiteralPath $ServerExe).Path
$taskData = (Resolve-Path -LiteralPath $DataDirectory).Path
if (Get-NetTCPConnection -LocalPort 9000 -State Listen -ErrorAction SilentlyContinue) {
    throw 'Port 9000 is already in use. Do not start a second game server.'
}
$taskNames = @('VECTOR_VIVOX_SERVER','VECTOR_VIVOX_DOMAIN','VECTOR_VIVOX_ISSUER','VECTOR_VIVOX_SIGNING_KEY')
$taskPrevious = @{}
foreach ($taskName in $taskNames) { $taskPrevious[$taskName] = [Environment]::GetEnvironmentVariable($taskName,'Process') }
$taskSecret = $null
$taskPointer = [IntPtr]::Zero
try {
    [Environment]::SetEnvironmentVariable($taskNames[0], (Read-Host 'Vivox HTTPS server URL'), 'Process')
    [Environment]::SetEnvironmentVariable($taskNames[1], (Read-Host 'Vivox domain'), 'Process')
    [Environment]::SetEnvironmentVariable($taskNames[2], (Read-Host 'Vivox token issuer'), 'Process')
    $taskSecret = Read-Host 'Vivox signing key (hidden; server only)' -AsSecureString
    $taskPointer = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($taskSecret)
    [Environment]::SetEnvironmentVariable($taskNames[3], [Runtime.InteropServices.Marshal]::PtrToStringBSTR($taskPointer), 'Process')
    $taskProcess = Start-Process -FilePath $taskServer -WorkingDirectory $taskData -WindowStyle Hidden -PassThru
    Write-Output "Game server started (PID $($taskProcess.Id)); signing key was not written to disk."
}
finally {
    if ($taskPointer -ne [IntPtr]::Zero) { [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($taskPointer) }
    if ($taskSecret) { $taskSecret.Dispose() }
    foreach ($taskName in $taskNames) { [Environment]::SetEnvironmentVariable($taskName,$taskPrevious[$taskName],'Process') }
}

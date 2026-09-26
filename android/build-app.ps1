$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
# Android Studio / JDK 17 und Android SDK 35 zuvor installieren.
if (-not (Get-Command java -ErrorAction SilentlyContinue)) {
    $jbr = Join-Path $env:ProgramFiles 'Android\Android Studio\jbr'
    if (Test-Path (Join-Path $jbr 'bin\java.exe')) { $env:JAVA_HOME = $jbr; $env:PATH = (Join-Path $jbr 'bin') + ';' + $env:PATH }
    else { throw 'JDK 17 installieren und JAVA_HOME/PATH setzen.' }
}
if (-not $env:ANDROID_HOME -and -not $env:ANDROID_SDK_ROOT) {
    $sdk = Join-Path $env:LOCALAPPDATA 'Android\Sdk'
    if (Test-Path $sdk) { $env:ANDROID_HOME = $sdk }
    else { throw 'Android SDK in Android Studio installieren oder ANDROID_HOME setzen.' }
}
$cache = Join-Path $env:LOCALAPPDATA 'CrocosaufBuild'
New-Item -ItemType Directory -Force -Path $cache | Out-Null
$gradle = Join-Path $cache 'gradle-8.11.1\bin\gradle.bat'
if (-not (Test-Path $gradle)) {
    $zip = Join-Path $cache 'gradle.zip'
    Invoke-WebRequest 'https://services.gradle.org/distributions/gradle-8.11.1-bin.zip' -OutFile $zip
    $checksum = (Invoke-WebRequest 'https://services.gradle.org/distributions/gradle-8.11.1-bin.zip.sha256').Content.Trim()
    if ((Get-FileHash $zip -Algorithm SHA256).Hash.ToLowerInvariant() -ne $checksum.ToLowerInvariant()) { throw 'Gradle checksum mismatch' }
    Expand-Archive -Path $zip -DestinationPath $cache -Force
}
& $gradle wrapper --gradle-version 8.11.1
if ($LASTEXITCODE -ne 0) { throw 'Gradle-Initialisierung fehlgeschlagen.' }
& $gradle --no-daemon :app:assembleDebug :app:lintDebug
if ($LASTEXITCODE -ne 0) { throw 'Build oder Android-Lint fehlgeschlagen. Meldungen oben prüfen.' }
Write-Host "APK erstellt: $PSScriptRoot\app\build\outputs\apk\debug\app-debug.apk"

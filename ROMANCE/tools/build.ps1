# Compile ROMANCE en Release (VST3 + standalone).
# /!\ Utiliser -like et non -match : le chemin contient des antislashs, qui sont
# des echappements en regex. Erreur attrapee au premier essai.
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$sln  = Join-Path $root 'Builds\VisualStudio2022\ROMANCE.sln'
$msb = Get-ChildItem 'C:\Program Files\Microsoft Visual Studio' -Recurse -Filter MSBuild.exe -ErrorAction SilentlyContinue |
       Where-Object { $_.FullName -like '*\Current\Bin\MSBuild.exe' } | Select-Object -First 1
if (-not $msb) { throw "MSBuild introuvable." }
Write-Host "ROMANCE - compilation" -ForegroundColor Cyan
& $msb.FullName $sln /p:Configuration=Release /p:Platform=x64 /v:minimal /nologo
exit $LASTEXITCODE

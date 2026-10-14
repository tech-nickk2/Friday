# Friday firmware helper
#   .\friday.ps1 build            incremental build
#   .\friday.ps1 flash COM7       flash + serial monitor (Ctrl+] to quit)
#   .\friday.ps1 monitor COM7     serial monitor only
param(
    [Parameter(Mandatory = $true)][ValidateSet("build", "flash", "monitor")][string]$Action,
    [string]$Port
)

$env:IDF_TOOLS_PATH = "C:\Espressif\tools"
. D:\esp\v6.1\export.ps1 *> $null
Set-Location $PSScriptRoot

switch ($Action) {
    "build"   { idf.py build }
    "flash"   { idf.py -p $Port flash monitor }
    "monitor" { idf.py -p $Port monitor }
}

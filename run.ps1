$ErrorActionPreference = 'Stop'
Push-Location $PSScriptRoot
try {
    & '.\.venv\Scripts\python.exe' visuals.py @args
    if ($LASTEXITCODE -ne 0) { throw 'Viewer failed. See the error above and README.md.' }
} finally { Pop-Location }

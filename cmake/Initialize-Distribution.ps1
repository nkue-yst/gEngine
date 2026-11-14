# 配置先の実体EXEへリンクする。フォルダー移設後もこのスクリプトを再実行して更新できる。
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$distributionRoot = [System.IO.Path]::GetFullPath($PSScriptRoot)
$editorPath = Join-Path $distributionRoot 'Editor\gengine_editor.exe'
if ((Test-Path -LiteralPath $editorPath -PathType Leaf) -eq $false) {
    throw 'Editor/gengine_editor.exe is missing. Restore the complete distribution first.'
}

# シェルに引数を解釈させず、Windows標準のショートカットへ対象と作業場所を別々に設定する。
$shellLink = New-Object -ComObject WScript.Shell
try {
    $shortcut = $shellLink.CreateShortcut((Join-Path $distributionRoot 'gEngine.lnk'))
    try {
        $shortcut.TargetPath = $editorPath
        $shortcut.Arguments = ''
        $shortcut.WorkingDirectory = $distributionRoot
        $shortcut.Description = 'gEngine Editor'
        $shortcut.Save()
    }
    finally {
        [void][System.Runtime.InteropServices.Marshal]::FinalReleaseComObject($shortcut)
    }
}
finally {
    [void][System.Runtime.InteropServices.Marshal]::FinalReleaseComObject($shellLink)
}
Write-Output 'Created gEngine.lnk for this distribution. Regenerate it after moving the folder.'

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

# 프레임 번호와 표정이 다른 테스트용 4×3 투명 PNG 시트를 생성하는 함수.
function New-TestCharacterSheet {
    $sheetPath = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../resources/character/sprites/test-character.png'))
    [void][System.IO.Directory]::CreateDirectory([System.IO.Path]::GetDirectoryName($sheetPath))
    $bitmap = [System.Drawing.Bitmap]::new(512, 384)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $graphics.Clear([System.Drawing.Color]::Transparent)
    $ink = [System.Drawing.SolidBrush]::new([System.Drawing.ColorTranslator]::FromHtml('#283b3a'))
    $pen = [System.Drawing.Pen]::new($ink.Color, 4)
    $font = [System.Drawing.Font]::new('Segoe UI', 13, [System.Drawing.FontStyle]::Bold)
    try {
        for ($frame = 0; $frame -lt 12; $frame++) {
            $row = [int][Math]::Floor($frame / 4)
            $step = $frame % 4
            $graphics.TranslateTransform($step * 128, $row * 128)
            $color = @('#a6c1a2', '#e9bf7c', '#91b5db')[$row]
            $body = [System.Drawing.SolidBrush]::new([System.Drawing.ColorTranslator]::FromHtml($color))
            try {
                $graphics.FillEllipse($body, 25, 30, 78, 66)
                $graphics.FillEllipse($ink, 35, 92, 20, 9)
                $graphics.FillEllipse($ink, 73, 92, 20, 9)
                if ($row -eq 0) {
                    $graphics.DrawLine($pen, 44, 59, 54, 59)
                    $graphics.DrawLine($pen, 74, 59, 84, 59)
                    $graphics.DrawString(('z' * (1 + $step)), $font, $ink, 75, 7)
                } else {
                    $graphics.FillEllipse($ink, 45, 53, 8, 10)
                    $graphics.FillEllipse($ink, 75, 53, 8, 10)
                    $graphics.DrawArc($pen, 55, 65, 18, 12, 0, 180)
                    if ($row -eq 1) {
                        $graphics.DrawString('!', $font, $ink, 103, 15)
                        $graphics.DrawLine($pen, 99, 70, 112, (42 + $step * 7))
                    } else {
                        $graphics.FillRectangle($body, 42, 78, 45, 21)
                        $graphics.DrawRectangle($pen, 42, 78, 45, 21)
                        $graphics.DrawLine($pen, 29, 73, 43, (80 + $step * 3))
                        $graphics.DrawLine($pen, 99, 73, 86, (89 - $step * 3))
                    }
                }
                $graphics.DrawString(('#' + $frame), $font, $ink, 43, 103)
            } finally { $body.Dispose() }
            $graphics.ResetTransform()
        }
        $bitmap.Save($sheetPath, [System.Drawing.Imaging.ImageFormat]::Png)
        $packImage = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../resources/imagepacks/test/images/test-character.png'))
        [void][System.IO.Directory]::CreateDirectory([System.IO.Path]::GetDirectoryName($packImage))
        Copy-Item -LiteralPath $sheetPath -Destination $packImage -Force
        Write-Output $sheetPath
    } finally {
        $font.Dispose(); $pen.Dispose(); $ink.Dispose(); $graphics.Dispose(); $bitmap.Dispose()
    }
}
New-TestCharacterSheet

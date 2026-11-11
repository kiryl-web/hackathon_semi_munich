# Export results/Task5_Presentation.pptx to PDF (and per-slide PNGs for review) with the installed PowerPoint.
param([switch]$Png)
$src = 'C:\Users\kpato\KP\Hackathon\results\Task5_Presentation.pptx'
$pdf = 'C:\Users\kpato\KP\Hackathon\results\Task5_Presentation.pdf'
$pp = New-Object -ComObject PowerPoint.Application
try {
    $deck = $pp.Presentations.Open($src, $true, $false, $false)   # read-only, untitled, no window
    $deck.SaveAs($pdf, 32)                                        # ppSaveAsPDF
    if ($Png) {
        $dir = 'C:\Users\kpato\KP\Hackathon\results\task5_charts\slides'
        New-Item -ItemType Directory -Force $dir | Out-Null
        foreach ($s in $deck.Slides) { $s.Export("$dir\slide_$($s.SlideIndex).png", 'PNG', 1600, 900) }
    }
    $deck.Close()
} finally { $pp.Quit() }
Write-Host "written $pdf"

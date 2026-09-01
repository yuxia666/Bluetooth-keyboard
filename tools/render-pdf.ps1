# render-pdf.ps1 - 用 WinRT PDF API 把 PDF 每页渲染为 PNG
# 用法: powershell -File render-pdf.ps1 -InputPdf <path> -OutDir <dir> [-MaxPages N]
param(
  [Parameter(Mandatory=$true)][string]$InputPdf,
  [Parameter(Mandatory=$true)][string]$OutDir,
  [int]$MaxPages = 50,
  [int]$Scale = 2   # 渲染缩放（1=原始，2=2倍，更清晰）
)

Add-Type -AssemblyName System.Runtime.WindowsRuntime
$asTaskGeneric = ([System.WindowsRuntimeSystemExtensions].GetMethods() | Where-Object {
  $_.Name -eq 'AsTask' -and $_.GetParameters().Count -eq 1 -and
  $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncOperation`1'
})[0]
Function Await($WinRtTask, $ResultType) {
  $asTask = $asTaskGeneric.MakeGenericMethod($ResultType)
  $netTask = $asTask.Invoke($null, @($WinRtTask))
  $netTask.Wait(-1) | Out-Null
  $netTask.Result
}
# IAsyncAction（无返回值）专用
$asTaskAction = ([System.WindowsRuntimeSystemExtensions].GetMethods() | Where-Object {
  $_.Name -eq 'AsTask' -and $_.GetParameters().Count -eq 1 -and
  $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncAction'
})[0]
Function AwaitAction($WinRtAction) {
  $netTask = $asTaskAction.Invoke($null, @($WinRtAction))
  $netTask.Wait(-1) | Out-Null
}

[Windows.Data.Pdf.PdfDocument,Windows.Data.Pdf,ContentType=WindowsRuntime] | Out-Null
[Windows.Storage.StorageFile,Windows.Storage,ContentType=WindowsRuntime] | Out-Null
[Windows.Storage.Streams.RandomAccessStream,Windows.Storage.Streams,ContentType=WindowsRuntime] | Out-Null

if (-not (Test-Path $OutDir)) { New-Item -ItemType Directory -Path $OutDir -Force | Out-Null }

$file = Await ([Windows.Storage.StorageFile]::GetFileFromPathAsync($InputPdf)) ([Windows.Storage.StorageFile])
$pdf  = Await ([Windows.Data.Pdf.PdfDocument]::LoadFromFileAsync($file)) ([Windows.Data.Pdf.PdfDocument])
$total = $pdf.PageCount
$n = [Math]::Min($total, $MaxPages)
Write-Host "PDF: $InputPdf  pages=$total  render=$n"

$base = [System.IO.Path]::GetFileNameWithoutExtension($InputPdf)
for ($i = 0; $i -lt $n; $i++) {
  $page = $pdf.GetPage([uint32]$i)
  $w = [Math]::Max(1, [int]($page.Size.Width  * $Scale))
  $h = [Math]::Max(1, [int]($page.Size.Height * $Scale))

  $stream = New-Object Windows.Storage.Streams.InMemoryRandomAccessStream
  $opts = New-Object Windows.Data.Pdf.PdfPageRenderOptions
  $opts.DestinationWidth  = [uint32]$w
  $opts.DestinationHeight = [uint32]$h

  AwaitAction ($page.RenderToStreamAsync($stream, $opts)) | Out-Null
  # 注意：PdfPage 无 Close 方法，跳过释放（GC 处理）

  # 读取流内容
  $stream.Seek(0) | Out-Null
  $reader = New-Object Windows.Storage.Streams.DataReader($stream.GetInputStreamAt(0))
  $size = [uint32]$stream.Size
  Await ($reader.LoadAsync($size)) ([uint32]) | Out-Null
  $bytes = New-Object byte[] $size
  $reader.ReadBytes($bytes) | Out-Null

  $outFile = Join-Path $OutDir ("{0}_p{1:D2}.png" -f $base, ($i + 1))
  [System.IO.File]::WriteAllBytes($outFile, $bytes)
  Write-Host "  saved: $outFile ($w x $h)"
}
Write-Host "Done. $n pages rendered."

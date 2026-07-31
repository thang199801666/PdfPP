param(
    [string]$Version = "1.3.2"
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$destination = Join-Path $repoRoot "third_party\zlib"
$tempRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("PdfPP-zlib-" + [Guid]::NewGuid().ToString("N"))
$archive = Join-Path $tempRoot "zlib.zip"
$extract = Join-Path $tempRoot "extract"
$url = "https://zlib.net/zlib132.zip"
$expectedSha256 = "e8bf55f3017aa181690990cb58a994e77885da140609fc8f94abe9b65d2cae28"

try {
    New-Item -ItemType Directory -Force -Path $tempRoot, $extract, $destination | Out-Null
    Write-Host "Downloading zlib $Version from $url ..."
    Invoke-WebRequest -Uri $url -OutFile $archive -UseBasicParsing

    $actualHash = (Get-FileHash -Path $archive -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actualHash -ne $expectedSha256) {
        throw "SHA-256 mismatch. Expected $expectedSha256, got $actualHash."
    }

    Expand-Archive -Path $archive -DestinationPath $extract -Force
    $sourceRoot = Get-ChildItem -Path $extract -Directory | Select-Object -First 1
    if (-not $sourceRoot) { throw "The downloaded zlib archive has no source directory." }

    $files = @(
        "adler32.c", "compress.c", "crc32.c", "crc32.h", "deflate.c", "deflate.h",
        "gzclose.c", "gzguts.h", "gzlib.c", "gzread.c", "gzwrite.c", "infback.c",
        "inffast.c", "inffast.h", "inffixed.h", "inflate.c", "inflate.h", "inftrees.c",
        "inftrees.h", "trees.c", "trees.h", "uncompr.c", "zconf.h", "zlib.h", "zutil.c", "zutil.h",
        "LICENSE", "README"
    )

    foreach ($file in $files) {
        $sourceFile = Join-Path $sourceRoot.FullName $file
        if (-not (Test-Path $sourceFile)) { throw "Missing zlib source file: $file" }
        Copy-Item $sourceFile (Join-Path $destination $file) -Force
    }

    Write-Host "zlib source embedded successfully in: $destination" -ForegroundColor Green
    Write-Host "Open Pdf++.sln and rebuild x64 Debug or Release."
}
finally {
    if (Test-Path $tempRoot) { Remove-Item $tempRoot -Recurse -Force }
}

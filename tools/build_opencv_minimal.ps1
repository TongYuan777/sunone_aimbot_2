[CmdletBinding(PositionalBinding = $false)]
param(
    [string]$RepoRoot = "",
    [string]$OpenCvVersion = "4.13.0",
    [string]$InstallDir = "",
    [string]$BuildDir = "",
    [switch]$ForceRebuild,
    [switch]$SkipDownload,
    [int]$MaxCpuCount = 0,
    [switch]$DryRun
)

# 精简版 OpenCV 编译脚本（DML 后端专用）
# ---------------------------------------------------------------------------
# 只编译项目实际使用到的 4 个模块：core / imgproc / imgcodecs / videoio
# 关闭 CUDA / DNN / highgui / FFMPEG / IPP / TBB / Eigen / LAPACK 等重型依赖。
# 产物布局与预编译版一致（include/opencv2 + x64/vc17/lib + x64/vc17/bin），
# 可通过 -DAIMBOT_OPENCV_DML_ROOT=...\build\dml_minimal 直接替换预编译版。
#
# 预期体积：opencv_world4xx.dll 约 15-20 MB（预编译版约 70 MB）
# ---------------------------------------------------------------------------

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Write-Step {
    param([string]$Message)
    Write-Host "[opencv-minimal] $Message" -ForegroundColor Cyan
}

function Resolve-NormalizedPath {
    param([string]$Path)
    return [System.IO.Path]::GetFullPath($Path)
}

function New-DirectoryIfMissing {
    param([string]$Path)
    if (-not (Test-Path -LiteralPath $Path)) {
        New-Item -ItemType Directory -Path $Path | Out-Null
    }
}

function Invoke-Download {
    param([string]$Uri, [string]$OutFile)

    Write-Step "Downloading: $Uri"
    if ($DryRun) {
        Write-Host ">> Invoke-WebRequest -Uri `"$Uri`" -OutFile `"$OutFile`""
        return
    }

    $originalSecurityProtocol = [Net.ServicePointManager]::SecurityProtocol
    try {
        [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12 -bor [Net.SecurityProtocolType]::Tls13 -bor [Net.SecurityProtocolType]::SystemDefault
        Invoke-WebRequest -Uri $Uri -OutFile $OutFile -MaximumRedirection 10 -UseBasicParsing
    }
    finally {
        [Net.ServicePointManager]::SecurityProtocol = $originalSecurityProtocol
    }
}

function Test-ValidZip {
    param([string]$Path, [int64]$MinSizeBytes = 1MB)

    if (-not (Test-Path -LiteralPath $Path)) { return $false }
    $fileInfo = Get-Item -LiteralPath $Path
    if ($fileInfo.Length -lt $MinSizeBytes) { return $false }

    $stream = [System.IO.File]::OpenRead($Path)
    try {
        $header = New-Object byte[] 4
        $read = $stream.Read($header, 0, 4)
    }
    finally { $stream.Dispose() }
    if ($read -lt 4) { return $false }
    # PK\x03\x04
    return ($header[0] -eq 0x50) -and ($header[1] -eq 0x4B) -and ($header[2] -eq 0x03) -and ($header[3] -eq 0x04)
}

function Expand-ZipSafe {
    param([string]$ZipPath, [string]$Destination)

    New-DirectoryIfMissing $Destination
    if ($DryRun) {
        Write-Host ">> Expand-Archive -Path `"$ZipPath`" -DestinationPath `"$Destination`" -Force"
        return
    }
    Expand-Archive -Path $ZipPath -DestinationPath $Destination -Force
}

function Find-Command {
    param([string]$Name)
    $cmd = Get-Command $Name -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    return $null
}

try {
    $scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
    if ([string]::IsNullOrWhiteSpace($RepoRoot)) {
        $RepoRoot = Resolve-NormalizedPath (Join-Path $scriptDir "..")
    } else {
        $RepoRoot = Resolve-NormalizedPath $RepoRoot
    }

    $downloadsDir = Join-Path $RepoRoot "sunone_aimbot_2\modules\_downloads"
    New-DirectoryIfMissing $downloadsDir

    if ([string]::IsNullOrWhiteSpace($InstallDir)) {
        $InstallDir = Join-Path $RepoRoot "sunone_aimbot_2\modules\opencv\build\dml_minimal"
    }
    $InstallDir = Resolve-NormalizedPath $InstallDir

    if ([string]::IsNullOrWhiteSpace($BuildDir)) {
        $BuildDir = Join-Path $downloadsDir ("opencv-" + $OpenCvVersion + "-minimal-build")
    }
    $BuildDir = Resolve-NormalizedPath $BuildDir

    # 已安装则跳过（除非 -ForceRebuild）
    $installedHeader = Join-Path $InstallDir "include\opencv2\opencv.hpp"
    if ((-not $ForceRebuild) -and (Test-Path -LiteralPath $installedHeader)) {
        $existingLibs = Get-ChildItem -Path (Join-Path $InstallDir "x64\*\vc*\lib\opencv_world*.lib") -ErrorAction SilentlyContinue |
            Where-Object { $_.BaseName -notmatch "d$" } |
            Sort-Object Name -Descending |
            Select-Object -First 1
        if ($existingLibs) {
            $stem = $existingLibs.BaseName
            $dllGuess = Join-Path $existingLibs.Directory.Parent.FullName ("bin\" + $stem + ".dll")
            if (Test-Path -LiteralPath $dllGuess) {
                Write-Step "Minimal OpenCV already installed: $InstallDir"
                Write-Host "AIMBOT_OPENCV_INCLUDE_DIR=$(Join-Path $InstallDir 'include')"
                Write-Host "AIMBOT_OPENCV_LIBRARY=$($existingLibs.FullName)"
                Write-Host "AIMBOT_OPENCV_DLL=$dllGuess"
                Write-Host "Pass to build_dml.ps1: -ExtraCMakeArgs `"-DAIMBOT_OPENCV_DML_ROOT=$InstallDir`""
                exit 0
            }
        }
    }

    # 1. 准备源码
    $srcRoot = Join-Path $downloadsDir ("opencv-" + $OpenCvVersion + "-src")
    $sourceMarker = Join-Path $srcRoot "CMakeLists.txt"
    if (-not (Test-Path -LiteralPath $sourceMarker)) {
        if ($SkipDownload) {
            throw "OpenCV source not found at $srcRoot and -SkipDownload was set."
        }

        $zipPath = Join-Path $downloadsDir ("opencv-" + $OpenCvVersion + ".zip")
        $urls = @(
            "https://github.com/opencv/opencv/archive/refs/tags/$OpenCvVersion.zip",
            "https://github.com/opencv/opencv/releases/download/$OpenCvVersion/opencv-$OpenCvVersion-windows.exe"
        )

        $downloaded = $false
        foreach ($uri in $urls) {
            try {
                Invoke-Download -Uri $uri -OutFile $zipPath
                if ($DryRun) { $downloaded = $true; break }
                if (Test-ValidZip -Path $zipPath) { $downloaded = $true; break }
                if (Test-Path -LiteralPath $zipPath) { Remove-Item -LiteralPath $zipPath -Force -ErrorAction SilentlyContinue }
                Write-Warning "[opencv-minimal] Downloaded file is not a valid zip, trying next URL."
            } catch {
                if (Test-Path -LiteralPath $zipPath) { Remove-Item -LiteralPath $zipPath -Force -ErrorAction SilentlyContinue }
                Write-Warning "[opencv-minimal] Download failed from '$uri': $($_.Exception.Message)"
            }
        }
        if (-not $downloaded) {
            throw "Could not download OpenCV $OpenCvVersion sources. Tried: $($urls -join '; ')"
        }

        if (-not $DryRun) {
            Expand-ZipSafe -ZipPath $zipPath -Destination $srcRoot
            # GitHub zip 解压后会有 opencv-4.13.0 子目录，提升到 srcRoot
            $nested = Join-Path $srcRoot ("opencv-" + $OpenCvVersion)
            if (Test-Path -LiteralPath (Join-Path $nested "CMakeLists.txt")) {
                Get-ChildItem -LiteralPath $nested -Force | ForEach-Object {
                    Move-Item -LiteralPath $_.FullName -Destination $srcRoot -Force
                }
                Remove-Item -LiteralPath $nested -Force -Recurse -ErrorAction SilentlyContinue
            }
        }
    } else {
        Write-Step "Using existing OpenCV source: $srcRoot"
    }

    # 2. 工具链检查
    $cmake = Find-Command "cmake"
    if (-not $cmake) { throw "cmake not found in PATH." }
    Write-Step "cmake: $cmake"

    $vcvarsAttempted = $false
    $envVcvars = $env:AIMBOT_VCVARS_PATH
    if ($envVcvars -and (Test-Path -LiteralPath $envVcvars)) {
        Write-Step "Using VC env from AIMBOT_VCVARS_PATH: $envVcvars"
        $vcvarsAttempted = $true
    }
    # 这里不强行导入 VS 环境；若用户从 Developer Command Prompt 运行则直接可用。
    # 默认 Ninja Multi-Config 不需要 VS 环境也能配置，但 cl.exe 需在 PATH。

    # 3. CMake 配置（精简模块集）
    New-DirectoryIfMissing $BuildDir

    if ($MaxCpuCount -le 0) {
        $cpuCount = [Environment]::ProcessorCount
        if ($cpuCount -lt 1) { $cpuCount = 2 }
        if ($cpuCount -gt 8) { $cpuCount = 8 }  # 限制并发，避免内存爆炸
    } else {
        $cpuCount = $MaxCpuCount
    }

    # 关键：只编译项目用到的 4 个模块
    # videoio 保留（virtual_camera.cpp 用 cv::VideoCapture）
    # imgcodecs 保留（capture.cpp / data_collector.cpp 用 cv::imwrite）
    # 不需要 highgui / dnn / cuda* / ml / flann / features2d / calib3d / video / photo / stitching / objdetect
    $cmakeArgs = @(
        "-S", $srcRoot,
        "-B", $BuildDir,
        "-G", "Ninja Multi-Config",
        "-DCMAKE_BUILD_TYPE=Release",
        "-DCMAKE_INSTALL_PREFIX=$InstallDir",

        # 只编译这些模块（OpenCV 4.x 官方选项）
        "-DBUILD_LIST=core,imgproc,imgcodecs,videoio",

        # 合并为单 DLL（opencv_world4xx.dll）
        "-DBUILD_SHARED_LIBS=ON",
        "-DBUILD_opencv_world=ON",

        # 关闭所有测试/示例/文档/绑定
        "-DBUILD_TESTS=OFF",
        "-DBUILD_PERF_TESTS=OFF",
        "-DBUILD_EXAMPLES=OFF",
        "-DBUILD_DOCS=OFF",
        "-DBUILD_opencv_python2=OFF",
        "-DBUILD_opencv_python3=OFF",
        "-DBUILD_opencv_java=OFF",
        "-DBUILD_opencv_apps=OFF",
        "-DBUILD_opencv_ts=OFF",
        "-DBUILD_WITH_INFO=OFF",
        "-DBUILD_WITH_DEBUG_INFO=OFF",
        "-DBUILD_PACKAGE=OFF",

        # 关闭 GPU / 计算后端（DML 版不需要 OpenCV 的 CUDA/OpenCL/DNN）
        "-DWITH_CUDA=OFF",
        "-DWITH_CUDNN=OFF",
        "-DWITH_OPENCL=OFF",
        "-DWITH_OPENCLAMDBLAS=OFF",
        "-DWITH_OPENCLAMDFFT=OFF",

        # 关闭不用的视频/图像 IO 后端
        "-DWITH_FFMPEG=OFF",
        "-DWITH_GSTREAMER=OFF",
        "-DWITH_V4L=OFF",
        "-DWITH_DSHOW=OFF",
        "-DWITH_VA=OFF",
        "-DWITH_VA_INTEL=OFF",
        # 保留 MSMF（Media Foundation），videoio 在 Windows 上的原生后端
        "-DWITH_MSMF=ON",

        # 关闭数值/科学库（项目不依赖）
        "-DWITH_LAPACK=OFF",
        "-DWITH_EIGEN=OFF",
        "-DWITH_PROTOBUF=OFF",
        "-DWITH_QUIRC=OFF",

        # 关闭 IPP（减小体积，略微影响性能；自瞄场景影响可忽略）
        "-DWITH_IPP=OFF",

        # 线程后端：用 OpenCV 默认（pthreads-pf on Windows 即 Win32 threads）
        "-DWITH_TBB=OFF",
        "-DWITH_OPENMP=OFF",

        # 图像编解码：保留 JPEG/PNG（imgcodecs 必需），关闭其他冷门格式
        "-DWITH_JPEG=ON",
        "-DWITH_PNG=ON",
        "-DWITH_TIFF=OFF",
        "-DWITH_WEBP=OFF",
        "-DWITH_OPENJPEG=OFF",
        "-DWITH_JASPER=OFF",
        "-DWITH_IMGCODEC_HDR=OFF",
        "-DWITH_IMGCODEC_PFM=OFF",
        "-DWITH_IMGCODEC_PXM=OFF",
        "-DWITH_IMGCODEC_SUNRASTER=OFF",

        # 其他
        "-DWITH_ITT=OFF",
        "-DWITH_1394=OFF",
        "-DWITH_GTK=OFF",
        "-DWITH_WIN32UI=OFF",
        "-DWITH_CAROTENE=OFF",

        # CRT：用动态 /MD（与 OpenCV 预编译版一致；exe 用 /MT 加载 /MD 的 DLL 是允许的，
        # 只要不在跨 DLL 边界传递 CRT 对象。OpenCV API 不跨边界传 CRT 对象。）
        "-DBUILD_WITH_STATIC_CRT=OFF",

        # 安装布局：x64/vc17/lib + x64/vc17/bin
        "-DOPENCV_SKIP_PYTHON_WARNING=ON",
        "-DCPU_BASELINE=SSE2",
        "-DCPU_DISPATCH=SSE2,SSE3,SSE4_1,SSE4_2,AVX,AVX2"
    )

    Write-Step "Configuring minimal OpenCV $OpenCvVersion (modules: core, imgproc, imgcodecs, videoio)"
    if ($DryRun) {
        Write-Host ">> cmake $($cmakeArgs -join ' ')"
    } else {
        & $cmake @cmakeArgs
        if ($LASTEXITCODE -ne 0) { throw "CMake configuration failed (exit $LASTEXITCODE)." }
    }

    # 4. 编译 + 安装
    Write-Step "Building Release with $cpuCount parallel jobs"
    if ($DryRun) {
        Write-Host ">> cmake --build `"$BuildDir`" --config Release --target install --parallel $cpuCount"
    } else {
        & $cmake --build $BuildDir --config Release --target install --parallel $cpuCount
        if ($LASTEXITCODE -ne 0) { throw "OpenCV build failed (exit $LASTEXITCODE)." }
    }

    # 5. 验证产物
    if (-not $DryRun) {
        if (-not (Test-Path -LiteralPath $installedHeader)) {
            throw "Install layout invalid: $installedHeader not found."
        }
        $libFile = Get-ChildItem -Path (Join-Path $InstallDir "x64\*\vc*\lib\opencv_world*.lib") -ErrorAction SilentlyContinue |
            Where-Object { $_.BaseName -notmatch "d$" } |
            Sort-Object Name -Descending |
            Select-Object -First 1
        if (-not $libFile) { throw "opencv_world*.lib not found under $InstallDir." }
        $dllFile = Join-Path $libFile.Directory.Parent.FullName ("bin\" + $libFile.BaseName + ".dll")
        if (-not (Test-Path -LiteralPath $dllFile)) { throw "opencv_world*.dll not found: $dllFile" }

        $dllSize = (Get-Item -LiteralPath $dllFile).Length / 1MB
        Write-Step "Done. opencv_world DLL size: $([math]::Round($dllSize, 2)) MB"
        Write-Host "AIMBOT_OPENCV_INCLUDE_DIR=$(Join-Path $InstallDir 'include')"
        Write-Host "AIMBOT_OPENCV_LIBRARY=$($libFile.FullName)"
        Write-Host "AIMBOT_OPENCV_DLL=$dllFile"
        Write-Host ""
        Write-Host "下一步：用以下命令构建 DML 版（替换为精简 OpenCV）：" -ForegroundColor Green
        Write-Host "  .\build_dml.bat -ExtraCMakeArgs `"-DAIMBOT_OPENCV_DML_ROOT=$InstallDir`"" -ForegroundColor Yellow
    } else {
        Write-Step "Dry-run complete."
        Write-Host "InstallDir=$InstallDir"
    }
}
catch {
    Write-Error $_
    exit 1
}

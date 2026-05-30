# Huffman压缩工具 CI测试脚本
# 用法: powershell -ExecutionPolicy Bypass -File test_huffman.ps1

$ErrorActionPreference = "Stop"
$HUFFMAN = ".\huffman.exe"
$TESTDIR = ".\test_tmp"
$PASS = 0
$FAIL = 0

# 清理并创建测试目录
function Reset-TestDir {
    if (Test-Path $TESTDIR) { Remove-Item -Recurse -Force $TESTDIR }
    New-Item -ItemType Directory -Path $TESTDIR | Out-Null
}

# 断言函数
function Assert-Equal($actual, $expected, $name) {
    if ($actual -eq $expected) {
        Write-Host "[PASS] $name" -ForegroundColor Green
        $script:PASS++
    } else {
        Write-Host "[FAIL] $name -- expected '$expected', got '$actual'" -ForegroundColor Red
        $script:FAIL++
    }
}

function Assert-True($condition, $name) {
    if ($condition) {
        Write-Host "[PASS] $name" -ForegroundColor Green
        $script:PASS++
    } else {
        Write-Host "[FAIL] $name" -ForegroundColor Red
        $script:FAIL++
    }
}

# 比较两个文件内容是否完全一致
function FilesEqual($a, $b) {
    $ba = [System.IO.File]::ReadAllBytes($a)
    $bb = [System.IO.File]::ReadAllBytes($b)
    if ($ba.Length -ne $bb.Length) { return $false }
    for ($i = 0; $i -lt $ba.Length; $i++) {
        if ($ba[$i] -ne $bb[$i]) { return $false }
    }
    return $true
}

Write-Host "========================================" 
Write-Host " Huffman压缩工具 CI测试"
Write-Host "========================================"
Write-Host ""

Reset-TestDir

# ==========================================
# 测试1: 帮助信息
# ==========================================
Write-Host "--- 测试组: 基本功能 ---"

$helpOutput = & $HUFFMAN -h 2>&1
Assert-True ($helpOutput -match "Huffman文件压缩工具") "T1: 帮助信息包含标题"
Assert-True ($helpOutput -match "用法:") "T1: 帮助信息包含用法"

# ==========================================
# 测试2: 单文件压缩/解压 - 普通文本
# ==========================================
Reset-TestDir
$testText = "Hello, Huffman! This is a test file for compression.`n" * 100
[System.IO.File]::WriteAllText("$TESTDIR\input.txt", $testText)

$exitCode = & $HUFFMAN -c "$TESTDIR\input.txt" "$TESTDIR\output.huf"
Assert-True ($LASTEXITCODE -eq 0) "T2a: 单文件压缩成功 (exit=0)"
Assert-True (Test-Path "$TESTDIR\output.huf") "T2a: 压缩文件已生成"

$origSize = (Get-Item "$TESTDIR\input.txt").Length
$compSize = (Get-Item "$TESTDIR\output.huf").Length
Assert-True ($compSize -lt $origSize) "T2b: 压缩后文件更小 ($compSize < $origSize)"

# 解压
New-Item -ItemType Directory -Path "$TESTDIR\decompressed" -Force | Out-Null
& $HUFFMAN -x "$TESTDIR\output.huf" "$TESTDIR\decompressed" | Out-Null
Assert-True ($LASTEXITCODE -eq 0) "T2c: 单文件解压成功 (exit=0)"
Assert-True (Test-Path "$TESTDIR\decompressed\input.txt") "T2c: 解压文件已生成"
Assert-True (FilesEqual "$TESTDIR\input.txt" "$TESTDIR\decompressed\input.txt") "T2d: 解压内容与原文一致"

# ==========================================
# 测试3: 单文件压缩/解压 - 全相同字符
# ==========================================
Reset-TestDir
$aaa = "a" * 10000
[System.IO.File]::WriteAllText("$TESTDIR\aaa.txt", $aaa)

& $HUFFMAN -c "$TESTDIR\aaa.txt" "$TESTDIR\aaa.huf" | Out-Null
Assert-True ($LASTEXITCODE -eq 0) "T3a: 全相同字符压缩成功"

$origSize = (Get-Item "$TESTDIR\aaa.txt").Length
$compSize = (Get-Item "$TESTDIR\aaa.huf").Length
Assert-True ($compSize -lt $origSize * 0.5) "T3b: 全相同字符压缩率>50% ($compSize vs $origSize)"

New-Item -ItemType Directory -Path "$TESTDIR\out" -Force | Out-Null
& $HUFFMAN -x "$TESTDIR\aaa.huf" "$TESTDIR\out" | Out-Null
Assert-True ($LASTEXITCODE -eq 0) "T3c: 全相同字符解压成功"
Assert-True (FilesEqual "$TESTDIR\aaa.txt" "$TESTDIR\out\aaa.txt") "T3d: 全相同字符解压内容一致"

# ==========================================
# 测试4: 单文件压缩/解压 - 空文件
# ==========================================
Reset-TestDir
[System.IO.File]::WriteAllText("$TESTDIR\empty.txt", "")

& $HUFFMAN -c "$TESTDIR\empty.txt" "$TESTDIR\empty.huf" | Out-Null
Assert-True ($LASTEXITCODE -eq 0) "T4a: 空文件压缩成功"

New-Item -ItemType Directory -Path "$TESTDIR\out" -Force | Out-Null
& $HUFFMAN -x "$TESTDIR\empty.huf" "$TESTDIR\out" | Out-Null
Assert-True ($LASTEXITCODE -eq 0) "T4b: 空文件解压成功"
Assert-True (Test-Path "$TESTDIR\out\empty.txt") "T4c: 空文件解压文件存在"
Assert-True ((Get-Item "$TESTDIR\out\empty.txt").Length -eq 0) "T4d: 解压后文件为空"

# ==========================================
# 测试5: 二进制文件压缩/解压
# ==========================================
Reset-TestDir
$binData = New-Object byte[] 65536
$rng = [System.Random]::new(42)
$rng.NextBytes($binData)
[System.IO.File]::WriteAllBytes("$TESTDIR\binary.bin", $binData)

& $HUFFMAN -c "$TESTDIR\binary.bin" "$TESTDIR\binary.huf" | Out-Null
Assert-True ($LASTEXITCODE -eq 0) "T5a: 二进制文件压缩成功"

New-Item -ItemType Directory -Path "$TESTDIR\out" -Force | Out-Null
& $HUFFMAN -x "$TESTDIR\binary.huf" "$TESTDIR\out" | Out-Null
Assert-True ($LASTEXITCODE -eq 0) "T5b: 二进制文件解压成功"
Assert-True (FilesEqual "$TESTDIR\binary.bin" "$TESTDIR\out\binary.bin") "T5c: 二进制文件解压内容一致"

# ==========================================
# 测试6: 多文件压缩/解压
# ==========================================
Reset-TestDir
[System.IO.File]::WriteAllText("$TESTDIR\file1.txt", "Alpha file content 12345")
[System.IO.File]::WriteAllText("$TESTDIR\file2.txt", "Beta file content 67890" * 50)
[System.IO.File]::WriteAllText("$TESTDIR\file3.txt", "Gamma" * 5000)

& $HUFFMAN -mc "$TESTDIR\multi.huf" "$TESTDIR\file1.txt" "$TESTDIR\file2.txt" "$TESTDIR\file3.txt" | Out-Null
Assert-True ($LASTEXITCODE -eq 0) "T6a: 多文件压缩成功"

New-Item -ItemType Directory -Path "$TESTDIR\multi_out" -Force | Out-Null
& $HUFFMAN -mx "$TESTDIR\multi.huf" "$TESTDIR\multi_out" | Out-Null
Assert-True ($LASTEXITCODE -eq 0) "T6b: 多文件解压成功"

Assert-True (FilesEqual "$TESTDIR\file1.txt" "$TESTDIR\multi_out\file1.txt") "T6c: file1.txt 解压一致"
Assert-True (FilesEqual "$TESTDIR\file2.txt" "$TESTDIR\multi_out\file2.txt") "T6d: file2.txt 解压一致"
Assert-True (FilesEqual "$TESTDIR\file3.txt" "$TESTDIR\multi_out\file3.txt") "T6e: file3.txt 解压一致"

# ==========================================
# 测试7: 压缩率测试
# ==========================================
Reset-TestDir
$repeated = "ABCDEFGH" * 1000
[System.IO.File]::WriteAllText("$TESTDIR\repeat.txt", $repeated)

& $HUFFMAN -c "$TESTDIR\repeat.txt" "$TESTDIR\repeat.huf" | Out-Null
$testOutput = & $HUFFMAN -t "$TESTDIR\repeat.huf" "$TESTDIR\repeat.txt" 2>&1
Assert-True ($LASTEXITCODE -eq 0) "T7a: 压缩率测试执行成功"
Assert-True ($testOutput -match "压缩率:") "T7b: 输出包含压缩率信息"

# ==========================================
# 测试8: 错误处理 - 无效参数
# ==========================================
$errOutput = & $HUFFMAN -c 2>&1
Assert-True ($LASTEXITCODE -ne 0) "T8a: 缺少参数返回非零退出码"

$errOutput = & $HUFFMAN --invalid 2>&1
Assert-True ($LASTEXITCODE -ne 0) "T8b: 未知选项返回非零退出码"

$errOutput = & $HUFFMAN -c "nonexistent_file.txt" "out.huf" 2>&1
Assert-True ($LASTEXITCODE -ne 0) "T8c: 不存在的输入文件返回非零退出码"

# ==========================================
# 测试9: 大文件压力测试
# ==========================================
Reset-TestDir
$largeData = New-Object byte[] 1048576  # 1MB
$rng2 = [System.Random]::new(99)
$rng2.NextBytes($largeData)
[System.IO.File]::WriteAllBytes("$TESTDIR\large.bin", $largeData)

& $HUFFMAN -c "$TESTDIR\large.bin" "$TESTDIR\large.huf" | Out-Null
Assert-True ($LASTEXITCODE -eq 0) "T9a: 1MB文件压缩成功"

New-Item -ItemType Directory -Path "$TESTDIR\out" -Force | Out-Null
& $HUFFMAN -x "$TESTDIR\large.huf" "$TESTDIR\out" | Out-Null
Assert-True ($LASTEXITCODE -eq 0) "T9b: 1MB文件解压成功"
Assert-True (FilesEqual "$TESTDIR\large.bin" "$TESTDIR\out\large.bin") "T9c: 1MB文件解压内容一致"

# ==========================================
# 测试10: 含Unicode/中文文件名
# ==========================================
Reset-TestDir
[System.IO.File]::WriteAllText("$TESTDIR\测试文件.txt", "中文内容测试abcdefg")

& $HUFFMAN -c "$TESTDIR\测试文件.txt" "$TESTDIR\unicode.huf" | Out-Null
Assert-True ($LASTEXITCODE -eq 0) "T10a: 中文文件名压缩成功"

New-Item -ItemType Directory -Path "$TESTDIR\out" -Force | Out-Null
& $HUFFMAN -x "$TESTDIR\unicode.huf" "$TESTDIR\out" | Out-Null
Assert-True ($LASTEXITCODE -eq 0) "T10b: 中文文件名解压成功"

# 查找解压出的文件
$decompressedFiles = Get-ChildItem "$TESTDIR\out" -File
Assert-True ($decompressedFiles.Count -ge 1) "T10c: 解压目录中有文件"
if ($decompressedFiles.Count -ge 1) {
    $decompressedContent = [System.IO.File]::ReadAllText($decompressedFiles[0].FullName)
    Assert-True ($decompressedContent -eq "中文内容测试abcdefg") "T10d: 中文文件解压内容一致"
}

# ==========================================
# 清理
# ==========================================
Remove-Item -Recurse -Force $TESTDIR -ErrorAction SilentlyContinue

# ==========================================
# 汇总
# ==========================================
Write-Host ""
Write-Host "========================================" 
Write-Host " 测试结果: $PASS 通过, $FAIL 失败"
Write-Host "========================================" 

if ($FAIL -gt 0) {
    exit 1
} else {
    exit 0
}
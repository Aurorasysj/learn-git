/*
 * test_huffman.c
 * Huffman压缩工具 自动化测试程序
 * 编译: gcc -O2 -o test_huffman test_huffman.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#endif

static int g_pass = 0;
static int g_fail = 0;

/* ==================== 文件系统工具 ==================== */

static int make_dir(const char *path)
{
    struct stat st;
    if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) return 1;
#ifdef _WIN32
    return _mkdir(path) == 0;
#else
    return mkdir(path, 0755) == 0;
#endif
}

static void make_dir_p(const char *parent, const char *sub)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", parent, sub);
    make_dir(path);
}

static void cleanup_dir(const char *dir)
{
    char cmd[512];
#ifdef _WIN32
    snprintf(cmd, sizeof(cmd), "cmd /c \"if exist \"%s\" rmdir /s /q \"%s\"\"", dir, dir);
#else
    snprintf(cmd, sizeof(cmd), "rm -rf \"%s\"", dir);
#endif
    system(cmd);
}

static void reset_testdir(const char *dir)
{
    cleanup_dir(dir);
    make_dir(dir);
}

static int file_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

static long file_size(const char *path)
{
    struct stat st;
    return (stat(path, &st) == 0) ? (long)st.st_size : -1;
}

static unsigned char *load_file(const char *path, int *out_len)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) { *out_len = -1; return NULL; }
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    unsigned char *buf = (unsigned char *)malloc((size_t)(sz > 0 ? sz : 1));
    if (!buf) { fclose(fp); *out_len = -1; return NULL; }
    *out_len = (int)fread(buf, 1, (size_t)sz, fp);
    fclose(fp);
    return buf;
}

static int files_equal(const char *a, const char *b)
{
    int na, nb;
    unsigned char *ba = load_file(a, &na);
    unsigned char *bb = load_file(b, &nb);
    int ok = (na >= 0 && nb >= 0 && na == nb && memcmp(ba, bb, na) == 0);
    free(ba); free(bb);
    return ok;
}

static int write_file(const char *path, const unsigned char *buf, int len)
{
    FILE *fp = fopen(path, "wb");
    if (!fp) return -1;
    (void)fwrite(buf, 1, len, fp);
    fclose(fp);
    return 0;
}

static int write_text(const char *path, const char *text)
{
    return write_file(path, (const unsigned char *)text, (int)strlen(text));
}

static void run_cmd(const char *cmd)
{
    system(cmd);
}

/* 捕获命令输出(stdout+stderr) */
static int capture_cmd(const char *cmd, char *out, int out_max)
{
    char full[1024];
    snprintf(full, sizeof(full), "%s 2>&1", cmd);
    FILE *fp = popen(full, "r");
    if (!fp) return -1;
    int total = 0;
    while (total < out_max - 1) {
        int ch = fgetc(fp);
        if (ch == EOF) break;
        out[total++] = (char)ch;
    }
    out[total] = '\0';
    pclose(fp);
    return total;
}

/* ==================== 断言 ==================== */
#define PASS(name) do { printf("  [PASS] %s\n", name); g_pass++; } while(0)
#define FAIL(name) do { printf("  [FAIL] %s\n", name); g_fail++; } while(0)
#define ASSERT(cond, name) do { if (cond) PASS(name); else FAIL(name); } while(0)

/* ==================== 测试用例 ==================== */

static void test_help(void)
{
    printf("--- T1: 帮助信息 ---\n");
    char buf[4096] = {0};
    capture_cmd("huffman.exe -h", buf, sizeof(buf));

    ASSERT(strstr(buf, "Huffman") != NULL, "T1a: 包含标题");
    ASSERT(strstr(buf, "用法") != NULL,   "T1b: 包含用法说明");
}

static void test_single_text(void)
{
    printf("--- T2: 单文件压缩/解压 - 普通文本 ---\n");
    reset_testdir("test_tmp");

    FILE *fp = fopen("test_tmp/input.txt", "w");
    for (int i = 0; i < 100; i++)
        fprintf(fp, "Hello, Huffman! This is a test file for compression.\n");
    fclose(fp);

    run_cmd("huffman.exe -c test_tmp/input.txt test_tmp/output.huf >nul 2>&1");
    ASSERT(file_exists("test_tmp/output.huf"), "T2a: 压缩文件已生成");

    long orig = file_size("test_tmp/input.txt");
    long comp = file_size("test_tmp/output.huf");
    ASSERT(comp > 0 && comp < orig, "T2b: 压缩后更小");

    make_dir_p("test_tmp", "decompressed");
    run_cmd("huffman.exe -x test_tmp/output.huf test_tmp/decompressed >nul 2>&1");

    ASSERT(file_exists("test_tmp/decompressed/input.txt"),
           "T2c: 解压文件已生成");
    ASSERT(files_equal("test_tmp/input.txt", "test_tmp/decompressed/input.txt"),
           "T2d: 解压内容与原文一致");
}

static void test_uniform_char(void)
{
    printf("--- T3: 全相同字符 ---\n");
    reset_testdir("test_tmp");

    FILE *fp = fopen("test_tmp/aaa.txt", "w");
    for (int i = 0; i < 10000; i++) fputc('a', fp);
    fclose(fp);

    run_cmd("huffman.exe -c test_tmp/aaa.txt test_tmp/aaa.huf >nul 2>&1");
    ASSERT(file_exists("test_tmp/aaa.huf"), "T3a: 压缩成功");

    long orig = file_size("test_tmp/aaa.txt");
    long comp = file_size("test_tmp/aaa.huf");
    ASSERT(comp < orig / 2, "T3b: 压缩率>50%");

    make_dir_p("test_tmp", "out");
    run_cmd("huffman.exe -x test_tmp/aaa.huf test_tmp/out >nul 2>&1");

    ASSERT(file_exists("test_tmp/out/aaa.txt"), "T3c: 解压成功");
    ASSERT(files_equal("test_tmp/aaa.txt", "test_tmp/out/aaa.txt"),
           "T3d: 解压内容一致");
}

static void test_empty_file(void)
{
    printf("--- T4: 空文件 ---\n");
    reset_testdir("test_tmp");
    fclose(fopen("test_tmp/empty.txt", "w"));

    run_cmd("huffman.exe -c test_tmp/empty.txt test_tmp/empty.huf >nul 2>&1");
    ASSERT(file_exists("test_tmp/empty.huf"), "T4a: 空文件压缩成功");

    make_dir_p("test_tmp", "out");
    run_cmd("huffman.exe -x test_tmp/empty.huf test_tmp/out >nul 2>&1");

    ASSERT(file_exists("test_tmp/out/empty.txt"), "T4b: 解压文件存在");
    ASSERT(file_size("test_tmp/out/empty.txt") == 0, "T4c: 解压后为空");
}

static void test_binary_file(void)
{
    printf("--- T5: 二进制文件 ---\n");
    reset_testdir("test_tmp");

    unsigned char bin[65536];
    srand(42);
    for (int i = 0; i < (int)sizeof(bin); i++)
        bin[i] = (unsigned char)(rand() & 0xFF);
    write_file("test_tmp/binary.bin", bin, sizeof(bin));

    run_cmd("huffman.exe -c test_tmp/binary.bin test_tmp/binary.huf >nul 2>&1");
    ASSERT(file_exists("test_tmp/binary.huf"), "T5a: 二进制压缩成功");

    make_dir_p("test_tmp", "out");
    run_cmd("huffman.exe -x test_tmp/binary.huf test_tmp/out >nul 2>&1");

    ASSERT(file_exists("test_tmp/out/binary.bin"), "T5b: 解压成功");
    ASSERT(files_equal("test_tmp/binary.bin", "test_tmp/out/binary.bin"),
           "T5c: 二进制内容一致");
}

static void test_multi_file(void)
{
    printf("--- T6: 多文件压缩/解压 ---\n");
    reset_testdir("test_tmp");

    write_text("test_tmp/file1.txt", "Alpha file content 12345");

    FILE *fp = fopen("test_tmp/file2.txt", "w");
    for (int i = 0; i < 50; i++) fprintf(fp, "Beta file content 67890");
    fclose(fp);

    fp = fopen("test_tmp/file3.txt", "w");
    for (int i = 0; i < 5000; i++) fprintf(fp, "Gamma");
    fclose(fp);

    run_cmd("huffman.exe -mc test_tmp/multi.huf "
        "test_tmp/file1.txt test_tmp/file2.txt test_tmp/file3.txt >nul 2>&1");
    ASSERT(file_exists("test_tmp/multi.huf"), "T6a: 多文件压缩成功");

    make_dir_p("test_tmp", "multi_out");
    run_cmd("huffman.exe -mx test_tmp/multi.huf test_tmp/multi_out >nul 2>&1");

    ASSERT(file_exists("test_tmp/multi_out/file1.txt"), "T6b: file1.txt 解压成功");
    ASSERT(file_exists("test_tmp/multi_out/file2.txt"), "T6c: file2.txt 解压成功");
    ASSERT(file_exists("test_tmp/multi_out/file3.txt"), "T6d: file3.txt 解压成功");

    ASSERT(files_equal("test_tmp/file1.txt", "test_tmp/multi_out/file1.txt"),
           "T6e: file1.txt 内容一致");
    ASSERT(files_equal("test_tmp/file2.txt", "test_tmp/multi_out/file2.txt"),
           "T6f: file2.txt 内容一致");
    ASSERT(files_equal("test_tmp/file3.txt", "test_tmp/multi_out/file3.txt"),
           "T6g: file3.txt 内容一致");
}

static void test_ratio(void)
{
    printf("--- T7: 压缩率测试 ---\n");
    reset_testdir("test_tmp");

    FILE *fp = fopen("test_tmp/repeat.txt", "w");
    for (int i = 0; i < 1000; i++) fprintf(fp, "ABCDEFGH");
    fclose(fp);

    run_cmd("huffman.exe -c test_tmp/repeat.txt test_tmp/repeat.huf >nul 2>&1");

    char buf[1024] = {0};
    capture_cmd("huffman.exe -t test_tmp/repeat.huf test_tmp/repeat.txt", buf, sizeof(buf));
    ASSERT(strstr(buf, "压缩率") != NULL, "T7: 输出包含压缩率信息");
}

static void test_error_handling(void)
{
    printf("--- T8: 错误处理 ---\n");
    reset_testdir("test_tmp");

    char buf[1024];

    buf[0] = '\0';
    capture_cmd("huffman.exe -c", buf, sizeof(buf));
    ASSERT(strlen(buf) > 0, "T8a: 缺少参数有错误输出");

    buf[0] = '\0';
    capture_cmd("huffman.exe --invalid", buf, sizeof(buf));
    ASSERT(strstr(buf, "未知") != NULL || strstr(buf, "用法") != NULL,
           "T8b: 未知选项有错误提示");

    buf[0] = '\0';
    capture_cmd("huffman.exe -c nonexistent_file.txt out.huf", buf, sizeof(buf));
    ASSERT(strlen(buf) > 0, "T8c: 不存在文件有错误输出");
}

static void test_large_file(void)
{
    printf("--- T9: 大文件压力测试 ---\n");
    reset_testdir("test_tmp");

    int size = 1024 * 1024;
    unsigned char *data = (unsigned char *)malloc(size);
    if (!data) { FAIL("T9: 内存分配失败"); return; }

    srand(99);
    for (int i = 0; i < size; i++)
        data[i] = (unsigned char)(rand() & 0xFF);
    write_file("test_tmp/large.bin", data, size);
    free(data);

    run_cmd("huffman.exe -c test_tmp/large.bin test_tmp/large.huf >nul 2>&1");
    ASSERT(file_exists("test_tmp/large.huf"), "T9a: 1MB压缩成功");

    make_dir_p("test_tmp", "out");
    run_cmd("huffman.exe -x test_tmp/large.huf test_tmp/out >nul 2>&1");

    ASSERT(file_exists("test_tmp/out/large.bin"), "T9b: 1MB解压成功");
    ASSERT(files_equal("test_tmp/large.bin", "test_tmp/out/large.bin"),
           "T9c: 1MB内容一致");
}

static void test_unicode_filename(void)
{
    printf("--- T10: 中文文件名 ---\n");
    reset_testdir("test_tmp");

    write_text("test_tmp/测试文件.txt", "中文内容测试abcdefg");

    run_cmd("huffman.exe -c \"test_tmp/测试文件.txt\" test_tmp/unicode.huf >nul 2>&1");
    ASSERT(file_exists("test_tmp/unicode.huf"), "T10a: 中文文件名压缩成功");

    make_dir_p("test_tmp", "out");
    run_cmd("huffman.exe -x test_tmp/unicode.huf test_tmp/out >nul 2>&1");

    int found = 0;

    if (file_exists("test_tmp/out/测试文件.txt")) {
        int n;
        unsigned char *buf = load_file("test_tmp/out/测试文件.txt", &n);
        if (buf && n == (int)strlen("中文内容测试abcdefg") &&
            memcmp(buf, "中文内容测试abcdefg", n) == 0) {
            found = 1;
        }
        free(buf);
    }

    if (!found) {
        char list_buf[4096] = {0};
        capture_cmd("cmd /c \"dir /b test_tmp\\out\"", list_buf, sizeof(list_buf));
        char *line = strtok(list_buf, "\r\n");
        while (line) {
            if (strlen(line) > 0 && strcmp(line, "list.txt") != 0) {
                char fpath[1024];
                snprintf(fpath, sizeof(fpath), "test_tmp/out/%s", line);
                int n;
                unsigned char *buf = load_file(fpath, &n);
                if (buf && n == (int)strlen("中文内容测试abcdefg") &&
                    memcmp(buf, "中文内容测试abcdefg", n) == 0) {
                    found = 1;
                    free(buf);
                    break;
                }
                free(buf);
            }
            line = strtok(NULL, "\r\n");
        }
    }

    ASSERT(found, "T10b: 中文文件解压内容一致");
}

/* ==================== main ==================== */
int main(void)
{
    printf("========================================\n");
    printf(" Huffman压缩工具 CI测试 (C版)\n");
    printf("========================================\n\n");

    test_help();
    test_single_text();
    test_uniform_char();
    test_empty_file();
    test_binary_file();
    test_multi_file();
    test_ratio();
    test_error_handling();
    test_large_file();
    test_unicode_filename();

    cleanup_dir("test_tmp");

    printf("\n========================================\n");
    printf(" 测试结果: %d 通过, %d 失败\n", g_pass, g_fail);
    printf("========================================\n");

    return (g_fail > 0) ? 1 : 0;
}
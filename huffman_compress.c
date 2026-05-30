/*
 * huffman_compress.c
 * 基于霍夫曼编码的文件压缩与解压工具
 * 支持单文件/多文件压缩与解压，CLI命令行界面
 * 编译: gcc -O2 -o huffman huffman_compress.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <sys/stat.h>

/* ==================== 常量定义 ==================== */
#define MAX_TREE_NODES  512     /* 霍夫曼树最大节点数: 256*2-1 */
#define MAX_CODE_LEN    256     /* 单个字符编码最大位数 */
#define MAX_FILENAME    1024    /* 文件名最大长度 */
#define MAGIC_NUMBER    0x46554648  /* "HUFF" 小端序 */
#define BUFFER_SIZE     8192    /* 文件读写缓冲区大小 */
#define ARCHIVE_VERSION 1       /* 压缩文件格式版本号 */

/* ==================== 数据结构 ==================== */

/* 霍夫曼树节点 */
typedef struct {
    uint64_t weight;            /* 权值(字符出现频次) */
    int16_t  parent;            /* 父节点索引, -1表示无父节点 */
    int16_t  left;              /* 左子节点索引, -1表示叶节点 */
    int16_t  right;             /* 右子节点索引, -1表示叶节点 */
    uint8_t  ch;                /* 叶节点对应的字符 */
} HuffmanNode;

/* 霍夫曼编码 */
typedef struct {
    uint8_t bits[MAX_CODE_LEN]; /* 编码位串, 0或1 */
    int16_t length;             /* 编码长度 */
} HuffmanCode;

/* 压缩文件头中的文件条目信息 */
typedef struct {
    char     filename[MAX_FILENAME]; /* 原始文件名 */
    uint32_t name_len;               /* 文件名长度 */
    uint64_t original_size;          /* 原始文件大小(字节) */
    uint64_t compressed_size;        /* 压缩后数据大小(字节) */
} FileEntry;

/* ==================== 全局变量 ==================== */
static HuffmanNode tree[MAX_TREE_NODES];
static HuffmanCode codes[256];
static int tree_size;           /* 霍夫曼树实际节点数 */

/* ==================== 函数声明 ==================== */
static void     count_frequency(FILE *fp, uint64_t freq[256]);
static int      build_huffman_tree(uint64_t freq[256]);
static void     generate_codes(int root);
static void     write_bits(FILE *out, uint8_t *buffer, int *bit_pos,
                           const HuffmanCode *code);
static void     flush_bits(FILE *out, uint8_t *buffer, int *bit_pos);
static int      compress_single(FILE *in, FILE *out, const char *filename,
                                uint64_t orig_size);
static int      decompress_single(FILE *in, FILE *out);
static int      do_compress(const char *input_path, const char *output_path);
static int      do_decompress(const char *input_path, const char *output_dir);
static int      do_multi_compress(int filec, char *filev[],
                                  const char *output_path);
static int      do_multi_decompress(const char *input_path,
                                    const char *output_dir);
static void     print_usage(void);
static double   get_time_ms(void);

/* ==================== 频率统计 ==================== */

/*
 * count_frequency: 统计文件中每个字节(0x00-0xFF)出现的频次
 * 参数: fp - 已打开的文件指针(从当前位置读到EOF)
 *       freq - 长度256的数组, 输出各字节频次
 */
static void count_frequency(FILE *fp, uint64_t freq[256])
{
    uint8_t buf[BUFFER_SIZE];
    size_t  nread;

    memset(freq, 0, sizeof(uint64_t) * 256);

    while ((nread = fread(buf, 1, BUFFER_SIZE, fp)) > 0) {
        for (size_t i = 0; i < nread; i++) {
            freq[buf[i]]++;
        }
    }
}

/* ==================== 霍夫曼树构建 ==================== */

/*
 * build_huffman_tree: 根据频次数组构建霍夫曼树
 * 参数: freq - 256个字节的频次数组
 * 返回: 根节点在tree数组中的索引
 *
 * 算法: 每次从所有未使用的节点中选出权值最小的两个节点,
 *       合并为一个新父节点, 直到只剩一个根节点。
 */
static int build_huffman_tree(uint64_t freq[256])
{
    int i, min1, min2;
    int count = 0;  /* 叶节点计数 */

    /* 初始化所有节点 */
    for (i = 0; i < MAX_TREE_NODES; i++) {
        tree[i].weight = 0;
        tree[i].parent = -1;
        tree[i].left   = -1;
        tree[i].right  = -1;
        tree[i].ch     = 0;
    }

    /* 创建叶节点: 仅对频次>0的字节创建节点 */
    tree_size = 0;
    for (i = 0; i < 256; i++) {
        if (freq[i] > 0) {
            tree[tree_size].weight = freq[i];
            tree[tree_size].ch = (uint8_t)i;
            tree[tree_size].left   = -1;
            tree[tree_size].right  = -1;
            tree[tree_size].parent = -1;
            tree_size++;
            count++;
        }
    }

    /* 特殊情况: 文件中只有一个不同的字符 */
    if (count == 1) {
        /* 只有一个字符时, 创建一个父节点, 左子为该叶节点 */
        tree[tree_size].weight  = tree[0].weight;
        tree[tree_size].left    = 0;
        tree[tree_size].right   = -1;
        tree[tree_size].parent  = -1;
        tree[0].parent = (int16_t)tree_size;
        tree_size++;
        return tree_size - 1;
    }

    /* 特殊情况: 文件为空 */
    if (count == 0) {
        return -1;
    }

    /* 循环合并: 每次选出两个最小权值节点合并 */
    while (1) {
        min1 = -1;
        min2 = -1;

        /* 找最小权值的两个未合并节点 */
        for (i = 0; i < tree_size; i++) {
            if (tree[i].parent == -1) {  /* 未被合并 */
                if (min1 == -1 || tree[i].weight < tree[min1].weight) {
                    min2 = min1;
                    min1 = i;
                } else if (min2 == -1 ||
                           tree[i].weight < tree[min2].weight) {
                    min2 = i;
                }
            }
        }

        if (min2 == -1) {
            /* 只剩一个节点, 即为根节点 */
            break;
        }

        /* 创建新父节点 */
        tree[tree_size].weight = tree[min1].weight + tree[min2].weight;
        tree[tree_size].left   = (int16_t)min1;
        tree[tree_size].right  = (int16_t)min2;
        tree[tree_size].parent = -1;

        tree[min1].parent = (int16_t)tree_size;
        tree[min2].parent = (int16_t)tree_size;

        tree_size++;
    }

    /* 返回根节点索引(最后一个创建的节点) */
    return tree_size - 1;
}

/* ==================== 编码生成 ==================== */

/*
 * generate_codes: 递归遍历霍夫曼树, 生成每个叶节点字符的编码
 * 参数: root - 当前子树根节点索引
 *
 * 遍历规则: 向左走记录0, 向右走记录1
 * 叶节点处保存完整编码
 */
static void generate_codes(int root)
{
    static uint8_t path[MAX_CODE_LEN];
    static int16_t depth = 0;

    if (root < 0) return;

    /* 叶节点: 保存编码 */
    if (tree[root].left == -1 && tree[root].right == -1) {
        memcpy(codes[tree[root].ch].bits, path, depth);
        codes[tree[root].ch].length = depth;
        /* 特殊: 如果depth==0(只有一个字符), 编码长度设为1 */
        if (depth == 0) {
            codes[tree[root].ch].bits[0] = 0;
            codes[tree[root].ch].length = 1;
        }
        return;
    }

    /* 向左走: 编码位为0 */
    if (tree[root].left >= 0) {
        path[depth++] = 0;
        generate_codes(tree[root].left);
        depth--;
    }

    /* 向右走: 编码位为1 */
    if (tree[root].right >= 0) {
        path[depth++] = 1;
        generate_codes(tree[root].right);
        depth--;
    }
}

/* ==================== 位操作工具 ==================== */

/*
 * write_bits: 将一个字符的霍夫曼编码写入输出缓冲区
 * 参数: out - 输出文件
 *       buffer - 位缓冲区(1字节)
 *       bit_pos - 当前位位置(0-7)
 *       code - 待写入字符的编码
 */
static void write_bits(FILE *out, uint8_t *buffer, int *bit_pos,
                       const HuffmanCode *code)
{
    for (int i = 0; i < code->length; i++) {
        if (code->bits[i]) {
            *buffer |= (1 << (7 - *bit_pos));
        }
        (*bit_pos)++;
        if (*bit_pos >= 8) {
            fwrite(buffer, 1, 1, out);
            *buffer = 0;
            *bit_pos = 0;
        }
    }
}

/*
 * flush_bits: 将缓冲区中不足8位的尾部写入文件
 */
static void flush_bits(FILE *out, uint8_t *buffer, int *bit_pos)
{
    if (*bit_pos > 0) {
        fwrite(buffer, 1, 1, out);
        *buffer = 0;
        *bit_pos = 0;
    }
}

/* ==================== 单文件压缩 ==================== */

/*
 * compress_single: 压缩单个文件的数据到输出流
 * 参数: in - 输入文件(已打开, 从头读取)
 *       out - 输出文件(已打开, 写入压缩数据)
 *       filename - 文件名(写入归档头)
 *       orig_size - 原始文件大小
 * 返回: 0成功, -1失败
 *
 * 数据格式:
 *   [频次表: 256*8字节]
 *   [原始大小: 8字节]
 *   [压缩数据大小: 8字节(先占位, 后回填)]
 *   [压缩数据: 变长]
 */
static int compress_single(FILE *in, FILE *out, const char *filename,
                           uint64_t orig_size)
{
    uint64_t freq[256];
    uint8_t  buf[BUFFER_SIZE];
    size_t   nread;
    int      root;
    uint8_t  byte_buf = 0;
    int      bit_pos = 0;
    long     size_pos;
    uint64_t comp_size;
    uint32_t name_len = (uint32_t)strlen(filename);

    /* 统计频次 */
    count_frequency(in, freq);

    /* 构建霍夫曼树 */
    root = build_huffman_tree(freq);

    /* 生成编码 */
    memset(codes, 0, sizeof(codes));
    if (root >= 0) {
        generate_codes(root);
    }

    /* 写入文件名 */
    fwrite(&name_len, sizeof(uint32_t), 1, out);
    fwrite(filename, 1, name_len, out);

    /* 写入原始大小 */
    fwrite(&orig_size, sizeof(uint64_t), 1, out);

    /* 记录压缩数据大小位置(稍后回填) */
    size_pos = ftell(out);
    comp_size = 0;
    fwrite(&comp_size, sizeof(uint64_t), 1, out);

    /* 写入频次表(256个uint64_t) */
    fwrite(freq, sizeof(uint64_t), 256, out);

    /* 重新读取文件并编码 */
    fseek(in, 0, SEEK_SET);

    while ((nread = fread(buf, 1, BUFFER_SIZE, in)) > 0) {
        for (size_t i = 0; i < nread; i++) {
            write_bits(out, &byte_buf, &bit_pos, &codes[buf[i]]);
        }
    }

    flush_bits(out, &byte_buf, &bit_pos);

    /* 回填压缩数据大小 */
    comp_size = (uint64_t)(ftell(out) - size_pos - sizeof(uint64_t) -
                           256 * sizeof(uint64_t));
    fseek(out, size_pos, SEEK_SET);
    fwrite(&comp_size, sizeof(uint64_t), 1, out);
    fseek(out, 0, SEEK_END);

    return 0;
}

/* ==================== 单文件解压 ==================== */

/*
 * decompress_single: 从归档流中解压单个文件
 * 参数: in - 归档文件(已定位到该文件数据起始)
 *       out - 输出文件(已打开, 写入解压数据)
 * 返回: 0成功, -1失败
 */
static int decompress_single(FILE *in, FILE *out)
{
    uint64_t freq[256];
    uint64_t orig_size, comp_size;
    int      root;
    int      current;
    uint64_t written = 0;
    uint8_t  byte_buf;
    int      bit_pos = 8;   /* 初始为8触发首次读取 */

    /* 跳过文件名 */
    uint32_t name_len;
    char     filename[MAX_FILENAME];
    fread(&name_len, sizeof(uint32_t), 1, in);
    fread(filename, 1, name_len, in);
    filename[name_len] = '\0';

    /* 读取原始大小和压缩数据大小 */
    fread(&orig_size, sizeof(uint64_t), 1, in);
    fread(&comp_size, sizeof(uint64_t), 1, in);

    /* 读取频次表 */
    fread(freq, sizeof(uint64_t), 256, in);

    /* 构建霍夫曼树 */
    root = build_huffman_tree(freq);

    if (root < 0) {
        /* 空文件 */
        return 0;
    }

    /* 记录压缩数据起始位置 */
    long data_start = ftell(in);

    /* 解压: 沿霍夫曼树下行, 到叶节点输出字符 */
    current = root;
    byte_buf = 0;
    bit_pos = 8;

    while (written < orig_size) {
        if (bit_pos >= 8) {
            if (fread(&byte_buf, 1, 1, in) != 1) break;
            bit_pos = 0;
        }

        int bit = (byte_buf >> (7 - bit_pos)) & 1;
        bit_pos++;

        if (bit == 0)
            current = tree[current].left;
        else
            current = tree[current].right;

        if (current < 0) break;  /* 防御性检查 */

        /* 到达叶节点 */
        if (tree[current].left == -1 && tree[current].right == -1) {
            uint8_t ch = tree[current].ch;
            fwrite(&ch, 1, 1, out);
            written++;
            current = root;
        }
    }

    return (written == orig_size) ? 0 : -1;
}

/* ==================== 单文件压缩入口 ==================== */

/*
 * do_compress: 压缩单个文件
 * 格式: [MAGIC(4B)][VERSION(4B)][FILE_COUNT(4B=1)][FILE_ENTRY...]
 */
static int do_compress(const char *input_path, const char *output_path)
{
    FILE *in, *out;
    struct stat st;
    uint64_t orig_size;
    uint32_t magic = MAGIC_NUMBER;
    uint32_t version = ARCHIVE_VERSION;
    uint32_t file_count = 1;
    double t_start, t_end;

    in = fopen(input_path, "rb");
    if (!in) {
        fprintf(stderr, "错误: 无法打开输入文件 '%s'\n", input_path);
        return -1;
    }

    if (stat(input_path, &st) != 0) {
        fclose(in);
        fprintf(stderr, "错误: 无法获取文件信息 '%s'\n", input_path);
        return -1;
    }
    orig_size = (uint64_t)st.st_size;

    out = fopen(output_path, "wb");
    if (!out) {
        fclose(in);
        fprintf(stderr, "错误: 无法创建输出文件 '%s'\n", output_path);
        return -1;
    }

    t_start = get_time_ms();

    /* 写入归档头 */
    fwrite(&magic, sizeof(uint32_t), 1, out);
    fwrite(&version, sizeof(uint32_t), 1, out);
    fwrite(&file_count, sizeof(uint32_t), 1, out);

    /* 压缩文件 */
    compress_single(in, out, input_path, orig_size);

    t_end = get_time_ms();

    fclose(in);
    fclose(out);

    /* 输出统计信息 */
    {
        struct stat out_st;
        stat(output_path, &out_st);
        double ratio = (orig_size > 0) ?
            (1.0 - (double)out_st.st_size / (double)orig_size) * 100.0 : 0.0;
        printf("压缩完成:\n");
        printf("  原始大小:    %llu 字节\n", (unsigned long long)orig_size);
        printf("  压缩后大小:  %llu 字节\n",
               (unsigned long long)out_st.st_size);
        printf("  压缩率:      %.2f%%\n", ratio);
        printf("  压缩时间:    %.2f 毫秒\n", t_end - t_start);
    }

    return 0;
}

/* ==================== 单文件解压入口 ==================== */

/*
 * do_decompress: 解压单文件归档
 */
static int do_decompress(const char *input_path, const char *output_dir)
{
    FILE *in, *out;
    uint32_t magic, version, file_count;
    char output_path[MAX_FILENAME * 2];
    double t_start, t_end;

    in = fopen(input_path, "rb");
    if (!in) {
        fprintf(stderr, "错误: 无法打开归档文件 '%s'\n", input_path);
        return -1;
    }

    /* 读取归档头 */
    fread(&magic, sizeof(uint32_t), 1, in);
    if (magic != MAGIC_NUMBER) {
        fclose(in);
        fprintf(stderr, "错误: 不是有效的Huffman压缩文件\n");
        return -1;
    }

    fread(&version, sizeof(uint32_t), 1, in);
    fread(&file_count, sizeof(uint32_t), 1, in);

    if (file_count != 1) {
        fclose(in);
        fprintf(stderr, "错误: 非单文件归档, 请使用多文件解压模式\n");
        return -1;
    }

    t_start = get_time_ms();

    /* 从归档中读取原始文件名 */
    {
        uint32_t name_len;
        char orig_filename[MAX_FILENAME];
        long pos = ftell(in);

        fread(&name_len, sizeof(uint32_t), 1, in);
        if (name_len >= MAX_FILENAME) name_len = MAX_FILENAME - 1;
        fread(orig_filename, 1, name_len, in);
        orig_filename[name_len] = '\0';

        /* 回退到条目起始, 让decompress_single正常读取 */
        fseek(in, pos, SEEK_SET);

        /* 仅取文件名部分(去掉可能的路径前缀) */
        const char *base = strrchr(orig_filename, '/');
        if (!base) base = strrchr(orig_filename, '\\');
        base = base ? base + 1 : orig_filename;

        /* 构造输出路径 */
        if (output_dir && strlen(output_dir) > 0) {
            snprintf(output_path, sizeof(output_path), "%s/%s",
                     output_dir, base);
        } else {
            snprintf(output_path, sizeof(output_path), "%s", base);
        }
    }

    out = fopen(output_path, "wb");
    if (!out) {
        fclose(in);
        fprintf(stderr, "错误: 无法创建输出文件 '%s'\n", output_path);
        return -1;
    }

    decompress_single(in, out);

    t_end = get_time_ms();

    fclose(in);
    fclose(out);

    printf("解压完成:\n");
    printf("  输出文件:    %s\n", output_path);
    printf("  解压时间:    %.2f 毫秒\n", t_end - t_start);

    return 0;
}

/* ==================== 多文件压缩 ==================== */

/*
 * do_multi_compress: 将多个文件压缩为一个归档
 * 参数: filec - 文件数量
 *       filev - 文件路径数组
 *       output_path - 输出归档路径
 */
static int do_multi_compress(int filec, char *filev[],
                             const char *output_path)
{
    FILE *out, *in;
    uint32_t magic = MAGIC_NUMBER;
    uint32_t version = ARCHIVE_VERSION;
    uint32_t count = (uint32_t)filec;
    uint64_t total_orig = 0, total_comp = 0;
    double t_start, t_end;

    out = fopen(output_path, "wb");
    if (!out) {
        fprintf(stderr, "错误: 无法创建输出文件 '%s'\n", output_path);
        return -1;
    }

    t_start = get_time_ms();

    /* 写入归档头 */
    fwrite(&magic, sizeof(uint32_t), 1, out);
    fwrite(&version, sizeof(uint32_t), 1, out);
    fwrite(&count, sizeof(uint32_t), 1, out);

    /* 依次压缩每个文件 */
    for (int i = 0; i < filec; i++) {
        struct stat st;

        in = fopen(filev[i], "rb");
        if (!in) {
            fprintf(stderr, "警告: 跳过无法打开的文件 '%s'\n", filev[i]);
            continue;
        }

        if (stat(filev[i], &st) != 0) {
            fclose(in);
            fprintf(stderr, "警告: 跳过无法获取信息的文件 '%s'\n", filev[i]);
            continue;
        }

        total_orig += (uint64_t)st.st_size;

        compress_single(in, out, filev[i], (uint64_t)st.st_size);

        fclose(in);
        printf("  已压缩: %s\n", filev[i]);
    }

    t_end = get_time_ms();

    fclose(out);

    /* 输出统计 */
    {
        struct stat out_st;
        stat(output_path, &out_st);
        total_comp = (uint64_t)out_st.st_size;
        double ratio = (total_orig > 0) ?
            (1.0 - (double)total_comp / (double)total_orig) * 100.0 : 0.0;

        printf("多文件压缩完成:\n");
        printf("  文件数量:    %d\n", filec);
        printf("  原始总大小:  %llu 字节\n",
               (unsigned long long)total_orig);
        printf("  压缩后大小:  %llu 字节\n",
               (unsigned long long)total_comp);
        printf("  压缩率:      %.2f%%\n", ratio);
        printf("  压缩时间:    %.2f 毫秒\n", t_end - t_start);
    }

    return 0;
}

/* ==================== 多文件解压 ==================== */

/*
 * do_multi_decompress: 解压多文件归档
 * 参数: input_path - 归档文件路径
 *       output_dir - 输出目录(可为NULL)
 */
static int do_multi_decompress(const char *input_path,
                               const char *output_dir)
{
    FILE *in, *out;
    uint32_t magic, version, file_count;
    char output_path[MAX_FILENAME * 2];
    double t_start, t_end;

    in = fopen(input_path, "rb");
    if (!in) {
        fprintf(stderr, "错误: 无法打开归档文件 '%s'\n", input_path);
        return -1;
    }

    fread(&magic, sizeof(uint32_t), 1, in);
    if (magic != MAGIC_NUMBER) {
        fclose(in);
        fprintf(stderr, "错误: 不是有效的Huffman压缩文件\n");
        return -1;
    }

    fread(&version, sizeof(uint32_t), 1, in);
    fread(&file_count, sizeof(uint32_t), 1, in);

    t_start = get_time_ms();

    printf("解压 %u 个文件...\n", file_count);

    for (uint32_t i = 0; i < file_count; i++) {
        /* 先读取文件名以构造输出路径 */
        uint32_t name_len;
        char filename[MAX_FILENAME];
        long pos = ftell(in);

        fread(&name_len, sizeof(uint32_t), 1, in);
        fread(filename, 1, name_len, in);
        filename[name_len] = '\0';

        /* 回退到条目起始 */
        fseek(in, pos, SEEK_SET);

        /* 仅取文件名部分(去掉路径前缀) */
        const char *base = strrchr(filename, '/');
        if (!base) base = strrchr(filename, '\\');
        base = base ? base + 1 : filename;

        /* 构造输出路径 */
        if (output_dir && strlen(output_dir) > 0) {
            snprintf(output_path, sizeof(output_path), "%s/%s",
                     output_dir, base);
        } else {
            snprintf(output_path, sizeof(output_path), "%s", base);
        }

        out = fopen(output_path, "wb");
        if (!out) {
            fprintf(stderr, "警告: 无法创建文件 '%s', 跳过\n", output_path);
            /* 需要跳过此文件条目的数据 */
            /* 读取原始大小和压缩大小以计算跳过量 */
            uint64_t orig_s, comp_s;
            fseek(in, pos, SEEK_SET);
            fseek(in, name_len, SEEK_CUR);  /* 跳过name_len+filename */
            fread(&orig_s, sizeof(uint64_t), 1, in);
            fread(&comp_s, sizeof(uint64_t), 1, in);
            fseek(in, 256 * sizeof(uint64_t), SEEK_CUR); /* 跳过频次表 */
            fseek(in, (long)comp_s, SEEK_CUR);           /* 跳过压缩数据 */
            continue;
        }

        decompress_single(in, out);
        fclose(out);

        printf("  已解压: %s\n", output_path);
    }

    t_end = get_time_ms();

    fclose(in);

    printf("解压完成:\n");
    printf("  解压时间:    %.2f 毫秒\n", t_end - t_start);

    return 0;
}

/* ==================== 辅助函数 ==================== */

/*
 * get_time_ms: 获取当前时间(毫秒), 用于性能测量
 */
static double get_time_ms(void)
{
    struct timespec ts;
#ifdef _WIN32
    /* Windows下使用clock()作为备选 */
    return (double)clock() / CLOCKS_PER_SEC * 1000.0;
#else
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
#endif
}

/*
 * print_usage: 打印命令行使用说明
 */
static void print_usage(void)
{
    printf("Huffman文件压缩工具 v1.0\n");
    printf("用法:\n");
    printf("  huffman -c <输入文件> <输出文件>         压缩单个文件\n");
    printf("  huffman -x <压缩文件> [输出目录]         解压单个文件\n");
    printf("  huffman -mc <输出文件> <文件1> [文件2...] 多文件压缩\n");
    printf("  huffman -mx <压缩文件> [输出目录]        多文件解压\n");
    printf("  huffman -t <压缩文件> <原始文件>          测试压缩率\n");
    printf("  huffman -h                               显示帮助\n");
    printf("\n示例:\n");
    printf("  huffman -c test.txt test.huf\n");
    printf("  huffman -x test.huf ./output/\n");
    printf("  huffman -mc archive.huf a.txt b.txt c.txt\n");
    printf("  huffman -mx archive.huf ./output/\n");
}

/* ==================== 主函数 ==================== */

int main(int argc, char *argv[])
{
    if (argc < 2) {
        print_usage();
        return 1;
    }

    if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
        print_usage();
        return 0;
    }

    /* 单文件压缩: huffman -c <input> <output> */
    if (strcmp(argv[1], "-c") == 0) {
        if (argc < 4) {
            fprintf(stderr, "用法: huffman -c <输入文件> <输出文件>\n");
            return 1;
        }
        return do_compress(argv[2], argv[3]);
    }

    /* 单文件解压: huffman -x <input> [output_dir] */
    if (strcmp(argv[1], "-x") == 0) {
        if (argc < 3) {
            fprintf(stderr, "用法: huffman -x <压缩文件> [输出目录]\n");
            return 1;
        }
        const char *outdir = (argc >= 4) ? argv[3] : ".";
        return do_decompress(argv[2], outdir);
    }

    /* 多文件压缩: huffman -mc <output> <file1> [file2...] */
    if (strcmp(argv[1], "-mc") == 0) {
        if (argc < 4) {
            fprintf(stderr,
                    "用法: huffman -mc <输出文件> <文件1> [文件2...]\n");
            return 1;
        }
        return do_multi_compress(argc - 3, &argv[3], argv[2]);
    }

    /* 多文件解压: huffman -mx <input> [output_dir] */
    if (strcmp(argv[1], "-mx") == 0) {
        if (argc < 3) {
            fprintf(stderr, "用法: huffman -mx <压缩文件> [输出目录]\n");
            return 1;
        }
        const char *outdir = (argc >= 4) ? argv[3] : ".";
        return do_multi_decompress(argv[2], outdir);
    }

    /* 压缩率测试: huffman -t <compressed> <original> */
    if (strcmp(argv[1], "-t") == 0) {
        if (argc < 4) {
            fprintf(stderr, "用法: huffman -t <压缩文件> <原始文件>\n");
            return 1;
        }
        struct stat cs, os;
        if (stat(argv[2], &cs) != 0 || stat(argv[3], &os) != 0) {
            fprintf(stderr, "错误: 无法获取文件信息\n");
            return 1;
        }
        double ratio = (os.st_size > 0) ?
            (1.0 - (double)cs.st_size / (double)os.st_size) * 100.0 : 0.0;
        printf("原始大小:    %lld 字节\n", (long long)os.st_size);
        printf("压缩后大小:  %lld 字节\n", (long long)cs.st_size);
        printf("压缩率:      %.2f%%\n", ratio);
        return 0;
    }

    fprintf(stderr, "未知选项: %s\n", argv[1]);
    print_usage();
    return 1;
}

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "cachelab.h"

void print_help() {
    printf("Usage: ./csim-ref [-hv] -s <num> -E <num> -b <num> -t <file>\n");
    printf("Options:\n");
    printf("  -h         Print this help message.\n");
    printf("  -v         Optional verbose flag.\n");
    printf("  -s <num>   Number of set index bits.\n");
    printf("  -E <num>   Number of lines per set.\n");
    printf("  -b <num>   Number of block offset bits.\n");
    printf("  -t <file>  Trace file.\n");
    printf("\n");
    printf("Examples:\n");
    printf("  linux>  ./csim-ref -s 4 -E 1 -b 4 -t traces/yi.trace\n");
    printf("  linux>  ./csim-ref -v -s 8 -E 2 -b 4 -t traces/yi.trace\n");
}

typedef struct cache_line_ {
    int valid;       // 有效位
    int tag;         // 标志位
    int time_stamp;  // 时间戳，用于 LRU 替换
} CacheLine;

CacheLine **cache;

int hit_count = 0, miss_count = 0, eviction_count = 0;
int s, b, E, S, B;
int verbose;
char t[100];

void init_cache() {
    S = 1 << s;
    B = 1 << b;
    cache = (CacheLine **)malloc(sizeof(CacheLine *) * S);
    for (int i = 0; i < S; i++) {
        cache[i] = (CacheLine *)malloc(sizeof(CacheLine) * E);
        for (int j = 0; j < E; j++) {
            cache[i][j].valid = 0;
            cache[i][j].tag = 0;
            cache[i][j].time_stamp = 0;
        }
    }
}

void LRU_time_inc(int g) {
    for (int i = 0; i < E; i++)
        if (cache[g][i].valid) cache[g][i].time_stamp += 1;
}

int LRU_evic_index(int group) {
    int max_time = -1, line;
    for (int i = 0; i < E; i++) {
        if (cache[group][i].time_stamp > max_time) {
            max_time = cache[group][i].time_stamp;
            line = i;
        }
    }
    return line;
}

int hit_index(int tag, int group) {
    for (int i = 0; i < E; i++)
        if (cache[group][i].valid && cache[group][i].tag == tag) return i;
    return -1;
}

int empty_index(int group) {
    for (int i = 0; i < E; i++)
        if (!cache[group][i].valid) return i;
    return -1;
}

void update_cache(int tag, int group) {
    LRU_time_inc(group);                  // 增加时间戳三种情况都会增加

    int idx = hit_index(tag, group);
    if (idx != -1) {            // 命中
        if (verbose) printf("hit ");
        hit_count++;
        cache[group][idx].time_stamp = 0;
        return;
    }

    if (verbose) printf("miss ");
    miss_count++;

    int empty_idx = empty_index(group), target_idx;
    if (empty_idx != -1) {  // 未命中但是有空行
        target_idx = empty_idx;
    } else {  // 未命中且无空行
        if (verbose) printf("eviction ");
        eviction_count++;
        target_idx = LRU_evic_index(group);
    }
    cache[group][target_idx].valid = 1;
    cache[group][target_idx].tag = tag;
    cache[group][target_idx].time_stamp = 0;
}

void sim() {
    FILE *fp;
    fp = fopen(t, "r");
    if (fp == NULL) {
        exit(-1);
    }
    char op;
    unsigned long address;
    int size;
    while (fscanf(fp, " %c %lx,%d", &op, &address, &size) != -1) {  // 假设都是对齐的，所以 size 没有用
        if (verbose && op != 'I') printf("%c %lx,%d ", op, address, size);
        int tag = address >> (s + b);
        int group = (address & ((1 << (s + b)) - 1)) >> b;
        // 只需要用一个 update 来模拟
        switch (op) {
            case 'M':
                update_cache(tag, group);
                update_cache(tag, group);
                break;
            case 'L':
                update_cache(tag, group);
                break;
            case 'S':
                update_cache(tag, group);
                break;
            default:
                continue;
        }
        if (verbose) printf("\n");
    }
    fclose(fp);
}

int main(int argc, char *argv[]) {
    char opt;
    const char *optstring = "hvs:E:b:t:";
    while ((opt = getopt(argc, argv, optstring)) != -1) {
        switch (opt) {
            case 'h':
                print_help();
                exit(-1);
            case 'v':
                verbose = 1;
                break;
            case 's':
                s = atoi(optarg);
                break;
            case 'E':
                E = atoi(optarg);
                break;
            case 'b':
                b = atoi(optarg);
                break;
            case 't':
                strcpy(t, optarg);
                break;
            default:
                printf("unknown args");
                print_help();
                exit(-1);
        }
    }
    init_cache();
    sim();
    printSummary(hit_count, miss_count, eviction_count);
    return 0;
}

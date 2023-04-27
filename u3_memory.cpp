// -*- mode:c++; coding:utf-8-ws -*-
/*
ja: 
空き領域と使用領域を別リストにする。
確保・解放に多少のコストはかかるが、要素の個数に影響しない平均的な速度になる。
解放後に、空き領域リストに繋げる。
そのとき、連続した空き領域であれば結合を行う。
*/
#include <cstdint>

#ifdef WIN32
    #ifdef MEM_DEBUG
        #include <stdio.h>   // vsprintf
        #include <stdarg.h>  // va_list
    #endif
#else
    #include <nitro.h>
#endif

#include "u3_types.h"
#include "u3_memory.h"

#ifdef WIN32
#endif
#ifdef BCC32
#else
    #define __inline
#endif

#define RoundUp(size, alignment) (((size) + (alignment)-1) & ~((alignment)-1))

#define BLOCK_SIZE (int)sizeof(BLOCK)
#define INFO_SIZE (int)sizeof(INFO)

//#define U3_R_TYPE       int32_t
#define U3_R_TYPE int64_t
#define U3_R_TYPE_SIZE sizeof(U3_R_TYPE)


typedef struct tagMemoryBlock {
    struct tagMemoryBlock* prev;
    struct tagMemoryBlock* next;
    long size;              //ja: 空き領域リスト  ：空きサイズ
                            //ja: アロケートリスト：使用サイズ
#ifdef MEM_DEBUG
    long checkLINE;
#else
    long reserve1;
#endif
} BLOCK;


typedef struct tagInfo {
    BLOCK* freeTop;
    BLOCK* freeBottom;
    BLOCK* allocTop;
    long allocCount;

#ifdef MEM_DEBUG
    long debugAllocCount;
    long debugFreeCount;
    long reserve1;
    long reserve2;
#endif
} INFO;

#ifdef MEM_DEBUG
static int s_CheckLINE = 0;
static uint8_t s_CheckFILE = 0;
static char s_NameCheckFILE[256] = "";
#endif


static __inline BLOCK* firstFrontBlock(BLOCK* block, int size) {
    for (/* None */; block; block = block->next) {
        //ja: リストの中に納まるサイズの要素があった
        if (size <= block->size) {
            break;
        }
    }

    return block;
}

static __inline BLOCK* firstBackBlock(BLOCK* block, int size) {
    for (/* None */; block; block = block->prev) {
        //ja: リストの中に納まるサイズの要素があった
        if (size <= block->size) {
            break;
        }
    }

    return block;
}

static __inline void linkFreeList(INFO* info, BLOCK* block) {
    BLOCK* pos;

    //ja: block に一番近い要素を探す
    for (pos = info->freeTop; pos; pos = pos->next) {
        if (block < pos) {
            break;
        }
    }

    //ja: block が先頭
    if (pos == 0) {
        pos = info->freeTop;  //ja: 現在地を freeTop にする

        //ja: 空きリストは block だけになる
        if (pos == 0) {
            // 空き領域リスト作成
            block->prev = 0;
            block->next = 0;

            // リスト設定
            info->freeTop = block;
            info->freeBottom = block;
        } else {
            //ja: block と pos は隣り合わせ
            if ((char*)block + block->size == (char*)pos) {
                //ja: block と pos を結合
                BLOCK* next = pos->next;

                block->prev = 0;
                block->next = next;

                if (next) {
                    next->prev = block;

                } else /*if (next == 0)*/ {
                    info->freeBottom = block;
                }
            } else {  //ja: block と pos は隣り合っていない
                //ja: block と pos をつなぐ
                block->prev = 0;
                block->next = pos;

                pos->prev = block;

                info->freeTop = block;
            }
        }

    } else {  //ja: pos <-> block <-> pos->next でつなぐ
        if (pos->next) {
            //ja: pos と block は隣り合わせ
            if ((char*)pos + pos->size == (char*)block) {
                //ja: pos と block と pos->next は隣り合わせ。
                if ((char*)block + block->size == (char*)pos->next) {
                    //ja: pos と block と pos->next を結合
                    BLOCK* next = pos->next->next;

                    pos->size = pos->size + block->size + pos->next->size;
                    pos->next = next;

                    if (next) {
                        next->prev = pos;
                    } else /*if (next == 0)*/ {
                        info->freeBottom = pos;
                    }

                } else {  //ja: pos と block だけ隣り合わせ
                    //ja: pos と block を結合
                    pos->size = pos->size + block->size;
                }
            } else if ((char*)block + block->size ==
                       (char*)pos->next) {  //ja: block と pos->next は隣り合わせ
                //ja: block と pos->next を結合
                BLOCK* next = pos->next->next;

                block->size = block->size + pos->next->size;
                block->next = next;

                if (next) {
                    next->prev = block;
                } else /*if (next == 0)*/ {
                    info->freeBottom = block;
                }
            }
        } else /*if (pos->next == 0)*/ {
            //ja: pos と block は隣り合わせ
            if ((char*)pos + pos->size == (char*)block) {
                //ja: pos と block を結合
                pos->size = pos->size + block->size;

            } else {  //ja: pos と block は隣り合っていない
                //ja: pos と block とつなげる
                pos->next = block;

                block->prev = pos;
                block->next = 0;

                info->freeBottom = block;
            }
        }
    }
}

static __inline void unlinkFreeList(INFO* info, BLOCK* block) {
    BLOCK* prev = block->prev;
    BLOCK* next = block->next;

    if (prev) {
        //ja: ○ prev ○ next
        if (next) {
            prev->next = next;
            next->prev = prev;
        } else /*if (next == 0)*/ {  //ja: ○ prev × next
            prev->next = 0;
            info->freeBottom = prev;
        }
    } else /*if (prev == 0)*/ {
        //ja: × prev ○ next
        if (next) {
            next->prev = 0;
            info->freeTop = next;
        } else /*if (next == 0)*/ {  //ja: × prev × next
            //ja: ここにくるとしたら空きがちょうどなくなったとき
            info->freeTop = 0;
            info->freeBottom = 0;
        }
    }
}

static __inline void linkAllocList(INFO* info, BLOCK* block) {
    //ja: アロケートリスト追加
    block->prev = 0;
    block->next = info->allocTop;

    //ja: allocTop があれば設定
    if (info->allocTop) {
        info->allocTop->prev = block;
    }

    info->allocTop = block;

    info->allocCount++;
}

static __inline void unlinkAllocList(INFO* info, BLOCK* block) {
    BLOCK* prev = block->prev;
    BLOCK* next = block->next;

    if (prev) {
        if (next) {
            prev->next = next;
            next->prev = prev;
        } else /*if (next == 0)*/ {
            prev->next = 0;
        }
    } else /*if (prev == 0)*/ {
        if (next) {
            next->prev = 0;
            info->allocTop = next;
        } else {
            info->allocTop = 0;
        }
    }

    info->allocCount--;
}

namespace u3 {

Memory::Memory() : addr_(nullptr), size_(0) {}

void Memory::initialize(void* addr, int size) {
    INFO* info = (INFO*)addr;
    BLOCK* block;

#ifdef MEM_DEBUG
    if (addr == 0) {
        printf("アドレスが不正\n");
        return;
    }
#endif

    //ja: 空き領域リスト作成（空きとして最初に１つ作成しておく）
    block = (BLOCK*)((char*)addr + INFO_SIZE);
    block->prev = 0;
    block->next = 0;
    block->size = size - INFO_SIZE;

    //ja: バッファの設定
    info->freeTop = block;
    info->freeBottom = block;
    info->allocTop = 0;
    info->allocCount = 0;

#ifdef MEM_DEBUG
    info->debugAllocCount = 0;
    info->debugFreeCount = 0;
#endif

    addr_ = addr;
    size_ = size;

#ifdef MEM_DEBUG
    printf("=======================================================\n");
    printf("MEMOEY CREATE!!\n");
    printf("  Area: 0x%.8x - 0x%.8x\n", addr, (char*)addr + size - 1);
    printf("  size: %d\n", size);
    printf("=======================================================\n");
#endif
}

void Memory::clear() {
    initialize(addr_, size_);
}

void* Memory::allocate(int size, int alignment) {
    INFO* info;
    BLOCK* block;
    BLOCK* add;

    if (size < 0) {
#ifdef MEM_DEBUG
        printf("サイズの指定が不正です\n");
#endif
        return (void*)0;
    }

    info = (INFO*)addr_;

#ifdef MEM_DEBUG
    size = RoundUp(size + BLOCK_SIZE + s_CheckFILE, alignment);
#else
    size = RoundUp(size + BLOCK_SIZE, alignment);
#endif

    //ja: メモリブロックの取得（前から）
    if (alignment > 0) {
        block = firstFrontBlock(info->freeTop, size);

        if (block == 0) {
#ifdef MEM_DEBUG
            printf("FRONT:有効なメモリブロックが見つかりません(%d)\n", size);
#endif
            return (void*)0;
        }

        add = block;

        if (block->size == size) {
            /*
            * 取得した空きリストがサイズ0になったため，空き領域として使えない
            * そのため，空き領域リストから切断する必要がある
            */

            unlinkFreeList(info, block);
        } else {
            /*
            * 前方からの取得に伴い，先頭アドレスが変化してしまうため
            * 取得した空きリストの残りサイズ分のブロックを再構築し
            * 再度，空き領域リストに接続しなおす
            */

            int rem_size = block->size - size;  //ja: 残りサイズ

            unlinkFreeList(info, block);

            block = (BLOCK*)((char*)block + size);
            block->size = rem_size;  // 残りサイズ更新

            linkFreeList(info, block);
        }

    } else /*if (alignment < 0)*/ {  //ja: メモリブロックの取得（後ろから）
        block = firstBackBlock(info->freeBottom, size);

        if (block == 0) {
#ifdef MEM_DEBUG
            printf("REAR:有効なメモリブロックが見つかりません(%d)\n", size);
#endif
            return (void*)0;
        }

        add = (BLOCK*)((char*)block + block->size - size);

        if (block->size == size) {
            unlinkFreeList(info, block);
        } else {
            /*
            * 後方からの取得なので空き領域リストに登録されているブロックサイズの更新だけでOK
            * 新たに接続しなおす必要がない
            */

            block->size = block->size - size;
        }
    }

    //ja: アロケートリスト更新
    linkAllocList(info, add);

    add->size = size;

#ifdef MEM_DEBUG
    SetCheckPoint(add);

    info->debugAllocCount++;
#endif

#ifdef MEM_DEBUG
    return (char*)add + BLOCK_SIZE + s_CheckFILE;
#else
    return (char*)add + BLOCK_SIZE; //ja: 返すポインタに情報は含まず
#endif
}

void Memory::free(void* p) {
    INFO* info;
    BLOCK* block;

    if (p == 0) {
        return;
    }

    info = (INFO*)addr_;

#ifdef MEM_DEBUG
    block = (BLOCK*)((char*)p - BLOCK_SIZE - *((uint8_t*)P - 1));
#else
    block = (BLOCK*)((char*)p - BLOCK_SIZE);
#endif

    //ja: アロケートリスト更新
    unlinkAllocList(info, block);

    //ja: 空き領域リスト更新
    linkFreeList(info, block);

#ifdef MEM_DEBUG
    info->debugFreeCount++;
#endif
}

int Memory::getMaxFreeSize() const {
    INFO* info = (INFO*)addr_;
    BLOCK* pos;
    int size = 0;

    for (pos = info->freeTop; pos; pos = pos->next) {
        if (size < pos->size) {
            size = pos->size;
        }
    }

    return size;
}

int Memory::getTotalFreeSize() const {
    INFO* info = (INFO*)addr_;
    BLOCK* pos;
    int size = 0;

    for (pos = info->freeTop; pos; pos = pos->next) {
        size += pos->size;
    }

    return size;
}

void Memory::dump() {
#ifdef MEM_DEBUG
    INFO* info = (INFO*)addr_;
    BLOCK* pos;
    int total, count;

    printf("\n---------------------\n");
    printf(".debugAllocCount  : %d\n", info->debugAllocCount);
    printf(".debugFreeCount : %d\n", info->debugFreeCount);

    printf("----- space list -----\n");
    total = 0;
    count = 0;
    for (pos = info->freeTop; pos; pos = pos->next) {
        count++;
        printf("%5d: 0x%08x - 0x%08x %d\n", count, pos, (char*)pos + pos->size - 1, pos->size);
        total += pos->size;
    }
    printf("== Total      : %d(%dKB)\n", total, total / 1024);
    printf("== Count      : %d\n", count);

    printf("----- alloc list ------\n");
    total = 0;
    count = 0;
    for (pos = info->allocTop; pos; pos = pos->next) {
        count++;
        if (0 < pos->checkLINE) {
            printf("%5d: 0x%08x - 0x%08x %d, %s(%d)\n", count, pos, (char*)pos + pos->size - 1, pos->size,
                                                                    (char*)pos + BLOCK_SIZE, pos->checkLINE);
        } else {
            printf("%5d: 0x%08x - 0x%08x %d, %s(%d)\n", count, pos, (char*)pos + pos->size - 1, pos->size,
                                                                    (char*)pos + BLOCK_SIZE, pos->checkLINE);
        }
        total += pos->size;
    }
    printf("== Total      : %d(%dKB)\n", total, total / 1024);
    printf("== Count      : %d\n", count);
    printf("== .allocCount: %d\n", info->allocCount);

    printf("---------------------\n");
#endif
}

void Memory::SetCheckPoint(char* checkFILE, int checkLINE) {
#ifdef MEM_DEBUG
    char* dst = s_NameCheckFILE;

    if (!(*checkFILE)) {
        s_CheckFILE = 0;
        s_CheckLINE = checkLINE;
        return;
    }

    while (*checkFILE) {
        *dst++ = *checkFILE++;
    }
    *dst++ = '\0';

    s_CheckFILE = (uint8_t)RoundUp((dst - s_NameCheckFILE) + 1, 4);  //ja: ４バイト境界になるように, +1 バイトは文字列の長さ記録用
    s_CheckLINE = checkLINE;
#else
    (void)checkFILE;
    (void)checkLINE;
#endif
}

int Memory::Compare(void* A, void* B, int size) {
    U3_R_TYPE* a1 = (U3_R_TYPE*)A;
    U3_R_TYPE* b1 = (U3_R_TYPE*)B;

    while (U3_R_TYPE_SIZE <= size) {
        size -= U3_R_TYPE_SIZE;

        if (*a1 != *b1) {
            return int(*a1 - *b1);
        }

        a1++;
        b1++;
    }

    int8_t* a8 = (int8_t*)a1;
    int8_t* b8 = (int8_t*)b1;

    while (0 < size) {
        size -= 1;

        if (*a8 != *b8) {
            return int(*a8 - *b8);
        }

        a8++;
        b8++;
    }

    return 0;
}

void* Memory::Copy(void* A, void* B, int size) {
    U3_R_TYPE* a1 = (U3_R_TYPE*)A;
    U3_R_TYPE* b1 = (U3_R_TYPE*)B;

    while (U3_R_TYPE_SIZE <= size) {
        size -= U3_R_TYPE_SIZE;
        *a1++ = *b1++;
    }

    int8_t* a8 = (int8_t*)a1;
    int8_t* b8 = (int8_t*)b1;

    while (0 < size) {
        size -= 1;
        *a8++ = *b8++;
    }

    return A;
}

void* Memory::Fill(void* A, int ch, int size) {
    const int32_t ch_ = ((ch & 0xff) << 24) | ((ch & 0xff) << 16) | ((ch & 0xff) << 8) | (ch & 0xff);

    int32_t* a1 = (int32_t*)A;

    while (int32_size <= size) {
        size -= int32_size;
        *a1++ = ch_;
    }

    int8_t* a8 = (int8_t*)a1;

    while (0 < size) {
        size -= 1;
        *a8++ = ch_ & 0xff;
    }

    return A;
}

void* Memory::Zero(void* A, int size) {
    return Fill(A, 0, size);
}

}  //u3

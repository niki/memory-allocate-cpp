// -*- mode:c++; coding:utf-8-ws -*-
#ifndef U3_MEMORY_H
#define U3_MEMORY_H

//#define MEM_DEBUG		// ja: デバッグ機能アリ

#ifdef MEM_DEBUG
#define __MEM_CHECKPOINT__ mi::Memory::SetCheckPoint(__FILE__, __LINE__),
#else
#define __MEM_CHECKPOINT__ /* 指定なし */
#endif

namespace u3 {

class Memory {
    public:
        Memory();

        void initialize(void* addr, int size);
        void clear();

        void* allocate(int size, int alignment = sizeof(int));
        void free(void* p);

        int getMaxFreeSize() const;
        int getTotalFreeSize() const;

        void dump();

        static void SetCheckPoint(char* checkFILE, int checkLINE);
        static int Compare(void* A, void* B, int size);
        static void* Copy(void* A, void* B, int size);
        static void* Fill(void* A, int ch, int size);
        static void* Zero(void* A, int size);

    private:
        void* addr_;
        long size_;
};

} //u3

#endif //U3_MEMORY_H

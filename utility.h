#pragma once

#include "windowsInclude.h"
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <future>
#include <immintrin.h>
#include <vector>
#include <atomic>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <iostream>

#if defined(__clang__)
#define COMPILER_CLANG
#elif defined(__INTEL_COMPILER)
#define COMPILER_INTEL
#elif defined(_MSC_VER)
#define COMPILER_MSVC
#elif defined(__GNUC__)
#define COMPILER_GCC
#endif

class ThreadPoolAsync {
    std::vector<std::future<void>> tasks;
    std::vector<std::shared_future<void>> sharedTasks;
public:
    std::shared_future<void> addSharedTask(std::function<void(void)> task) {
        return sharedTasks.emplace_back(std::async(std::launch::async, task).share());
    }
    void addTask(std::function<void(void)> task) {
        tasks.emplace_back(std::async(std::launch::async, task));
    }
    void wait() {
        for (auto& task : tasks)
            task.wait();
        for (auto& task : sharedTasks)
            task.wait();
        tasks.clear();
        sharedTasks.clear();
    }
};


class ThreadPool {
    std::condition_variable taskInQueueOrAbortCondVar;
    std::condition_variable taskDoneCondVar;
    std::queue<std::function<void()>> tasks;
    int activeTasksCount = 0;
    std::mutex tasksMutex;
    std::vector<std::thread> threads;
    bool waiting = false;
    bool active = false;

    void workerThread() {
        std::unique_lock tasksLock(tasksMutex);
        while (true) {
            activeTasksCount -= 1;
            tasksLock.unlock();
            if (waiting && activeTasksCount == 0 && tasks.empty())
                taskDoneCondVar.notify_all();
            tasksLock.lock();
            taskInQueueOrAbortCondVar.wait(tasksLock, [this] {
                return !tasks.empty() || !active;
            });
            if (!active)
                break;
            {
                const std::function<void()> task = std::move(tasks.front());
                tasks.pop();
                activeTasksCount += 1;
                tasksLock.unlock();
                task();
            }
            tasksLock.lock();
        }
    }

public:
    ThreadPool(int threadCount = 0) {
        if (threadCount <= 0) {
            threadCount = (std::thread::hardware_concurrency() > 0) ? std::thread::hardware_concurrency() : 1;
        }
        activeTasksCount = threadCount;
        active = true;
        for (int i = 0; i < threadCount; ++i) {
            threads.emplace_back(std::thread(&ThreadPool::workerThread, this));
        }
    }

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool& operator=(ThreadPool&&) = delete;

    ~ThreadPool() {
        wait();
        {
            const std::scoped_lock tasks_lock(tasksMutex);
            active = false;
        }
        taskInQueueOrAbortCondVar.notify_all();
        for (int i = 0; i < threads.size(); ++i) {
            threads[i].join();
        }
    }

    template<typename F> void addTask(F&& task) {
        {
            std::scoped_lock tasks_lock(tasksMutex);
            tasks.emplace(std::forward<F>(task));
        }
        taskInQueueOrAbortCondVar.notify_one();
    }

    void wait() {
        std::unique_lock tasks_lock(tasksMutex);
        waiting = true;
        taskDoneCondVar.wait(tasks_lock, [this] {
            return activeTasksCount == 0 && tasks.empty();
        });
        waiting = false;
    }
}; 


template<typename T> bool isRunning(const std::future<T>& f) {
    return f.valid() && f.wait_for(std::chrono::seconds(0)) != std::future_status::ready;
}

class Timer {
    std::chrono::time_point<std::chrono::steady_clock> startTime;
public:
    Timer(std::chrono::time_point<std::chrono::steady_clock> time) : startTime(time) {}
    Timer() : startTime(std::chrono::steady_clock::now()) {}
    void start() {
        startTime = std::chrono::steady_clock::now();
    }
    double getTime() {
        auto endTime = std::chrono::steady_clock::now();
        return std::chrono::duration<double>(endTime - startTime).count();
    }
};

struct DynamicBitset {
    using IntType = uint64_t;
    static constexpr int IntTypeBitSize = sizeof(IntType) * 8;
    IntType* bits;

    DynamicBitset() : bits(nullptr) {}
    DynamicBitset(int size) {
        bits = (IntType*)calloc(1, (size + IntTypeBitSize) / 8);
    }
    DynamicBitset(const DynamicBitset&) = delete;
    DynamicBitset(DynamicBitset&&) = delete;
    ~DynamicBitset() {
        free(bits);
    }
    void init(int size) {
        free(bits);
        bits = (IntType*)calloc(1, (size + IntTypeBitSize) / 8);
    }
    bool test(int i) const {
        return bits[i / IntTypeBitSize] & singleBit(i);
    }
    void set(int i) {
        bits[i / IntTypeBitSize] |= singleBit(i);
    }
private:
    IntType singleBit(int i) const {
        return (1ull << (i % IntTypeBitSize));
    }
};

template<typename T, int StackSize = 16> struct FastSmallVector {
    T* data_;
    int size_ = 0;
    int capacity_ = 0;
    T stackData[StackSize];

    FastSmallVector() {
        data_ = stackData;
    }
    FastSmallVector(const FastSmallVector&) = delete;
    FastSmallVector(FastSmallVector&&) = delete;
    ~FastSmallVector() {
        if (data_ != stackData) {
            std::free(data_);
        }
        data_ = nullptr;
    }

    void emplace_back(T&& val) {
        if (size_ == StackSize) {
            capacity_ = StackSize * 2;
            data_ = (T*)std::malloc(capacity_ * sizeof(T));
            std::memcpy(data_, stackData, StackSize * sizeof(T));
        } else if (size_ > StackSize && capacity_ <= size_) {
            capacity_ *= 2;
            data_ = (T*)realloc(data_, capacity_ * sizeof(T));
        }
        data_[size_++] = std::move(val);
    }
    void push_back(const T& val) {
        auto v = val;
        emplace_back(std::move(v));
    }
    T& operator[](int i) { return data_[i]; }
    T& back() { return data_[size_ - 1]; }
    T* begin() { return data_; }
    T* end() { return data_ + size_; }
    int size() { return size_; }
};

static uint64_t mostSignificantBitPosition(uint64_t a) {
#if defined(COMPILER_GCC) || defined(COMPILER_CLANG)
    return (63 - __builtin_clzll(a));
#elif defined(COMPILER_MSVC)
    DWORD index;
    _BitScanReverse64(&index, a);
    return index;
#else
    static_assert(false, "unsupported compiler. Cannot compute mostSignificantBit");
#endif
}

static void fastBigStringToLower(char* str, int size) {
#if defined(__AVX2__)
    const auto asciiA = _mm256_set1_epi8('A' - 1);
    const auto asciiZ = _mm256_set1_epi8('Z' + 1);
    const auto diff = _mm256_set1_epi8('a' - 'A');
    while (size >= 32) {
        auto inp = _mm256_loadu_si256((__m256i*)str);
        auto greaterThanA = _mm256_cmpgt_epi8(inp, asciiA);
        auto lessEqualZ = _mm256_cmpgt_epi8(asciiZ, inp);
        auto mask = _mm256_and_si256(greaterThanA, lessEqualZ);
        auto toAdd = _mm256_and_si256(mask, diff);
        auto added = _mm256_add_epi8(inp, toAdd);
        _mm256_storeu_si256((__m256i*)str, added);
        size -= 32;
        str += 32;
    }
    while (size-- > 0) {
        *str = tolower(*str);
        ++str;
    }
#elif defined(__SSE2__) || defined(_M_AMD64) || defined(_M_X64)
    const auto asciiA = _mm_set1_epi8('A' - 1);
    const auto asciiZ = _mm_set1_epi8('Z' + 1);
    const auto diff = _mm_set1_epi8('a' - 'A');
    while (size >= 16) {
        auto inp = _mm_loadu_si128((__m128i*)str);
        auto greaterThanA = _mm_cmpgt_epi8(inp, asciiA);
        auto lessEqualZ = _mm_cmplt_epi8(inp, asciiZ);
        auto mask = _mm_and_si128(greaterThanA, lessEqualZ);
        auto toAdd = _mm_and_si128(mask, diff);
        auto added = _mm_add_epi8(inp, toAdd);
        _mm_storeu_si128((__m128i*)str, added);
        size -= 16;
        str += 16;
    }
    while (size-- > 0) {
        *str = tolower(*str);
        ++str;
    }
#else
    std::transform(str, str + size, str, tolower);
#endif
}


template<typename T> void atomicMax(std::atomic<T>& max, T newVal) {
    auto curMax = max.load();
    while (std::max(newVal, curMax) != curMax) {
        if (max.compare_exchange_weak(curMax, newVal))
            break;
    }
}

template<typename T> struct ThreadSafeVec {
    static constexpr int smallestPower2 = 2;

    FastSmallVector<std::unique_ptr<std::vector<T>>, 32> blocks;
    int blockCount;
    int totalCapacity;
    std::mutex mutex;

    ThreadSafeVec() : totalCapacity(1 << smallestPower2) {
        blocks.emplace_back(std::make_unique<std::vector<T>>(totalCapacity));
        blockCount = 1;
    }
    std::pair<int, int> getBlockIdAndPosInBlock(int i) {
        if (i == 0)
            return { 0, 0 };
        auto bitPos = mostSignificantBitPosition(i);
        auto blockId = std::max<int>(0, int(bitPos - smallestPower2 + 1));
        auto posInBlock = (blockId == 0) ? i : (i - (1 << bitPos));
        return { blockId, posInBlock };
    }
    T& operator[](int i) {
        auto [blockId, posInBlock] = getBlockIdAndPosInBlock(i);
        if (blockId >= blockCount) {
            std::unique_lock l{ mutex };
            while (blockId >= blockCount) {
                blocks.emplace_back(std::make_unique<std::vector<T>>(totalCapacity));
                totalCapacity *= 2;
                blockCount += 1;
            }
        }
        return (*blocks[blockId])[posInBlock];
    }
};

static int wideCharToUtf8(char* buf, int buf_size, unsigned int c) {
    if (c < 0x80) {
        buf[0] = (char)c;
        return 1;
    }
    if (c < 0x800) {
        if (buf_size < 2) return 0;
        buf[0] = (char)(0xc0 + (c >> 6));
        buf[1] = (char)(0x80 + (c & 0x3f));
        return 2;
    }
    if (c < 0x10000) {
        if (buf_size < 3) return 0;
        buf[0] = (char)(0xe0 + (c >> 12));
        buf[1] = (char)(0x80 + ((c >> 6) & 0x3f));
        buf[2] = (char)(0x80 + ((c) & 0x3f));
        return 3;
    }
    if (c <= 0x10FFFF) {
        if (buf_size < 4) return 0;
        buf[0] = (char)(0xf0 + (c >> 18));
        buf[1] = (char)(0x80 + ((c >> 12) & 0x3f));
        buf[2] = (char)(0x80 + ((c >> 6) & 0x3f));
        buf[3] = (char)(0x80 + ((c) & 0x3f));
        return 4;
    }
    return 0;
}

static int wideStringToUtf8(char* buf, int buf_size, const wchar_t* in_text, const wchar_t* in_text_end) {
    char* buf_out = buf;
    const char* buf_end = buf + buf_size;
    while (buf_out < buf_end - 1 && (!in_text_end || in_text < in_text_end) && *in_text)
    {
        unsigned int c = (unsigned int)(*in_text++);
        if (c < 0x80)
            *buf_out++ = (char)c;
        else
            buf_out += wideCharToUtf8(buf_out, (int)(buf_end - buf_out - 1), c);
    }
    *buf_out = 0;
    return (int)(buf_out - buf);
}





template <class T, std::size_t BufferSize = 1024>
class PoolAllocator {
    struct Block {
        Block* next;
    };

    class Buffer {
        static constexpr std::size_t BlockSize = std::max(sizeof(T), sizeof(Block));
    public:
        Buffer(Buffer* next) : next(next) {}
        T* getBlock(std::size_t index) { return reinterpret_cast<T*>(&data[BlockSize * index]); }

        Buffer* next;
    private:
        alignas(T) alignas(Block) char data[BlockSize * BufferSize];
    };

    Block* freeBlocksHead = nullptr;
    Buffer* bufferHead = nullptr;
    std::size_t indexInCurrentBuffer = BufferSize;

public:
    PoolAllocator() {};
    PoolAllocator(const PoolAllocator&) = delete;
    PoolAllocator operator=(const PoolAllocator&) = delete;

    ~PoolAllocator() {
        while (bufferHead) {
            auto buffer = bufferHead;
            bufferHead = buffer->next;
            delete buffer;
        }
    }

    T* allocate() {
        if (freeBlocksHead) {
            auto block = freeBlocksHead;
            freeBlocksHead = block->next;
            return reinterpret_cast<T*>(block);
        }
        if (indexInCurrentBuffer >= BufferSize) {
            bufferHead = new Buffer(bufferHead);
            indexInCurrentBuffer = 0;
        }
        return bufferHead->getBlock(indexInCurrentBuffer++);
    }

    void deallocate(T* ptr) {
        auto block = reinterpret_cast<Block*>(ptr);
        block->next = freeBlocksHead;
        freeBlocksHead = block;
    }
};

template<typename T> struct FastThreadSafeishHashSet {
    struct Node {
        Node(const T& value) { new (&value_) T(value); }
        ~Node() { value().~T(); }
        Node(const Node&) = delete;
        Node& operator=(const Node&) = delete;
        T& value() { return *(T*)value_; }

        Node* next = nullptr;
    private:
        alignas(T) char value_[sizeof(T)];
    };

private:
    Node** data;
    int capacity_;
    PoolAllocator<Node, sizeof(T) * 8192> allocator;
    std::mutex mutex;

    static constexpr int InitialCapacity = 256;
    static constexpr double ResizeThreshold = 0.8;

public:
    FastThreadSafeishHashSet() : data(allocData(InitialCapacity)), capacity_(InitialCapacity) {}
    FastThreadSafeishHashSet(int power2InitialCapacity) : data(allocData(1 << power2InitialCapacity)), capacity_(1 << power2InitialCapacity) {}

    Node*& node(int index) { return data[index]; }
    Node*& node(Node** data_, int index) { return data_[index]; }
    T& at(int index) { return node(index)->value(); }
    T& at(Node** data_, int index) { return node(data_, index)->value(); }
    Node*& next(int index) { return node(index)->next; }
    Node*& next(Node** data_, int index) { return node(data_, index)->next; }
    int capacity() { return capacity_; }

    Node** allocData(int size) {
        auto x = (Node**)::operator new(size * sizeof(Node*), std::align_val_t(alignof(Node*)));
        memset(x, 0, size * sizeof(Node*));
        return x;
    }
    void deallocData(Node** data) {
        ::operator delete(data, capacity_ * sizeof(Node*), std::align_val_t(alignof(Node*)));
    }
    Node* allocNode(T&& value) {
        auto node = allocator.allocate();
        new (node) Node(std::move(value));
        return node;
    }
    Node* allocNode(const T& value) {
        allocNode(T(value));
    }
    void deallocNode(Node* node) {
        node->~Node();
        allocator.deallocate(node);
    }

    std::size_t hash(const T& value) {
        return std::hash<T>{}(value);
    }
    std::size_t getIndexWithCapacity(const T& value, std::size_t capacity) {
        return hash(value) & (capacity - 1);
    }
    std::size_t modIndex(std::size_t index) {
        return index & (capacity_ - 1);
    }
    std::size_t getIndex(const T& value) {
        return modIndex(hash(value));
    }

    T* find(const T& value) {
        auto i = getIndex(value);
        Node* n = node(i);
        while (n) {
            if (n->value() == value)
                return &n->value();
            n = n->next;
        }
        return nullptr;
    }

    void emplace(T&& value) {
        auto i = getIndex(value);
        auto n = node(i);
        Node* allocatedNode;
        {
            std::scoped_lock l{ mutex };
            allocatedNode = allocNode(std::move(value));
        }
        // This is not thread safe, but good enough in practice.
        // worst case what happens is I will add into same location twice which will result in orphan node which won't be findable,
        // or it will add duplicate of the same node.
        // however niether is a big problem, because orphan pointers are going to get deallocated anyway and having some duplicates
        // is not a problem.
        if (!n) {
            node(i) = allocatedNode;
            return;
        }
        while (n->next) {
            n = n->next;
        };
        n->next = allocatedNode;
    }
};


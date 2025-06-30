//g++ -std=c++20 -fcoroutines -lpthread sched5.cpp -o sched5

#include <coroutine>
#include <iostream>
#include <thread>
#include <vector>
#include <queue>
#include <mutex>
#include <cassert>
#include <unistd.h>
#include <pthread.h>

// ===== Constants =====
constexpr int MAX_THREADS = 32;

// ===== Forward Declaration =====
class Scheduler;

// ===== Global Scheduler Array =====
Scheduler* schedulers[MAX_THREADS] = {nullptr};

// ===== Coroutine Task Type =====
struct utask {
    struct promise_type;
    using handle_type = std::coroutine_handle<promise_type>;

    handle_type handle;
    int utask_id;
    int thread_id;  // 소유한 thread ID

    utask(handle_type h, int tid) : handle(h), thread_id(tid) {}
    utask(const utask&) = delete;
    utask& operator=(const utask&) = delete;

    utask(utask&& other) noexcept
        : handle(other.handle), utask_id(other.utask_id), thread_id(other.thread_id) {
        other.handle = nullptr;
    }

    ~utask() {
        if (handle)
            handle.destroy();
    }

    handle_type get_handle() { return handle; }

    struct promise_type {
        int thread_id;

        auto get_return_object() {
            return utask{handle_type::from_promise(*this), thread_id};
        }

        std::suspend_always initial_suspend() noexcept { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        void return_void() {}
        void unhandled_exception() { std::terminate(); }
    };
};

// ===== Scheduler Class =====
class Scheduler {
public:
    int thread_id;
    std::queue<utask> coroutine_queue;
    std::queue<utask> wait_list;
    std::mutex mutex;

    Scheduler(int tid) : thread_id(tid) {
        schedulers[tid] = this;
    }

    void emplace(utask&& task) {
        std::lock_guard<std::mutex> lock(mutex);
        coroutine_queue.push(std::move(task));
    }

    void enqueue_to_wait_list(utask&& task) {
        std::lock_guard<std::mutex> lock(mutex);
        wait_list.push(std::move(task));
    }

    void schedule() {
        {
            std::lock_guard<std::mutex> lock(mutex);

            if (!wait_list.empty()) {
                while (!wait_list.empty()) {
                    coroutine_queue.push(std::move(wait_list.front()));
                    wait_list.pop();
                }
            }
        }

        if (!coroutine_queue.empty()) {
            utask task = std::move(coroutine_queue.front());
            coroutine_queue.pop();

            auto handle = task.get_handle();
            handle.resume();

            if (!handle.done()) {
                emplace(std::move(task));
            }
        }
    }
};

// ===== Work Request Type (미사용) =====
struct Work_request {
    int type;
    uint64_t key;
    uint64_t value;
};
// move i
//
int post_mycoroutines_to(int from_tid, int to_tid) {
	
}
// ===== Coroutine Definitions =====
utask worker(int tid, int coroid) {
    co_await std::suspend_always{};
    std::cout << "[Coroutine " << tid << "-" << coroid << "] running on thread " << tid << "\n";
    co_return;
}

utask master(int tid, int coro_count, std::vector<utask>& workers) {
    auto& sched = *schedulers[tid];
    for (auto& t : workers)
        sched.emplace(std::move(t));

    int count = 0;
    while (count++ < 5) {
        sched.schedule();//5번 sched
    }

    if (tid == 1) {
        // move 모든 task to thread 0
        auto& to_sched = *schedulers[0];
        auto& from_sched = *schedulers[1];

        std::lock_guard<std::mutex> lock_from(from_sched.mutex);
        std::lock_guard<std::mutex> lock_to(to_sched.mutex);

        while (!from_sched.coroutine_queue.empty()) {
            to_sched.wait_list.push(std::move(from_sched.coroutine_queue.front()));
            from_sched.coroutine_queue.pop();
        }
    }

    co_return;
}

// ===== Thread Function =====
void thread_func(int tid, int coro_count) {
    pthread_t this_thread = pthread_self();
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(tid, &cpuset);
    pthread_setaffinity_np(this_thread, sizeof(cpu_set_t), &cpuset);

    Scheduler sched(tid);

    std::vector<utask> tasks;
    for (int i = 0; i < coro_count; ++i) {
        auto task = worker(tid, i);
        task.utask_id = tid * 100 + i;
        tasks.push_back(std::move(task));
    }

    auto master_task = master(tid, coro_count, tasks);
    master_task.get_handle().resume();
}

// ===== Main =====
int main() {
    const int coro_count = 3;

    std::thread t0(thread_func, 0, coro_count);
    sleep(1);
    std::thread t1(thread_func, 1, coro_count);

    t0.join();
    t1.join();
    return 0;
}


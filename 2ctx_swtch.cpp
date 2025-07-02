//

#include <coroutine>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>
#include <array>
#include <chrono>

static constexpr int N = 5;               // 스레드/코루틴 개수
static constexpr int SWITCHES = 1'000'000; // 스위칭 횟수

void bind_cpu(int cpu_id = 0) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpu_id, &cpuset);
    pthread_t tid = pthread_self();
    pthread_setaffinity_np(tid, sizeof(cpuset), &cpuset);
}

// ========== Thread 벤치마크 ==========
std::mutex              th_mtx;
std::condition_variable th_cv[N];
int                     th_turn = 0;

void thread_worker(int id) {
    bind_cpu(0);
    for (int i = 0; i < SWITCHES; ++i) {
        std::unique_lock<std::mutex> lk(th_mtx);
        th_cv[id].wait(lk, [&]{ return th_turn == id; });
        th_turn = (id + 1) % N;
        th_cv[th_turn].notify_one();
    }
}

void bench_threads() {
    // 워커 스레드 5개 생성
    std::vector<std::thread> threads;
    for (int i = 0; i < N; ++i)
        threads.emplace_back(thread_worker, i);

    // 벤치 시작
    auto t0 = std::chrono::high_resolution_clock::now();
    {
        std::lock_guard<std::mutex> lk(th_mtx);
        th_cv[0].notify_one();  // 스레드 0부터 시작
    }

    // 종료 대기
    for (auto &t : threads) t.join();
    auto t1 = std::chrono::high_resolution_clock::now();

    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
    std::cout << "[Threads] Context switches: " << SWITCHES * N
              << ", time = " << ms << " ms\n";
}

// ========== Coroutine 벤치마크 ==========
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
static std::array<utask::handle_type, N> co_handles;
static std::vector<utask> tasks;
int arr[N];
utask coroutine_worker(int id) {
     printf("worker Start %d\n",id);
     int i;
     for (i=0; i <SWITCHES; i++) {
        // 다음 코루틴 재개
        int nxt = (id+1) % (N);
        //if(i%10000==0) 
	printf("[ID:%d]%d\n",id,i);
	//co_handles[nxt].resume();
        while(arr[nxt]==1){
		printf("it is 1\n");
		nxt = (nxt+1) % N;
		if (nxt == id) break;	
	}
	tasks[nxt].get_handle().resume();
	// 자신은 여기서 일시 중단
        co_await std::suspend_always{};
        if(i%10000==0) printf("[ID:%d]%d end\n",id,i);
    }
     arr[id]=1;
     //지금 끝난 코루틴을 어떻게 처리할지? 
     co_return;
}
void bench_coroutines() {
    bind_cpu(0);
    tasks.reserve(N);
	// i코루틴 5개 생성만, 실행은 전부 대기 상태(initial_suspend)
    for (int i = 0; i < N; i++) {
        printf("%d\n",i);
	auto task=coroutine_worker(i);
	tasks.push_back(std::move(task));
    	//co_handles[i]=tasks.back().get_handle();
    }
    printf("resume\n");
    // 벤치 시작
    //while(1); 
    auto t0 = std::chrono::high_resolution_clock::now();
    // 첫 번째 코루틴만 resume -> 순환 시작
    tasks[0].get_handle().resume();
    printf("end\n");
    auto t1 = std::chrono::high_resolution_clock::now();

    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
    std::cout << "[Coroutines] Context switches: " << SWITCHES * N
              << ", time = " << ms << " ms\n";
}

int main() {
    bind_cpu(0);
    printf("coroutine\n");
    bench_coroutines();
    std::cout << "=== Thread vs Coroutine Context-Switch Benchmark ===\n";
    printf("thread\n");
    bench_threads();
    return 0;
}


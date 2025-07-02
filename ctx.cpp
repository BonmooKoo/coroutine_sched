//g++ -O2 ctx.cpp -o ctx   -I./ -I/usr/local/include   -L/usr/local/lib -Wl,-rpath,/usr/local/lib   -lboost_coroutine -lboost_context -lboost_system   -libverbs -lmemcached -lpthread

#include <boost/coroutine/symmetric_coroutine.hpp>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>
#include <array>
#include <chrono>

static constexpr int N = 20;               // 스레드/코루틴 개수
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

    auto ms = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
    std::cout << "[Threads] Context switches: " << SWITCHES * N
              << ", time = " << ms << " ns\n";
}

// ========== Coroutine 벤치마크 ==========
using coro_t    = boost::coroutines::symmetric_coroutine<void>;
using CoroCall  = coro_t::call_type;
using CoroYield = coro_t::yield_type;
using namespace std::placeholders;
CoroCall master;
CoroCall workers[N];
int arr[N];
static void coroutine_worker(CoroYield &yield,int coro_id)
{
     printf("worker Start %d\n",coro_id);
     int i;
     for (i=0; i <SWITCHES; i++) {
        // 다음 코루틴 재개
        int nxt = (coro_id+1) % (N);
        //if(i%10000==0) 
	//printf("[ID:%d]%d\n",coro_id,i);
	//co_handles[nxt].resume();
        while(arr[nxt]==1){
//		printf("it is 1\n");
		nxt = (nxt+1) % N;
		if (nxt == coro_id) break;	
	}
        yield(workers[nxt]);
        //if(i%10000==0) printf("[ID:%d]%d end\n",coro_id,i);
    }
     arr[coro_id]=1;
     //지금 끝난 코루틴을 어떻게 처리할지? 
}
void bench_coroutines() {
    bind_cpu(0);
	// 코루틴 5개 생성만, 실행은 전부 대기 상태(initial_suspend)
    for (int i = 0; i < N; i++) {
    	workers[i]=CoroCall(std::bind(&coroutine_worker,_1,i));
    }
    master = CoroCall([&](CoroYield &yield) {
    	yield(workers[0]);  // workers[0]을 첫 번째로 실행
    });
    printf("resume\n");
    // 벤치 시작
    //while(1); 
    auto t0 = std::chrono::high_resolution_clock::now();
    // 첫 번째 코루틴만 resume -> 순환 시작
    master();
    auto t1 = std::chrono::high_resolution_clock::now();

    auto ms = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
    std::cout << "[Coroutines] Context switches: " << SWITCHES * N
              << ", time = " << ms << " ns\n";
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


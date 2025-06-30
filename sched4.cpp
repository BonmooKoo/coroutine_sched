//g++ -std=c++20 -fcoroutines -lpthread sched4.cpp -o sched4

#include <coroutine>
#include <iostream>
#include <thread>
#include <vector>
#include <queue>
#include <chrono>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <cassert>
#include <pthread.h>

// ========== Coroutine Task Definition ==========
struct utask {
  struct promise_type {
    std::suspend_always initial_suspend() noexcept { return {}; }
    std::suspend_always final_suspend() noexcept { return {}; }

    utask get_return_object() {
      return utask{std::coroutine_handle<promise_type>::from_promise(*this)};
    }
    void return_void() {}
    void unhandled_exception() {}
  };
  std::coroutine_handle<promise_type> handle;
  int utask_id;//for debuging
  //생성자 : 코루틴 handle을 받아서 저장.
  utask(std::coroutine_handle<promise_type> handle) : handle{handle} {}
  //utask(const utask&) = delete;
  //utask& operator=(const utask&) = delete;
  utask(utask&& other) noexcept : handle{other.handle} { other.handle = nullptr; }
  ~utask() { if (handle) handle.destroy(); }

  auto get_handle() { return handle; }

};
using utask_ptr = std::shared_ptr<utask>;
//=========== Global Variable ==========
constexpr int MAX_THREADS = 32;
std::vector<std::queue<utask_ptr>> coroutine_queues(MAX_THREADS); // Queue 배열 coroutine_queues[32]
std::vector<std::queue<utask_ptr>> wait_list(MAX_THREADS); // 다른 thread에서 실행중인 coroutine을 넘겨줄때 사용 (동기화 피하기 위함.
std::mutex queue_mutexes[MAX_THREADS]; // wait_list 동기화에 사용되는 mutex lock

// ========== Scheduler ==========
class Scheduler {
public:
  int thread_id;

  void emplace(std::shared_ptr<utask> task) {
    //std::lock_guard<std::mutex> lock(queue_mutexes[thread_id]);
    coroutine_queues[thread_id].push(task);
  }

  auto suspend() {
    return std::suspend_always{};
  }

  void schedule() {
    //std::lock_guard<std::mutex> lock(queue_mutexes[thread_id]);
    if (!coroutine_queues[thread_id].empty()) {
      printf("[Schedule Tid : %d]\n",thread_id);
      std::shared_ptr<utask> task = coroutine_queues[thread_id].front();
      //printf("Task : %d\n",task.utask_id);
      coroutine_queues[thread_id].pop();
      auto handle = task.get_handle();
      handle.resume();
      if (!handle.done()) {
        coroutine_queues[thread_id].push(task);
      } 
    }
    if (!wait_list[thread_id].empty()){
      std::unique_lock<std::mutex> lock(queue_mutexes[thread_id], std::try_to_lock);
      if (!lock.owns_lock()) {
        // 락 획득 실패했으면 그냥 return (또는 continue 등)
      	goto sched_end;
      }
      printf("[Wait Tid : %d]\n",thread_id);
      //wait list의 task를 내 coroutine queu로 emplace함.
      while (!wait_list[thread_id].empty()) {
        auto task = wait_list[thread_id].front();
        wait_list[thread_id].pop();
        emplace(task); // 현재 스레드의 스케줄러에 넣음
      }
      printf("[Wait Tid: %d] Pull end. size : %d\n",thread_id,coroutine_queues[thread_id].size()); 
    }
sched_end:

  }
};

struct Work_request {
  int type;
  uint64_t key;
  uint64_t value;
};

std::queue<Work_request> global_WQ;
std::queue<Work_request>* local_WQ[MAX_THREADS];

utask worker(int tid, int coroid, Scheduler& sched) {
  printf("[Coroutine %d-%d]:started at thread%d\n", tid, coroid, gettid());
  co_await sched.suspend();
  printf("[Coroutine %d-%d]:ended at thread%d\n", tid, coroid, gettid());
  co_return;
}

void post_mycoroutines_to(int from_tid, int to_tid) {
  // 1. 각 큐에 대한 락을 잡음 (중첩되지 않게 순서 주의)
  std::lock_guard<std::mutex> lock_from(queue_mutexes[from_tid]);
  std::lock_guard<std::mutex> lock_to(queue_mutexes[to_tid]);

  // 2. 현재 coroutine queue에 남아있는 task를 to_tid의 wait_list로 이동
  while (!coroutine_queues[from_tid].empty()) {
    auto task = std::move(coroutine_queues[from_tid].front());
    coroutine_queues[from_tid].pop();
    wait_list[to_tid].push(std::move(task));
  }

  // 3. wait_list[from_tid]도 모두 to_tid로 넘김
  while (!wait_list[from_tid].empty()) {
    auto task = std::move(wait_list[from_tid].front());
    wait_list[from_tid].pop();
    wait_list[to_tid].push(std::move(task));
  }
}

utask master(int tid, int coro_count, std::vector<utask>& workers, Scheduler& sched) {
  printf("[Master Coroutine%d] Started on thread %d\n", tid, gettid());
  //Worker Coroutiune 초기화 과정
  for (int i = 0; i < coro_count; ++i) {
    auto handle = workers[i].get_handle();
    sched.emplace(handle);
  }
  //Worker Coroutine 실행 과정
  while (1) {
    sched.schedule();
    if (tid == 2) {
      post_mycoroutines_to(tid, 1);
      printf("[Master Coroutine2] Transferred coroutines\n");
  //    while(1){}
      break;
    }
  }
  printf("[Master Coroutine%d] Dead on thread %d\n", tid, gettid());
  co_return;
}

void thread_func(int tid, int coro_count) {
  //초기화 하는 함수
  pthread_t this_thread = pthread_self();
  cpu_set_t cpuset;
  CPU_ZERO(&cpuset);
  CPU_SET(tid, &cpuset);
  int ret = pthread_setaffinity_np(this_thread, sizeof(cpu_set_t), &cpuset);
  if (ret != 0) perror("pthread_setaffinity_np");

  Scheduler sched;
  sched.thread_id = tid;
  std::vector<utask> tasks;

  for (int i = 0; i < coro_count; ++i) {
    //여기서 생성되는 worker는 초기에 생성되는 worker
    auto t = std::make_shared<utask>(worker(tid, i, sched));//worker()는 코루틴 함수이기 때문에, 일반 함수와 다르게 즉시 전체 body를 실행하지않고
				    //promise_type 객체를 생성하고 croutine_handle 생성이 발생한다.
				    //1. promise_type 생성
				    //2. coroutine_handle 생성
				    //3. get_return_object() -> utask 생성
				    //4. initial_suspend() -> suspend 상태로 시작
				    //5. utask t 는 handle을 가지고 있고 실행은 안된 상태.
    t.utask_id = tid*coro_count+i;
    tasks.push_back(std::move(t));
  }
  //master을 시작함.
  //master는 worker coroutine을 초기화하고, 
  utask master_task = master(tid, coro_count, tasks, sched);
  master_task.handle.resume();
  printf("[Thread%d] end\n",tid);
}

void scheduler_thread(int tid) {//지금은 사용 안함.
  pthread_t this_thread = pthread_self();
  cpu_set_t cpuset;
  CPU_ZERO(&cpuset);
  CPU_SET(tid, &cpuset);
  int ret = pthread_setaffinity_np(this_thread, sizeof(cpu_set_t), &cpuset);
  if (ret != 0) perror("pthread_setaffinity_np");
  return;
}

int main() {
  const int coro_count = 3;
  std::thread t1(thread_func, 1, coro_count);
  sleep(3);
  std::thread t2(thread_func, 2, coro_count);
  t1.join();
  t2.join();
  return 0;
}


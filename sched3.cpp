//g++ -std=c++20 -fcoroutines -lpthread sched3.cpp -o sched3

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

  utask(std::coroutine_handle<promise_type> handle) : handle{handle} {}
  utask(const utask&) = delete;
  utask& operator=(const utask&) = delete;
  utask(utask&& other) noexcept : handle{other.handle} { other.handle = nullptr; }
  ~utask() { if (handle) handle.destroy(); }

  auto get_handle() { return handle; }

  std::coroutine_handle<promise_type> handle;
};

// ========== Scheduler ==========
class Scheduler {
  std::queue<std::coroutine_handle<>> _tasksQ;
  std::queue<std::coroutine_handle<>> _rdma_tasksQ;
  std::queue<std::coroutine_handle<>> _IO_tasksQ;

public:
  void emplace(std::coroutine_handle<> task) {
    _tasksQ.push(task);
  }

  auto suspend() {
    return std::suspend_always{};
  }

  void schedule() {
    if (!_tasksQ.empty()) {
      auto task = _tasksQ.front();
      _tasksQ.pop();
      task.resume();

      if (!task.done()) {
        // 예시 분기, 실제 사용 시 coroutine 내부에서 type 지정 필요
        _tasksQ.push(task);
      } else {
        task.destroy();
      }
    }
  }
};

struct Work_request{
	int type; // 1. Get 2. Insert 3. Update
	uint64_t key;
	uint64_t value;
}
struct Work_Queue{
	std::queue<
}
//=========== Global Variable ==========
//Enable thread list
constexpr int MAX_THREADS = 32;
//coroutine Queue : 실행중인 코루틴
std::vector<std::queue<std::coroutine_handle<>>> coroutine_queues(MAX_THREADS);
std::mutex queue_mutexes[MAX_THREADS];
//Global Work Queue : 실행해야하는 request
std::queue<Work_request> global_WQ;
//=========== Thread Local Variable ====
//Local Work Queue
std::queue<Work_request>* local_WQ[MAX_THREADS];
// ========== Coroutine Logic ==========
utask worker(int tid, int coroid, Scheduler& sched) {
  std::cout << "[Coroutine " << coroid << "] started on thread " << tid << "\n";
  co_await sched.suspend();
  std::cout << "[Coroutine " << coroid << "] end on thread " << tid << "\n";
  co_return;
}

utask master(int tid, int coro_count, std::vector<utask>& workers, Scheduler& sched) {
  std::cout << "[Master Coroutine] Start on thread " << tid << "\n";
  for (int i = 0; i < coro_count; ++i) {
    sched.emplace(workers[i].get_handle());
  }
  while(1){
	pull_request();
  	sched.schedule();
	if(tid==2){
		utask master(int tid, int coro_count, std::vector<utask>& workers, Scheduler& sched) {
  std::cout << "[Master Coroutine] Start on thread " << tid << "\n";
  for (int i = 0; i < coro_count; ++i) {
    sched.emplace(workers[i].get_handle());
  }
  while(1){
        pull_request();
        sched.schedule();
        if(tid==2){                                                                                                                                                 //post my coroutine to other thread                                                                                                                 post_mycorutine(1);
                break;
        }
  }
  co_return;
}//post my coroutine to other thread
		post_mycorutine(1);
		break;
	}
  }
  co_return;
}

// ========== Thread Function ==========
void thread_func(int tid, int coro_count) {
  // bind thread to specific core
  pthread_t this_thread = pthread_self();
  cpu_set_t cpuset;
  CPU_ZERO(&cpuset);
  CPU_SET(tid, &cpuset);
  int ret = pthread_setaffinity_np(this_thread, sizeof(cpu_set_t), &cpuset);
  if (ret != 0) perror("pthread_setaffinity_np");

  Scheduler sched;
  std::vector<utask> tasks;

  for (int i = 0; i < coro_count; ++i) {
    utask t = worker(tid, i, sched);
    tasks.push_back(std::move(t));
  }
  
  utask master_task = master(tid, coro_count, tasks, sched);
  master_task.handle.resume();
}
void scheduler_thread(){
  pthread_t this_thread = pthread_self();
  cpu_set_t cpuset;
  CPU_ZERO(&cpuset);
  CPU_SET(tid, &cpuset);
  int ret = pthread_setaffinity_np(this_thread, sizeof(cpu_set_t), &cpuset);
  if (ret != 0) perror("pthread_setaffinity_np");
  //이 스케줄러 thread는 전체 request를 삽입하고 조절하는 역할을함.
  
  return; 
}
int main() {
  const int coro_count = 3;
  //for(int i=1;i<thread_count+1;i++){
  std::thread t1(thread_func, 1, coro_count);
  std::thread t2(thread_func, 2, coro_count);

  t1.join();
  t2.join();
  return 0;
}

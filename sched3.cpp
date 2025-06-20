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
    while (!_tasksQ.empty()) {
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

struct work_request{

}

//=========== Global Variable ==========
//Global Work Queue

//=========== Thread Local Variable ====
//Local Work Queue

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
  sched.schedule();
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

int main() {
  const int coro_count = 3;
  std::thread t1(thread_func, 0, coro_count);
  std::thread t2(thread_func, 1, coro_count);

  t1.join();
  t2.join();
  return 0;
}

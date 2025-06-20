//g++ -fcoroutines sched1.cpp -o sched1

#include <coroutine>
#include <iostream>
#include <thread>
#include <vector>
#include <queue>
#include <chrono>
#include <atomic>
#include <mutex>
#include <condition_variable>

// ========== Coroutine Task Definition ==========
struct utask {//coroutine wrapper : 코루틴 구조체를 정의. 코루틴 상태를 제어
    struct promise_type {//코루틴 핵심 객체 : 컴파일러는 코루틴 함수를 만나면 promise_type을 생성해 사용
        std::coroutine_handle<> continuation;//코루틴 핸들

        utask get_return_object() {//코루틴 함수가 호출되면 from_promise를 통해
				   //promise_type 인스턴스와 연결된 코루틴 핸들을 생성
            return utask{std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        std::suspend_always initial_suspend() { return {}; }
	//initail_suspend : 최초 시작 직후에 어떻게 할지 :
	//	suspend_always : resume 할떄까지 대기
	//	suspend_never : 바로 시작
        std::suspend_always final_suspend() noexcept {
            if (continuation) continuation.resume();
            return {};
        }
	//final_suspend : 코루틴이 끝났을때 호출됨
	//	suspend_always : 일이 끝나도 일시정지한 상태로 남아있음
	//	suspend_never : 일이 끝나면 바로 return 
        void return_void() {}
	//종료되고 return 시 : 아무것도 안함
        void unhandled_exception() { std::terminate(); }
	//예외 발생시 : terminate함
    };
    //using == type def	
    using handle_type = std::coroutine_handle<promise_type>;
    handle_type coro;
    //std :: coroutine_handle : 코루틴 상태를 제어하는 핵심 컨트롤 객체. 코루틴의 실행을 시작/중단
    //				종료 되었는지 확인하고 파괴
    //				resume / done /destroy / promise 등 제공

    utask(handle_type h) : coro(h) {}//생성자
    utask(const utask&) = delete;//복사 금지1
    utask& operator=(const utask&) = delete;//복사 금지2
    utask(utask&& other) noexcept : coro(other.coro) { other.coro = nullptr; }
    ~utask() {//소멸자
        if (coro) coro.destroy();//소멸시 코루틴 메모리 해제
    }

    void resume() const {
        if (coro && !coro.done()) coro.resume();//코루틴을 다시시작
    }

    bool done() const {
        return !coro || coro.done();//해당 코루틴이 종료되었는지 확인
    }
};

// ========== Shared Coroutine Queue ==========
std::queue<utask::handle_type> coroutine_queue;
std::mutex queue_mutex;
std::condition_variable cv;

// ========== Coroutine Logic ==========
utask master(int tid,int coro_id) {
	
    co_return;
}
utask worker(int id) {
    std::cout << "[Coroutine " << id << "] started on thread " << std::this_thread::get_id() << "\n";

    // suspend and enqueue myself
    co_await std::suspend_always{};

    std::cout << "[Coroutine " << id << "] resumed on thread " << std::this_thread::get_id() << "\n";
    co_return;
}

// ========== Thread Functions ==========
void thread1_func() {
    std::vector<utask> tasks;
    for (int i = 0; i < 5; ++i) {
        utask task = worker(i + 1);
        {//여기에 괄호가 있는 이유는 lock의 수명을 괄호 안으로 한정하기 위해서. 
	 //물론 lock - unlock을 해도 되는데, lock_guard를 해당 괄호 안에서만 유효하게 한다.
            std::lock_guard<std::mutex> lock(queue_mutex);
            coroutine_queue.push(task.coro);
        }
        tasks.push_back(std::move(task));
    }

    std::cout << "[Thread 1] Launched coroutines and suspended.\n";
    std::this_thread::sleep_for(std::chrono::seconds(3));
    cv.notify_one(); // notify thread 2
}

void thread2_func() {
    std::cout << "[Thread 2] Waiting to resume coroutines...\n";
    std::unique_lock<std::mutex> lock(queue_mutex);
    cv.wait(lock, [] { return !coroutine_queue.empty(); });

    while (!coroutine_queue.empty()) {
        auto handle = coroutine_queue.front();
        coroutine_queue.pop();
        lock.unlock();

        std::cout << "[Thread 2] Resuming coroutine...\n";
        handle.resume();

        lock.lock();
    }
}

int main() {
    std::thread t1(thread1_func);
    std::thread t2(thread2_func);

    t1.join();
    t2.join();
    return 0;
}

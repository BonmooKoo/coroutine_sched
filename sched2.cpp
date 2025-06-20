//g++ -fcoroutines sched2.cpp -o sched2

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
struct utask {

  struct promise_type {
    std::suspend_always initial_suspend() noexcept { return {}; }
    std::suspend_always final_suspend() noexcept { return {}; }

    utask get_return_object() { 
        return std::coroutine_handle<promise_type>::from_promise(*this); 
    }
    void return_void() {}
    void unhandled_exception() {}
  };

  utask(std::coroutine_handle<promise_type> handle): handle{handle} {}

  auto get_handle() { return handle; }

  std::coroutine_handle<promise_type> handle;
};


class Scheduler {
//scheduler works at Master coroutine
  std::queue<std::coroutine_handle<>> _tasksQ;
  std::queue<std::coroutine_handle<>> _rdma_tasksQ;
  std::queue<std::coroutine_handle<>> _IO_tasksQ;
  public: 

    void emplace(std::coroutine_handle<> task) {
      _tasksQ.push(task);
    }

    void schedule() {
     while(true) {
      //1. Try poll rdma_task
     	if(!_rdma_tasksQ.empty()){
     		//try poll
		//if poll success
		//resume where id = rdma_tasks.id
		//else continue
     	}
     //2. Try poll IO
     	if(!_IO_tasksQ.empty()){
		//try poll
		//if poll success
		//resume where id = io_tasks.id
		//else continue
     	}
	if(!_tasksQ.empty()){	
        	auto task = _tasksQ.front();
        	_tasksQ.pop();
        	task.resume();
		//해당 task 끝나고 다시 scheduler로 돌아옴.
        	if(!task.done()) {
      		//1 : common CPU intensive Job
      		//2 : RDMA request
      		//3 : IO task
	  		if(task.type==1){     	
          			_tasksQ.push(task);
	  		}
	  		if(task.type==2){
	  			_rdma_tasksQ.push(task);
	  		}
	  		if(task.type==3){
	  			_IO_tasksQ.push(task);
	 		}
        	}
        	else {
          		task.destroy();
        	}
	}
      }
    }

    auto suspend() {
      	    return std::suspend_always{};
    }
};
// ========== Shared Coroutine Queue ==========
thread_local std::queue<utask::handle_type> coroutine_queue;
// ========== Coroutine Logic ==========
utask master(int id,int coro_count,std::vector<utask> &worker) {
//master coroutine은 thread위에서 작동하는 coroutine scheduler
//Core 위에는 하나의 thread 만 작동하기 떄문에 사실상 core의 작업을 결정하는 스케줄러
//coroutine은 기본적으로 non-preemptive로 스스로 포기해야하기때문에 coroutine
//
//고민중 : RDMA post / poll coroutine이 필요한데, polling을 이 master가 담당할지..
//         (06/20)걍 담당하자. Network - IO 모두 Master가 완료되었는지 판단하고 끝났으면 이거 넘겨줌.
//Master 
//	1. Worker Scheduling : 다음으로 실행할 worker 을 결정 > 사실 이건 queue로 FCFS 로 수행됨.
//	2. Network - IO polling : RDMA request를 1회 polling 하고 만약 완료된 request가 있으면 해당 coroutine을 깨움. 만약 IO가 종료된게 있으면 마찬가지로 해당 coroutine을 깨움.
//	3. Work Queue(Thread Global함)을 확인하고 atomic하게 request를 가져옴.
//	4. 만약, 내가 실행하는 request가 너무 idle하다. 근데 그 상태가 계속된다. 그러면 내가 갖고 있는 coroutine을 모두 다른 thread에게 넘겨줌 (왜냐면 지금 실행중인 work도 있으니까.) 내가 존재하는 thread를 죽이고, CPU를 꺼버림.
//	
	Scheduler sched;
	int i;
	//scheduling 시작
	for(i=0;i<coro_count;i++){
		//run worker coroutine
		sch.emplace(worker[i].get_handle());
	}
	//while(1)
	//1.pull request from Global Work queue
		//pull_request();
	//2.schedule
		//sched.schedule();	
	//3. If idle
	//	3.0 break; 
	//	3.1. Stop get request from work queue
	//	3.2. give my coroutine (Except RDMA) to other thread
	//	3.3. finish on going Network coroutine
	//	   왜? RDMA 는 Thread bind 되어있어서
	//	3.4. 내 coroutine 내용 정리하고 return.
}

utask worker(int tid,int coroid,Scheduler& sched) {
    std::cout << "[Coroutine " << coroid << "] started on thread " << tid << "\n";

    // suspend and enqueue myself
    co_await sch.suspend();

    std::cout << "[Coroutine " << coroid << "] end on thread " << tid << "\n";
    co_return;
}

// ========== Thread Functions ==========
void thread_func(int tid,int coro_count) {
    //이 thread_func는 사실상 init code 임
    //실행될때 : 
    //1. 맨 처음 applicaion 이 시작될때
    //2. Thread busy로 새로운 core 할당이 필요할때
    //
    //멈출때 : 
    //1. Thread 가 idle 해짐. 
    //(기준 : User 의 request가 줄어서 내 Work Queue에 request가 안들어옴)

/////	
     //bind thread
  pthread_t this_thread = pthread_self();
  thread_id = tid;
  cpu_set_t cpuset;
  CPU_ZERO(&cpuset);
  CPU_SET(thread_id, &cpuset);
  int ret = pthread_setaffinity_np(this_thread, sizeof(cpu_set_t), &cpuset);
    if (ret != 0) {
        perror("pthread_setaffinity_np");
    }	
    /////
    std::vector<utask> tasks;//tasks : worker coroutine 목록. 이거는 thread 별로 공유가 된다. 만약 thread가 죽으면 삭제가 되도록.
    for (int i = 0; i < coro_count; ++i) {
        utask task = worker(tid,i);
        tasks.push_back(std::move(task));
    }
    utask master_coroutine = master(tid,coro_count,std::ref(tasks));
    std::cout << "[Thread"<<tid<<"] Starts Master \n";
    
    master_coroutine.resume();
}

int main() {
	
    std::thread t1(thread_func);
    std::thread t2(thread_func);

    t1.join();
    t2.join();
    return 0;
}

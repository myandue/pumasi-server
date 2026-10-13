#include "thread_pool.h"

ThreadPool::ThreadPool(size_t n) {
    for (size_t i = 0 ; i < n ; i++) {
        // emplace_back: 벡터(workers_)를 채워야할 원소를 즉시 생성해서 벡터 뒤에 추가해줌
        // 전달하는 인자들은, 해당 벡터를 구성해야할 원소(std::thread)를 생성하기 위한 인자
        // std::thread 생성자: std::thread(할일_함수, 함수에_줄_인자들), 할일_함수가 멤버함수일 경우에는 두번째 인자는 '어느 객체에서 실행할지'가 된다.
        // 즉, worker_loop를 실행할 스레드를 생성한 것이고, 해당 함수를 어느 객체에서 실행할지(this)를 함께 넘긴다.
        // 그 스레드는 this->worker_loop()를 실행하게 됨  
        workers_.emplace_back(&ThreadPool::worker_loop, this);
    }
}

ThreadPool::~ThreadPool() {
    running_ = false;
    cv_.notify_all();
    for (auto& t : workers_) {
        t.join();
    }
}

void ThreadPool::enqueue(std::function<void()> job) {
    {
        std::lock_guard<std::mutex> lock(mtx_);
        tasks_.push(job);
    }
    cv_.notify_one();
}

void ThreadPool::worker_loop() {
    while(true) {
        std::function<void()> job;
        {
            std::unique_lock<std::mutex> lock(mtx_);
            cv_.wait(lock, [this] {return !tasks_.empty() || !running_; });

            if (!running_ && tasks_.empty()) break;

            job = tasks_.front();
            tasks_.pop();
        }
        job();
    }
}
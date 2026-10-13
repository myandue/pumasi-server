#pragma once

#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional> // std::function
#include <atomic>

class ThreadPool {
public:
    // n개의 워커 스레드를 만들어 즉시 일하게 시작
    explicit ThreadPool(size_t n);

    // 소멸 시: 남은 작업 마무리 방식 정하고, 워커 전부 join
    ~ThreadPool();

    // 작업 하나를 큐에 넣고 워커 하나 깨우기
    void enqueue(std::function<void()> job);

private:
    void worker_loop(); // queue.cpp의 worker() 함수가 여기로 들어옴

    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    std::mutex mtx_;
    std::condition_variable cv_;
    std::atomic<bool> running_{true};
};
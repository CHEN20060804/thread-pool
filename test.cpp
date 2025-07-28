// a simple test for ThreadPool implementation

#include "ThreadPool.h"
#include <chrono>
#include <iostream>
#include <vector>

// compute Fibonacci number using recursion (CPU intensive task)
unsigned long long fib(int n) {
    if (n <= 1) return n;
    return fib(n - 1) + fib(n - 2);
}

// execute Fibonacci calculation in a single thread
void run_single_thread(const std::vector<int>& inputs) {
    auto start = std::chrono::high_resolution_clock::now();
    std::vector<unsigned long long> results;
    for (int n : inputs) {
        results.push_back(fib(n));
    }
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    std::cout << "Single-thread execution time: " << duration << " ms\n";
    // 打印部分结果以验证
    std::cout << "Results (first 3): ";
    for (size_t i = 0; i < std::min<size_t>(3, results.size()); ++i) {
        std::cout << results[i] << " ";
    }
    std::cout << "...\n";
}

// execute Fibonacci calculation using ThreadPool
void run_thread_pool(yc::ThreadPool& pool, const std::vector<int>& inputs) {
    auto start = std::chrono::high_resolution_clock::now();
    std::vector<std::future<unsigned long long>> futures;

    // 提交任务
    for (int n : inputs) {
        futures.push_back(pool.submit(fib, n));
    }

    // 收集结果
    std::vector<unsigned long long> results;
    for (auto& f : futures) {
        results.push_back(f.get());
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    std::cout << "ThreadPool execution time: " << duration << " ms\n";
    // 打印部分结果以验证
    std::cout << "Results (first 3): ";
    for (size_t i = 0; i < std::min<size_t>(3, results.size()); ++i) {
        std::cout << results[i] << " ";
    }
    std::cout << "...\n";
}

// test dynamic thread adjustment in ThreadPool
void test_dynamic_threads(yc::ThreadPool& pool, const std::vector<int>& inputs) {
    std::cout << "\nTesting dynamic thread adjustment...\n";
    auto start = std::chrono::high_resolution_clock::now();

    pool.change_thread_num(2);// 设置线程数为 2
    std::cout << "Set thread count to 2\n";
    std::vector<std::future<unsigned long long>> futures;

    size_t half = inputs.size() / 2;// 将总任务分为两部分
    for (size_t i = 0; i < half; ++i) {
        futures.push_back(pool.submit(fib, inputs[i]));
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    pool.change_thread_num(8);// 动态增加线程数到 8
    std::cout << "Increased thread count to 8\n";

    for (size_t i = half; i < inputs.size(); ++i) {
        futures.push_back(pool.submit(fib, inputs[i]));
    }

    std::vector<unsigned long long> results;
    for (auto& f : futures) {
        results.push_back(f.get());
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    std::cout << "Dynamic ThreadPool execution time: " << duration << " ms\n";
}

int main() {
    std::vector<int> inputs(32, 35); // compute fib(35) 32 times

    // single thread test
    std::cout << "Running single-thread test...\n";
    run_single_thread(inputs);

    // thread pool test with 8 threads
    std::cout << "\nRunning ThreadPool test (8 threads)...\n";
    yc::ThreadPool pool(8);
    run_thread_pool(pool, inputs);

    // dynamic thread adjustment test(increase from 2 to 8 threads)
    test_dynamic_threads(pool, inputs);

    // shutdown gracefully
    pool.shut_down();

    return 0;
}
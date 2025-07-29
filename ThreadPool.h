//
// Created by yyc08 on 25-7-27.
//

#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <iostream>
#include <stdexcept>
#include <algorithm>
#include <vector>
#include <set>
#include <string>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <future>
#include <atomic>
#include <type_traits>

namespace yc {

    class ThreadPool {
    private:
        std::vector<std::thread> threads;
        std::mutex queue_mtx;
        std::condition_variable task_condition;
        std::condition_variable exit_condition;
        std::queue<std::function<void()>> queue;
        static constexpr size_t THREAD_POOL_UPPER_LIMIT = 128;
        static constexpr size_t QUEUE_UPPER_LIMIT = 10000;
        std::atomic<bool> stop;
        std::atomic<bool> force_stop;
        std::atomic<size_t> thread_to_stop;
        std::set<std::thread::id> exited_threads;

        void worker_loop() {
            while (true) {
                std::unique_lock<std::mutex> lock(queue_mtx);
                task_condition.wait(lock, [this]{ return stop || !queue.empty() || force_stop || thread_to_stop > 0; });
                if (stop && queue.empty()) {
                    return;
                }
                if (force_stop) {
                    return;
                }
                if (thread_to_stop> 0 && queue.empty()) {
                    thread_to_stop.fetch_sub(1);// --
                    exited_threads.insert(std::this_thread::get_id()); //get thread id that will exit
                    exit_condition.notify_one(); // notify change_thread_num
                    return;
                }
                std::function<void()> task = std::move(queue.front());
                queue.pop();
                lock.unlock();
                try {
                    task();
                }
                catch (const std::exception& e) {
                    std::cout << e.what() << std::endl;
                }
                catch (...) {
                    std::cout << "Unknown exception" << std::endl;
                }
            }
        }

    public:
        explicit ThreadPool(size_t num_of_threads = std::thread::hardware_concurrency()) : stop(false), force_stop(false), thread_to_stop(0) {
            num_of_threads = std::min(num_of_threads, THREAD_POOL_UPPER_LIMIT);
            for (int i = 0; i < num_of_threads; i++) {
                threads.emplace_back(&ThreadPool::worker_loop, this);
            }
        }

        ~ThreadPool() {
            shut_down();
        }

        template<typename F, typename ... Args>
        auto submit(F&& f, Args&& ... args) -> std::future<std::invoke_result_t<F, Args...>> {
            using return_type = std::invoke_result_t<F, Args...>;

            auto task = std::make_shared<std::packaged_task<return_type()>> (
                std::bind(std::forward<F>(f), std::forward<Args>(args)...)
                );

            std::future<return_type> res = task->get_future();

            {
                std::unique_lock<std::mutex> lock(queue_mtx);

                task_condition.wait(lock, [this] {
                    return stop || queue.size() < QUEUE_UPPER_LIMIT;
                });

                if (stop) {
                    throw std::runtime_error("ThreadPool stopped");
                }
                queue.emplace([task]() { (*task)(); });
            }
            task_condition.notify_one();
            return res;
        }

        size_t get_thread_num() const {
            return threads.size();
        }

        size_t get_task_num() const {
            return queue.size();
        }

        bool is_stop() const {
            return stop;
        }

        void shut_down() {    // shutdown gracefully
            stop = true;
            task_condition.notify_all();
            for (auto& thread : threads) {
                if (thread.joinable()) {
                    thread.join();
                }
            }
            threads.clear();
        }
        // tips: if shut_down() is called, the thread pool will be discarded after tasks are completed.


        std::vector<std::function<void()>> shut_down_now() {    //force shutdown
            std::vector<std::function<void()>> remaining_tasks;
            {
                std::unique_lock<std::mutex> lock(queue_mtx);
                stop = true;
                force_stop = true;
                while (!queue.empty()) {
                    remaining_tasks.emplace_back(std::move(queue.front()));// collect remaining tasks
                    queue.pop();
                }
            }
            task_condition.notify_all();
            for (auto& thread : threads) {
                if (thread.joinable()) {
                    thread.join();
                }
            }
            threads.clear();
            return remaining_tasks;
        }
        // tips: if shut_down_now() is called, the thread pool will be discarded instantly,
        //       and return tasks that were not executed.

        // whatever shut_down() or shut_down_now() is called, the pool will be discarded and can't be reused.


        void change_thread_num(size_t new_num_of_threads) {
            std::unique_lock<std::mutex> lock(queue_mtx);
            if (stop) {
                throw std::runtime_error("Cannot change thread number after shutdown");
            }
            const size_t current_num = threads.size();
            if (current_num == new_num_of_threads) {
                return;
            } else if (current_num > new_num_of_threads) {
                size_t diff = current_num - new_num_of_threads;
                thread_to_stop += diff;
                task_condition.notify_all();
                exit_condition.wait(lock, [this, diff]{ return exited_threads.size() >= diff; });
                // collect exited threads
                std::vector<std::thread> to_join;
                for (auto it = threads.begin(); it != threads.end(); ) {
                    if (exited_threads.contains(it->get_id())) {
                        to_join.emplace_back(std::move(*it));
                        it = threads.erase(it);
                    } else {
                        ++it;
                    }
                }
                exited_threads.clear();
                lock.unlock(); // unlock before joining threads
                for (auto& t : to_join) {
                    if (t.joinable()) {
                        t.join();
                    }
                }
            } else {
                if (new_num_of_threads > THREAD_POOL_UPPER_LIMIT) {
                    throw std::runtime_error("ThreadPool upper limit exceeded (" + std::to_string(THREAD_POOL_UPPER_LIMIT) + ")");
                }
                for (size_t i = current_num; i < new_num_of_threads; ++i) {
                    threads.emplace_back(&ThreadPool::worker_loop, this);
                }
            }
        }
    };

}

#endif //THREADPOOL_H

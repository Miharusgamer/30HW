#include <iostream>
#include <vector>
#include <future>
#include <algorithm>
#include <functional>
#include <thread>
#include <queue>
#include <chrono>
#include <condition_variable>

class ThreadPool {
public:
    ThreadPool(int numThreads) : stop(false) {
        for (int i = 0; i < numThreads; ++i) {
            threads.emplace_back(std::thread(&ThreadPool::workerThread, this));
        }
    }

    ~ThreadPool() {
        {
            std::unique_lock<std::mutex> lock(queueMutex);
            stop = true;
        }

        condition.notify_all();

        for (std::thread& thread : threads) {
            thread.join();
        }
    }

    template <typename Func, typename... Args>
    auto enqueue(Func&& func, Args&&... args) -> std::future<typename std::result_of<Func(Args...)>::type> {
        using returnType = typename std::result_of<Func(Args...)>::type;

        auto task = std::make_shared<std::packaged_task<returnType()>>(
            std::bind(std::forward<Func>(func), std::forward<Args>(args)...)
        );

        std::future<returnType> result = task->get_future();

        {
            std::unique_lock<std::mutex> lock(queueMutex);
            if (stop) {
                throw std::runtime_error("enqueue on stopped ThreadPool");
            }

            tasks.emplace([task]() { (*task)(); });
        }

        condition.notify_one();

        return result;
    }

private:
    void workerThread() {
        while (true) {
            std::function<void()> task;

            {
                std::unique_lock<std::mutex> lock(queueMutex);
                condition.wait(lock, [this]() { return stop || !tasks.empty(); });

                if (stop && tasks.empty()) {
                    return;
                }

                task = std::move(tasks.front());
                tasks.pop();
            }

            try {
                task();
            }
            catch (const std::exception& e) {
                std::cerr << "Exception in worker thread: " << e.what() << std::endl;
            }
            catch (...) {
                std::cerr << "Unknown exception in worker thread" << std::endl;
            }
        }
    }

    std::vector<std::thread> threads;
    std::queue<std::function<void()>> tasks;
    std::mutex queueMutex;
    std::condition_variable condition;
    bool stop;
};

void quicksort(std::vector<int>& array, int left, int right, ThreadPool& pool, std::shared_ptr<std::promise<void>> parentPromise) {
    if (left >= right) {
        parentPromise->set_value();
        return;
    }

    int pivot = array[left + (right - left) / 2];
    int i = left;
    int j = right;
    while (i <= j) {
        while (array[i] < pivot) {
            i++;
        }
        while (array[j] > pivot) {
            j--;
        }
        if (i <= j) {
            std::swap(array[i], array[j]);
            i++;
            j--;
        }
    }

    if (right - i > 100000) {
        auto subtaskPromise = std::make_shared<std::promise<void>>();

        try {
            pool.enqueue([&array, i, right, &pool, subtaskPromise]() {
                quicksort(array, i, right, pool, subtaskPromise);
                });
        }
        catch (const std::exception& e) {
            std::cerr << "Exception during enqueue: " << e.what() << std::endl;
        }
        catch (...) {
            std::cerr << "Unknown exception during enqueue" << std::endl;
        }

        try {
            pool.enqueue([&array, left, j, &pool, parentPromise]() {
                quicksort(array, left, j, pool, parentPromise);
                });
        }
        catch (const std::exception& e) {
            std::cerr << "Exception during enqueue: " << e.what() << std::endl;
        }
        catch (...) {
            std::cerr << "Unknown exception during enqueue" << std::endl;
        }
    }
    else {
        quicksort(array, left, j, pool, parentPromise);
        quicksort(array, i, right, pool, parentPromise);
    }
}

int main() {
    std::vector<int> array = { 5, 2, 9, 1, 7, 6, 4, 8, 3 };

    ThreadPool pool(4);

    auto start = std::chrono::high_resolution_clock::now();

    auto mainPromise = std::make_shared<std::promise<void>>();
    auto mainFuture = mainPromise->get_future();

    try {
        quicksort(array, 0, array.size() - 1, pool, mainPromise);
    }
    catch (const std::exception& e) {
        std::cerr << "Exception during quicksort: " << e.what() << std::endl;
    }
    catch (...) {
        std::cerr << "Unknown exception during quicksort" << std::endl;
    }

    mainFuture.wait();

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> duration = end - start;

    for (int num : array) {
        std::cout << num << " ";
    }
    std::cout << std::endl;

    std::cout << "Execution time: " << duration.count() << " seconds" << std::endl;

    return 0;
}

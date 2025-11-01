#ifndef THREAD_POOL_H
#define THREAD_POOL_H
// 参考内容: https://www.cnblogs.com/chenleideblog/p/12915534.html

#include <vector>
#include <queue>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <future>
#include <functional>
#include <stdexcept>

class ThreadPool {
public:
    ThreadPool(size_t);
    template<class F, class... Args>
    auto enqueue(F&& f, Args&&... args)
        -> std::future<typename std::result_of<F(Args...)>::type>;
    ~ThreadPool();
private:
    std::vector<std::thread> workers; // 存放线程的数组, 最后对此全部join
    std::queue<std::function<void()>> tasks; // 任务队列, 里面存的是函数

    // 同步
    std::mutex queue_mutex; // 访问任务队列的互斥锁, 在插入任务或者线程取出任务都需要借助互斥锁进行安全访问
    std::condition_variable cv; // 用于通知线程, 任务队列状态的条件变量. 有任务通知线程执行, 没有则wait
    bool stop; // 标识线程池的状态，用于构造与析构中对线程池状态的了解
};

// 线程池构造函数: 创建线程并启动
inline ThreadPool::ThreadPool(size_t threads) // inline: 允许重复定义(多个cpp文件包含时)
    : stop(false) // stop为false表示线程池启动着
{
    for (size_t i = 0; i < threads; ++i)
        workers.emplace_back(
            [this]
            {
                for (;;) // 每个线程都会这样反复执行
                {
                    std::function<void()> task; // 用来接受后续从任务队列中弹出的真实任务

                    {
                        std::unique_lock<std::mutex> lock(this->queue_mutex);
                        // 线程池已经停止 || 任务队列非空 => 继续执行
                        this->cv.wait(lock,
                            [this] { return this->stop || !this->tasks.empty(); }); // 阻塞当前线程直至条件变量被通知
                        if (this->stop && this->tasks.empty()) // 保证线程池在关闭阶段仍能正确执行完所有已提交的任务
                            return;
                        task = std::move(this->tasks.front()); // 避免函数可能是昂贵的拷贝
                        this->tasks.pop();
                    } // 退出作用区域时自动解锁, 释放unique_lcok

                    task();
                }
            }
        );
}

// 往任务队列里添加任务函数
template<class F, class... Args>
auto ThreadPool::enqueue(F&& f, Args&&... args)
-> std::future<typename std::result_of<F(Args...)>::type> // 尾置返回类型
{
    using return_type = typename std::result_of<F(Args...)>::type; // result_of 会得到函数调用后的返回类型

    // 把“用户提交的函数 + 参数”包装成一个可以异步执行、可获取返回值的任务
    auto task = std::make_shared<std::packaged_task<return_type()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...)
    );

    std::future<return_type> res = task->get_future();
    {
        std::unique_lock<std::mutex> lock(queue_mutex);

        // 停止线程池后, 不允许排队
        if (stop)
            throw std::runtime_error("enqueue on stopped ThreadPool");

        tasks.emplace([task]() { (*task)(); }); // 创建了个 匿名函数，当执行它时，会调用 task 中真正的用户函数
    }
    cv.notify_one(); // 只唤醒一个, 防止惊群
    return res;
}

// the destructor joins all threads
inline ThreadPool::~ThreadPool()
{
    {
        std::unique_lock<std::mutex> lock(queue_mutex);
        stop = true;
    }
    cv.notify_all();
    for (std::thread& worker : workers)
        worker.join();
}

/*
* 补充:
* 
* 尾置返回类型:
* 在函数模板中，编译器在看到函数名之前必须知道返回值类型。
* 而在模板中，这时 F、Args... 还没推导出来，所以不能直接在函数名左边写完整的返回类型
* 此时需要借助尾置返回类型    (尾置返回类型其实只是让编译器先看到参数列表再决定返回类型, 而不是普通的立即要知道)
* 格式: auto 函数名(参数列表) -> 实际返回类型
* 这样返回类型可以依赖模板参数
* 
* ===================================
* 
* 包装函数+参数:
* std::packaged_task<return_type()> 任务包装器  格式: packaged_task<返回类型(输入参数)> task变量(函数名);
* 使用: future<return_type()> ret = task.get_future(); // 获得future
* 这里"获取future" 其实是一个结果占位符, 代表一个未来才会产生的值
* task()
* ret.get(); // 等待任务执行并获取结果
* 
*/

#endif

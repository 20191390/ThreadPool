#include "ThreadPool.h"

int main()
{
    ThreadPool pool(4);
    std::vector<std::future<int>> results;

    for (int i = 0; i < 8; ++i) {
        results.emplace_back( // 保存每个异步结果
            pool.enqueue([i] { // 将每个任务插入到任务队列里, 每个任务的功能均为: 打印+睡眠1s+打印+返回结果
                std::cout << "hello " << i << std::endl;
                std::this_thread::sleep_for(std::chrono::seconds(1));
                std::cout << "world " << i << std::endl;
                return i * i;
                })
        );
    }

    for (auto&& result : results) // 一次性取出保存在results中的异步结果
        std::cout << result.get() << ' ';
    std::cout << std::endl;

    return 0;
}

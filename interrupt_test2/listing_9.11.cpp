#include <future>
#include <algorithm>
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <thread>
#include <future>
#include <iostream>
#include <chrono>   

class interrupt_flag;

void interruption_point();

class thread_interrupted {

};



class interrupt_flag
{
    std::atomic<bool> flag;
    std::condition_variable* thread_cond;
    std::mutex set_clear_mutex;
    
public:
    interrupt_flag():
        thread_cond(0)
    {}

    void set()
    {
        flag.store(true,std::memory_order_relaxed);
        std::lock_guard<std::mutex> lk(set_clear_mutex);
        if(thread_cond)
        {
            thread_cond->notify_all();
        }
    }  
    bool is_set() const
    {
        return flag.load(std::memory_order_relaxed);
    }

    void set_condition_variable(std::condition_variable& cv)
    {
        std::lock_guard<std::mutex> lk(set_clear_mutex);
        thread_cond=&cv;
    }

    void clear_condition_variable()
    {
        std::lock_guard<std::mutex> lk(set_clear_mutex);
        thread_cond=0;
    }

    struct clear_cv_on_destruct
    {
        ~clear_cv_on_destruct();
    };
};

static interrupt_flag this_thread_interrupt_flag;

interrupt_flag::clear_cv_on_destruct::~clear_cv_on_destruct() {
    this_thread_interrupt_flag.clear_condition_variable();
}

void interruption_point()
{
    if (this_thread_interrupt_flag.is_set())
    {
        throw thread_interrupted();
    }
}

void interruptible_wait(std::condition_variable& cv,
                        std::unique_lock<std::mutex>& lk)
{
    interruption_point();
    //interrupt_flag this_thread_interrupt_flag;
    this_thread_interrupt_flag.set_condition_variable(cv);
    interrupt_flag::clear_cv_on_destruct guard;
    interruption_point();
    while (!this_thread_interrupt_flag.is_set()) {
        cv.wait_for(lk, std::chrono::milliseconds(1));
    }
   
    interruption_point();
}

class interruptible_thread
{
    std::thread internal_thread;
    interrupt_flag* flag;
public:
    template<typename FunctionType>
    interruptible_thread(FunctionType f)
    {
        std::promise<interrupt_flag*> p;
        internal_thread = std::thread([f, &p] {
            p.set_value(&this_thread_interrupt_flag);
            try
            {
                f();
            }
            catch (thread_interrupted const&)
            {
                std::cout << "interrupted" << std::endl;
            }
            });
        flag = p.get_future().get();
    }
    void join()
    {
        internal_thread.join();
    }
    void interrupt()
    {
        if (flag)
        {
            flag->set();
        }
    }
};
std::mutex config_mutex;
std::condition_variable cv;
void background_thread() {
    static int i = 0;
    while (true) {
        interruption_point();
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        std::cout << "do work" << i++ << std::endl;
        std::unique_lock<std::mutex> lk(config_mutex);
        interruptible_wait(cv, lk);

    }
}


std::vector<interruptible_thread> background_threads;


int main()
{
    //std::unique_lock<std::mutex> lk(config_mutex);
    background_threads.push_back(
        interruptible_thread(background_thread));
    background_threads.push_back(
        interruptible_thread(background_thread));
    background_threads.push_back(
        interruptible_thread(background_thread));

    std::this_thread::sleep_for(std::chrono::seconds(10));

    for (unsigned i = 0; i < background_threads.size(); ++i)
    {
        background_threads[i].interrupt();
    }
    for (unsigned i = 0; i < background_threads.size(); ++i)
    {
        background_threads[i].join();
    }

    return 0;
}
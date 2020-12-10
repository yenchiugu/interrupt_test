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
    std::condition_variable_any* thread_cond_any;
    //std::condition_variable* thread_cond_any;
    std::mutex set_clear_mutex;

public:
    interrupt_flag():
           thread_cond(0),thread_cond_any(0)
    {}
    void set()
    {
        flag.store(true,std::memory_order_relaxed);
        std::lock_guard<std::mutex> lk(set_clear_mutex);
        if(thread_cond)
        {
            thread_cond->notify_all();
        }
        else if(thread_cond_any)
        {
            thread_cond_any->notify_all();
        }
    }

    bool is_set() const
    {
        return flag.load(std::memory_order_relaxed);
    }

    template<typename Lockable>
    void wait(std::condition_variable_any& cv,Lockable& lk)
    {
        struct custom_lock
        {
            interrupt_flag* self;
            Lockable& lk;

            custom_lock(interrupt_flag* self_,
                        std::condition_variable_any& cond,
                //std::condition_variable& cond,
                        Lockable& lk_):
                self(self_),lk(lk_)
            {
                self->set_clear_mutex.lock();
                //lock();
                self->thread_cond_any=&cond;
            }

            void unlock()
            {
                lk.unlock();
                self->set_clear_mutex.unlock();
            }

            void lock()
            {
                std::lock(self->set_clear_mutex,lk);
            }

            ~custom_lock()
            {
                self->thread_cond_any=0;
                //unlock();
                self->set_clear_mutex.unlock();
            }
        };

        custom_lock cl(this,cv,lk);
        interruption_point();
        
        cv.wait(cl, [&]{ return this->is_set(); });
        interruption_point();
    }

    // rest as before
};
static interrupt_flag this_thread_interrupt_flag;


void interruption_point()
{
    if (this_thread_interrupt_flag.is_set())
    {
        throw thread_interrupted();
    }
}


template<typename Lockable>
void interruptible_wait(
    std::condition_variable_any& cv,
    //std::condition_variable& cv,
                        Lockable& lk)
{   
    this_thread_interrupt_flag.wait(cv,lk);
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
std::condition_variable_any cv;
void background_thread() {
    static int i = 0;
    while (true) {
        interruption_point();
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        std::cout << "do work" <<i++ << std::endl;
        std::unique_lock<std::mutex> lk(config_mutex);
        interruptible_wait(cv, config_mutex);
        
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

    std::this_thread::sleep_for(std::chrono::seconds(3));

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
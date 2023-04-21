#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <pthread.h>
#include <list>
#include "locker.h"
#include <cstdio>

// why template
template <typename T>
class Threadpool
{
public:
    Threadpool(int thread_number = 8, int max_requests = 10000);
    ~Threadpool();
    bool Append(T *request);

private:
    // why?
    static void *Worker(void *arg);
    void Run();

private:
    int thread_number_;
    pthread_t *threads_;
    int max_requests_;
    std::list<T *> workqueue_;
    Locker queue_locker_;
    Sem queue_stat_;
    bool stop_;
};

// list initialization
template <typename T>
Threadpool<T>::Threadpool(int thread_number, int max_requests) : thread_number_(thread_number), max_requests_(max_requests),
                                                                 stop_(false), threads_(NULL)
{
    if ((thread_number <= 0) || (max_requests <= 0))
    {
        throw std::exception();
    }

    threads_ = new pthread_t[thread_number_];
    if (!threads_)
    {
        throw std::exception();
    }

    for (int i = 0; i < thread_number_; ++i)
    {
        printf("create the %dth thread\n", i);

        if (pthread_create(threads_ + i, NULL, Worker, this) != 0)
        {
            delete[] threads_;
            throw std::exception();
        }

        if (pthread_detach(threads_[i]))
        {
            delete[] threads_;
            throw std::exception();
        }
    }
}

template <typename T>
Threadpool<T>::~Threadpool()
{
    delete threads_;
    stop_ = true;
}

template <typename T>
bool Threadpool<T>::Append(T *request)
{
    queue_locker_.Lock();
    if (workqueue_.size() > max_requests_)
    {
        queue_locker_.Unlock();
        return false;
    }

    workqueue_.push_back(request);
    queue_locker_.Unlock();
    queue_stat_.Post();
    return stop_;
}

template <typename T>
void *Threadpool<T>::Worker(void *arg)
{
    Threadpool *pool = (Threadpool *)arg;
    pool->Run();
    return pool;
}

template <typename T>
void Threadpool<T>::Run()
{
    while (!stop_)
    {
        queue_stat_.Wait();
        queue_locker_.Lock();
        if (workqueue_.empty())
        {
            queue_locker_.Unlock();
            continue;
        }

        T *request = workqueue_.front();
        workqueue_.pop_front();
        queue_locker_.Unlock();

        if (!request)
        {
            continue;
        }
        request->Process();
    }
}

#endif
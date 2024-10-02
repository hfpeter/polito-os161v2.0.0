#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAX_THREADS 1024

typedef struct {
    pthread_t thread_id;
    void* stack_ptr;
    // 其他需要记录的线程信息
} thread_info;

thread_info thread_list[MAX_THREADS];
int thread_count = 0;
struct lock  * thread_list_lock;

void* thread_function(void* arg) {
    printf("Hello from the new thread!\n");
    return NULL;
}

int my_pthread_create(pthread_t* thread, const pthread_attr_t* attr,
                      void* (*start_routine)(void*), void* arg) {
    //int result = thread_fork(thread, attr, start_routine, arg);
 	//int result  = thread_fork("new thread", NULL,start_routine,arg,
    //NULL,NULL); //&newThread
   
    if (result == 0) {
        lock_acquire(thread_list_lock);
        thread_list[thread_count].thread_id = *thread;
        thread_list[thread_count].stack_ptr = arg; // 示例中简单地将arg作为栈指针
        thread_count++;
        lock_release(&thread_list_lock);
    }
    return result;
}

void print_thread_info() {
    pthread_mutex_lock(&thread_list_lock);
    printf("Currently %d threads:\n", thread_count);
    for (int i = 0; i < thread_count; i++) {
        printf("Thread ID: %lu, Stack Ptr: %p\n",
               thread_list[i].thread_id,
               thread_list[i].stack_ptr);
    }
    pthread_mutex_unlock(&thread_list_lock);
}

int info_thread() {
    pthread_mutex_init(&thread_list_lock, NULL);

    pthread_t thread;
    if (my_pthread_create(&thread, NULL, thread_function, NULL) != 0) {
        fprintf(stderr, "Error creating thread\n");
        return 1;
    }

    printf("Hello from the main thread!\n");

    // Wait for the new thread to finish
    pthread_join(thread, NULL);

    print_thread_info();

    pthread_mutex_destroy(&thread_list_lock);
    return 0;
}

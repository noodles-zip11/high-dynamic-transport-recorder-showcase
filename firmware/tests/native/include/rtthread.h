#ifndef RTTHREAD_H
#define RTTHREAD_H

#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

typedef int rt_err_t;
typedef int rt_int32_t;
typedef int rt_base_t;
typedef int rt_bool_t;
typedef size_t rt_size_t;
typedef uint32_t rt_tick_t;
typedef void *rt_thread_t;
typedef void (*rt_thread_entry_t)(void *parameter);

rt_err_t native_rt_thread_idle_sethook(void (*hook)(void));
#ifdef NATIVE_POWER_CRITICAL_TEST
rt_base_t native_rt_hw_interrupt_disable(void);
void native_rt_hw_interrupt_enable(rt_base_t level);
rt_tick_t native_rt_tick_get(void);
void native_rt_enter_critical(void);
void native_rt_exit_critical(void);
#endif

struct rt_mutex
{
    int locked;
    uint32_t take_count;
    uint32_t release_count;
};

#define RT_EOK 0
#define RT_ERROR 1
#define RT_ENOMEM 5
#define RT_EINVAL 10
#define RT_EBUSY 16
#define RT_NULL ((void *)0)
#define RT_TRUE 1
#define RT_FALSE 0
#define RT_TICK_PER_SECOND 1000U
#define RT_WAITING_FOREVER (-1)
#define RT_IPC_FLAG_PRIO 0U
#define rt_thread_idle_sethook native_rt_thread_idle_sethook
#define MSH_CMD_EXPORT(command, description)

static inline rt_base_t rt_hw_interrupt_disable(void)
{
#ifdef NATIVE_POWER_CRITICAL_TEST
    return native_rt_hw_interrupt_disable();
#else
    return 0;
#endif
}

static inline void rt_enter_critical(void)
{
#ifdef NATIVE_POWER_CRITICAL_TEST
    native_rt_enter_critical();
#endif
}

static inline void rt_exit_critical(void)
{
#ifdef NATIVE_POWER_CRITICAL_TEST
    native_rt_exit_critical();
#endif
}

static inline void rt_hw_interrupt_enable(rt_base_t level)
{
#ifdef NATIVE_POWER_CRITICAL_TEST
    native_rt_hw_interrupt_enable(level);
#else
    (void)level;
#endif
}

static inline rt_tick_t rt_tick_get(void)
{
#ifdef NATIVE_POWER_CRITICAL_TEST
    return native_rt_tick_get();
#else
    return 0U;
#endif
}

static inline rt_thread_t rt_thread_create(const char *name,
                                           rt_thread_entry_t entry,
                                           void *parameter,
                                           uint32_t stack_size,
                                           uint8_t priority,
                                           uint32_t tick)
{
#ifdef NATIVE_AI_STACK_OBSERVER
    void native_ai_thread_create_observer(const char *thread_name,
                                           uint32_t requested_stack_size,
                                           uint8_t requested_priority,
                                           uint32_t requested_tick);

    native_ai_thread_create_observer(name, stack_size, priority, tick);
#endif
    (void)name;
    (void)entry;
    (void)parameter;
    (void)stack_size;
    (void)priority;
    (void)tick;
    return (rt_thread_t)1;
}

static inline void rt_thread_startup(rt_thread_t thread)
{
    (void)thread;
}

static inline void rt_thread_mdelay(uint32_t milliseconds)
{
    (void)milliseconds;
}

static inline rt_err_t rt_mutex_init(struct rt_mutex *mutex,
                                     const char *name,
                                     uint8_t flag)
{
    (void)name;
    (void)flag;
    mutex->locked = 0;
    mutex->take_count = 0U;
    mutex->release_count = 0U;
    return RT_EOK;
}

static inline rt_err_t rt_mutex_take(struct rt_mutex *mutex, rt_int32_t timeout)
{
    (void)timeout;
    if (mutex->locked)
    {
        return -RT_ERROR;
    }

    mutex->locked = 1;
    mutex->take_count++;
    return RT_EOK;
}

static inline rt_err_t rt_mutex_release(struct rt_mutex *mutex)
{
    mutex->locked = 0;
    mutex->release_count++;
    return RT_EOK;
}

static inline int rt_kprintf(const char *format, ...)
{
    (void)format;
    return 0;
}

static inline int rt_snprintf(char *buffer,
                              size_t capacity,
                              const char *format,
                              ...)
{
    va_list args;
    int result;

    va_start(args, format);
    result = vsnprintf(buffer, capacity, format, args);
    va_end(args);
    return result;
}

static inline int rt_strcmp(const char *left, const char *right)
{
    return strcmp(left, right);
}

#endif

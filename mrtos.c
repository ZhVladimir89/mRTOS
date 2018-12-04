/******************************************************************************
* File Name     : 'mrtos.c'
* Title         : Main Module of mRTOS
* Author        : Movila V.N. - Copyright (C) 2009
* Created       : 07/01/2009
* Revised       : 18/04/2009
* Version       : 1.09
* Target MCU    : Atmel AVR series
* Editor Tabs   : 4
*
* Notes:          mRTOS - Cooperative priority real time OS for Atmel AVR
*                         series microcontrolers
*                 NOTE:
*
* This code is distributed under the GNU Public License
* which can be found at http://www.gnu.org/licenses/gpl.txt
*
* Revision History:
* When          Who             Rev     Description of change
* -----------   -----------     ------- -----------------------
* 18-Apr-2009   Samotkov O.V.   2       Ported to AVR GCC
* 07-Jan-2009   Movila V.N      1       Created the program structure
*******************************************************************************/

#include <avr/io.h>
#include <inttypes.h>
#include <util/atomic.h>
#include "mrtos.h"

volatile struct TCB mRTOS_Tasks[mRTOS_MAX_TASKS]; // массив структур TCB всех задач приложения (Task Control Block)
uint8_t mRTOS_CurrentTask;                // номер текущей задачи
static volatile struct ECB mRTOS_Events[mRTOS_MAX_EVENTS]; // массив структур ECB приложения (Event Task Control Block)
static uint8_t mRTOS_InitTasksCounter,    // счётчик количества инициализированных задач в приложении
mRTOS_Scheduler_pri,       // переменные планировщика задач
mRTOS_Scheduler_i,
mRTOS_Scheduler_i_pri,
mRTOS_FlagStart,           // флаг признака запуска mRTOS
mRTOS_FlagSchedulerActive; // флаг планировщика задач
static volatile uint32_t mRTOS_SystemTime; // счётчик времени работы системы в системных тиках

/**
* Функция перехода на задачу (переключение задачи)
* входной параметр:
* \param TaskContextPtr - указатель на структуру контекста задачи на
*                         которую будет переключение
*/
static void mRTOS_JmpTask(struct TaskContext* TaskContextPtr) __attribute__((noinline));
static void mRTOS_JmpTask(struct TaskContext* TaskContextPtr) {
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        asm volatile(
                    "movw r26, %A0"                 "\n\t"  // сохранить адрес структуры контекста задачи в X
                    "ld   %A0, X+"                  "\n\t"  // прочитать мл. байт адреса точки входа в задачу
                    "ld   %B0, X+"                  "\n\t"  // прочитать ст. байт адреса точки входа в задачу
                    "pop __tmp_reg__"               "\n\t"  // удалить адрес возврата с вершины стека
                    "pop __tmp_reg__"               "\n\t"
                    "push %A0"                      "\n\t"  // записать мл. байт адреса точки входа в задачу в стек
                    "push %B0"                      "\n\t"  // записать ст. байт адреса точки входа в задачу в стек
                    "ld  __tmp_reg__, X"            "\n\t"  // прочитать байт состояния регистра SREG из структуры контекста задачи
                    "out  __SREG__, __tmp_reg__"    "\n\t"  // инициализировать регистр SREG
                    : "+r" ((uint16_t)TaskContextPtr)
                    :
                    : "r26", "r27", "r0"
                    );
    }
}

/**
* Функция сохранения контекста задачи
* входные параметры:
* \param Task - указатель на функцию задачи контекст которой
*               надо сохранить;
* \param TaskContextPtr - указатель на структуру контекста задачи куда
*                         будет сохранён контекст.
*/
static inline void mRTOS_SaveContext(void (*Task)(void), struct TaskContext* TaskContextPtr) {
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        asm volatile(
                    "movw r26, %A1"                 "\n\t"  // сохранить адрес структуры контекста задачи в X
                    "st   X+, %A0"                  "\n\t"  // сохранить мл. байт адреса точки входа в задачу в структуре контекста задачи
                    "st   X+, %B0"                  "\n\t"  // сохранить ст. байт адреса точки входа в задачу в структуре контекста задачи
                    "in   __tmp_reg__, __SREG__"    "\n\t"  // прочитать регистр SREG
                    "st   X, __tmp_reg__"           "\n\t"  // сохранить регистр SREG в структуре контекста задачи
                    :
                    : "r" ((uint16_t)Task),
                    "r" ((uint16_t)TaskContextPtr)
                    : "r26", "r27", "r0"
                    );
    }
}

/**
* Функция перевода текущей задачи в состояние Wait на определённое
* время
* входные параметры:
* \param Delay - интервал времени в тиках на который задача будет
*                переведена в состояние Wait;
* \param TaskContextPtr - указатель на структуру контекста задачи которая
*                         будет переведена в состояние Wait.
*/
void mRTOS_WaitTask(uint16_t Delay, struct TaskContext* TaskContextPtr) __attribute__((noinline));
void mRTOS_WaitTask(uint16_t Delay, struct TaskContext* TaskContextPtr) {
    mRTOS_Tasks[mRTOS_CurrentTask].State = WAIT; // установить состояние текущей задачи в Wait
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        mRTOS_Tasks[mRTOS_CurrentTask].Delay = Delay; // установить время состояния Wait текущей задачи
        asm volatile(
                    "movw r26, %A0"                 "\n\t" // сохранить адрес структуры контекста задачи в X
                    "pop  __tmp_reg__"              "\n\t" // прочитать ст. байт адреса возврата из стека
                    "pop  __zero_reg__"             "\n\t" // прочитать мл. байт адреса возврата из стека
                    "st   X+, __zero_reg__"         "\n\t" // сохранить мл. байт адреса возврата в структуре контекста задачи
                    "st   X+, __tmp_reg__"          "\n\t" // сохранить ст. байт адреса возврата в структуре контекста задачи
                    "in   __tmp_reg__, __SREG__"    "\n\t" // прочитать регистр SREG
                    "st   X, __tmp_reg__"           "\n\t" // сохранить регистр SREG в структуре контекста задачи
                    "clr  __zero_reg__"             "\n\t" // восстановить регистр r1 (должен быть всегда 0)
                    :
                    : "r" ((uint16_t)TaskContextPtr)
                    : "r26", "r27", "r0"
                    );
    }
    mRTOS_Scheduler();                         // вызвать функцию планировщика задач
}

/**
* Функция диспетчера задач
* входной параметр:
* \param TaskContextPtr - указатель на структуру контекста текущей задачи
*/
void mRTOS_DispatchTask(struct TaskContext* TaskContextPtr) __attribute__((noinline));
void mRTOS_DispatchTask(struct TaskContext* TaskContextPtr) {
    mRTOS_Tasks[mRTOS_CurrentTask].State = ACTIVE; // состояние текущей задачи установить в Active
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        asm volatile(
                    "movw r26, %A0"                 "\n\t" // сохранить адрес структуры контекста задачи в X
                    "pop %B0"                       "\n\t" // прочитать ст. байт адреса возврата из стека
                    "pop %A0"                       "\n\t" // прочитать мл. байт адреса возврата из стека
                    "st  X+, %A0"                   "\n\t" // сохранить мл. байт адреса возврата в структуре контекста задачи
                    "st  X+, %B0"                   "\n\t" // сохранить ст. байт адреса возврата в структуре контекста задачи
                    "in   __tmp_reg__, __SREG__"    "\n\t" // прочитать регистр SREG
                    "st   X, __tmp_reg__"           "\n\t" // сохранить регистр SREG в структуре контекста задачи
                    : "+r" ((uint16_t)TaskContextPtr)
                    :
                    : "r26", "r27", "r0"
                    );
    }
    mRTOS_Scheduler(); // вызвать функцию планировщика задач
}

/**
*  Функция нулевой задачи (процесс по умолчанию)
*/
static void mRTOS_Idle(void) {
    mRTOS_SetTaskNStatus(0, ACTIVE);      // вызвать функцию установки состояния 0-й задачи - Active
    while(1) {                            // цикл работы задачи Idle
        mRTOS_DISPATCH;                   // вызвать планировщик задач
    }
}

/**
* Обработчик прерывания по переполнению таймера T0
* (таймер системного времени mRTOS)
*/
ISR(TIMER0_OVF_vect) {
    uint8_t i;
    TCNT0 = mRTOS_SYSTEM_TIMER_RELOAD_VALUE;  // инициализация значения таймера для обеспечения заданного времени системного тика
    mRTOS_SystemTime++;                       // инкремент счётчика системного времени
    for(i=0; i < mRTOS_InitTasksCounter; i++) // цикл сканирования инициализированных задач
        if(mRTOS_Tasks[i].Delay)              // если интервал времени задержки задачи не истёк, то
            --mRTOS_Tasks[i].Delay;           // декремент счётчика времени задержки задачи
}

/**
* Функция инициализации mRTOS
*/
void mRTOS_Init(void) {
    uint8_t i;
    for(i=0; i < mRTOS_MAX_TASKS; i++) {         // цикл инициализации массива структур задач
        mRTOS_Tasks[i].Priority = 0;             // обнулить приоритет задачи
        mRTOS_Tasks[i].CurrentPriority = 0;      // обнулить текущий приоритет задачи
        mRTOS_Tasks[i].State = NOINIT;           // установить состояние задачи - NoInit
        mRTOS_Tasks[i].Delay = 0;                // обнулить поле задержки
    }
    for(i=0; i < mRTOS_MAX_EVENTS; i++) {        // цикл инициализации массива структур событий
        mRTOS_Events[i].TaskNumber = 0;          // обнулить номер закреплённой за событием задачи
        mRTOS_Events[i].FlagControlEvent = 0;    // сбросить флаг разрешения события
        mRTOS_Events[i].FlagEvent = 0;           // обнулить флаг события
    }
    mRTOS_InitTasksCounter = 0;                  // обнулить счётчик количества инициализированных задач в приложении
    mRTOS_FlagStart = 0;                         // сбросить флаг признака запуска mRTOS
    mRTOS_CurrentTask = 0;                       // установить номер текущей задачи - 0
    mRTOS_SystemTime = 0;                        // сбросить счётчик системного времени
    mRTOS_CreateTask(mRTOS_Idle, 5, ACTIVE);     // вызвать функцию создания фоновой задачи Idle с приоритетом 5 с состоянием Active
    TCNT0 = mRTOS_SYSTEM_TIMER_RELOAD_VALUE;     // инициализация системного таймера (T0)
    TCCR0 = mRTOS_SYSTEM_TIMER_PRESCALER_VALUE;  // запуск системного таймера
}

/**
* Функция создания задачи
* входные параметры:
* \param Task - указатель на функцию задачи
* \param Priority - приоритет задачи (1...255)
* \param State - состояние задачи на момент создания
*   возвращает:
* \return   1 - задача успешно создана
* \return   0 - ошибка, задача не создана
*/
uint8_t mRTOS_CreateTask(void (*Task)(void), uint8_t Priority, enum TaskState State) {
    if((mRTOS_InitTasksCounter >= mRTOS_MAX_TASKS) || // если счётчик количества инициализированных задач достиг максимума
       (Priority == 0))                               // или приоритет не верный, то
        return 0;                                     // выход с кодом ошибки
    mRTOS_SaveContext(Task, &mRTOS_Tasks[mRTOS_InitTasksCounter].Context); // вызвать функцию сохранения контекста задачи
    mRTOS_Tasks[mRTOS_InitTasksCounter].Priority = Priority;               // установить приоритет задачи
    mRTOS_Tasks[mRTOS_InitTasksCounter].CurrentPriority = Priority;        // установить текущий приоритет задачи
    mRTOS_Tasks[mRTOS_InitTasksCounter++].State = State;                   // установить состояние задачи и инкремент счётчика инициализированных задач
    return 1;                                         // выход с кодом успешного выполнения
}

/**
*  Функция планировщика задач
*/
void mRTOS_Scheduler(void) {
    uint16_t temp;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        asm volatile(
                    "pop  __tmp_reg__"              "\n\t"  // очистить текущее состояние стека
                    "pop  __tmp_reg__"              "\n\t"
                    :
                    :
                    : "r0"
                    );
    }
    mRTOS_Scheduler_pri = 0;
    mRTOS_FlagSchedulerActive = 0;
    if(!mRTOS_FlagStart) {                                      // если первый вход в планировщик (при запуске mRTOS), то
        mRTOS_FlagStart = 1;                                    // взвести флаг признака запуска mRTOS
        mRTOS_JmpTask(&mRTOS_Tasks[mRTOS_CurrentTask].Context); // вызывать функцию передачи управление первой задаче
    }
    if(--mRTOS_Tasks[mRTOS_CurrentTask].CurrentPriority == 0)   // декремент текущего приоритета текущей задачи и если он равен нулю хотя бы для одной задачи, то восстановить приоритет всех задач
        for(mRTOS_Scheduler_i=0;                                // цикл восстановления приоритета всех задач
            mRTOS_Scheduler_i < mRTOS_InitTasksCounter;
            mRTOS_Scheduler_i++)
            mRTOS_Tasks[mRTOS_Scheduler_i].CurrentPriority = mRTOS_Tasks[mRTOS_Scheduler_i].Priority; // восстановить приоритет задачи
    if(mRTOS_Tasks[mRTOS_CurrentTask].State == ACTIVE) { // если состояние текущей задачи Active, то
        mRTOS_Tasks[mRTOS_CurrentTask].State = SUSPEND;  // перевести текущую задачу в состояние Suspend
        mRTOS_Scheduler_i_pri = mRTOS_CurrentTask;       // сохранить номер текущей задачи
        mRTOS_FlagSchedulerActive = 1;                   // взвести флаг активности
    }
    for(mRTOS_Scheduler_i=0; mRTOS_Scheduler_i < mRTOS_InitTasksCounter; mRTOS_Scheduler_i++) { // цикл сканирования задач
        ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
            temp = mRTOS_Tasks[mRTOS_Scheduler_i].Delay; // прочитать значение задержки текущей задачи
        }
        if((temp == 0) && (mRTOS_Tasks[mRTOS_Scheduler_i].State == WAIT)) {       // если состояние текущей задачи Wait и задержка истекла, то
            mRTOS_Scheduler_pri = mRTOS_Tasks[mRTOS_Scheduler_i].CurrentPriority; // сохранить текущий приоритет этой задачи
            mRTOS_CurrentTask = mRTOS_Scheduler_i;                                // сохранить номер текущей задачи
            break;   // выход из цикла (то есть передать управление этой задаче не зависимо от приоритета)
        }
        if((mRTOS_Tasks[mRTOS_Scheduler_i].CurrentPriority >= mRTOS_Scheduler_pri) && (mRTOS_Tasks[mRTOS_Scheduler_i].State == ACTIVE)) { // если состояние текущей задачи Active и её приоритет выше, то
            mRTOS_Scheduler_pri = mRTOS_Tasks[mRTOS_Scheduler_i].CurrentPriority; // сохранить приоритет этой задачи (поиск задачи с наиболее высоким приоритетом)
            mRTOS_CurrentTask = mRTOS_Scheduler_i;                                // сохранить номер текущей задачи
        }
    }
    if(mRTOS_FlagSchedulerActive)                           // если флаг активности взведён, то
        mRTOS_Tasks[mRTOS_Scheduler_i_pri].State = ACTIVE;  // установить состояние текущей задачи Active

    mRTOS_JmpTask(&mRTOS_Tasks[mRTOS_CurrentTask].Context); // вызвать функцию передачи управления текущей задачи
}

/**
* Функция инициализации события (закрепление события за текущей задачей,
* разрешение события и сброс флага события)
* входной параметр:
* \param EventNumber - номер события закрепляемый за текущей задачей
* возвращает:
* \return 1 - событие успешно инициализировано
* \return 0 - ошибка, событие не инициализировано
*/
uint8_t mRTOS_InitEvent(uint8_t EventNumber) {
    if(EventNumber >= mRTOS_MAX_EVENTS)    // если номер события не верный, то
        return 0;                          // выход с кодом ошибки
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        mRTOS_Events[EventNumber].TaskNumber = mRTOS_CurrentTask; // сохранить номер текущей задачи в структуре события
        mRTOS_Events[EventNumber].FlagControlEvent = 1;           // взвести флаг разрешения события
        mRTOS_Events[EventNumber].FlagEvent = 0;                  // сбросить флаг события
    }
    return 1;  // выход с кодом успешного выполнения
}

/**
* Функция запрета события
* входной параметр:
* \param EventNumber - номер события
* возвращает:
* \return 1 - событие успешно установлено
* \return 0 - ошибка, событие не установлено
*/
uint8_t mRTOS_DisableEvent(uint8_t EventNumber) {
    if(EventNumber >= mRTOS_MAX_EVENTS)  // если номер события не верный, то
        return 0;                        // выход с кодом ошибки
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        mRTOS_Events[EventNumber].FlagControlEvent = 0; // сбросить флаг разрешения события (запретить событие)
    }
    return 1;                            // выход с кодом успешного выполнения
}

/**
* Функция разрешения события
* входной параметр:
* \param EventNumber - номер события
* возвращает:
* \return 1 - событие успешно установлено
* \return 0 - ошибка, событие не установлено
*/
uint8_t mRTOS_EnableEvent(uint8_t EventNumber) {
    if(EventNumber >= mRTOS_MAX_EVENTS)  // если номер события не верный, то
        return 0;                        // выход с кодом ошибки
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        mRTOS_Events[EventNumber].FlagControlEvent = 1; // взвести флаг разрешения события (разрешить событие)
    }
    return 1;                            // выход с кодом успешного выполнения
}

/**
* Функция установки флага события если оно разрешено
* входной параметр:
* \param  EventNumber - номер события
* возвращает:
* \return 1 - событие успешно установлено
* \return 0 - ошибка, событие не установлено
*/
uint8_t mRTOS_SetEvent(uint8_t EventNumber) {
    if(EventNumber >= mRTOS_MAX_EVENTS)  // если номер события не верный, то
        return 0;                        // выход с кодом ошибки
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        if(mRTOS_Events[EventNumber].FlagControlEvent) // если флаг разрешения события взведён, то
            mRTOS_Events[EventNumber].FlagEvent++;     // инкремент флага события
    }
    return 1;                            // выход с кодом успешного выполнения
}

/**
* Функция установки значение флага события если оно разрешено
* входные параметры:
* \param EventNumber - номер события
* \param FlagEventValue - устанавливаемое значение флага события
* возвращает:
* \return 1 - флаг события успешно установлен
* \return 0 - ошибка, флаг события не установлен
*/
uint8_t mRTOS_SetEventValue(uint8_t EventNumber, uint8_t FlagEventValue) {
    if(EventNumber >= mRTOS_MAX_EVENTS)  // если номер события не верный, то
        return 0;                        // выход с кодом ошибки
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        if(mRTOS_Events[EventNumber].FlagControlEvent)            // если флаг разрешения события взведён, то
            mRTOS_Events[EventNumber].FlagEvent = FlagEventValue; // инициализировать значение флага события
    }
    return 1;                            // выход с кодом успешного выполнения
}

/**
* Функция чтения флага события с последующим сбросом флага
* события
* входные параметры:
* \param EventNumber - номер события
* возвращает:
* \return значение флага события
* \return 0 - при ошибке чтения события
*/
uint8_t mRTOS_GetEvent(uint8_t EventNumber) {
    uint8_t temp=0;
    if(EventNumber >= mRTOS_MAX_EVENTS)  // если номер события не верный, то
        return temp;                     // выход с возвратом 0
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        if(mRTOS_Events[EventNumber].FlagControlEvent) { // если событие разрешено, то
            temp = mRTOS_Events[EventNumber].FlagEvent;  // прочитать значение флага события
            mRTOS_Events[EventNumber].FlagEvent = 0;     // сбросить флаг события
        }
    }
    return temp;                         // выход с возвратом флага события
}

/**
* Функция чтения флага события без сброса флага события
* входной параметр:
* \param EventNumber - номер события
* возвращает:
* \return значение флага события
* \return 0 - при ошибке чтения события
*/
uint8_t mRTOS_PopEvent(uint8_t EventNumber) {
    uint8_t temp=0;
    if(EventNumber >= mRTOS_MAX_EVENTS)  // если номер события не верный, то
        return temp;                     // выход с возвратом 0
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        if(mRTOS_Events[EventNumber].FlagControlEvent)  // если событие разрешено, то
            temp = mRTOS_Events[EventNumber].FlagEvent; // прочитать значение флага события
    }
    return temp;                         // выход с возвратом флага события
}

/**
* Функция установки состояния текущей задачи
* входной параметр:
* \param Status - устанавливаемое состояние текущей задачи
*/
void mRTOS_SetTaskStatus(enum TaskState Status) {
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        mRTOS_Tasks[mRTOS_CurrentTask].State = Status; // установить состояние текущей задачи
    }
}

/**
* Функция установки состояния задачи с заданным номером
* входные параметры:
* \param TaskNumber - номер задачи
* \param Status - устанавливаемое состояние
* возвращает:
* \return 1 - состояние задачи успешно установлено
* \return 0 - ошибка, состояние задачи не установлено
*/
uint8_t mRTOS_SetTaskNStatus(uint8_t TaskNumber, enum TaskState Status) {
    if(TaskNumber >= mRTOS_MAX_TASKS)    // если номер задачи неверный, то
        return 0;                        // выход с кодом ошибки
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        if(mRTOS_Tasks[TaskNumber].State == STOP)   // если заданная задача остановлена, то
            mRTOS_Tasks[TaskNumber].State = Status; // установить состояние этой задачи
    }
    return 1;                            // выход с кодом успешного выполнения
}

/**
* Функция установки значения системного времени в тиках
* входной параметр:
* \param Time - устанавливаемое значение системного времени в тиках
*/
void mRTOS_SetSystemTime(uint32_t Time) {
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        mRTOS_SystemTime = Time;           // установить значение системного времени
    }
}

/**
* Функция чтения текущего значения системного времени в тиках
* возвращает:
* \return текущее значение системного времени в тиках
*/
uint32_t mRTOS_GetSystemTime(void) {
    uint32_t temp;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        temp = mRTOS_SystemTime;           // прочитать значение системного времени
    }
    return temp;
}

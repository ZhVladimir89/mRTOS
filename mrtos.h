/******************************************************************************
* File Name     : 'mrtos.h'
* Title         : Main Module header of mRTOS
* Author        : Movila V.N. - Copyright (C) 2009
* Created       : 07/01/2009
* Revised       : 18/04/2009
* Version       : 1.09
* Target MCU    : Atmel AVR
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

#ifndef mRTOS_H_INCLUDED
#define mRTOS_H_INCLUDED

enum TaskState{ NOINIT, ACTIVE, SUSPEND, WAIT, SEMAPHORE, STOP }; // состояние (статус) задачи

// --- структура контекста задачи ---
struct TaskContext {
    uint16_t TaskAddress;        // адреса точки входа в задачу
    uint8_t  TaskCpuState;       // состояние регистра SREG задачи
};
// --- структура блока контроля задачи (Task Control Block) ---
struct TCB {
    uint8_t Priority,            // приоритет задачи
    CurrentPriority;             // текущий приоритет задачи
    struct TaskContext Context;  // текущий контекст задачи
    enum TaskState State;        // текущее состояние задачи
    uint16_t Delay;              // время задержки в тиках в состоянии задачи Wait
};
// --- структура блока контроля события (Event Control Block) ---
struct ECB {
    uint8_t TaskNumber,          // номер задачи закрепленной за событием
    FlagControlEvent,            // флаг установлен - событие разрешено (ожидание события)
    FlagEvent;                   // флаг установлен - событие произошло
};

// --- Макросы mRTOS ---

#define mRTOS_APPLICATION_TASKS 1                        // количество пользовательских задач в приложении
#define mRTOS_MAX_EVENTS        1                        // количество событий в приложении
#define mRTOS_MAX_TASKS    (mRTOS_APPLICATION_TASKS + 1) // общее количество задач в приложении (задача Idle создаётся всегда)

// Для работы RTOS используется таймер T0 - задает времменой интервал - системный тик
// Ktcnt0 = 256 - T / (Tclk * Pscl), где Tclk = 1 / Fxtal, Pscl - значение предделителя T0
// T = 1 мсек.  при Xtal = 8 МГц, Tclk = 0.125 мксек, Pscl = 64
#define mRTOS_SYSTEM_TIMER_PRESCALER_VALUE 3    // Ktcnt0 = 256 - 1000 / (0.125 * 64) = 131; T = (256 - Ktcnt0) * (Tclk * Pscl) = (256 - 131) * (0.125 * 64) = 1.000 мсек.
#define mRTOS_SYSTEM_TIMER_RELOAD_VALUE    131  // значение перезагрузки системного таймера для обеспечения заданного интервала системного тика

// вызов функции диспетчера задач
#define mRTOS_DISPATCH  mRTOS_DispatchTask(&mRTOS_Tasks[mRTOS_CurrentTask].Context)
// вызов функции перевода текущей задачи в состояние Wait на время d тиков (d = 0 .. 65535)
#define mRTOS_TASK_WAIT(d)  mRTOS_WaitTask(d, &mRTOS_Tasks[mRTOS_CurrentTask].Context)
// вызов функции перевода задачи под номером n в состояние Active
#define mRTOS_TASK_ACTIVE(n)  mRTOS_SetTaskNStatus(n, ACTIVE)
// вызов функции перевода текущей задачи в состояние Stop с последующим вызовом диспетчера задач
#define mRTOS_TASK_STOP  {mRTOS_SetTaskStatus(STOP); mRTOS_DISPATCH;}

// --- Функции mRTOS ---

void mRTOS_Init(void);                           // функция инициализация OS
uint8_t mRTOS_CreateTask(void (*Task)(void), uint8_t Priority, enum TaskState State); // функция создания задачи
void mRTOS_WaitTask(uint16_t Delay, struct TaskContext* TaskContextPtr); // функция перевода задачи в состояняие Wait на время Delay тиков
void mRTOS_DispatchTask(struct TaskContext* TaskContextPtr); // функция вызова диспетчера задач
void mRTOS_Scheduler(void);                      // функция планировщика задач
void mRTOS_SetTaskStatus(enum TaskState Status); // функция перевода текущей задачи в состояние Status
uint8_t mRTOS_SetTaskNStatus(uint8_t TaskNumber, enum TaskState Status); // функция перевода задачи под номером TaskNumber в состояние Status

// -- функции работы с событиями --

uint8_t mRTOS_InitEvent(uint8_t EventNumber);    // функция инициализации события под номером EventNumber в текущей задаче
uint8_t mRTOS_DisableEvent(uint8_t EventNumber); // функция запрета события под номером EventNumber
uint8_t mRTOS_EnableEvent(uint8_t EventNumber);  // функция разрешения события под номером EventNumber
uint8_t mRTOS_SetEvent(uint8_t EventNumber);     // функция установки события под номером EventNumber
uint8_t mRTOS_SetEventValue(uint8_t EventNumber, uint8_t FlagEventValue); // функция установки значения флага события FlagEventValue под номером EventNumber
uint8_t mRTOS_GetEvent(uint8_t EventNumber);     // функция чтения состояния события под номером EventNumber с последующим сбросом события
uint8_t mRTOS_PopEvent(uint8_t EventNumber);     // функция чтения состояния события под номером EventNumber без сброса события

// -- функции работы с системным временем --

void mRTOS_SetSystemTime(uint32_t Time);         // функция установки системного времени в тиках
uint32_t mRTOS_GetSystemTime(void);              // функция чтения текущего значения системного времени в тиках

extern volatile struct TCB mRTOS_Tasks[mRTOS_MAX_TASKS]; // массив структур TCB всех задач приложения (Task Control Block)
extern uint8_t mRTOS_CurrentTask;                // номер текущей задачи

#endif

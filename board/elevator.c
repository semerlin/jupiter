/**
* This file is part of the auto-elevator project.
*
* Copyright 2018, Huang Yang <elious.huang@gmail.com>. All rights reserved.
*
* See the COPYING file for the terms of usage and distribution.
*/
#include "elevator.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "queue.h"
#include "trace.h"
#include "led_status.h"
#include "protocol.h"
#include "keymap.h"
#include "keyctl.h"
#include "robot.h"
#include "floormap.h"
#include "global.h"
#include "switch_monitor.h"

#undef __TRACE_MODULE
#define __TRACE_MODULE  "[elev]"


/* elevator current floor */
static char elev_cur_floor = 1;

static bool hold_door = FALSE;
static uint8_t hold_cnt = 0;

/* elevator state */
static elev_run_state run_state = run_stop;
static elev_work_state work_state = work_idle;

/* key press queue */
static xQueueHandle xQueueFloor = NULL;

static xQueueHandle xArriveQueue = NULL;
static xSemaphoreHandle xNotifySemaphore = NULL;
#define MAX_CHECK_CNT 5

/**
 * @brief elevator task
 * @param pvParameters - task parameters
 */
static void vElevHold(void *pvParameters)
{
    for (;;)
    {
        if (hold_door)
        {
            hold_cnt ++;
            if (hold_cnt > 15)
            {
                hold_door = FALSE;
                hold_cnt = 0;
                keyctl_release(keymap_open());
            }
        }
        
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

/**
 * @brief elevator task
 * @param pvParameters - task parameters
 */
static void vElevControl(void *pvParameters)
{
    uint8_t key = 0;
    for (;;)
    {
        if(xQueueReceive(xQueueFloor, &key, portMAX_DELAY))
        {
            keyctl_press(key);
            vTaskDelay(500 / portTICK_PERIOD_MS);
            keyctl_release(key);
        }
    }
}

/**
 * @brief elevator task
 * @param pvParameters - task parameters
 */
static void vElevArrive(void *pvParameters)
{
    uint8_t err_cnt = 0;
    char floor = 0;
    for (;;)
    {
        err_cnt = 0;
        if (xQueueReceive(xArriveQueue, &floor, portMAX_DELAY))
        {
            while (pdTRUE != xSemaphoreTake(xNotifySemaphore, 
                                         500 / portTICK_PERIOD_MS))
            {
                err_cnt ++;
                if (err_cnt > MAX_CHECK_CNT)
                {
                    break;
                }
                notify_arrive(floor);
            }
        }
    }
}

/**
 * @brief arrive notify callback
 * @brief data - data received
 * @param len - data length
 */
void arrive_hook(const uint8_t *data, uint8_t len)
{
    xSemaphoreGive(xNotifySemaphore);
}

/**
 * @brief initialize elevator
 * @return init status
 */
bool elev_init(void)
{
    TRACE("initialize elevator...\r\n");
    xQueueFloor = xQueueCreate(1, 1);
    xArriveQueue = xQueueCreate(1, 1);
    xNotifySemaphore = xSemaphoreCreateBinary();
    register_arrive_cb(arrive_hook);
    xTaskCreate(vElevHold, "elvhold", ELEV_STACK_SIZE, NULL,
                    ELEV_PRIORITY, NULL);
    xTaskCreate(vElevControl, "elvctl", ELEV_STACK_SIZE, NULL,
                    ELEV_PRIORITY, NULL);
    xTaskCreate(vElevArrive, "elvarrive", ELEV_STACK_SIZE, NULL,
                    ELEV_PRIORITY, NULL);
    return TRUE;
}

/**
 * @brief elevator going floor
 * @param floor - going floor
 */
void elev_go(char floor)
{
    TRACE("elevator go floor: %d\r\n", floor);
    uint8_t key = keymap_floor_to_key(floor);
    xQueueOverwrite(xQueueFloor, &key);
}

/**
 * @brief indicate elevator arrive
 * @param floor - arrive floor
 */
void elev_arrived(char floor)
{
    if (work_robot == work_state)
    {
        if (robot_is_checkin(floormap_dis_to_phy(floor)))
        {
            if (elev_cur_floor == floor)
            {
                TRACE("floor arrive: %d\r\n", floor);
                notify_arrive(floor);
                xQueueOverwrite(xArriveQueue, &floor);
            }
        }
    }
}

/**
 * @brief hold elevator door open
 * @param flag - open or close
 */
void elev_hold_open(bool flag)
{
    uint8_t key = keymap_open();
    if (flag)
    {
        if (switch_arrive == switch_get_status())
        {
            hold_cnt = 0;
            hold_door = TRUE;
            keyctl_press(key);
        }
    }
    else
    {
        if (hold_door)
        {
            hold_door = FALSE;
            hold_cnt = 0;
            keyctl_release(key);
        }
    }
}

/**
 * @brief decrease current floor
 */
void elev_decrease(void)
{
    elev_cur_floor --;
    if (0 == elev_cur_floor)
    {
        elev_cur_floor --;
    }
    TRACE("decrease floor: %d\r\n", elev_cur_floor);
    if (is_down_led_on(elev_cur_floor))
    {
        run_state = run_down;
    }
    else if (is_up_led_on(elev_cur_floor))
    {
        run_state = run_up;
    }
    else
    {
        run_state = run_stop;
    }
}

/**
 * @brief increase current floor
 */
void elev_increase(void)
{
    elev_cur_floor ++;
    if (0 == elev_cur_floor)
    {
        elev_cur_floor ++;
    }
    TRACE("increase floor: %d\r\n", elev_cur_floor);
    if (is_up_led_on(elev_cur_floor))
    {
        run_state = run_up;
    }
    else if (is_down_led_on(elev_cur_floor))
    {
        run_state = run_down;
    }
    else
    {
        run_state = run_stop;
    }
}

/**
 * @brief set current floor as first floor
 */
void elev_set_first_floor(void)
{
    TRACE("set first floor\r\n");
    elev_cur_floor = 1;
}

/**
 * @brief get elevator previous run status
 * @return floor previous run status
 */
elev_run_state elev_state_run(void)
{
    return run_state;
}

/**
 * @brief get elevator current state
 * @return elevator current state
 */
elev_work_state elev_state_work(void)
{
    return work_state;
}

/**
 * @brief set elevator state
 * @param state - elevator state
 */
void elevator_set_state_work(elev_work_state state)
{
    work_state = state;
}

/**
 * @brief get elevator current floor
 * @return elevator current floor
 */
char elev_floor(void)
{
    return elev_cur_floor;
}



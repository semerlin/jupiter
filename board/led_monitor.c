/**
* This file is part of the auto-elevator project.
*
* Copyright 2018, Huang Yang <elious.huang@gmail.com>. All rights reserved.
*
* See the COPYING file for the terms of usage and distribution.
*/
#include "led_monitor.h"
#include "FreeRTOS.h"
#include "task.h"
#include "trace.h"
#include "led_status.h"
#include "keymap.h"
#include "elevator.h"
#include "parameter.h"
#include "global.h"
#include "floormap.h"
#include "elevator.h"
#include "robot.h"

#undef __TRACE_MODULE
#define __TRACE_MODULE  "[ledmtl]"

#define LED_INTERVAL            (200)
#define LED_MONITOR_INTERVAL    (LED_INTERVAL / portTICK_PERIOD_MS)
#define LED_WORK_MONITOR_INTERVAL    (1000 / portTICK_PERIOD_MS)

static uint16_t led_status = 0;

/* led password */
uint8_t led_pwd[5];

typedef struct
{
    uint8_t pwd;
    uint32_t time;
}pwd_node;

static pwd_node pwds[4] = 
{
    {0, 0},
    {0, 0},
    {0, 0},
    {0, 0}
};

#define LED_PWD_CHECK_TIME 6000

/**
 * @brief check if floor arrived, 1->0 means arrive
 * @return check result
 */
static bool __INLINE is_floor_arrive(uint16_t origin, uint16_t new)
{
    return (0 == (origin & new));
}

/**
 * @brief get 1 bit position
 * @param data - data to process
 */
static uint8_t bit_to_pos(uint16_t data)
{
    uint8_t pos = 0;
    while (0 == (data & 0x01))
    {
        data >>= 1;
        pos ++;
    }

    return pos;
}

/**
 * @brief push password node to array
 * @param node - password node
 */
static void push_pwd_node(const pwd_node *node)
{
    TRACE("push pwd node: key(%d), time(%d)\r\n", node->pwd, node->time);
    for (int i = 0; i < 3; ++i)
    {
        pwds[i].pwd = pwds[i + 1].pwd;
        pwds[i].time = pwds[i + 1].time;
    }
    pwds[3].pwd = node->pwd;
    pwds[3].time = node->time;

    /* validate time */
    if (pwds[3].time > pwds[0].time)
    {
        if ((pwds[3].time - pwds[0].time) * LED_INTERVAL < LED_PWD_CHECK_TIME)
        {
            /* validate password */
            for (int i = 0; i < 3; ++i)
            {
                if (pwds[i].pwd != led_pwd[i])
                {
                    return ;
                }
            }
            elev_set_first_floor();
        }
    }
}

/**
 * @brief led monitor task
 * @param pvParameters - task parameter
 */
static void vLedWorkMonitor(void *pvParameters)
{
    char floor = 0;
    for (;;)
    {
        /* check elevator status */
        if (work_robot == elev_state_work())
        {
            if (DEFAULT_CHECKIN != robot_checkin_cur())
            {
                floor = floormap_phy_to_dis(robot_checkin_cur());
                if (!is_led_on(floor) && (floor != elev_floor()))
                {
                    elev_go(floor);
                }
            }
        }
        
        vTaskDelay(LED_WORK_MONITOR_INTERVAL);      
    }
}

/**
 * @brief led monitor task
 * @param pvParameters - task parameter
 */
static void vLedMonitor(void *pvParameters)
{
    uint16_t cur_status = 0;
    uint16_t changed_status = 0;
    uint16_t per_changed_bit = 0;
    char floor = 0;
    led_status = led_status_get();
    uint32_t timestamp = 0;
    for (;;)
    {
        cur_status = led_status_get();
        changed_status = cur_status ^ led_status;
        if (0 != changed_status)
        {
            /* led status changed */
            do
            {
                per_changed_bit = changed_status & ~(changed_status - 1);
                floor = keymap_key_to_floor(bit_to_pos(per_changed_bit));
                if (INVALID_FLOOR != floor)
                {
                    if (is_floor_arrive(led_status, per_changed_bit))
                    {
                        TRACE("floor led off: %d\r\n", floor);
                        /* notify floor arrived */
                        elev_arrived(floor);
                    }
                    else
                    {
                        TRACE("floor led on: %d\r\n", floor);
                        /* push password */
                        pwd_node node = {(uint8_t)floor, timestamp};
                        push_pwd_node(&node);
                    }
                }
                changed_status &= changed_status - 1;
            }while (0 != changed_status);
            led_status = cur_status;
        }
        vTaskDelay(LED_MONITOR_INTERVAL);
        
        timestamp ++;
    }
}

/**
 * @brief initialize led monitor
 * @return init status
 */
bool led_monitor_init(void)
{
    TRACE("initialize led monitor...\r\n");
    param_get_pwd(led_pwd);
    xTaskCreate(vLedMonitor, "ledmonitor", LED_MONITOR_STACK_SIZE, NULL, 
                    LED_MONITOR_PRIORITY, NULL);
    xTaskCreate(vLedWorkMonitor, "ledworkmonitor", LED_MONITOR_STACK_SIZE, NULL, 
                    LED_MONITOR_PRIORITY, NULL);

    return TRUE;
}

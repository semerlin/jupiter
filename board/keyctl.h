/**
* This file is part of the auto-elevator project.
*
* Copyright 2018, Huang Yang <elious.huang@gmail.com>. All rights reserved.
*
* See the COPYING file for the terms of usage and distribution.
*/
#ifndef _KEYCTL_H_
  #define _KEYCTL_H_

#include "types.h"

BEGIN_DECLS

void keyctl_init(void);
void keyctl_press(uint8_t num);
void keyctl_release(uint8_t num);
void keyctl_release_all(void);

END_DECLS


#endif /* _LED_MOTOR_H_ */


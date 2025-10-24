/*
 * app_main.h
 *
 *  Created on: Sep 12, 2023
 *      Author: Ishaan
 */

#pragma once

//have to define these as C functions so they can be called from main.c
#ifdef __cplusplus
extern "C"
{
#endif

void app_init();
void app_loop();

#ifdef __cplusplus
}
#endif


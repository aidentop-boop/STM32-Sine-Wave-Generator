#ifndef SSD_H

#define SSD_H

#include "stm32f4xx.h"

void SSD_Init(void);
void SSD_DisplayValue(int value);
void SSD_Refresh(void);

#endif
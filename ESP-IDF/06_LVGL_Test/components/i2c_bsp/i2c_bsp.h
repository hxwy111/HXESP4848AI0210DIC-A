#ifndef I2C_BSP_H
#define I2C_BSP_H

#include <stdint.h>
#include "driver/i2c.h"

/* 初始化板级 I2C 主机。 */
void I2C_master_Init(void);

/* 向指定从设备寄存器连续写入数据。 */
uint8_t I2C_write_buff(uint8_t addr,uint8_t reg,uint8_t *buf,uint8_t len);

/* 从指定从设备寄存器连续读取数据。 */
uint8_t I2C_read_buff(uint8_t addr,uint8_t reg,uint8_t *buf,uint8_t len);

/* 执行一次通用的 I2C 写后读事务。 */
uint8_t I2C_master_write_read_device(uint8_t addr,uint8_t *writeBuf,uint8_t writeLen,uint8_t *readBuf,uint8_t readLen);

/* 扫描并打印当前 I2C 总线上的设备地址。 */
void I2C_scan_devices(void);

#endif

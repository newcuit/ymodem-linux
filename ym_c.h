/**************************************************************************************
 **** Copyright (C), 2017, xx xx xx xx info&tech Co., Ltd.

 * File Name     : ymodem.h
 * Author        :
 * Date          : 2017-03-15
 * Description   : .C file function description
 * Version       : 1.0
**************************************************************************************/
#ifndef __YMODEM_H_
#define __YMODEM_H_

#include <stdio.h>

/**************************************************************************************
* Description    : 定义ymodem需要的协议变量
**************************************************************************************/
#define PACKET_SEQNO_INDEX      (1)
#define PACKET_SEQNO_COMP_INDEX (2)

#define PACKET_HEADER           (3)
#define PACKET_TRAILER          (2)
#define PACKET_OVERHEAD         (PACKET_HEADER + PACKET_TRAILER)
#define PACKET_SIZE             (128)
#define PACKET_1K_SIZE          (1024)

#define FILE_NAME_LENGTH        (256)
#define FILE_SIZE_LENGTH        (16)

 /* 表示后面是128字节的数据 */
#define SOH                     (0x01)
 /* 表示后面是1024字节的数据 */
#define STX                     (0x02)
 /* 结束一次传输 */
#define EOT                     (0x04)
 /* 成功应答 */
#define ACK                     (0x06)
 /* Nak */
#define NACK                    (0x15)
 /* 中断一次传输  */
#define CA                      (0x18)
 /* 请求一个报文 */
#define CRC16                   (0x43)

 /* 中止A */
#define ABORT1                  (0x41)
 /* 中止a */
#define ABORT2                  (0x61)

#define NAK_TIMEOUT             (0x40000)
#define MAX_ERRORS              (5)

/**************************************************************************************
* FunctionName   : ymodem_use_nand()
* Description    : 设置采用nand flash升级
* EntryParameter : None
* ReturnValue    : None
**************************************************************************************/
void ymodem_use_nand(void);

/**************************************************************************************
* FunctionName   : ymodem_flash()
* Description    : 使用ymodem协议，接收升级一个镜像
* EntryParameter : filename,文件名, addr，表示升级地址, rom_size,表示要擦除的大小
* ReturnValue    : 返回升级文件的大小
**************************************************************************************/
uint32_t ymodem_flash(char *filename, uint32_t addr, uint32_t rom_size);

/**************************************************************************************
* FunctionName   : ymodem_init()
* Description    : ymodem协议初始化
* EntryParameter : None
* ReturnValue    : 返回错误码
**************************************************************************************/
uint8_t ymodem_init(void);

#endif 

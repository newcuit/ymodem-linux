/**************************************************************************************
 **** Copyright (C), 2017, xx xx xx xx info&tech Co., Ltd.

 * File Name     : ymodem.h
 * Author        :
 * Date          : 2017-03-15
 * Description   : .C file function description
 * Version       : 1.0
**************************************************************************************/
#include "ym_c.h"
#include "string.h"
#include "stdlib.h"
#include "image.h"
#include "uart.h"
#include "rom.h"
#include "crc16.h"
#include "gpio.h"
#include "upgrade.h"

/**************************************************************************************
* Description    : nand flash起始升级page
**************************************************************************************/
static int32_t ymodem_nand_page = NAND_PROGRAM_SPAGE;
static int8_t nand_upgrade = 0;

/**************************************************************************************
* FunctionName   : ymodem_use_nand()
* Description    : 设置采用nand flash升级
* EntryParameter : None
* ReturnValue    : None
**************************************************************************************/
void ymodem_use_nand(void)
{
	nand_upgrade = 1;
}

/**************************************************************************************
* FunctionName   : ymodem_recv_byte()
* Description    : 接收一个字节
* EntryParameter : c要接收的字节,timeout，超时
* ReturnValue    : 返回错误码,0表示成功
**************************************************************************************/
static int32_t ymodem_recv_byte(uint8_t *c, uint32_t timeout)
{
	return !lpuart_block_read(FLASH_UART_ID, c, 1, timeout);
}

/**************************************************************************************
* FunctionName   : ymodem_recv_buffer()
* Description    : 接收一个buffer
* EntryParameter : buffer, 读取一串数据放到buffer中， len,需要读取的长度, timeout,超时时间
* ReturnValue    : 返回错误码,0表示成功
**************************************************************************************/
static int32_t ymodem_recv_buffer(uint8_t *buffer, uint32_t len, uint32_t timeout)
{
	return lpuart_block_read(FLASH_UART_ID, buffer, len, timeout);
}

/**************************************************************************************
* FunctionName   : ymodem_send_byte()
* Description    : 发送一个byte
* EntryParameter : c即将发送的数据
* ReturnValue    : 返回错误码,0表示成功
**************************************************************************************/
static uint32_t ymodem_send_byte(uint8_t c)
{
	return lpuart_phy_write(FLASH_UART_ID, &c, 1);
}

/**************************************************************************************
* FunctionName   : ymodem_flash_erase()
* Description    : 擦除flash指定的地址
* EntryParameter : addr,指定擦除的地址
* ReturnValue    : 返回错误码,0表示成功
**************************************************************************************/
static int32_t ymodem_flash_erase(uint32_t addr, uint32_t rom_size)
{
	s32gpio_pin_set(IMAGE_LED1, !s32gpio_pin_get(IMAGE_LED1));
	if(!nand_upgrade) {
		s32gpio_pin_set(IMAGE_LED2, !s32gpio_pin_get(IMAGE_LED2));
		s32_rom_erase((uint32_t *)addr, rom_size);
	} else {
		s32gpio_pin_set(IMAGE_LED2, !s32gpio_pin_get(IMAGE_LED1));
		image_burn_start();
	}
	return rom_size;
}

/**************************************************************************************
* FunctionName   : ymodem_flash_failed()
* Description    : 升级失败
* EntryParameter : None
* ReturnValue    : None
**************************************************************************************/
static inline void ymodem_flash_failed(void)
{
	int i = 0;

	s32gpio_pin_set(IMAGE_LED2, 0);
	s32gpio_pin_set(IMAGE_LED1, 0);
	while(1) {
		s32gpio_pin_set(IMAGE_LED2, !s32gpio_pin_get(IMAGE_LED2));
		s32gpio_pin_set(IMAGE_LED1, !s32gpio_pin_get(IMAGE_LED1));
		for(i = 0;i < 200000; i++);
	}
}

/**************************************************************************************
* FunctionName   : ymodem_flash_write()
* Description    : 写flash或者rom
* EntryParameter : addr,写入的地址偏移, buf,写入的buf指针， len，写入的长度
* ReturnValue    : 返回写入的长度
**************************************************************************************/
static int32_t ymodem_flash_write(uint32_t addr, uint8_t *buf, uint32_t len)
{
	s32gpio_pin_set(IMAGE_LED1, !s32gpio_pin_get(IMAGE_LED1));
	if(!nand_upgrade) {
		s32gpio_pin_set(IMAGE_LED2, !s32gpio_pin_get(IMAGE_LED2));
		return s32_rom_write(buf, (uint32_t *)addr, len);
	} else {
		s32gpio_pin_set(IMAGE_LED2, !s32gpio_pin_get(IMAGE_LED2));
		ymodem_nand_page = image_burn_buffer(ymodem_nand_page, buf, len);
		if(ymodem_nand_page < NAND_PROGRAM_SPAGE) ymodem_flash_failed();
	}

	return len;
}

/**************************************************************************************
* FunctionName   : ym_crc16()
* Description    : 计算校验码
* EntryParameter : buf,待校验buffer, count，校验的长度
* ReturnValue    : 返回写入的长度
**************************************************************************************/
static uint16_t ym_crc16(uint8_t *buf, uint32_t count)
{
	return crc16(0, buf, count);
}

/**************************************************************************************
* FunctionName   : ymodem_receive_packet()
* Description    : 使用ymodem协议，接收一个包
* EntryParameter : data,接收缓冲区, length，表示接收到的数据长度, timeout表示超时等待
* ReturnValue    : 返回错误码,0表示成功, -1 1表示失败
**************************************************************************************/
static int32_t ymodem_receive_packet (uint8_t *data, int32_t *length, uint32_t timeout)
{
	uint16_t packet_size;

	*length = 0;
	/* 读取协议头部,1个字节 */
	if (ymodem_recv_byte(data, timeout) != 0) return -1;

	switch (*data) {
    case SOH:
    	packet_size = PACKET_SIZE;
    	break;
    case STX:
    	packet_size = PACKET_1K_SIZE;
    	break;
    case CA:
    	if ((ymodem_recv_byte(data, timeout) == 0) && (*data == CA)) {
    		*length = -1;
    		return 0;
    	}
    	return -1;
    case EOT:
    	return 0;
    case ABORT1:
    case ABORT2:
    	return 1;
    default:
    	return -1;
	}

	/* 读取后面的数据包 */
	ymodem_recv_buffer(data+1, packet_size + PACKET_OVERHEAD - 1, timeout);
	*length = packet_size;

	return 0;
}

/**************************************************************************************
* FunctionName   : ymodem_flash()
* Description    : 使用ymodem协议，接收升级一个镜像
* EntryParameter : filename,文件名, addr，表示升级地址, rom_size,表示要擦除的大小
* ReturnValue    : 返回升级文件的大小
**************************************************************************************/
uint32_t ymodem_flash(char *filename, uint32_t addr, uint32_t rom_size)
{
	uint16_t crc1, crc2;
	uint8_t *file_ptr,flag_eot;
	int32_t i, packet_length, session_done, file_done;
	int32_t packets_received, errors, session_begin, size = 0;
	static char file_size[FILE_SIZE_LENGTH];
	static uint8_t packet_data[PACKET_1K_SIZE + PACKET_OVERHEAD];

	flag_eot = 0;
	for (session_done = 0, errors = 0, session_begin = 0; ;) {
		for (packets_received = 0, file_done = 0; ;) {
			switch (ymodem_receive_packet(packet_data, &packet_length, NAK_TIMEOUT)) {
			case 0:
				errors = 0;
				switch (packet_length) {
				/* 收到的报文长度为-1，终止传输 */
				case - 1:
					ymodem_send_byte(ACK);
					return 0;
				/* 收到的报文长度为0， 结束传输 */
				case 0:
					/* 第一个EOT */
					if(flag_eot == 0) {
						ymodem_send_byte(NACK);
						flag_eot = 1;
					} else if (flag_eot == 1) {
						ymodem_send_byte(ACK);
						ymodem_send_byte('C');
						file_done = 1;
					}
					break;
				/* 正常数据包处理 */
				default:
					if ((packet_data[PACKET_SEQNO_INDEX] & 0xff) != (packets_received & 0xff)) {
						/* 本地记录的序列号和收到的序列号不一致 */
						ymodem_send_byte(NACK);
					} else {
						if (packets_received == 0) {
							/* 第一次接收，里面包含文件名称 */
							if (packet_data[PACKET_HEADER] != 0) {
								/* 获取文件名 */
								for (i = 0, file_ptr = packet_data + PACKET_HEADER; (*file_ptr != 0) && (i < FILE_NAME_LENGTH);) {
									filename[i++] = *file_ptr++;
								}
								filename[i++] = '\0';

								/* 获取文件大小 */
								for (i = 0, file_ptr ++; (*file_ptr != ' ') && (i < FILE_SIZE_LENGTH);) {
									file_size[i++] = *file_ptr++;
								}
								file_size[i++] = '\0';
								size = atoi(file_size);

								/* 测试收到的镜像是否会超过最大值 */
								if (size > (int32_t)(IMAGE_LENGTH + 1)) {
									/* 超过最大值，结束处理 */
									ymodem_send_byte(CA);
									ymodem_send_byte(CA);
									return -1;
								}
								/* 擦除flash对应区域 */
								ymodem_flash_erase(addr, rom_size);
								ymodem_send_byte(ACK);
								ymodem_send_byte(CRC16);
							} else {
								/* 文件名为空 */
								ymodem_send_byte(ACK);
								file_done = 1;
								session_done = 1;
								break;
							}
						} else {
							crc1 = ym_crc16((uint8_t *)(packet_data + PACKET_HEADER), packet_length);
							crc2 = ((uint16_t)(packet_data[PACKET_HEADER+packet_length]))*256 +
									packet_data[PACKET_HEADER+packet_length+1];
							if(crc1 != crc2) {
								ymodem_send_byte(NACK);
								/* 重传数据包，不参与计数 */
								packets_received --;
							} else {
								/* 写入flash, 假设写入flash始终成功，flash不会损坏 */
								addr += ymodem_flash_write(addr, (uint8_t *)(packet_data + PACKET_HEADER), packet_length);
								ymodem_send_byte(ACK);
							}
						}
						packets_received ++;
						session_begin = 1;
					}
				}
				break;
			case 1:
				ymodem_send_byte(CA);
				ymodem_send_byte(CA);
				return -3;
			default:
				if (session_begin > 0) {
					errors ++;
				}
				/* 传输过程中， 失败次数太多， 中止传输 */
				if (errors > MAX_ERRORS) {
					ymodem_send_byte(CA);
					ymodem_send_byte(CA);
					return 0;
				}
				ymodem_send_byte(CRC16);
				break;
			}
			if (file_done != 0) {
				break;
			}
		}
		if (session_done != 0) {
			break;
		}
	}
	if(nand_upgrade) image_burn_finish(ymodem_nand_page);

	return (int32_t)size;
}

/**************************************************************************************
* FunctionName   : ymodem_init()
* Description    : ymodem协议初始化
* EntryParameter : None
* ReturnValue    : 返回错误码
**************************************************************************************/
uint8_t ymodem_init(void)
{
	/* 初始化串口2，用于串口升级支持 */
	return lpuart_phy_init(FLASH_UART_ID, FLASH_UART_BAUD);
}

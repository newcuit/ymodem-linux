//测试文件
// 校验计算
uint16_t crc16(uint16_t crc, uint8_t *p, uint32_t count)
{
	uint32_t i;

	while(count--) {
		crc = crc ^ *p++ << 8;
		for (i=0; i<8; i++) {
			if (crc & 0x8000) crc = crc << 1 ^ 0x1021;
			else crc = crc << 1;
		}
	}
	return crc;
}

static int8_t image_valid(struct img_hdr *header)
{
	uint16_t crc = 0;

	/* 前4个字节crc16和refresh字段 不参加校验 */
	crc = crc16(crc, (uint8_t *)(IMAGE_HDR_BASE+4), header->len + IMAGE_HDR_LEN - 4);

	if(crc != header->crc16){
		return IMAGE_FAILED;
	}

	return IMAGE_SUCCESS;
}

int test(void)
{
	image_entry entry;
	int32_t entry_addr;
	struct img_hdr header;

	/* 初始化ymodem */
	ymodem_init();

	if(!ymodem) goto boot;

	ymodem_flash((char *)header, (uint32_t)IMAGE_HDR_BASE, IMAGE_LENGTH);
	image_valid(header);
	reset();

	/* 系统启动 , 加载，跳转到主镜像 */
boot:
	/* arm M系列规定 中断向量表的第二个向量为入口函数地址 */
	entry_addr = *((int32_t *)(IMAGE_ENTRY_BASE + 4));
	entry = (image_entry)(entry_addr);
	entry();

	/* 永远不会进入  */
	return 0;
}

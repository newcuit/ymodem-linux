#include <sys/stat.h>
#include <string.h>
#include <stdio.h>
#include <sys/types.h> 
#include <sys/wait.h>
#include <errno.h>
#include "ym_h.h"


enum uart_cfgs {
	BAUD_RATE,
	DATA_BITS,
	STOP_BITS,
	PARITY_BITS,
	UART_CFG_NUM
};

static int g_term_fd = 0;
struct upgraded {
	int up_force;
	int uart_cfg[UART_CFG_NUM];
	char *up_file;
	char * dev_file;
	int mcu_signal_level;
};

static struct upgraded g_up;


static uint32_t Send_Byte (uint8_t c)
{
	if (write(g_term_fd, &c, 1) < 0)  {
		fprintf(stderr, "write error : %s\n", strerror(errno));
	}
    return 0;
}

static  int32_t Receive_Byte (char *c, uint32_t timeout)
{
	int ret = 0;
    while (timeout-- > 0)
    {
		ret = read(g_term_fd, c, 1);
		if (ret > 0) {
			return 0;
		}

		usleep(1000);
    }
    return -1;
}

void Int2Str(uint8_t* str, int32_t intnum)
{
    uint32_t i, Div = 1000000000, j = 0, Status = 0;

    for (i = 0; i < 10; i++)
    {
        str[j++] = (intnum / Div) + 48;

        intnum = intnum % Div;
        Div /= 10;
        if ((str[j-1] == '0') & (Status == 0))
        {
            j = 0;
        }
        else
        {
            Status++;
        }
    }
}

void 
Ymodem_PrepareIntialPacket(
	uint8_t *data, 
	const char* fileName, 
	uint32_t *length 
)
{
    uint16_t i, j;
    uint8_t file_ptr[10];

    data[0] = SOH;
    data[1] = 0x00;
    data[2] = 0xff;

    for (i = 0; (fileName[i] != '\0') && (i < FILE_NAME_LENGTH); i++)
    {
        data[i + PACKET_HEADER] = fileName[i];
    }

    data[i + PACKET_HEADER] = 0x00;

    Int2Str (file_ptr, *length);
    for (j =0, i = i + PACKET_HEADER + 1; file_ptr[j] != '\0' ; )
    {
        data[i++] = file_ptr[j++];
    }

    for (j = i; j < PACKET_SIZE + PACKET_HEADER; j++)
    {
        data[j] = 0;
    }
}

void Ymodem_SendPacket(uint8_t *data, uint16_t length)
{
    uint16_t i;
    i = 0;
    while (i < length)
    {
        Send_Byte(data[i]);
        i++;
    }
}

uint16_t UpdateCRC16(uint16_t crcIn, uint8_t byte)
{
    uint32_t crc = crcIn;
    uint32_t in = byte|0x100;
    do
    {
        crc <<= 1;
        in <<= 1;
        if (in&0x100)
            ++crc;
        if (crc&0x10000)
            crc ^= 0x1021;
    }
    while (!(in&0x10000));
    return crc&0xffffu;
}

uint16_t Cal_CRC16(const uint8_t* data, uint32_t size)
{
    uint32_t crc = 0;
    const uint8_t* dataEnd = data+size;
    while (data<dataEnd)
        crc = UpdateCRC16(crc,*data++);

    crc = UpdateCRC16(crc,0);
    crc = UpdateCRC16(crc,0);
    return crc&0xffffu;
}

void Ymodem_PreparePacket(uint8_t *SourceBuf, uint8_t *data, uint8_t pktNo, uint32_t sizeBlk)
{
    uint16_t i, size, packetSize;
    uint8_t* file_ptr;

    packetSize = PACKET_SIZE;//sizeBlk >= PACKET_1K_SIZE ? PACKET_1K_SIZE : PACKET_SIZE;
    size = sizeBlk < packetSize ? sizeBlk :packetSize;
    if (packetSize == PACKET_1K_SIZE)
    {
        data[0] = STX;
    }
    else
    {
        data[0] = SOH;
    }
    data[1] = pktNo;
    data[2] = (~pktNo);
    file_ptr = SourceBuf;

    for (i = PACKET_HEADER; i < size + PACKET_HEADER; i++)
    {
        data[i] = file_ptr[i - PACKET_HEADER];//*file_ptr++;
    }
    if ( size  <= packetSize)
    {
        for (i = size + PACKET_HEADER; i < packetSize + PACKET_HEADER; i++)
        {
            data[i] = 0x1A;
        }
    }
}

uint8_t CalChecksum(const uint8_t* data, uint32_t size)
{
    uint32_t sum = 0;
    const uint8_t* dataEnd = data+size;
    while (data < dataEnd )
        sum += *data++;
    return sum&0xffu;
}

static int32_t
Ymodem_WaitReceiver(uint32_t retry)
{
	int cnt = 0;
	char ch = 0;
	while (1) {
		if (Receive_Byte(&ch, 1000) == -1) {
			cnt++;
			if (retry <= cnt) {
				fprintf(stderr, "wait for file start 'C' fail\n");
				return -1;
			}
			continue;				
		}
		//printf("ch = %c, %d\n", ch, ch);
		if ((ch & MASK) != 'C')
			continue;
		break;
	}

	return 0;
}

uint8_t Ymodem_Transmit (uint8_t *buf, const char* sendFileName, uint32_t sizeFile)
{
	int i = 0;
    uint8_t packet_data[PACKET_1K_SIZE + PACKET_OVERHEAD];
    char FileName[FILE_NAME_LENGTH];
    uint8_t *buf_ptr, tempCheckSum ;
    uint16_t tempCRC, blkNumber;
    char receivedC[2], CRC16_F = 0;
    uint32_t errors, ackReceived, size = 0, pktSize;

    errors = 0;
    ackReceived = 0;
    for (i = 0; i < (FILE_NAME_LENGTH - 1); i++)
    {
        FileName[i] = sendFileName[i];
    }
    CRC16_F = 1;

	if (Ymodem_WaitReceiver(10000) == -1)
		return -1;

    Ymodem_PrepareIntialPacket(&packet_data[0], FileName, &sizeFile);	

    do
    {
		fprintf(stderr, "send file\n");
        Ymodem_SendPacket(packet_data, PACKET_SIZE + PACKET_HEADER);
        if (CRC16_F)
        {
            tempCRC = Cal_CRC16(&packet_data[3], PACKET_SIZE);
            Send_Byte(tempCRC >> 8);
            Send_Byte(tempCRC & 0xFF);
        }
        else
        {
            tempCheckSum = CalChecksum (&packet_data[3], PACKET_SIZE);
            Send_Byte(tempCheckSum);
        }

        if (Receive_Byte(&receivedC[0], 1000) == 0)
        {
			fprintf(stderr, "recevd = %x, %d, %d, %d\n", receivedC[0], receivedC[0] & MASK, ACK, CRC16);
            if ((receivedC[0] & MASK) == ACK || (receivedC[0] & MASK) == CRC16)
            {
				fprintf(stderr, "dfsdf\n");
                ackReceived = 1;
            }
			else 
			{
				fprintf(stderr, "recv no ack error, recevd = %x, %d, %d, %d\n", receivedC[0], receivedC[0] & MASK, ACK, CRC16);
			}
        }
        else
        {
            errors++;
			fprintf(stderr, "errors = %d\n", errors);
        }

    } while (!ackReceived && (errors < 0x0A));

    if (errors >=  0x0A)
    {
        return errors;
    }
	if ((receivedC[0] & MASK) == ACK) {
		if (Ymodem_WaitReceiver(10) == -1)
			return -1;
	}
    buf_ptr = buf;
    size = sizeFile;
    blkNumber = 0x01;
	fprintf(stderr, "total number = %d\n", size / 128);
    while (size)
    {
		fprintf(stderr, "blkNumber = %d\n", blkNumber);
        Ymodem_PreparePacket(buf_ptr, &packet_data[0], blkNumber, size);
        ackReceived = 0;
        receivedC[0]= 0;
        errors = 0;
        do
        {
            /*if (size >= PACKET_1K_SIZE)
            {
                pktSize = PACKET_1K_SIZE;

            }
            else*/
            {
                pktSize = PACKET_SIZE;
            }
            Ymodem_SendPacket(packet_data, pktSize + PACKET_HEADER);
            if (CRC16_F)
            {
                tempCRC = Cal_CRC16(&packet_data[3], pktSize);
                Send_Byte(tempCRC >> 8);
                Send_Byte(tempCRC & 0xFF);
            }
            else
            {
                tempCheckSum = CalChecksum (&packet_data[3], pktSize);
                Send_Byte(tempCheckSum);
            }

			//fprintf(stderr, "00000\n");
			do {
				//usleep(g_ymodem_recv_timeout);
				if ((Receive_Byte(&receivedC[0], 1000) == 0))
				{
					fprintf(stderr, "recevd = %x, %d, %d, %d\n", receivedC[0], receivedC[0] & MASK, ACK, CRC16);
					if ((receivedC[0] & MASK) == ACK) {
						ackReceived = 1;
						if (size > pktSize)
						{
							buf_ptr += pktSize;
							size -= pktSize;
							//if (blkNumber == (sizeFile/1024))
							//{
							//    return 0xFF; //´íÎó
							//}
							//else
							{
								blkNumber++;
								
							}
						}
						else
						{
							buf_ptr += pktSize;
							size = 0;
						}
					} 	
					else 
					{
						Ymodem_SendPacket(packet_data, pktSize + PACKET_HEADER);
						Send_Byte(tempCRC >> 8);
						Send_Byte(tempCRC & 0xFF);
						errors++;
					}
				}
				else
				{
					Ymodem_SendPacket(packet_data, pktSize + PACKET_HEADER);
					Send_Byte(tempCRC >> 8);
					Send_Byte(tempCRC & 0xFF);
					errors++;
				}
			}while (!ackReceived && (errors < 0x0A));
        } while (0);// (!ackReceived && (errors < 0x0A));
        if (errors >=  0x0A)
        {
            return errors;
        }

    }
    ackReceived = 0;
    receivedC[0] = 0x00;
    errors = 0;
    do
    {
		fprintf(stderr, "send EOT\n");
        Send_Byte(EOT);
        if ((Receive_Byte(&receivedC[0], 10000) == 0)) // && receivedC[0] == ACK)
        {
		if ((receivedC[0] & MASK) == NAK) 
				Send_Byte(EOT);
			else if ((receivedC[0] & MASK) == ACK)
		        ackReceived = 1;
			else if ((receivedC[0] & MASK) == CRC16)
				return -1;	
			else
				errors++;
        }
        else
        {
            errors++;
        }
    } while (!ackReceived && (errors < 0x0A));

    if (errors >=  0x0A)
    {
        return errors;
    }
    ackReceived = 0;
    receivedC[0] = 0x00;
    errors = 0;

    packet_data[0] = SOH;
    packet_data[1] = 0;
    packet_data [2] = 0xFF;

	if (Ymodem_WaitReceiver(10) == -1)
		return -1;

    for (i = PACKET_HEADER; i < (PACKET_SIZE + PACKET_HEADER); i++)
    {
        packet_data [i] = 0x00;
    }

    do
    {
		fprintf(stderr, "dfsdfsf --- \n");
        Ymodem_SendPacket(packet_data, PACKET_SIZE + PACKET_HEADER);
        tempCRC = Cal_CRC16(&packet_data[3], PACKET_SIZE);
        Send_Byte(tempCRC >> 8);
        Send_Byte(tempCRC & 0xFF);

        if (Receive_Byte(&receivedC[0], 10) == 0)
        {
            if ((receivedC[0] & MASK) == ACK)
                ackReceived = 1;
			else 
				errors++;
        }
        else
        {
            errors++;
        }

    } while (!ackReceived && (errors < 0x03));
    if (errors >=  0x0A)
    {
        return errors;
    }

    /*do
    {
        Send_Byte(EOT);
        if ((Receive_Byte(&receivedC[0], 10000) == 0)  && receivedC[0] == ACK)
        {
            ackReceived = 1;
        }
        else
        {
            errors++;
        }
    } while (!ackReceived && (errors < 0x0A));

    if (errors >=  0x0A)
    {
        return errors;
    }*/
    return 0;
}

static void 
set_bit(
	int bit,
	struct termios *term
)
{
	switch (bit) {
		case 7:
			term->c_cflag |= CS7;
			break;
		case 8:
			term->c_cflag |= CS8;
			break;
		default:
			break;
	}
}

static void
set_ecc(
	int ecc,
	struct termios *term
)
{
	switch (ecc) {
		case 0:
			term->c_cflag &= ~PARENB;
			break;
		case 1:
			term->c_cflag |= PARENB;
			term->c_cflag |= PARODD;
			term->c_cflag |= (INPCK | ISTRIP);
			break;
		default:
			break;
	}
}

static void
set_speed(
	int speed,
	struct termios *term
)
{
	switch (speed) {
		case 2500:
			cfsetispeed(term, B2400);
			cfsetospeed(term, B2400);
			break;
		case 4800:
			cfsetispeed(term, B4800);
			cfsetospeed(term, B4800);
			break;
		case 9600:
			cfsetispeed(term, B9600);
			cfsetispeed(term, B9600);
			break;
		case 57600:
			cfsetispeed(term, B57600);
			cfsetospeed(term, B57600);
			break;
		case 115200:
			cfsetispeed(term, B115200);
			cfsetospeed(term, B115200);
			break;
		case 460800:
			cfsetispeed(term, B460800);
			cfsetospeed(term, B460800);
			break;
		default:
			cfsetispeed(term, B9600);
			cfsetospeed(term, B9600);
			break;
	}
}

static void
set_stop(
	int stop,
	struct termios *term
)
{
	switch (stop) {
		case 1:
			term->c_cflag &= ~CSTOPB;
			break;
		case 2:
			term->c_cflag |= CSTOPB;
			break;
		default:
			break;
	}
}

static int 
set_opt(
	int fd,
	int bit,
	int ecc,
	int speed,
	int stop,
	int block
)
{
	int ret = 0;
	struct termios newstr;
	struct termios oldstr;

	memset(&newstr, 0, sizeof(newstr));
	memset(&oldstr, 0, sizeof(oldstr));

	tcgetattr(fd, &oldstr);

	newstr.c_cflag |= (CLOCAL | CREAD);
	newstr.c_cflag &= ~CSIZE;
	newstr.c_lflag &=~ICANON;
	
	set_bit(bit, &newstr);
	set_ecc(ecc, &newstr);
	set_speed(speed, &newstr);
	set_stop(stop, &newstr);
	newstr.c_cc[VTIME] = 10;
	newstr.c_cc[VMIN] = 0;

	if (block)
		fcntl(fd, F_SETFL, 0);
	
	tcflush(fd, TCIFLUSH);

	ret = tcsetattr(fd, TCSAFLUSH, &newstr);
	if (ret != 0) {
		fprintf(stderr, "com set error: %s\n", strerror(errno));
		return 1;
	}
	fprintf(stderr, "com set sucess!\n");
	return 0;
}

static uint8_t *
ymodem_file_content_get(
	char *file_name,
	uint32_t *size
)
{
	uint8_t *buf = NULL;
	FILE *fp = NULL;
	struct stat sbuf;

	fp = fopen(file_name, "r");
	if (!fp) {
		fprintf(stderr, "fopen %s error: %s\n", file_name, strerror(errno));
		exit(-1);
	}
	stat(file_name, &sbuf);
	buf = (uint8_t*)calloc(1, sbuf.st_size);
	if (!buf) {
		fprintf(stderr, "calloc %ld bytes error: %s\n", sbuf.st_size, strerror(errno));
		fclose(fp);
		exit(-1);
	}
	(*size) = sbuf.st_size;
	fread(buf, sizeof(char), sbuf.st_size, fp);
	fclose(fp);
	return buf;
}

static int serial_open(char *device)
{
	int fd;
	char *tmp = "1242";
	struct termios t;
	printf("open device %s \n",device);

	fd = open(device, O_RDWR | O_NOCTTY | O_NDELAY);
	if (fd < 0){
		printf("Can't open device %s: %s", device, strerror(errno));
		return -1;
	}
//int set_opt(int fd, int nSpeed, int nBits, char nEvent, int nStop)
	set_opt(fd, g_up.uart_cfg[DATA_BITS], g_up.uart_cfg[STOP_BITS], 
				g_up.uart_cfg[BAUD_RATE], g_up.uart_cfg[PARITY_BITS], 0);

	if (tcgetattr(fd, &t) < 0) {
		printf("Can't tcgetattr for %s: %s", device, strerror(errno));
		close(fd);
		return -1;
	}

	cfmakeraw(&t);

	//tmp = iot_uci_get(NULL, "device", "info.oid");
	//if (!tmp) {
		//printf("no oid set\n");
		//return -1;
	//}
	if (!strncmp(tmp, "0101", 4)) {
		free(tmp);
		t.c_cflag |= (PARODD | PARENB);
		t.c_iflag |= INPCK;            
		t.c_cflag |= CS8;
		//t.c_iflag |= INPCK;
		cfsetispeed(&t, B38400);
		cfsetospeed(&t, B38400);
		if (tcsetattr(fd, TCSANOW, &t) < 0) {
			printf("Can't tcsetattr for %s: %s", device, strerror(errno));
			close(fd);
			return -1;
		}
	} else {
		//free(tmp);
		t.c_cflag &= ~PARENB;
		cfsetispeed(&t, B115200);
		cfsetospeed(&t, B115200);
		 if (tcsetattr(fd, TCSAFLUSH, &t)) {
			printf("Can't tcsetattr for %s: %s", device, strerror(errno));
			close(fd);
			return -1;
		 }
	}

	//t.c_cflag &= ~PARENB;

	tcflush(fd, TCIFLUSH);

	return fd;
}

int 
ymodem_send_main(
	char *file_path,
	char *serial_path
)
{
	uint8_t ret = 0;
	uint8_t retry_cnt = 0,
			retry_num = 5;
	uint8_t *buf = NULL;
	uint32_t size = 0;

/*	g_term_fd = open(serial_path, O_RDWR | O_NOCTTY);
	if (g_term_fd < 0) {
		fprintf(stderr, "open %s error : %s\n", serial_path, strerror(errno));
		exit(-1);
	}
	
	set_opt(g_term_fd, 8, 0, 9600, 1, 1);*/

	g_term_fd = serial_open(serial_path);

	buf = ymodem_file_content_get(file_path, &size);

	while (1) {
		ret = Ymodem_Transmit(buf, file_path, size);
		if (ret != 0) {
			retry_cnt++;
			if (retry_cnt > retry_num) {
				printf("Ymodem transmit error\n");
				return -1;
			}
			printf("Retry ymodem transmit %dth\n", retry_cnt);
			continue;
		}
		else
			break;
	}

	free(buf);
	close(g_term_fd);

	return 0;
}


static void parse_uart_param(char *param)
{
	char buf[1024];
	int len;
	char *str;
	int idx = 0;

	
	len = strlen(param);
	len = len > sizeof(buf) ? sizeof(buf) : len;
	
	strncpy(buf, param, len);

	str = strtok(buf, "-");
	while (str)
	{
		if (idx < sizeof(g_up.uart_cfg) / sizeof(g_up.uart_cfg[0]))
			g_up.uart_cfg[idx++] = atoi(str);
		else
			printf("uart param too many\n");
		
		str = strtok(NULL, "-");		
	}
}


static void Usage(char *name)
{
	printf("\nUsage:\n%s [-f] <bin file> [dev file] [uart cfg...] [signal level]\n\n", name);
	printf("  -f	force upgraded mcu\n");
	printf("  -b	bin file path\n");
	printf("  -d	dev file path\n");
	printf("  -u	uart config, brate-data bits-stop bits-parity bits, example: -u 115200-8-1-0\n");
	printf("  -v	mcu notify upgrade signal level\n\n");
}


int parse_args(int argc, char *argv[])
{
	int ch;
	int len;

	
	while ((ch = getopt(argc, argv, "fb:d:u:v:")) != -1)
	{
		//printf("optind: %d\n", optind);
       	switch (ch) 
    	{
			case 'f':
				g_up.up_force = 1;
				break;

			case 'b':
				len = strlen(optarg);
				g_up.up_file = malloc(len);
				strcpy(g_up.up_file, optarg);
				break;
			
			case 'd':
				len = strlen(optarg);
				g_up.dev_file = malloc(len);
				strcpy(g_up.dev_file, optarg);
				break;
				
			case 'u':
				parse_uart_param(optarg);
				break;
				
			case 'v':
				g_up.mcu_signal_level = !!atoi(optarg);
				break;
				
			default:
				Usage(argv[0]);
				return -1;
				break;				
       	}
	}

	return 0;
}


void upgraded_send_cmd(void)
{
	int ret;
	FILE *fp;
	
	fp = popen("ucmd send 0C080A", "r");
	if (!fp)
	{
		perror("system exec fail");
		return;
	}

	ret = pclose(fp);
	if (ret == -1)
		perror("pclose fail");
	else
		printf("exit state=%d, retval=%d\n", ret, WEXITSTATUS(ret));
}


static void prepare_init(void)
{
	g_up.dev_file 				= "/dev/ttyHS0";
	g_up.up_file 				= "mcu.bin";	
	g_up.uart_cfg[BAUD_RATE] 	= 115200;
	g_up.uart_cfg[DATA_BITS] 	= 8;
	g_up.uart_cfg[STOP_BITS] 	= 1;
	g_up.uart_cfg[PARITY_BITS] 	= 0;	
	g_up.up_force				= 0;
	g_up.mcu_signal_level		= 0;
}


int main(int argc, char *argv[])
{
	prepare_init();
	
	if (parse_args(argc, argv) < 0)
		return -1;

	ymodem_send_main(g_up.up_file, g_up.dev_file);
	
	return 0;
}


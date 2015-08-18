#include <common.h>
#include <command.h>
#include <s3c2416.h>
#include "cmd_hsmmc.h"

#define DEBUG_HSMMC
#ifdef  DEBUG_HSMMC
#define Debug(x...)		printf(x)
#else
#define Debug(x...) 
#endif

static unsigned char CardType; // ������
static unsigned int RCA; // ����Ե�ַ

static unsigned char Hsmmc_Buffer[16*1024] 
__attribute__((__aligned__(4), section(".no_cache")));

static void Delay_us(unsigned int nCount) 
{
	//��ʱ1us,����ʱnCount us
	__asm__ __volatile__ (
			"000:\n"	
			"ldr  r1, =100\n"  // Arm clockΪ400M		
			"111:\n"
			"subs r1, r1, #1\n"  // һ��Arm clock
			"bne  111b\n"      // ��ת������ˮ�ߣ�3��Arm clock
			"subs %0, %1, #1\n"	 // ������ȷ��nCount��Ϊ0
			"bne  000b\n"
			: "=r"(nCount) // nCount�Ĵ�����ֵ���Լ��ı�

			: "0"(nCount) // ʹ�������������ͬ�ļĴ���

			: "r1" // ��ʱʹ����r1�Ĵ���
			);
}

static void Hsmmc_ClockOn(unsigned char On)
{
	if (On) {
		rHM_CLKCON |= (1<<2); // sdʱ��ʹ��
		while (!(rHM_CLKCON & (1<<3))) {
			// �ȴ�SD���ʱ���ȶ�
		}
	} else {
		rHM_CLKCON &= ~(1<<2); // sdʱ�ӽ�ֹ
	}
}

static void Hsmmc_SetClock(unsigned int Div)
{
	Hsmmc_ClockOn(0); // �ر�ʱ��	
	// ѡ��SCLK_HSMMC:EPLLout
	rCLKSRC &= ~(1<<17); // HSMMC1 EPLL(96M)
	rHM_CONTROL2 = 0xc0000120; // SCLK_HSMMC
	rHM_CONTROL3 = (0<<31) | (0<<23) | (0<<15) | (0<<7);
	// SDCLKƵ��ֵ��ʹ���ڲ�ʱ��
	rHM_CLKCON &= ~(0xff<<8);
	rHM_CLKCON |= (Div<<8) | (1<<0);
	while (!(rHM_CLKCON & (1<<1))) {
		// �ȴ��ڲ�ʱ�����ȶ�
	}
	Hsmmc_ClockOn(1); // ȫ��ʱ��

}

static int Hsmmc_WaitForCommandDone()
{
	unsigned int i;	
	int ErrorState;
	// �ȴ���������
	for (i=0; i<20000000; i++) {
		if (rHM_NORINTSTS & (1<<15)) { // ���ִ���
			break;
		}
		if (rHM_NORINTSTS & (1<<0)) {
			do {
				rHM_NORINTSTS = (1<<0); // ����������λ
			} while (rHM_NORINTSTS & (1<<0));			
			return 0; // ����ͳɹ�
		}
	}
	ErrorState = rHM_ERRINTSTS & 0x1ff; // ����ͨ�Ŵ���,CRC�������,��ʱ��
	rHM_NORINTSTS = rHM_NORINTSTS; // ����жϱ�־
	rHM_ERRINTSTS = rHM_ERRINTSTS; // ��������жϱ�־	
	do {
		rHM_NORINTSTS = (1<<0); // ����������λ
	} while (rHM_NORINTSTS & (1<<0));

	Debug("Command error, rHM_ERRINTSTS = 0x%x ", ErrorState);	
	return ErrorState; // ����ͳ���	
}

static int Hsmmc_WaitForTransferDone()
{
	int ErrorState;
	unsigned int i;
	// �ȴ����ݴ������
	for (i=0; i<20000000; i++) {
		if (rHM_NORINTSTS & (1<<15)) { // ���ִ���
			break;
		}											
		if (rHM_NORINTSTS & (1<<1)) { // ���ݴ�����								
			do {
				rHM_NORINTSTS |= (1<<1); // ����������λ
			} while (rHM_NORINTSTS & (1<<1));	
			rHM_NORINTSTS = (1<<3); // ���DMA�жϱ�־								
			return 0;
		}
		Delay_us(1);
	}

	ErrorState = rHM_ERRINTSTS & 0x1ff; // ����ͨ�Ŵ���,CRC�������,��ʱ��
	rHM_NORINTSTS = rHM_NORINTSTS; // ����жϱ�־
	rHM_ERRINTSTS = rHM_ERRINTSTS; // ��������жϱ�־
	Debug("Transfer error, rHM_ERRINTSTS = 0x%04x\n\r", ErrorState);	
	do {
		rHM_NORINTSTS = (1<<1); // ���������������λ
	} while (rHM_NORINTSTS & (1<<1));

	return ErrorState; // ���ݴ������		
}

static int Hsmmc_IssueCommand(unsigned char Cmd, unsigned int Arg, unsigned char Data, unsigned char Response)
{
	unsigned int i;
	unsigned int Value;
	unsigned int ErrorState;
	// ���CMD���Ƿ�׼���÷�������
	for (i=0; i<1000000; i++) {
		if (!(rHM_PRNSTS & (1<<0))) {
			break;
		}
	}
	if (i == 1000000) {
		Debug("CMD line time out, rHM_PRNSTS: %04x\n\r", rHM_PRNSTS);
		return -1; // ���ʱ
	}
	// ���DAT���Ƿ�׼����
	if (Response == Response_R1b) { // R1b�ظ�ͨ��DAT0����æ�ź�
		for (i=0; i<1000000; i++) {
			if (!(rHM_PRNSTS & (1<<1))) {
				break;
			}		
		}
		if (i == 1000000) {
			Debug("Data line time out, rHM_PRNSTS: %04x\n\r", rHM_PRNSTS);			
			return -2;
		}		
	}

	rHM_ARGUMENT = Arg; // д���������
	Value = (Cmd << 8); // command index
	// CMD12����ֹ����
	if (Cmd == 0x12) {
		Value |= (0x3 << 6); // command type
	}
	if (Data) {
		Value |= (1 << 5); // ��ʹ��DAT����Ϊ�����
	}	

	switch (Response) {
		case Response_NONE:
			Value |= (0<<4) | (0<<3) | 0x0; // û�лظ�,��������CRC
			break;
		case Response_R1:
		case Response_R5:
		case Response_R6:
		case Response_R7:		
			Value |= (1<<4) | (1<<3) | 0x2; // ���ظ��е�����,CRC
			break;
		case Response_R2:
			Value |= (0<<4) | (1<<3) | 0x1; // �ظ�����Ϊ136λ,����CRC
			break;
		case Response_R3:
		case Response_R4:
			Value |= (0<<4) | (0<<3) | 0x2; // �ظ�����48λ,���������CRC
			break;
		case Response_R1b:
			Value |= (1<<4) | (1<<3) | 0x3; // �ظ���æ�ź�,��ռ��Data[0]��
			break;
		default:
			break;	
	}
	rHM_CMDREG = Value;

	ErrorState = Hsmmc_WaitForCommandDone();
	if (ErrorState) {
		Debug("Command = %d\r\n", Cmd);
	}	
	return ErrorState; // ����ͳ���
}

// 512λ��sd����չ״̬λ
int Hsmmc_GetSdState(unsigned char *pState)
{
	int ErrorState;
	unsigned int i;
	if (CardType == SD_HC || CardType == SD_V2 || CardType == SD_V1) {
		if (Hsmmc_GetCardState() != 4) { // ������transfer status
			return -1; // ��״̬����
		}		
		Hsmmc_IssueCommand(CMD55, RCA<<16, 0, Response_R1);

		rHM_SYSAD = (unsigned int)Hsmmc_Buffer; // �����ַ	
		rHM_BLKSIZE = (7<<12) | (64<<0); // ���DMA�����С,blockΪ512λ64�ֽ�			
		rHM_BLKCNT = 1; // д����ζ�1 block��sd״̬����
		rHM_ARGUMENT = 0; // д���������	

		// DMA����ʹ��,������		
		rHM_TRNMOD = (0<<5) | (1<<4) | (0<<2) | (1<<1) | (1<<0);	
		// ��������Ĵ���,��״̬����CMD13,R1�ظ�
		rHM_CMDREG = (CMD13<<8)|(1<<5)|(1<<4)|(1<<3)|0x2;
		ErrorState = Hsmmc_WaitForCommandDone();
		if (ErrorState) {
			Debug("CMD13 error\r\n");
			return ErrorState;
		}
		ErrorState = Hsmmc_WaitForTransferDone();
		if (ErrorState) {
			Debug("Get sd status error\r\n");
			return ErrorState;
		}
		for (i=0; i<64; i++) {
			*pState++ = Hsmmc_Buffer[i];
		}
		return 0;
	}
	return -1; // ��sd��
}

int Hsmmc_Get_CSD(unsigned char *pCSD)
{
	unsigned int i;
	unsigned int Response[4];
	int State = 1;

	if (CardType != SD_HC && CardType != SD_V1 && CardType != SD_V2) {
		return State; // δʶ��Ŀ�
	}
	// ȡ����ѡ��,�κο������ظ�,��ѡ��Ŀ�ͨ��RCA=0ȡ��ѡ��,
	// ���ص�stand-by״̬
	Hsmmc_IssueCommand(CMD7, 0, 0, Response_NONE);
	for (i=0; i<1000; i++) {
		if (Hsmmc_GetCardState() == 3) { // CMD9��������standy-by status
			Debug("Get CSD: Enter to the Stand-by State\n\r");					
			break; // ״̬��ȷ
		}
		Delay_us(100);
	}
	if (i == 1000) {
		return State; // ״̬����
	}	
	// �����ѱ�ǿ����Ϳ��ض�����(CSD),��ÿ���Ϣ
	if (!Hsmmc_IssueCommand(CMD9, RCA<<16, 0, Response_R2)) {
		pCSD++; // ·����һ�ֽ�,CSD��[127:8]λ��λ�Ĵ����е�[119:0]
		Response[0] = rHM_RSPREG0;
		Response[1] = rHM_RSPREG1;
		Response[2] = rHM_RSPREG2;
		Response[3] = rHM_RSPREG3;	
		Debug("CSD: ");
		for (i=0; i<15; i++) { // �����ظ��Ĵ����е�[119:0]��pCSD��
			*pCSD++ = ((unsigned char *)Response)[i];
			Debug("%02x", *(pCSD-1));
		}
		State = 0; // CSD��ȡ�ɹ�
	}

	Hsmmc_IssueCommand(CMD7, RCA<<16, 0, Response_R1); // ѡ��,���ص�transfer״̬
	return State;
}

// R1�ظ��а�����32λ��card state,��ʶ���,������һ״̬ͨ��CMD13��ÿ�״̬
int Hsmmc_GetCardState(void)
{
	if (Hsmmc_IssueCommand(CMD13, RCA<<16, 0, Response_R1)) {
		return -1; // ������
	} else {
		return ((rHM_RSPREG0>>9) & 0xf); // ����R1�ظ��е�[12:9]��״̬
	}
}

static int Hsmmc_SetBusWidth(unsigned char Width)
{
	int State;
	if ((Width != 1) || (Width != 4)) {
		return -1;
	}
	State = -1; // ���ó�ʼΪδ�ɹ�
	rHM_NORINTSTSEN &= ~(1<<8); // �رտ��ж�
	Hsmmc_IssueCommand(CMD55, RCA<<16, 0, Response_R1);
	if (Width == 1) {
		if (!Hsmmc_IssueCommand(CMD6, 0, 0, Response_R1)) { // 1λ��
			rHM_HOSTCTL &= ~(1<<1);
			State = 0; // ����ɹ�
		}
	} else {
		if (!Hsmmc_IssueCommand(CMD6, 2, 0, Response_R1)) { // 4λ��
			rHM_HOSTCTL |= (1<<1);
			State = 0; // ����ɹ�
		}
	}
	rHM_NORINTSTSEN |= (1<<8); // �򿪿��ж�	
	return State; // ����0Ϊ�ɹ�
}

int Hsmmc_EraseBlock(unsigned int StartBlock, unsigned int EndBlock)
{
	unsigned int i;

	if (CardType == SD_V1 || CardType == SD_V2) {
		StartBlock <<= 9; // ��׼��Ϊ�ֽڵ�ַ
		EndBlock <<= 9;
	} else if (CardType != SD_HC) {
		return -1; // δʶ��Ŀ�
	}	
	Hsmmc_IssueCommand(CMD32, StartBlock, 0, Response_R1);
	Hsmmc_IssueCommand(CMD33, EndBlock, 0, Response_R1);
	if (!Hsmmc_IssueCommand(CMD38, 0, 0, Response_R1b)) {
		for (i=0; i<10000; i++) {
			if (Hsmmc_GetCardState() == 4) { // ������ɺ󷵻ص�transfer״̬
				Debug("erasing complete!\n\r");
				return 0; // �����ɹ�				
			}
			Delay_us(1000);			
		}		
	}		

	Debug("Erase block failed\n\r");
	return 1; // ����ʧ��
}

int Hsmmc_ReadBlock(unsigned char *pBuffer, unsigned int BlockAddr, unsigned int BlockNumber)
{
	unsigned int Address = 0;
	unsigned int ReadBlock;
	unsigned int i;
	int ErrorState;

	if (pBuffer == 0 || BlockNumber == 0) {
		return -1;
	}

	// �����ж�ʹ��,������Ӧ���ж��ź�
	rHM_NORINTSIGEN &= ~0xffff; // ��������ж�ʹ��
	rHM_NORINTSIGEN |= (1<<1); // ��������ж�ʹ��

	while (BlockNumber > 0) {
		for (i=0; i<1000; i++) {
			if (Hsmmc_GetCardState() == 4) { // ��д��������transfer status
				break; // ״̬��ȷ
			}
			Delay_us(100);
		}
		if (i == 1000) {
			return -2; // ״̬����
		}		

		if (BlockNumber <= sizeof(Hsmmc_Buffer)/512) {
			ReadBlock = BlockNumber; // ��ȡ�Ŀ���С�ڻ���32 Block(16k)
			BlockNumber = 0; // ʣ���ȡ����Ϊ0
		} else {
			ReadBlock = sizeof(Hsmmc_Buffer)/512; // ��ȡ�Ŀ�������32 Block,�ֶ�ζ�
			BlockNumber -= ReadBlock;
		}
		// ����sd������������׼,��˳��д��������������Ӧ�ļĴ���
		rHM_SYSAD = (unsigned int)Hsmmc_Buffer; // �����ַ,�ڴ�����Ϊ�ر�cache,��DMA����			
		rHM_BLKSIZE = (7<<12) | (512<<0); // ���DMA�����С,blockΪ512�ֽ�			
		rHM_BLKCNT = ReadBlock; // д����ζ�block��Ŀ


		if (CardType == SD_HC) {
			Address = BlockAddr; // SDHC��д���ַΪblock��ַ
		} else if (CardType == SD_V1 || CardType == SD_V2) {
			Address = BlockAddr << 9; // ��׼��д���ַΪ�ֽڵ�ַ
		}	
		BlockAddr += ReadBlock; // ��һ�ζ���ĵ�ַ
		rHM_ARGUMENT = Address; // д���������		

		if (ReadBlock == 1) {
			// ���ô���ģʽ,DMA����ʹ��,������
			rHM_TRNMOD = (0<<5) | (1<<4) | (0<<2) | (1<<1) | (1<<0);
			// ��������Ĵ���,�����CMD17,R1�ظ�
			rHM_CMDREG = (CMD17<<8)|(1<<5)|(1<<4)|(1<<3)|0x2;
		} else {
			// ���ô���ģʽ,DMA����ʹ��,�����			
			rHM_TRNMOD = (1<<5) | (1<<4) | (1<<2) | (1<<1) | (1<<0);
			// ��������Ĵ���,����CMD18,R1�ظ�	
			rHM_CMDREG = (CMD18<<8)|(1<<5)|(1<<4)|(1<<3)|0x2;			
		}	
		ErrorState = Hsmmc_WaitForCommandDone();
		if (ErrorState) {
			Debug("Read Command error\r\n");
			return ErrorState;
		}		
		ErrorState = Hsmmc_WaitForTransferDone();
		if (ErrorState) {
			Debug("Read block error\r\n");
			return ErrorState;
		}
		// ���ݴ���ɹ�,����DMA��������ݵ�ָ���ڴ�
		for (i=0; i<ReadBlock*512; i++) {
			*pBuffer++ = Hsmmc_Buffer[i];
		}		
	}
	return 0; // ���п����
}

int Hsmmc_WriteBlock(unsigned char *pBuffer, unsigned int BlockAddr, unsigned int BlockNumber)
{
	unsigned int Address = 0;
	unsigned int WriteBlock;
	unsigned int i;
	int ErrorState;

	if (pBuffer == 0 || BlockNumber == 0) {
		return -1; // ��������
	}

	rHM_NORINTSIGEN &= ~0xffff; // ��������ж�ʹ��
	// ���ݴ�������ж�ʹ��
	rHM_NORINTSIGEN |= (1<<0);

	while (BlockNumber > 0) {
		for (i=0; i<1000; i++) {
			if (Hsmmc_GetCardState() == 4) { // ��д��������transfer status
				break; // ״̬��ȷ
			}
			Delay_us(100);
		}
		if (i == 1000) {
			return -2; // ״̬�����Programming��ʱ
		}

		if (BlockNumber <= sizeof(Hsmmc_Buffer)/512) {
			WriteBlock = BlockNumber;// д��Ŀ���С�ڻ���32 Block(16k)
			BlockNumber = 0; // ʣ��д�����Ϊ0
		} else {
			WriteBlock = sizeof(Hsmmc_Buffer)/512; // д��Ŀ�������32 Block,�ֶ��д
			BlockNumber -= WriteBlock;
		}
		if (WriteBlock > 1) { // ���д,����ACMD23������Ԥ��������
			Hsmmc_IssueCommand(CMD55, RCA<<16, 0, Response_R1);
			Hsmmc_IssueCommand(CMD23, WriteBlock, 0, Response_R1);
		}

		for (i=0; i<WriteBlock*512; i++) {
			Hsmmc_Buffer[i] = *pBuffer++; // ��д���ݴ�ָ���ڴ���������������
		}
		// ����sd������������׼,��˳��д��������������Ӧ�ļĴ���		
		rHM_SYSAD = (unsigned int)Hsmmc_Buffer; // �����ַ,�ڴ�����Ϊ�ر�cache,��DMA����		
		rHM_BLKSIZE = (7<<12) | (512<<0); // ���DMA�����С,blockΪ512�ֽ�		
		rHM_BLKCNT = WriteBlock; // д��block��Ŀ	

		if (CardType == SD_HC) {
			Address = BlockAddr; // SDHC��д���ַΪblock��ַ
		} else if (CardType == SD_V1 || CardType == SD_V2) {
			Address = BlockAddr << 9; // ��׼��д���ַΪ�ֽڵ�ַ
		}
		BlockAddr += WriteBlock; // ��һ��д��ַ
		rHM_ARGUMENT = Address; // д���������			

		if (WriteBlock == 1) {
			// ���ô���ģʽ,DMA����д����
			rHM_TRNMOD = (0<<5) | (0<<4) | (0<<2) | (1<<1) | (1<<0);
			// ��������Ĵ���,����дCMD24,R1�ظ�
			rHM_CMDREG = (CMD24<<8)|(1<<5)|(1<<4)|(1<<3)|0x2;			
		} else {
			// ���ô���ģʽ,DMA����д���
			rHM_TRNMOD = (1<<5) | (0<<4) | (1<<2) | (1<<1) | (1<<0);
			// ��������Ĵ���,���дCMD25,R1�ظ�
			rHM_CMDREG = (CMD25<<8)|(1<<5)|(1<<4)|(1<<3)|0x2;					
		}
		ErrorState = Hsmmc_WaitForCommandDone();
		if (ErrorState) {
			Debug("Write Command error\r\n");
			return ErrorState;
		}		
		ErrorState = Hsmmc_WaitForTransferDone();
		if (ErrorState) {
			Debug("Write block error\r\n");
			return ErrorState;
		}
	}
	return 0; // д����������
}

int do_hsmmc_init()
{
	Debug("do_hsmc_init.\n");
	unsigned int i;
	unsigned int OCR;

	// ����HSMMC1�Ľӿ���������,�����ֲ�260ҳ
	Debug("config GPLCONF register.\n");
	rGPLCON &= ~((0xffff<<0) | (0xf<<16));
	rGPLCON |= (0xaaaa<<0) | (0xa<<16);
	rGPLUDP &= ~((0xffff<<0) | (0xf<<16)); // ��������ֹ

	//����HSMMC0�Ľӿ��������ã������ֲ�251ҳ
	//Debug("config GPECONF register.\n");
	//rGPECON &= ~(0xFFF<<10);
	///rGPECON |=  0xAAA<<10;	//����Ϊ��������
	//rGPEUP &= ~(0xFFF<10);	//��ֹ������

	Debug("reset HSMMC.\n");
	//rHM_SWRST = 0x7; // ��λHSMMC
	Debug("Hsmmc_SetClock(0x80).\n");
	Hsmmc_SetClock(0x80); // SDCLK=96M/256=375K
	Debug("set timeout.\n");
	rHM_TIMEOUTCON = (0xe << 0); // ���ʱʱ��
	Debug("set normal speed.\n");
	rHM_HOSTCTL &= ~(1<<2); // �����ٶ�ģʽ
	Debug("clear irq.\n");
	rHM_NORINTSTS = rHM_NORINTSTS; // ����ж�״̬��־
	rHM_ERRINTSTS = rHM_ERRINTSTS; // ��������ж�״̬��־
	Debug("enable irq.\n");
	rHM_NORINTSTSEN = 0x7fff; // [14:0]�ж�ʹ��
	rHM_ERRINTSTSEN = 0x3ff; // [9:0]�����ж�ʹ��
	Debug("Hsmmc_IssueCommand(cmd0).\n");
	Hsmmc_IssueCommand(CMD0, 0, 0, Response_NONE); // ��λ���п�������״̬

	CardType = UnusableCard; // �����ͳ�ʼ��������
	Debug("Hsmmc_IssueCommand(cmd8).\n");
	if (Hsmmc_IssueCommand(CMD8, 0x1aa, 0, Response_R7)) { // û�лظ�,MMC/sd v1.x/not card
		for (i=0; i<1000; i++) {
			Hsmmc_IssueCommand(CMD55, 0, 0, Response_R1);
			if (!Hsmmc_IssueCommand(CMD41, 0, 0, Response_R3)) { // CMD41�лظ�˵��Ϊsd��
				OCR = rHM_RSPREG0; // ��ûظ���OCR(���������Ĵ���)ֵ
				if (OCR & 0x80000000) { // ���ϵ��Ƿ�����ϵ�����,�Ƿ�busy
					CardType = SD_V1; // ��ȷʶ���sd v1.x��
					Debug("SD card version 1.x is detected\n\r");
					break;
				}
			} else {
				// MMC��ʶ��
			}
			Delay_us(100);				
		}
	} else { // sd v2.0
		Debug("ok2\n");
		if (((rHM_RSPREG0&0xff) == 0xaa) && (((rHM_RSPREG0>>8)&0xf) == 0x1)) { // �жϿ��Ƿ�֧��2.7~3.3v��ѹ
			OCR = 0;
			for (i=0; i<1000; i++) {
				Hsmmc_IssueCommand(CMD55, 0, 0, Response_R1);
				Hsmmc_IssueCommand(CMD41, OCR, 0, Response_R3); // reday̬
				OCR = rHM_RSPREG0;
				if (OCR & 0x80000000) { // ���ϵ��Ƿ�����ϵ�����,�Ƿ�busy
					if (OCR & (1<<30)) { // �жϿ�Ϊ��׼�����Ǹ�������
						CardType = SD_HC; // ��������
						Debug("SDHC card is detected\n\r");
					} else {
						CardType = SD_V2; // ��׼��
						Debug("SD version 2.0 standard card is detected\n\r");
					}
					break;
				}
				Delay_us(100);
			}
		}
	}
	if (CardType == SD_HC || CardType == SD_V1 || CardType == SD_V2) {
		Hsmmc_IssueCommand(CMD2, 0, 0, Response_R2); // ���󿨷���CID(��ID�Ĵ���)��,����ident
		Hsmmc_IssueCommand(CMD3, 0, 0, Response_R6); // ���󿨷����µ�RCA(����Ե�ַ),Stand-by״̬
		RCA = (rHM_RSPREG0 >> 16) & 0xffff; // �ӿ��ظ��еõ�����Ե�ַ

		Hsmmc_IssueCommand(CMD7, RCA<<16, 0, Response_R1); // ѡ���ѱ�ǵĿ�,transfer״̬
		Debug("Enter to the transfer state\n\r");		
		Hsmmc_SetClock(0x2); // ����SDCLK = 96M/4 = 24M

		if (!Hsmmc_SetBusWidth(4)) {
			Debug("Set bus width error\n\r");
			return 1; // λ�����ó���
		}
		if (Hsmmc_GetCardState() == 4) { // ��ʱ��Ӧ��transfer̬
			if (!Hsmmc_IssueCommand(CMD16, 512, 0, Response_R1)) { // ���ÿ鳤��Ϊ512�ֽ�
				rHM_NORINTSTS = 0xffff; // ����жϱ�־
				Debug("Card Initialization succeed\n\r");
				return 0; // ��ʼ���ɹ�
			}
		}
	}
	Debug("Card Initialization failed\n\r");
	return 1; // �������쳣
}

U_BOOT_CMD(
	hsmmcinit,	1,	0,	do_hsmmc_init,
	"hsmmcinit - init hs_mmc card\n",
	NULL
);

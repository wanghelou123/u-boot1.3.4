
#include <common.h>
#include <command.h>
#include <s3c2416.h>
#include <s3c24x0.h>
int do_update_image_size(cmd_tbl_t *cmdtp, int flag, int argc, char *argv[])
{
	int bootloader_size=0;
	int kernel_size=0;
	int rootfs_size=0;

	char linebuf[60];
	char size[3][20]={0};
	char buf[10];
	memcpy(linebuf, (void *)0xc0000000, sizeof(linebuf)); 
	//linebuf[60-1]='\0';
	//printf("linebuf:%s\n", linebuf);

	int i=0,j=0, cnt=0;
	for(i=0; i<3; i++) {
		cnt = 0;
		while(linebuf[j]!=0x0a && cnt < 16) {
			size[i][cnt++]=linebuf[j++];
		}
		j++;
	}

	bootloader_size = simple_strtoul(size[0], NULL, 10);
	kernel_size = simple_strtoul(size[1], NULL, 10);
	rootfs_size = simple_strtoul(size[2], NULL, 10);

	if(bootloader_size < 200*1024  || bootloader_size >400*1024 ) {
		printf("Not found images size!");

		return -1;
	}

	printf("bootloader_size: %d bytes,%dKB\n",bootloader_size, bootloader_size/1024+(bootloader_size%1024?1:0));
	printf("kernel_size: %d bytes,%dMB\n", kernel_size, kernel_size/1024/1024+(kernel_size%(1024*1024)?1:0));
	printf("rootfs_size: %d bytes,%dMB\n", rootfs_size, rootfs_size/1024/1024+(rootfs_size%(1024*1024)?1:0));
#if 0
	j=0;
	for(i=0; i<3; i++) {
		printf("size[%d]:%s\n", i, size[i]);
		printf("size[%d][%d]:", i,j);
			for(j=0; j<16; j++) {
				printf("%.2x ", size[i][j]);
			}
		printf("\n");
	}

#endif

	sprintf(buf, "%x", bootloader_size);
	setenv("bootloader_size", buf);
	sprintf(buf, "%x", kernel_size);
	setenv("kernel_size", buf);
	sprintf(buf, "%x", rootfs_size);
	setenv("rootfs_size", buf);

	return 0;
}

U_BOOT_CMD(
		updateimagesize,	//命令名，非字符串，但在U_BOOT_CMD中用“#”符号转化为字符串
		3,		//命令的最大参数个数
		0,		//是否自动重复（按Enter键是否会重复执行）
		do_update_image_size, //该命令对应的响应函数
		"updateimagesize - update the u-boot.bin kernel rootfs size environment.\n",//简短的使用说明（字符串）
		"NULL"
		);

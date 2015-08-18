#include <common.h>
#include <command.h>
#include <s3c2416.h>
#include <s3c24x0.h>

#include <fat.h>
int do_update_image_name(cmd_tbl_t *cmdtp, int flag, int argc, char *argv[])
{
	long size;
	char linebuf[1024];
	char kernel[256];
	char rootfs[256];
	int i;
	block_dev_desc_t *dev_desc=NULL;
	dev_desc = get_dev("mmc", 0);
	if (dev_desc==NULL) {
		puts ("\n** Invalid boot device **\n");
		return 1;
	}
	if (fat_register_device(dev_desc, 1)!=0) {
		printf ("\n** Unable to use mmc %d:%d for fatload **\n",0,0);
		return 1;
	}
	if (dev_desc==NULL) {
		puts ("\n** Invalid boot device **\n");
		return 1;
	}
	size = file_fat_read ("/images/uEnv.txt", (unsigned char *) linebuf, 0);

	if(size==-1) {
		printf("\n** Unable to read /images/uEnv.txt\n");
		return 1;
	}
	printf("file size = %ld bytes\n", size);


	char *p = strchr(linebuf, '=') + 1; 		
	for(i=0; i<size && *p != '\r' && *p != '\n'; i++) {
		kernel[i] = *p++;
	}
	kernel[i]='\0';

	p = strrchr(linebuf, '=') + 1; 		
	int len = size - (p-linebuf);
	for(i=0; i<len && *p != '\r' && *p != '\n'; i++) {
		rootfs[i]=*p++;
	}
	rootfs[i]='\0';

	printf("kernel:%s\n", kernel);
	printf("rootfs:%s\n", rootfs);
	//设置环境变量
	setenv("kernel", kernel);
	setenv("rootfs", rootfs);

	return 0;
}

U_BOOT_CMD(
		uin,	//命令名，非字符串，但在U_BOOT_CMD中用“#”符号转化为字符串
		3,		//命令的最大参数个数
		0,		//是否自动重复（按Enter键是否会重复执行）
		do_update_image_name, //该命令对应的响应函数
		"uin- update the kernel rootfs name  environment.\n",//简短的使用说明（字符串）
		"NULL"
);

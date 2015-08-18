#include <common.h>
#include <command.h>
#include <s3c2416.h>
#include <s3c24x0.h>

//此处注意头文件的包含顺序，如果不按此顺序编译也会出错

#ifdef CONFIG_CMD_LED
void led_on()
{
	GPCDAT_REG &= ~(1 << 15); //低电平亮
	GPBDAT_REG &= ~(1 << 0); //低电平亮
}

void led_off()
{
	GPCDAT_REG |= (1 << 15);	//高电平灭
	GPBDAT_REG |= (1 << 0);	//高电平灭

}
void led_blink(int cnt)
{
	int i;
	for(i=0; i<cnt;i++) {
		led_on();
		udelay(400000);
		led_off();
		udelay(200000);
	}

}
int do_led( cmd_tbl_t *cmdtp, int flag, int argc, char *argv[])
{
	//核心上的led灯用的是GPC15管脚,管脚00b:input Mode, 01b:Output Mode
	GPCCON_REG = (GPCCON_REG & ~(3 << 15*2)) | (1 << 15*2);     /* Output Mode,主板上的指示灯 */
	GPBCON_REG = (GPBCON_REG & ~(3 << 0*2)) | (1 << 0*2);     /* Output Mode ，核心板上的指示灯*/
	if(!strcmp(argv[1], "on")) {
		printf("led on.\n");	
		led_on();
	}else if(!strcmp(argv[1], "off")) {
		printf("led off.\n");	
		led_off();
	}else if(!strcmp(argv[1], "blink")){
		int cnt=1;
		if(argc==3) {
			cnt = simple_strtoul(argv[2], NULL, 10);
		}
		printf("led blink %d times.\n", cnt);
		led_blink(cnt);
	}
}

U_BOOT_CMD(
		led,	//命令名，非字符串，但在U_BOOT_CMD中用“#”符号转化为字符串
		3,		//命令的最大参数个数
		0,		//是否自动重复（按Enter键是否会重复执行）
		do_led, //该命令对应的响应函数
		"led - control the core board led.\n",//简短的使用说明（字符串）
		"led on	- light on the system led.\n"	//较详细的使用说明（字符串）
		"led off - light on the system led.\n"
		"led blink {cnt} - blink cnt times the system led.\n"
		);

#endif
